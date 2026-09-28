"""
Host-side test for flipctl's serial recovery. Needs no Flipper.

macOS drops the USB CDC endpoint for a second or two whenever an RPC session
ends badly, and every read against the old endpoint then fails with OSError 6,
"Device not configured". That used to kill whatever was running - most
annoyingly a log stream armed for someone to tap a card, where the scan they
were about to do was the whole point.

These tests inject that failure, because it cannot be provoked on demand from a
real device.
"""
import argparse
import importlib.util
import io
import sys
import types
from pathlib import Path

HERE = Path(__file__).resolve().parent

spec = importlib.util.spec_from_file_location("flipctl", HERE / "flipctl.py")
flipctl = importlib.util.module_from_spec(spec)
sys.modules["flipctl"] = flipctl
spec.loader.exec_module(flipctl)
# patch() replaces the liveness check, so keep the real one to test directly.
real_cli_alive = flipctl.cli_alive

# pyserial is imported lazily inside each function, so a stub carrying the
# exception type the handlers catch is enough.
_serial = types.ModuleType("serial")
class SerialException(Exception):
    pass
_serial.SerialException = SerialException
def _refuse(*a, **k):
    raise AssertionError("opened the port directly instead of via open_serial")
_serial.Serial = _refuse
sys.modules["serial"] = _serial

DROP = OSError(6, "Device not configured")

failures = 0


def check(what, ok):
    global failures
    print(f"  [{'PASS' if ok else 'FAIL'}] {what}")
    if not ok:
        failures += 1


class Handle:
    """A port that serves canned reads and can drop its endpoint on cue."""

    def __init__(self, script):
        self.script = list(script)
        self.written = []

    def write(self, data):
        self.written.append(data)
        return len(data)

    def flush(self):
        pass

    def close(self):
        pass

    def reset_input_buffer(self):
        pass

    def read(self, n=1):
        if not self.script:
            return b""
        item = self.script.pop(0)
        if item is DROP:
            raise DROP
        return item


def patch(opens, alive=True):
    """Point flipctl at a scripted sequence of ports."""
    opened = []

    def open_serial(port, timeout=0.2, attempts=6):
        if len(opened) >= len(opens):
            raise DROP
        h = Handle(opens[len(opened)])
        opened.append(h)
        return h

    flipctl.open_serial = open_serial
    flipctl.find_port = lambda: "/dev/fake"
    flipctl.require_free_port = lambda *a, **k: None
    flipctl.drain_to_prompt = lambda *a, **k: None
    flipctl.cli_alive = lambda *a, **k: alive
    return opened


class Captured(list):
    """Collect what the stream printed to stdout, so lines can be asserted on."""

    def __enter__(self):
        self._real = sys.stdout
        sys.stdout = self._buf = io.StringIO()
        return self

    def __exit__(self, *exc):
        sys.stdout = self._real
        self.extend(self._buf.getvalue().splitlines())
        return False


def log_args(**over):
    args = dict(force=False, keys=None, settle=0.1, level="debug",
                all=True, seconds=2, grep=None, arm=False, shot=None,
                appid="flipso")
    args.update(over)
    return argparse.Namespace(**args)


def main():
    print("flipctl serial recovery")

    # A drop mid-stream must not end the stream: reopen, re-arm, carry on.
    # The leading b"" on each script is what arm() consumes when it drains the
    # freshly opened port; without it the first real line is eaten by the
    # arming read and the test proves nothing about lines surviving.
    before = [b"", b"1 [I][Flipso] before\n", DROP]
    after = [b"", b"2 [I][Flipso] after\n", b""]
    opened = patch([before, after])
    with Captured() as out:
        rc = flipctl.cmd_log(log_args())
    check("a mid-stream drop reconnects rather than dying", len(opened) == 2)
    check("the stream still reports success", rc == 0)
    check("lines before the drop are printed",
          any("before" in line for line in out))
    check("lines after the drop are printed - the point of reconnecting",
          any("after" in line for line in out))
    check("a truncated read is not printed as a line",
          not any(line.strip() in ("1", "2") for line in out))
    # The log level belongs to the session, so a reopened port that was not
    # re-armed would silently stream at the system default instead.
    check("the reconnected port is re-armed with the log level",
          any(b"log debug" in w for w in opened[1].written))

    # A port that never comes back ends the stream with a message, not a
    # traceback: the reader needs to know their armed log is gone.
    opened = patch([[b"", b"1 [I][Flipso] before\n", DROP]])
    with Captured() as out:
        rc = flipctl.cmd_log(log_args())
    check("an unrecoverable port ends the stream cleanly", rc == 0)
    check("no further ports were opened", len(opened) == 1)
    check("what arrived before the drop is still printed",
          any("before" in line for line in out))

    # Flipper.cmd: a query opts into the retry and recovers.
    opened = patch([[DROP], [b"free\r\n  total 1000\r\n>: "]])
    f = flipctl.Flipper.__new__(flipctl.Flipper)
    f.port = "/dev/fake"
    f._fixed_port = None
    f._connect_timeout = 1.0
    f.s = flipctl.open_serial("/dev/fake")
    f._sync = lambda limit: None
    out = f.cmd("free", limit=1.0, retry=True)
    check("a query recovers from a drop", "total 1000" in out)

    # ...but a command that is not a query must not be repeated: re-running an
    # `input send` is a second key press the user never asked for.
    opened = patch([[DROP], [b">: "]])
    f = flipctl.Flipper.__new__(flipctl.Flipper)
    f.port = "/dev/fake"
    f._fixed_port = None
    f._connect_timeout = 1.0
    f.s = flipctl.open_serial("/dev/fake")
    f._sync = lambda limit: None
    raised = False
    try:
        f.cmd("input send ok short", limit=1.0)
    except OSError:
        raised = True
    check("a non-query raises rather than repeating itself", raised)
    check("and did not reopen the port behind our back", len(opened) == 1)

    # The pre-tap check. An armed reader and an app sitting on its idle screen
    # are indistinguishable from the host in every way except this one: the NFC
    # poller logs while the field is up and nothing at all while it is down.
    # Silence must therefore never pass for readiness - that is exactly the
    # mistake that gets someone sent to tap a card at a dead app.
    patch([[b"", b"9 [D][Nfc] FWT Timeout\n"]])
    check("an armed reader is recognised from the NFC poller's own line",
          flipctl.nfc_field_live("/dev/fake", limit=0.5) is not None)

    patch([[b"", b"", b""]])
    check("a silent log is not mistaken for an armed reader",
          flipctl.nfc_field_live("/dev/fake", limit=0.5) is None)

    # A card already lying on the reader is read the moment the field comes up,
    # so the poller never logs an idle timeout. That is a live reader too.
    patch([[b"", b"9 [I][Flipso] Shell owner: OID 1 (Example)\n"]])
    check("a card already on the reader also counts as armed",
          flipctl.nfc_field_live("/dev/fake", limit=0.5) is not None)

    # A protocol poller's own line means the field is up, not that a card is
    # there: only the app reports a card.
    flipctl.NFC_ARM_GRACE_S = 0.2
    patch([[b"", b"9 [D][Iso14443_3aPoller] Collision\n", b""]])
    watch = flipctl.watch_nfc("/dev/fake", limit=1.0)
    check("a poller line on an empty reader is armed, not a card read",
          watch.kind == "idle")

    # An unrelated app being chatty must not be read as an armed NFC field.
    patch([[b"", b"9 [I][Loader] Starting\n", b""]])
    check("an unrelated log line does not count as an armed reader",
          flipctl.nfc_field_live("/dev/fake", limit=0.5) is None)

    # The key that starts a scan goes down the stream's own session, ahead of
    # `log`, in one write. Sent separately it lands before the stream is open,
    # and a card already on the reader is read in the gap - measured on
    # 2026-09-28, when `arm` saw nothing and restarted the app over the read.
    opened = patch([[b"9 [D][Nfc] FWT Timeout\n"]])
    flipctl.watch_nfc("/dev/fake", limit=0.5, press=["ok"])
    sent = b"".join(opened[0].written)
    check("the scan key is sent in the stream's session",
          b"input send ok release" in sent)
    check("and before the stream starts",
          sent.index(b"input send ok release") < sent.index(b"log debug"))

    # A card on the reader is followed to the end of its read, so the caller
    # sees what the app made of it, not just that something answered.
    flipctl.NFC_READ_QUIET_S = 0.3
    patch([[b"9 [I][Flipso] Card detected: 1 protocol(s), ISO 14443-4\n",
            b"9 [D][Nfc] FWT Timeout\n",
            b"9 [I][Flipso] Shell owner: OID 287 (unknown)\n"]])
    watch = flipctl.watch_nfc("/dev/fake", limit=0.5, press=["ok"])
    check("a card on the reader is reported as a card, not an armed field",
          watch.kind == "card")
    check("and the whole read is kept",
          any("Shell owner" in line for line in watch.lines))
    check("without the poller's chatter",
          not any("FWT" in line for line in watch.lines))

    # The poller's first pass times out even with a card lying on it, so the
    # first idle line is not yet "armed": the card turns up a moment later.
    flipctl.NFC_ARM_GRACE_S = 0.3
    patch([[b"9 [D][Nfc] FWT Timeout\n",
            b"9 [I][Flipso] Card detected: 1 protocol(s), ISO 14443-4\n"]])
    watch = flipctl.watch_nfc("/dev/fake", limit=0.5, press=["ok"])
    check("a card found just after the first idle poll is still a card",
          watch.kind == "card")
    patch([[b"9 [D][Nfc] FWT Timeout\n", b"", b""]])
    watch = flipctl.watch_nfc("/dev/fake", limit=5.0, press=["ok"])
    check("and an empty field is armed once the grace has passed",
          watch.kind == "idle")

    # `scan` wants a card: a polling field is not the end of the wait...
    patch([[b"9 [D][Nfc] FWT Timeout\n",
            b"9 [I][Flipso] Card detected: 1 protocol(s), ISO 14443-4\n"]])
    watch = flipctl.watch_nfc("/dev/fake", limit=0.5, press=["ok"], want_card=True)
    check("scan waits past an idle field for the card", watch.kind == "card")
    # ...but a field that polls and never finds one is its own answer.
    patch([[b"9 [D][Nfc] FWT Timeout\n", b""]])
    watch = flipctl.watch_nfc("/dev/fake", limit=0.3, press=["ok"], want_card=True)
    check("a field with no card in it is reported as such", watch.kind == "idle")

    # `log --keys` bundles its last key the same way, and does not print the
    # echo of it as though the device had logged it.
    opened = patch([[b">: input send ok release\r\n", b"9 [I][Flipso] read\n"]])
    with Captured() as out:
        flipctl.cmd_log(log_args(keys=["ok"], seconds=0.3))
    sent = b"".join(opened[0].written)
    check("log --keys sends its last key in the stream's session",
          sent.index(b"input send ok release") < sent.index(b"log debug"))
    check("and the echo is not printed as a log line",
          not any("input send" in line for line in out))

    # Silence from `loader info` is not the desktop. Treating it as one sent a
    # launch at a device that was not running commands.
    f = flipctl.Flipper.__new__(flipctl.Flipper)
    f.cmd = lambda *a, **k: ""
    sleep = flipctl.time.sleep
    flipctl.time.sleep = lambda s: None
    raised = False
    try:
        f.app_running()
    except flipctl.CliSilent:
        raised = True
    flipctl.time.sleep = sleep
    check("an empty `loader info` is reported, not read as the desktop", raised)
    f.cmd = lambda *a, **k: "No application is running"
    check("the desktop is still the desktop", f.app_running() is None)

    # Alive means a prompt, not any bytes: a CLI that echoes and runs nothing
    # passed the old test.
    patch([[b"^C\r\n"]])
    check("an echo without a prompt is not a live CLI",
          not real_cli_alive("/dev/fake", wait=0))
    patch([[b"\x1b[0m>: ^C\r\n>: \r\n>: "]])
    check("a prompt is", real_cli_alive("/dev/fake", wait=0))

    # A reboot that never takes the port down was never acted on, and saying
    # "did not come back" sends the reader looking in the wrong place.
    glob_mod = flipctl.glob
    flipctl.glob = types.SimpleNamespace(glob=lambda pattern: ["/dev/cu.usbmodemflip_x"])
    limit_gone = flipctl.REBOOT_GONE_LIMIT_S
    flipctl.REBOOT_GONE_LIMIT_S = 0.2
    err = io.StringIO()
    real_err, sys.stderr = sys.stderr, err
    try:
        flipctl.wait_for_port()
        died = False
    except SystemExit:
        died = True
    sys.stderr = real_err
    flipctl.glob = glob_mod
    flipctl.REBOOT_GONE_LIMIT_S = limit_gone
    check("a reboot the device ignored is reported as ignored",
          died and "never restarted" in err.getvalue())

    # `log --arm` exists so a stream under someone's eyes always means a reader
    # that will answer. If arming fails it must not stream anyway: an empty log
    # from a dead app looks just like an empty log from a patient one.
    flipctl.cmd_arm = lambda a: 1
    opened = patch([[b"", b"1 [I][Flipso] x\n"]])
    with Captured():
        rc = flipctl.cmd_log(log_args(arm=True))
    check("--arm refuses to stream when the reader could not be armed", rc == 1)
    check("and does not open the port to stream anyway", len(opened) == 0)

    # A launch counts only when the app says its UI is up. The loader reports an
    # app stuck in its own startup as running - measured on 2026-09-26, with
    # the desktop on screen and every key press landing on it.
    def launcher(script):
        patch([script])
        f = flipctl.Flipper.__new__(flipctl.Flipper)
        f.s = flipctl.open_serial("/dev/fake")
        f._sync = lambda limit: None
        return f

    limit = flipctl.APP_STARTUP_LIMIT_S
    flipctl.APP_STARTUP_LIMIT_S = 0.5
    f = launcher([b"loader open /ext/apps/NFC/flipso.fap\r\n",
                  b"10 [I][Flipso] UI ready\r\n"])
    with Captured():
        check("a launch that logs UI ready is up", flipctl.launch_app(f, "flipso"))
    check("the launch and the log go down one session, in that order",
          any(b"loader open" in w and w.index(b"loader open") < w.index(b"log info")
              for w in f.s.written))

    f = launcher([b"loader open /ext/apps/NFC/flipso.fap\r\n",
                  b"10 [I][Flipso] Station table: 2600 entries\r\n", b""])
    with Captured():
        check("an app that stops before UI ready is not up",
              not flipctl.launch_app(f, "flipso"))

    f = launcher([b"10 [I][Other] UI ", b"ready\r\n"])
    with Captured():
        check("another app's line with the same words does not count",
              not flipctl.launch_app(f, "flipso"))
    f = launcher([b"[Flipso] UI ", b"ready\r\n"])
    with Captured():
        check("the marker is recognised across two reads", flipctl.launch_app(f, "flipso"))
    flipctl.APP_STARTUP_LIMIT_S = limit

    print("FAILED" if failures else "All flipctl recovery tests passed")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
