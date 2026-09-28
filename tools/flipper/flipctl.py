#!/usr/bin/env python3
"""
flipctl - drive a connected Flipper Zero from the command line.

Everything Flipso development needs from the hardware lives here: building and
installing the app, driving its UI, capturing the screen, streaming the device
log, and sampling the heap.

Run through the ``flipctl`` wrapper next to this file, which picks the right
interpreter and creates the virtualenv on first use:

    tools/flipper/flipctl doctor

Why this exists
---------------
The Flipper exposes one USB serial port and exactly one process may hold it.
``ufbt launch``, the log stream and every RPC call all want that port, so the
failure mode is a silent hang rather than an error. This tool owns the port
centrally, clears stale holders, and turns the well-known hangs into messages.
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(os.path.dirname(HERE))

BAUD = 230400
PROMPT = b">: "
ANSI = re.compile(rb"\x1b\[[0-9;]*[A-Za-z]")

# Processes that are safe to kill when they are squatting on the port: our own
# log stream, and ufbt's uploader, which hangs forever when the app it wants to
# replace is still running on the device.
RECLAIMABLE = ("flipctl.py", "runfap.py", "flipso_log.py", "logcap.py")

# Log lines worth surfacing by default. The firmware is chatty at debug level
# and most of it is unrelated to Flipso.
LOG_KEEP = (
    "Flipso", "ITSO", "Nfc", "nfc", "Desfire", "DESFire", "Iso14443", "Iso7816",
    "furi", "Furi", "Error", "error", "ERROR", "assert", "Assert", "crash",
    "Crash", "malloc", "heap", "Fault", "fault", "lockup",
)


def die(msg: str, code: int = 1):
    print(f"flipctl: {msg}", file=sys.stderr)
    raise SystemExit(code)


# ---------------------------------------------------------------------------
# Port ownership
# ---------------------------------------------------------------------------

def find_port() -> str:
    ports = sorted(glob.glob("/dev/cu.usbmodemflip_*"))
    if not ports:
        die("no Flipper serial port found - check the USB cable and that the "
            "device is not in DFU mode (ls /dev/cu.usbmodemflip_*)")
    return ports[0]


def port_holders(port: str):
    """Other processes with the serial port open, as (pid, command) pairs."""
    if not shutil.which("lsof"):
        return []
    out = subprocess.run(["lsof", "-t", port], capture_output=True, text=True).stdout
    holders = []
    for pid in out.split():
        if int(pid) == os.getpid():
            continue
        cmd = subprocess.run(
            ["ps", "-o", "command=", "-p", pid], capture_output=True, text=True
        ).stdout.strip()
        if cmd:
            holders.append((int(pid), cmd))
    return holders


def reclaim_port(port: str, force: bool = False) -> list:
    """Kill stale helpers holding the port. Returns what was killed."""
    killed = []
    for pid, cmd in port_holders(port):
        if force or any(name in cmd for name in RECLAIMABLE):
            try:
                os.kill(pid, 15)
                killed.append((pid, cmd))
            except ProcessLookupError:
                pass
    if killed:
        time.sleep(1.5)
        for pid, _ in killed:
            try:
                os.kill(pid, 9)
            except ProcessLookupError:
                pass
        time.sleep(0.5)
    return killed


def require_free_port(port: str, auto: bool = True, force: bool = False):
    killed = reclaim_port(port, force) if (auto or force) else []
    for pid, cmd in killed:
        print(f"[flipctl] released the port from pid {pid}: {cmd[:90]}", file=sys.stderr)
    remaining = port_holders(port)
    if remaining:
        lines = "\n".join(f"  pid {p}: {c[:100]}" for p, c in remaining)
        die("the serial port is held by another process:\n" + lines +
            "\nStop it, or re-run with --force to kill it.")


def open_serial(port: str, timeout: float = 0.2, attempts: int = 6):
    """Open the CDC port, retrying the transient "Device not configured".

    macOS drops the endpoint for a second or two whenever an RPC session ends
    without closing cleanly, which a screenshot or a ``ufbt launch`` can do on
    its way out. Every read against the old endpoint then fails with OSError 6
    until it heals. Retrying belongs here rather than in each caller, because
    every caller wants the same thing: wait for it to come back.

    Raises the last error rather than exiting, so a caller that has its own
    meaning for an unreachable port - cli_alive, the crash watcher - can decide
    for itself.
    """
    import serial

    last = None
    for attempt in range(attempts):
        try:
            s = serial.Serial(port, timeout=timeout)
            s.baudrate = BAUD
            return s
        except (serial.SerialException, OSError) as exc:
            last = exc
            time.sleep(1.0 + attempt)
    raise last


def drain_to_prompt(s, settle: float = 0.2):
    """Clear a half-typed line and drain whatever the device still owes us."""
    for _ in range(3):
        s.write(b"\x03")
        time.sleep(settle)
        s.read(65536)
    s.write(b"\r\n")
    time.sleep(0.4)
    s.read(65536)


# ---------------------------------------------------------------------------
# CLI session
class CliSilent(Exception):
    """The CLI took a command and printed nothing back, not even an error."""


def key_lines(name: str, kind: str = "short") -> list:
    """The CLI lines for one key press. See Flipper.key for why there are three."""
    if kind in ("press", "release"):
        return [f"input send {name} {kind}"]
    return [f"input send {name} press", f"input send {name} {kind}",
            f"input send {name} release"]


def split_keys(seq) -> tuple:
    """Split a key sequence into (sent first, bundled with the stream).

    Whatever the last key sets off happens while the stream is being opened,
    and a card already on the reader is read in well under a second - so a
    stream opened after the keys misses the whole scan. The last key therefore
    goes down the stream's own session, in the same write as `log`. A trailing
    '@N' wait means the caller wants the pause before streaming, so nothing is
    bundled.
    """
    seq = list(seq or [])
    if not seq or seq[-1].startswith("@"):
        return seq, []
    name, _, kind = seq[-1].partition(":")
    return list(seq[:-1]), key_lines(name.lower(), kind or "short")


# ---------------------------------------------------------------------------

class Flipper:
    """A Flipper CLI session over USB serial.

    Reads are bounded by the ``>: `` prompt rather than by a fixed sleep. Fixed
    sleeps desynchronise as soon as one command is slower than expected, and
    every later command then reads the previous one's tail.
    """

    def __init__(self, port: str | None = None, connect_timeout: float = 5.0):
        import serial  # imported lazily so --help works without the venv

        self.port = port or find_port()
        self._fixed_port = port
        self._connect_timeout = connect_timeout
        last = None
        for attempt in range(6):
            try:
                self.s = open_serial(self.port, attempts=1)
                self._sync(connect_timeout)
                return
            except (serial.SerialException, OSError) as exc:
                last = exc
                time.sleep(1.0 + attempt)
                self.port = port or find_port()
        die(f"cannot talk to {self.port}: {last}\n"
            "If this persists, unplug and replug the Flipper, or run "
            "tools/flipper/flipctl doctor.")

    def _reconnect(self) -> bool:
        """Reopen the session after the endpoint dropped. True if it came back."""
        import serial

        try:
            self.s.close()
        except Exception:
            pass
        try:
            self.port = self._fixed_port or find_port()
            self.s = open_serial(self.port)
            self._sync(self._connect_timeout)
            return True
        except (serial.SerialException, OSError):
            return False

    def _sync(self, limit: float):
        """Get to a clean prompt.

        A half-typed line left in the device's input buffer by an interrupted
        session is echoed back into the next command's output and truncates it -
        that is what makes ``help`` list only a handful of commands. Ctrl-C
        clears the line editor; then we drain until the device goes quiet.
        """
        for _ in range(3):
            self.s.write(b"\x03")
            time.sleep(0.2)
            self.s.read(65536)
        self.s.write(b"\r\n")
        deadline = time.time() + limit
        quiet = 0
        while time.time() < deadline:
            if self.s.read(65536):
                quiet = 0
            else:
                quiet += 1
                if quiet >= 3:
                    return
            time.sleep(0.1)

    def _exchange(self, line: str, limit: float) -> bytes:
        """Send one command and read until the prompt. Raises if the port drops."""
        self.s.reset_input_buffer()
        self.s.write(line.encode() + b"\r\n")
        self.s.flush()
        buf = b""
        deadline = time.time() + limit
        while time.time() < deadline:
            chunk = self.s.read(4096)
            if chunk:
                buf += chunk
                if ANSI.sub(b"", buf).rstrip().endswith(b">:"):
                    break
            else:
                time.sleep(0.02)
        return buf

    def cmd(self, line: str, limit: float = 20.0, retry: bool = False) -> str:
        """Run one CLI command and return its output without echo or prompt.

        @p retry re-runs the command once if the CDC endpoint drops mid-read -
        see open_serial. It is opt-in rather than the default because the
        device cannot tell us how far it got: re-running a query costs nothing,
        but a repeated ``input send`` is a second key press.
        """
        import serial

        try:
            buf = self._exchange(line, limit)
        except (serial.SerialException, OSError):
            if not retry or not self._reconnect():
                raise
            buf = self._exchange(line, limit)
        text = ANSI.sub(b"", buf).decode("utf-8", "replace")
        out = []
        for raw in text.replace("\r", "").split("\n"):
            if raw.strip() in (line, ">:", ""):
                continue
            out.append(raw.rstrip())
        return "\n".join(out).strip()

    def key(self, name: str, kind: str = "short"):
        """Send one key press.

        The GUI discards non-complementary input: it tracks which keys are
        currently down, and drops a Short or Long event for a key it never saw
        pressed. `input send ok short` on its own therefore reaches no app at
        all - it has to be bracketed by Press and Release, the way the hardware
        emits it.
        """
        for line in key_lines(name, kind):
            self.cmd(line, limit=5.0)

    def press(self, seq, settle: float = 0.3):
        """Send a key sequence. '@1.5' sleeps, 'ok:long' sends a long press."""
        for item in seq:
            if item.startswith("@"):
                time.sleep(float(item[1:]))
                continue
            name, _, kind = item.partition(":")
            self.key(name.lower(), kind or "short")
            time.sleep(settle)

    def app_running(self) -> str | None:
        """The app the loader says is running, or None at the desktop.

        An empty reply is not "the desktop": the loader always says something,
        even if only "No application is running", so a CLI that returns nothing
        has stopped running commands. On 2026-09-28 a device in that state was
        reported as "none (desktop)", which sends the next step - a launch - at
        a device that will not act on it. So silence raises, and main() reports
        it.
        """
        info = self.cmd("loader info", limit=8.0, retry=True)
        if not info:
            time.sleep(1.0)
            info = self.cmd("loader info", limit=8.0, retry=True)
        if not info:
            raise CliSilent("loader info")
        m = re.search(r'Application "([^"]+)" is running', info)
        return m.group(1) if m else None

    def heap(self) -> dict:
        stats = {}
        for line in self.cmd("free", limit=8.0, retry=True).splitlines():
            m = re.match(r"([A-Za-z ]+):\s+(\d+)", line.strip())
            if m:
                stats[m.group(1).strip()] = int(m.group(2))
        return stats

    def close(self):
        try:
            self.s.write(b"\x03")
            self.s.close()
        except Exception:
            pass


# ---------------------------------------------------------------------------
# Subcommands
# ---------------------------------------------------------------------------

def cmd_doctor(args):
    ok = True
    print("Flipper")
    ports = sorted(glob.glob("/dev/cu.usbmodemflip_*"))
    if ports:
        print(f"  port                 {ports[0]}")
    else:
        print("  port                 NOT FOUND - is the Flipper plugged in?")
        ok = False

    if ports:
        holders = port_holders(ports[0])
        if holders:
            print(f"  port holders         {len(holders)} process(es) - run with --force to clear")
            for pid, cmd in holders:
                print(f"                       pid {pid}: {cmd[:80]}")
        else:
            print("  port holders         none (free)")

    print("Host tooling")
    print(f"  ufbt                 {shutil.which('ufbt') or 'NOT FOUND (pip install ufbt)'}")
    if not shutil.which("ufbt"):
        ok = False
    venv_py = os.path.join(HERE, ".venv", "bin", "python")
    print(f"  venv                 {'ok' if os.path.exists(venv_py) else 'missing (run any flipctl command to build it)'}")
    try:
        import serial  # noqa: F401
        print("  pyserial             ok")
    except ImportError:
        print("  pyserial             MISSING")
        ok = False
    try:
        import flipperzero_protobuf  # noqa: F401
        print("  flipperzero-protobuf ok (screenshots available)")
    except ImportError:
        print("  flipperzero-protobuf MISSING (flipctl shot will not work)")

    if ports:
        print("Device")
        require_free_port(ports[0], auto=True, force=args.force)
        if not cli_alive(ports[0]):
            print("  state                HALTED - the CLI is silent")
            print()
            print(HALTED_ADVICE)
            return 2
        f = Flipper(ports[0])
        try:
            try:
                running = f.app_running()
            except CliSilent:
                print("  running app          NO ANSWER - the CLI takes input but runs nothing")
                print()
                print(HALTED_ADVICE)
                return 2
            print(f"  running app          {running or 'none (desktop)'}")
            if running:
                # Worth saying plainly, because this line is exactly what an
                # earlier session trusted before sending someone to tap a card
                # at an app that could not receive it.
                print("                       (the loader reports this even when "
                      "the app is not on screen -")
                print("                        `flipctl arm` before relying on "
                      "it for a card tap)")
            h = f.heap()
            if h:
                print(f"  free heap            {h.get('Free heap size', 0):,} of {h.get('Total heap size', 0):,}")
                print(f"  heap low-water       {h.get('Minimum heap size', 0):,} (since boot)")
            else:
                print("  free heap            no answer from the CLI")
                ok = False
            stat = f.cmd(f"storage stat /ext/apps/NFC/{args.appid}.fap", limit=8.0,
                         retry=True)
            # A missing file is an error message; only silence is no answer.
            if not stat:
                print("  installed .fap       no answer from the CLI")
                ok = False
            elif "not exist" in stat:
                print("  installed .fap       not installed")
            else:
                print(f"  installed .fap       {stat}")
        finally:
            f.close()

    print()
    print("OK" if ok else "Problems found - see above")
    return 0 if ok else 1


def cmd_ready(args):
    """Relaunch the app so the device is usable.

    This is a step, not a guarantee. It proves the CLI answers and that the
    loader accepted a launch; it cannot prove the app is on screen, because the
    only thing it can ask afterwards is `loader info`, which says the app is
    running in every broken state. Use `arm` before asking anyone to tap a
    card - that checks the NFC field itself.
    """
    port = find_port()
    require_free_port(port, force=args.force)
    if not cli_alive(port):
        print("state: HALTED - the CLI is silent")
        print()
        print(HALTED_ADVICE)
        return 2

    port, message = ensure_usable(port, args, want_running=not args.desktop)
    f = Flipper(port)
    try:
        running = f.app_running()
        h = f.heap()
    finally:
        f.close()

    print(f"{message}: {running or 'desktop'}")
    if h:
        print(f"  free heap  {h.get('Free heap size', 0):,} of "
              f"{h.get('Total heap size', 0):,}")
    if not args.desktop and (running is None or message != "ready"):
        return 1
    if not args.desktop:
        # "UI ready" means the first scene is up and input reaches the app. It
        # does not mean the reader is on: the scan scene starts idle.
        print("  on screen (the app logged \"UI ready\") - the reader is off "
              "until `flipctl arm`")
    return 0


# What the NFC stack logs while the field is up with nothing in it: one line
# roughly every 100 ms. With the field down the log is completely silent, so
# this is a positive signal, not an absence of one - and it comes from the
# firmware's poller rather than from Flipso, so it says the field is really
# radiating, not that the app thinks it started something.
#
# The firmware's protocol pollers log under their own tags too, and any of
# their lines also means the field is up. None of them means a card: only the
# app says that, with "Card detected" the moment the poller hands it one, and
# every line it logs while reading. So a card is a [Flipso] line and nothing
# else, and an empty reader cannot be mistaken for one that has just read.
NFC_IDLE_POLL = ("FWT Timeout", "Iso14443", "Desfire", "DESFire", "Iso7816")
NFC_CARD_PRESENT = ("[Flipso]",)

# Once a card has been seen, how long the log has to go quiet before the read
# counts as finished. A DESFire read with a full journey log takes about a
# second; a card that is lost and retried takes longer, and keeps logging.
NFC_READ_QUIET_S = 2.0
NFC_READ_LIMIT_S = 20.0

# How long to keep watching after the first idle poll before calling the field
# armed. The poller logs a timeout on its first pass even with a card lying on
# it, and only finds the card a few hundred ms later: measured on 2026-09-28,
# `arm` returned ARMED on that first line and the card was read a moment after,
# leaving the app on the card menu with the field down.
NFC_ARM_GRACE_S = 1.5


class NfcWatch:
    """What the log said once the scan key went down.

    kind is None (nothing - the field never came up), "idle" (the field is
    polling with no card in it) or "card" (a card answered, and lines holds
    what the app logged while reading it).
    """

    def __init__(self):
        self.kind = None
        self.proof = None
        self.lines = []


def watch_nfc(port: str, limit: float = 5.0, press=None,
              want_card: bool = False) -> NfcWatch:
    """Watch the log for proof that the NFC field is up.

    This is the only observation that separates a device someone can usefully
    tap a card against from one that will ignore them. Everything else the host
    can ask - `loader info`, the free heap, the thread table - answers the same
    way whether Flipso owns the screen or the desktop does, and none of them
    knows whether the reader within Flipso has been started at all.

    @p press is a key sequence sent down this same session just before the
    stream starts (split_keys). It has to be: a card already on the reader is
    detected and read within a second of the key, which is gone before a
    stream opened afterwards is listening - measured on 2026-09-28, when a
    card left on the reader was read, never seen, and `arm` restarted the app
    over the result.

    With @p want_card, the field polling is not enough: keep watching until a
    card answers or @p limit runs out. Either way, once a card is seen the read
    is followed until the log goes quiet, so the caller gets the whole of it.
    """
    import serial

    watch = NfcWatch()
    armed_at = None
    first, prelude = split_keys(press or [])
    if first:
        f = Flipper(port)
        try:
            f.press(first)
        finally:
            f.close()
    try:
        s = open_serial(port)
    except (serial.SerialException, OSError):
        return watch
    try:
        start_log_stream(s, "debug", prelude=prelude)
        deadline = time.time() + limit
        last_card_line = None
        buf = b""
        while True:
            now = time.time()
            if last_card_line is not None:
                if now - last_card_line > NFC_READ_QUIET_S or now > deadline + NFC_READ_LIMIT_S:
                    break
            elif armed_at is not None and now - armed_at > NFC_ARM_GRACE_S:
                return watch  # polling, and no card turned up in the grace
            elif now > deadline:
                break
            try:
                chunk = s.read(512)
            except (serial.SerialException, OSError):
                break
            if not chunk:
                time.sleep(0.02)
                continue
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                text = log_line(raw)
                if any(m in text for m in NFC_CARD_PRESENT):
                    if watch.kind != "card":
                        watch.kind, watch.proof = "card", text
                    last_card_line = time.time()
                    watch.lines.append(text)
                elif any(m in text for m in NFC_IDLE_POLL):
                    if watch.kind is None:
                        watch.kind, watch.proof = "idle", text
                        if not want_card:
                            armed_at = time.time()
                    elif watch.kind == "card" and ("[E]" in text or "[W]" in text):
                        # A protocol error mid-read is part of the story. The
                        # idle polls after a finished read are not, and must
                        # not hold the read open.
                        watch.lines.append(text)
                        last_card_line = time.time()
    finally:
        try:
            s.write(b"\x03")
            s.close()
        except Exception:
            pass
    return watch


def nfc_field_live(port: str, limit: float = 5.0, press=None) -> str | None:
    """The log line that proves the field is up, or None. See watch_nfc."""
    return watch_nfc(port, limit, press).proof


def start_scan(args, want_card: bool) -> tuple:
    """Relaunch, press Scan and watch, retrying a launch that never polls.

    Returns (port, NfcWatch). Only a field that never came up is retried: a
    card, or a field polling with no card in it, is a real answer.
    """
    port = find_port()
    require_free_port(port, force=args.force)
    if not cli_alive(port):
        print("state: HALTED - the CLI is silent")
        print()
        print(HALTED_ADVICE)
        raise SystemExit(2)

    watch = NfcWatch()
    for attempt in range(1, args.tries + 1):
        port, message = ensure_usable(port, args, want_running=True)
        if message == "ready":
            watch = watch_nfc(port, limit=args.timeout, press=["ok"],
                              want_card=want_card)
            if watch.kind:
                break
        more = " - restarting the app and retrying" if attempt < args.tries else ""
        print(f"[flipctl] attempt {attempt} of {args.tries}: no NFC activity "
              f"within {args.timeout:g}s{more}", file=sys.stderr)
    return port, watch


NOT_ARMED = (
    "The app was relaunched and sent OK, and the NFC field never started "
    "polling.\nTry `tools/flipper/flipctl reboot` and run this again; if it "
    "still fails,\nthe screen will say why - `tools/flipper/flipctl shot`."
)


def print_read(watch: NfcWatch):
    """What the app logged while reading a card."""
    for line in watch.lines or [watch.proof]:
        print(f"  {line}")


def cmd_arm(args):
    """Start a scan and prove the NFC field is polling. Run this, and read its
    exit status, before asking anyone to tap a card.

    `ready` is not enough and never was. It relaunches the app and then asks
    `loader info` whether it is running - the one signal this file documents as
    untrustworthy - so it reports "ready" in the state where the app is resident
    but the desktop owns the screen. And even when the app really is on screen,
    Flipso's scan scene starts idle by design: the field only comes up when the
    user presses OK, because it costs power. So the documented pre-tap check
    passed in both of the states where a tap does nothing.

    This replaces the guesswork with the one fact that matters: the field is
    radiating, which is only true if the app is on screen, in the scan scene,
    and scanning.

    Exit status: 0 armed, 1 not armed, 2 halted, 3 a card was already on the
    reader and has been read - the reader is off again, so nobody should tap.
    """
    port, watch = start_scan(args, want_card=False)

    if not watch.kind:
        # Deliberately loud and deliberately non-zero: the whole point of this
        # command is that nobody is sent to tap a card on a guess.
        print()
        print("NOT ARMED - do not ask anyone to tap a card.")
        print()
        print(NOT_ARMED)
        return 1

    if args.shot:
        snapshot(port, args.shot)

    if watch.kind == "card":
        # Not a failure, but not armed either: the scan has finished, the app
        # has moved on to the card, and the field is down. `scan` is the
        # command for this.
        print("CARD READ - a card was already on the reader, and the app has read it.")
        print("The reader is off again: do not ask anyone to tap. The app logged:")
        print_read(watch)
        return 3

    f = Flipper(port)
    try:
        h = f.heap()
    finally:
        f.close()
    print("ARMED - the NFC field is polling, a card tapped now will be read")
    print(f"  proof      {watch.proof}")
    if h:
        print(f"  free heap  {h.get('Free heap size', 0):,} of "
              f"{h.get('Total heap size', 0):,}")
    # Nothing in the app stops a scan on a timer, so this stays true until the
    # card arrives, someone presses Back, or something here restarts the app.
    print("  the scan does not time out - it stays armed until the card arrives")
    return 0


def cmd_scan(args):
    """Read a card that is already lying on the reader, and say what happened.

    `arm` is for a card someone is about to tap; this is for one that is
    already there, which is how a card is usually left for an unattended
    session. It relaunches the app, presses Scan, and follows the read in the
    log from the key press on, then screenshots the result.

    Exit status: 0 a card was read, 1 the field never came up, 2 halted, 3 the
    field is polling but no card answered.
    """
    port, watch = start_scan(args, want_card=True)

    if not watch.kind:
        print()
        print("NOT ARMED - the reader never came up, so nothing was read.")
        print()
        print(NOT_ARMED)
        return 1

    if args.shot:
        snapshot(port, args.shot)

    if watch.kind == "idle":
        print(f"NO CARD - the field polled for {args.timeout:g}s and nothing answered.")
        print("The reader is still on: a card placed on it now will be read.")
        print("If one is already there, it is not over the antenna: ask for it "
              "to be moved\nacross the back of the Flipper, then run this again.")
        return 3

    print("CARD READ - the app logged:")
    print_read(watch)
    print("The screen has the verdict; the card menu means it decoded.")
    return 0


def cmd_cmd(args):
    port = find_port()
    require_free_port(port, force=args.force)
    f = Flipper(port)
    try:
        for c in args.command:
            out = f.cmd(c, limit=args.timeout)
            if len(args.command) > 1:
                print(f"$ {c}")
            print(out)
    finally:
        f.close()


def cmd_keys(args):
    port = find_port()
    require_free_port(port, force=args.force)
    f = Flipper(port)
    try:
        f.press(args.keys, settle=args.settle)
        if args.check:
            print(f.app_running() or "no application is running")
    finally:
        f.close()


def cmd_close(args):
    port = find_port()
    require_free_port(port, force=args.force)
    f = Flipper(port)
    try:
        print("closed" if ensure_closed(f) else "could not close the running app")
    finally:
        f.close()


# Flipso costs roughly 48 KB of heap. Anything like that much coming back at
# once means the app has actually exited, whatever the loader claims.
APP_HEAP_JUMP = 20_000


# Logged by flipso_app() immediately before view_dispatcher_run(). See launch_app.
APP_UI_READY = b"[Flipso] UI ready"
APP_STARTUP_LIMIT_S = 15.0


def launch_app(f: Flipper, appid: str) -> bool:
    """Start the installed .fap and prove it reached its event loop.

    External apps open by path, not by name: `loader open <name>` only resolves
    the built-in apps that `loader list` prints.

    `loader info` cannot be the check. The loader calls an app running from the
    moment its thread exists, and on 2026-09-26 Flipso was measured blocked
    inside its own startup - in a storage_file_open() that waits for ever when
    the storage service believes the path is already open - with the desktop
    on screen, every key going to the desktop, and `loader close` answering
    "has to be closed manually" because the exit handler is only registered by
    view_dispatcher_run(). So the launch goes down the same CLI session as a log
    stream, and the app's own "UI ready" line is what counts.
    """
    s = f.s
    drain_to_prompt(s)
    s.write(f"loader open /ext/apps/NFC/{appid}.fap\r\nlog info\r\n".encode())
    deadline = time.time() + APP_STARTUP_LIMIT_S
    buf = b""
    ready = False
    while time.time() < deadline:
        chunk = s.read(4096)
        if not chunk:
            time.sleep(0.05)
            continue
        buf += chunk
        if APP_UI_READY in ANSI.sub(b"", buf):
            ready = True
            break
    f._sync(2.0)  # Ctrl-C ends the stream and gets back to a prompt
    if not ready:
        text = ANSI.sub(b"", buf).decode("utf-8", "replace").replace("\r", "")
        said = [l for l in text.split("\n")
                if l.strip() and "log info" not in l and "loader open" not in l]
        print("[flipctl] the app did not report \"UI ready\" within "
              f"{APP_STARTUP_LIMIT_S:g}s - it is not on screen, whatever `loader info` "
              "says", file=sys.stderr)
        for l in said[-8:]:
            print(f"[flipctl]   {l.strip()}", file=sys.stderr)
    return ready


def launch_verified(port: str, args) -> tuple:
    """launch_app, rebooting once if the app does not come up. Returns (port, ok).

    A reboot is the right second attempt rather than a relaunch: an app stuck in
    startup cannot be closed by the loader, and whatever it was stuck on - a
    storage registration, a held lock - is state the reboot clears.
    """
    f = Flipper(port)
    try:
        ok = launch_app(f, args.appid)
    finally:
        f.close()
    if ok or getattr(args, "no_reboot", False):
        return port, ok
    print("[flipctl] rebooting and launching once more", file=sys.stderr)
    port = reboot_device(port)
    f = Flipper(port)
    try:
        ok = launch_app(f, args.appid)
    finally:
        f.close()
    if not ok:
        print("[flipctl] still not up after a reboot - the app is blocking in its "
              "own startup. See it with `flipctl close; flipctl log --launch --all`",
              file=sys.stderr)
    return port, ok


def ensure_usable(port: str, args, want_running: bool = True) -> tuple:
    """Leave the device in a state where the app will actually respond.

    This restarts the app rather than inspecting it, because the broken states
    cannot be told apart from a healthy one by anything the host can ask.
    Measured on a device that was refusing to open the app: `loader info` says
    it is running, the heap agrees because its memory is still held, and its
    thread table is byte-for-byte identical to a healthy run - yet the desktop
    owns the screen and `loader open` answers "Loader is locked, please close
    the ... first". The only difference is what is drawn, which the firmware
    does not expose.

    So do not classify the state, replace it. Closing and relaunching costs a
    few seconds, is idempotent, and ends with an app that is certainly on
    screen - which is what someone about to tap a card needs to be true.

    Returns (port, message); the port can change because a reboot re-enumerates.
    """
    port = close_running_app(port, args)
    if not want_running:
        return port, "at the desktop"

    port, ok = launch_verified(port, args)
    return port, ("ready" if ok else "the app did not start")


def ensure_closed(f: Flipper, tries: int = 3) -> bool:
    """Get back to the desktop so the loader will accept a new upload.

    Two things make this harder than it should be.

    `loader info` is not trustworthy: it keeps reporting an app as running after
    it has exited. The heap is, because an app's memory comes back the moment it
    goes - so a jump of tens of KB is the real signal, and believing the loader
    instead is what leads to pressing Back once too often.

    And `loader close` is the only way out that is worth taking. Injecting Back
    presses instead wedges the GUI whenever one lands after the app has gone: it
    navigates the desktop's app browser, whose frame then stays on screen with
    input going nowhere while the next launch runs invisibly behind it. The
    device is then in the worst state there is, because it still answers the
    CLI - `loader info` names the app, the heap still shows it resident - so
    every later check says the app is up while the desktop owns the screen, and
    someone gets asked to tap a card at a dead app.

    That cannot be made safe by checking between presses. The check costs a
    `loader info` and a `free` round trip, so the app can exit inside the window
    and the next press still lands; the failure is a race, not a missing test.
    So there are no Back presses here at all. If `loader close` will not do it,
    say so and let the caller reboot, which is a few seconds slower and always
    works.
    """
    if f.app_running() is None:
        return True
    baseline = f.heap().get("Free heap size", 0)

    def gone() -> bool:
        if f.app_running() is None:
            return True
        return f.heap().get("Free heap size", 0) - baseline > APP_HEAP_JUMP

    for _ in range(tries):
        f.cmd("loader close", limit=10.0, retry=True)
        time.sleep(1.5)
        if gone():
            return True
    return False


# How long a rebooting Flipper takes to drop off USB. It goes within a second
# or two; one still there after this never acted on the reboot.
REBOOT_GONE_LIMIT_S = 15.0


def wait_for_port(gone_first: bool = True, limit: float = 60.0) -> str:
    """Wait for the Flipper to disappear and come back after a reboot."""
    deadline = time.time() + limit
    if gone_first:
        gone_by = time.time() + REBOOT_GONE_LIMIT_S
        while glob.glob("/dev/cu.usbmodemflip_*") and time.time() < gone_by:
            time.sleep(0.5)
        if glob.glob("/dev/cu.usbmodemflip_*"):
            # Not "did not come back": it never went. Measured 2026-09-28, on
            # a device whose CLI answered a keystroke but ran no command, so
            # the reboot was sent and never acted on.
            die("`power reboot` was sent but the Flipper never restarted - its "
                "USB port stayed up.\nThe CLI is taking input without running "
                "it, so nothing sent from here will be acted on.\n\n"
                + HALTED_ADVICE)
    while time.time() < deadline:
        ports = sorted(glob.glob("/dev/cu.usbmodemflip_*"))
        if ports:
            time.sleep(2.0)  # let the CDC endpoint settle before opening it
            return ports[0]
        time.sleep(0.5)
    die("the Flipper did not come back after the reboot")


def reboot_device(port: str) -> str:
    """Reboot and wait for the CLI to answer again.

    This is the escape hatch for a wedged device: once the GUI stops processing
    input, nothing short of a restart brings it back, and asking the user to
    press buttons is exactly what this tool exists to avoid.
    """
    import serial

    if not cli_alive(port):
        die("the CLI is not answering, so `power reboot` cannot be delivered.\n\n"
            + HALTED_ADVICE)
    print("[flipctl] rebooting the Flipper", file=sys.stderr)
    try:
        s = open_serial(port, timeout=0.3)
        s.write(b"\x03\r\n")
        time.sleep(0.3)
        s.read(65536)
        s.write(b"power reboot\r\n")
        time.sleep(0.5)
        s.close()
    except Exception:
        pass
    port = wait_for_port()
    f = Flipper(port, connect_timeout=15.0)
    f.close()
    print(f"[flipctl] back up on {port}", file=sys.stderr)
    return port


def cmd_reboot(args):
    port = find_port()
    require_free_port(port, force=True)
    port = reboot_device(port)
    f = Flipper(port)
    try:
        show_heap(f, label=f.app_running() or "desktop")
    finally:
        f.close()
    return 0


def run_ufbt(target: str | None, timeout: float):
    argv = ["ufbt"] + ([target] if target else [])
    try:
        proc = subprocess.run(argv, cwd=PROJECT, capture_output=True, text=True,
                              timeout=timeout)
        return proc.returncode, (proc.stdout or "") + (proc.stderr or "")
    except subprocess.TimeoutExpired as exc:
        out = (exc.stdout or b"") + (exc.stderr or b"")
        if isinstance(out, bytes):
            out = out.decode("utf-8", "replace")
        # ufbt leaves its uploader behind when it times out, still on the port.
        subprocess.run(["pkill", "-f", "runfap.py"], capture_output=True)
        return 124, out + f"\n[timed out after {timeout:g}s]"


COMPILE_ERROR = re.compile(r"\berror:|undefined reference|No such file or directory")

# A timeout that is generous for a 150 KB .fap starves one carrying a 20 MB
# asset table, so the allowance is scaled from the file actually built. The rate
# is deliberately pessimistic: a 21 MB .fap was measured NOT finishing inside
# 559s, i.e. under 38 KB/s, and a timeout that fires early costs far more than
# one that fires late - it leaves a half-written .fap on the card, which the
# loader reports as "invalid file", and then a reboot-and-retry on top.
# The floor keeps small .faps behaving exactly as before.
UPLOAD_FLOOR_S = 45.0
UPLOAD_OVERHEAD_S = 30.0
UPLOAD_RATE_BPS = 15_000.0


def upload_timeout(size: int, override: float | None) -> float:
    """How long to let an upload run before calling it a hang."""
    if override is not None:
        return override
    return max(UPLOAD_FLOOR_S, UPLOAD_OVERHEAD_S + size / UPLOAD_RATE_BPS)


def cmd_deploy(args):
    port = find_port()
    require_free_port(port, force=True)  # always clear stale log streams
    if not args.build_only and not cli_alive(port):
        die("the device is not answering, so nothing can be installed on it.\n\n"
            + HALTED_ADVICE)

    # Compile first, on its own: a compiler error is about the code and should
    # not be buried under an upload that was never going to happen.
    print("[flipctl] ufbt (compile)")
    code, out = run_ufbt(None, args.timeout)
    if code != 0:
        diag = [l for l in out.splitlines() if COMPILE_ERROR.search(l) or "warning:" in l]
        print("\n".join(diag or out.splitlines()[-30:]))
        die(f"compile failed (exit {code})")
    warnings = [l for l in out.splitlines() if "warning:" in l]
    if warnings:
        print("\n".join(warnings))

    built = os.path.expanduser(f"~/.ufbt/build/{args.appid}.fap")
    size = 0
    if os.path.exists(built):
        size = os.path.getsize(built)
        os.makedirs(os.path.join(PROJECT, "dist"), exist_ok=True)
        shutil.copy2(built, os.path.join(PROJECT, "dist", f"{args.appid}.fap"))
        print(f"[flipctl] {size:,} bytes -> dist/{args.appid}.fap")

    if args.build_only:
        return 0

    allowed = upload_timeout(size, args.launch_timeout)
    if allowed > UPLOAD_FLOOR_S:
        print(f"[flipctl] allowing {allowed / 60:.0f} min for the upload "
              f"({size / 1e6:.0f} MB)")

    for attempt in range(2):
        if not args.no_close:
            port = close_running_app(port, args)

        print("[flipctl] ufbt launch (upload)")
        code, out = run_ufbt("launch", allowed)
        if code == 0:
            break

        # An upload failure is almost always the old instance still holding the
        # loader: ufbt reports it as a bare scons error, or just hangs.
        print("\n".join(l for l in out.splitlines()
                         if re.search(r"Error|error|Traceback|timed out", l))[-800:])
        if attempt == 0 and not args.no_reboot:
            print("[flipctl] upload failed - rebooting and retrying once")
            port = reboot_device(port)
            continue
        die(f"upload failed (exit {code}). Check the device screen, or run "
            "`tools/flipper/flipctl reboot` and try again.")

    time.sleep(1.5)
    f = Flipper(port)
    try:
        # The firmware unpacks .fapassets to the SD card on the first launch
        # after an install, and the app does not answer until it has. That is
        # seconds for the station table and minutes for the stop table, so the
        # wait is scaled the same way the upload is.
        running = f.app_running()
        attempts = max(4, int(size / 200_000))
        for _ in range(attempts):
            if running:
                break
            time.sleep(1.5)
            running = f.app_running()
        h = f.heap()
        print(f"[flipctl] running: {running or 'none'}")
        if h:
            print(f"[flipctl] free heap {h.get('Free heap size', 0):,} "
                  f"(low-water {h.get('Minimum heap size', 0):,})")
        if running is None:
            print("[flipctl] the app is not running - it may have crashed on "
                  "start; check `tools/flipper/flipctl log`")
    finally:
        f.close()

    # `ufbt launch` starts the app over RPC and cannot say whether it reached
    # the screen, and the loader says "running" either way. Close that instance
    # and launch again the checked way; a refused close is itself the sign of an
    # app stuck in startup, and close_running_app reboots out of it.
    port = close_running_app(port, args)
    port, ok = launch_verified(port, args)
    if not ok:
        print("[flipctl] installed, but the app does not come up - see above")
        return 1
    print("[flipctl] on screen (the app logged \"UI ready\")")
    return 0


def close_running_app(port: str, args) -> str:
    """Get the device back to the desktop, rebooting if the app is wedged."""
    f = Flipper(port)
    stuck = False
    running = None
    try:
        running = f.app_running()
        if running:
            print(f"[flipctl] {running} is running - backing out to the desktop")
            stuck = not ensure_closed(f)
    finally:
        f.close()
    if stuck:
        if args.no_reboot:
            die(f"{running} would not close and --no-reboot was given. Press Back "
                "on the device until the desktop shows, then re-run.")
        print(f"[flipctl] {running} is wedged (the loader refuses to close it) "
              "- rebooting to recover")
        port = reboot_device(port)
    time.sleep(0.5)
    return port


def start_log_stream(handle, level: str, launch: str | None = None, prelude=()):
    """Put a freshly opened port into streaming state.

    With @p launch, `loader open` goes down the same session immediately before
    `log`, in one write. The CLI runs them back to back, so the stream is live
    within milliseconds of the app thread starting - which is the only way to
    see an app's startup at all, because the port cannot be held by a stream
    and a separate `loader open` at once, and the firmware keeps no log history.

    @p prelude does the same for any other CLI lines, which is how a key press
    that starts a scan is sent: a card already on the reader is read before a
    stream opened afterwards is listening.
    """
    drain_to_prompt(handle)
    # The level belongs to this session: sending `log debug` from a previous
    # connection and `log` here silently gives the system default instead.
    # That is also why a reconnect has to re-arm rather than just reopen.
    opener = f"loader open {launch}\r\n" if launch else ""
    opener += "".join(f"{line}\r\n" for line in prelude)
    handle.write(f"{opener}log {level}\r\n".encode())
    if opener:
        # Swallowing the echo would swallow the first lines it caused with it.
        return
    time.sleep(0.5)
    handle.read(65536)


def log_line(raw: bytes) -> str:
    """One log line, stripped of ANSI colour and the carriage return."""
    return ANSI.sub(b"", raw).decode("utf-8", "replace").replace("\r", "").strip()


def cmd_log(args):
    """Stream the device log, line buffered so it works under a Monitor."""
    import serial

    if args.arm:
        # Arm first and refuse to stream if it fails, so that a stream running
        # under someone's eyes always means a reader that will answer a card.
        # An empty log from an unarmed app looks exactly like an empty log from
        # an armed one waiting patiently, which is how a dead app came to be
        # watched for a minute while someone tapped at it.
        rc = cmd_arm(argparse.Namespace(
            force=args.force, appid=args.appid, no_reboot=False,
            shot=args.shot, timeout=5.0, tries=2))
        # 3 is a card that was already on the reader: arm has printed the read,
        # and there is nothing left to stream for.
        if rc != 0:
            return rc
        print("[flipctl] ---- armed; tap the card now ----", flush=True)

    port = find_port()
    require_free_port(port, force=args.force)

    # Only one process may hold the port, and the log stream owns the session
    # until it stops - so keys cannot be sent while streaming. All but the last
    # are sent first; the last goes down the stream's own session (split_keys).
    first, prelude = split_keys(args.keys)
    if first:
        f = Flipper(port)
        try:
            f.press(first, settle=args.settle)
        finally:
            f.close()
        time.sleep(args.settle)

    def arm(handle, launch=None, prelude=()):
        start_log_stream(handle, args.level, launch, prelude)

    launch = None
    if getattr(args, "launch", False):
        f = Flipper(port)
        try:
            if f.app_running():
                die("an app is already running, so `--launch` would only be refused "
                    "by the loader. Run `flipctl close` first.")
        finally:
            f.close()
        launch = f"/ext/apps/NFC/{args.appid}.fap"

    s = open_serial(port)
    # Only the first session sends the keys: a reconnect re-arms the level but
    # must not press anything twice.
    arm(s, launch, prelude)
    echoes = set(prelude)
    print(f"[flipctl] streaming log at level '{args.level}'"
          f"{'' if args.all else ' (filtered)'}"
          f"{f' for {args.seconds:g}s' if args.seconds else ''}", flush=True)

    pattern = re.compile(args.grep) if args.grep else None
    deadline = time.time() + args.seconds if args.seconds else None
    buf = b""
    printed = 0
    reconnects = 0
    try:
        while deadline is None or time.time() < deadline:
            try:
                chunk = s.read(512)
            except (serial.SerialException, OSError) as exc:
                # The endpoint dropped, not the device: a stream that has been
                # armed for a user to tap a card must not die under them, and
                # the scan they are about to do is the whole point of it.
                # Anything half-read is dropped rather than printed, because a
                # truncated line looks like the app stopping there.
                buf = b""
                reconnects += 1
                print(f"[flipctl] serial dropped ({exc.__class__.__name__}) - "
                      f"reconnecting, still watching", file=sys.stderr, flush=True)
                try:
                    s.close()
                except Exception:
                    pass
                try:
                    s = open_serial(port)
                    arm(s)
                except (serial.SerialException, OSError):
                    print("[flipctl] the port did not come back - "
                          "run `flipctl doctor`, or reboot the Flipper.",
                          file=sys.stderr, flush=True)
                    break
                continue
            if not chunk:
                time.sleep(0.05)
                continue
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                text = log_line(raw)
                if not text or text.lstrip("> :") in echoes:
                    continue
                if pattern:
                    if pattern.search(text):
                        printed += 1
                        print(text, flush=True)
                elif args.all or any(k in text for k in LOG_KEEP):
                    printed += 1
                    print(text, flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        try:
            s.write(b"\x03")
            s.close()
        except Exception:
            pass
    if reconnects:
        print(f"[flipctl] reconnected {reconnects}x during this stream",
              file=sys.stderr, flush=True)
    if printed == 0:
        # Silence is normal: an idle app logs nothing. Say so, so that an empty
        # capture is not mistaken for a broken log stream.
        print("[flipctl] no matching log lines - the device was idle. Lines "
              "appear when a card enters the field or the app logs something.",
              flush=True)
    return 0


def cmd_mem(args):
    port = find_port()
    require_free_port(port, force=args.force)
    f = Flipper(port)
    try:
        if args.samples <= 1 and not args.cost:
            show_heap(f, label=f.app_running() or "desktop")
            return 0

        if args.cost:
            running = f.app_running()
            if not running:
                die("nothing is running - launch the app first "
                    "(tools/flipper/flipctl deploy)")
            with_app = f.heap()
            print(f"with {running}:")
            show_heap(f, stats=with_app)
            if not ensure_closed(f):
                die("could not close the app to measure the baseline")
            time.sleep(1.0)
            idle = f.heap()
            print("\nidle (desktop):")
            show_heap(f, stats=idle)
            cost = idle.get("Free heap size", 0) - with_app.get("Free heap size", 0)
            print(f"\n{running} costs {cost:,} bytes of heap "
                  f"({cost / 1024:.1f} KB of {idle.get('Total heap size', 0) / 1024:.0f} KB total)")
            return 0

        print(f"{'t':>6}  {'free':>9}  {'low-water':>9}  {'max block':>9}")
        start = time.time()
        for i in range(args.samples):
            h = f.heap()
            print(f"{time.time() - start:6.1f}  {h.get('Free heap size', 0):9,}  "
                  f"{h.get('Minimum heap size', 0):9,}  {h.get('Maximum heap block', 0):9,}",
                  flush=True)
            if i + 1 < args.samples:
                time.sleep(args.interval)
    finally:
        f.close()
    return 0


def show_heap(f: Flipper, label: str = "", stats: dict | None = None):
    h = stats if stats is not None else f.heap()
    if not h:
        die("the CLI did not answer `free`")
    total = h.get("Total heap size", 0)
    free = h.get("Free heap size", 0)
    if label:
        print(f"running: {label}")
    print(f"  free heap        {free:,} of {total:,} ({100 * free / total:.0f}% free)")
    print(f"  low-water        {h.get('Minimum heap size', 0):,}  (smallest free heap since boot)")
    print(f"  largest block    {h.get('Maximum heap block', 0):,}  "
          "(a big gap from free heap means fragmentation)")


def snapshot(port: str, out: str, scale: int = 3, invert: bool = False,
             timeout: float = 20.0):
    """Capture the screen over RPC and write it as a PNG. Dies on failure."""
    try:
        from flipperzero_protobuf.flipper_proto import FlipperProto
    except ImportError:
        die("flipperzero-protobuf is not installed - run "
            "tools/flipper/flipctl doctor to rebuild the venv")

    import signal

    class Timeout(Exception):
        pass

    def on_alarm(signum, frame):
        raise Timeout()

    signal.signal(signal.SIGALRM, on_alarm)
    signal.alarm(int(timeout))
    proto = None
    try:
        # FlipperProto's constructor opens the port and starts the RPC session;
        # do not call start_rpc_session() as well or the handshake is sent twice.
        proto = FlipperProto()
        frame = proto.rpc_gui_snapshot_screen()
        signal.alarm(0)
        write_png(frame, out, scale=scale, invert=invert)
        print(f"captured {out}")
    except Timeout:
        if not cli_alive(port):
            die(f"the RPC screenshot did not answer within {timeout:g}s, and "
                "neither does the CLI.\n\n" + HALTED_ADVICE)
        die(f"the RPC screenshot did not answer within {timeout:g}s, but the "
            "CLI is alive - the device is busy (a scan in progress, or ufbt still "
            "holding the port). Retry, or `tools/flipper/flipctl reboot`.")
    finally:
        signal.alarm(0)
        if proto is not None:
            try:
                # Always end the session: a half-open one leaves the serial port
                # in RPC mode and the text CLI stops answering.
                proto.rpc_stop_session()
            except Exception:
                pass


def cmd_shot(args):
    port = find_port()
    require_free_port(port, force=args.force)

    if args.keys or args.settle:
        f = Flipper(port)
        try:
            f.press(args.keys, settle=args.settle)
        finally:
            f.close()
        time.sleep(0.4)

    snapshot(port, args.out, scale=args.scale, invert=args.invert,
             timeout=args.timeout)
    return 0


def write_png(frame: bytes, path: str, scale: int = 3, invert: bool = False):
    """The screen comes back as a column-major 1bpp bitmap, 8 rows per byte."""
    import struct
    import zlib

    W, H = 128, 64
    on, off = (0xE8, 0x11) if invert else (0x11, 0xE8)
    rows = b""
    for y in range(H):
        line = bytearray()
        base = (y // 8) * W
        bit = y % 8
        for x in range(W):
            line += bytes([on if (frame[base + x] >> bit) & 1 else off]) * scale
        rows += (b"\x00" + bytes(line)) * scale

    def chunk(tag, payload):
        body = tag + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", W * scale, H * scale, 8, 0, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(rows, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as fh:
        fh.write(png)


def cmd_pull(args):
    port = find_port()
    require_free_port(port, force=args.force)
    storage = sdk_storage()
    # `receive` writes the file itself. The obvious-looking `read` dumps it to
    # stdout behind a progress meter and a "Text data:" banner, and once the
    # payload is binary there is no telling that framing from the file's own
    # bytes - a card dump pulled that way is silently wrong, which is the one
    # kind of wrong this tool exists to avoid.
    argv = [sdk_python(), storage, "-p", port, "receive", args.remote, args.local]
    proc = subprocess.run(argv, capture_output=True, timeout=3600)
    if proc.returncode != 0 or not os.path.exists(args.local):
        die((proc.stderr or b"").decode("utf-8", "replace").strip() or
            f"storage receive failed for {args.remote}")
    print(f"{args.remote} -> {args.local} ({os.path.getsize(args.local):,} bytes)")
    return 0


def sdk_storage() -> str:
    """The SDK's storage.py, which owns the RPC file transfer both ways."""
    storage = os.path.expanduser("~/.ufbt/current/scripts/storage.py")
    if not os.path.exists(storage):
        die(f"{storage} not found - run ufbt once to fetch the SDK")
    return storage


def sdk_python() -> str:
    """The interpreter the SDK's own scripts expect.

    storage.py imports colorlog, which is installed in the ufbt toolchain's
    Python and not in flipctl's venv - ufbt runs it through fbtenv for exactly
    that reason. Running it under sys.executable dies at the import instead.
    """
    for path in sorted(glob.glob(
            os.path.expanduser("~/.ufbt/toolchain/*/bin/python3"))):
        if os.access(path, os.X_OK):
            return path
    return sys.executable


# storage.py's `send` was measured at under 6 KB/s over the CDC port: a 21 MB
# table ran for a full hour without finishing and halted the device doing it,
# twice. Anything much above a megabyte belongs on the SD card via a reader, so
# push refuses rather than tying up the port for hours and wedging the Flipper.
PUSH_SANE_MAX = 4 * 1024 * 1024
PUSH_RATE_BPS = 6_000.0


def cmd_push(args):
    if not os.path.exists(args.local):
        die(f"{args.local} does not exist")
    size = os.path.getsize(args.local)

    if size > PUSH_SANE_MAX and not args.force:
        die(f"{args.local} is {size / 1e6:.0f} MB. Over this port that is about "
            f"{size / PUSH_RATE_BPS / 60:.0f} minutes, and a transfer that long "
            "has halted the device every time it has been tried.\n\n"
            "Copy it with a card reader instead - power the Flipper down, take "
            f"the microSD out, and put the file at:\n"
            f"    <SD card>{args.remote.replace('/ext', '')}\n\n"
            "Pass --force to push it over USB anyway.")

    port = find_port()
    require_free_port(port, force=args.force)
    storage = sdk_storage()

    # The reference tables land in a directory the app creates on first run, so
    # it may not exist on a device that has only ever been flashed. mkdir is
    # best effort: it fails harmlessly when the directory is already there.
    parent = os.path.dirname(args.remote)
    if parent:
        subprocess.run([sdk_python(), storage, "-p", port, "mkdir", parent],
                       capture_output=True, timeout=60)

    print(f"{args.local} -> {args.remote} ({size:,} bytes)", flush=True)
    if size > PUSH_SANE_MAX:
        print(f"[flipctl] --force: expect roughly "
              f"{size / PUSH_RATE_BPS / 60:.0f} minutes", flush=True)
    argv = [sdk_python(), storage, "-p", port, "send", args.local, args.remote]
    proc = subprocess.run(argv, capture_output=True, timeout=3600)
    if proc.returncode != 0:
        die((proc.stderr or b"").decode("utf-8", "replace").strip() or "storage send failed")
    print("sent")
    return 0


def cmd_ls(args):
    port = find_port()
    require_free_port(port, force=args.force)
    f = Flipper(port)
    try:
        print(f.cmd(f"storage list {args.path}", limit=15.0, retry=True))
    finally:
        f.close()
    return 0


# Everything the firmware prints when something goes wrong. A crash halts the
# device with a message on screen and these lines on the serial port.
CRASH_MARKERS = (
    "furi_check failed", "furi_assert failed", "Fatal", "fatal",
    "HardFault", "MemManage", "BusFault", "UsageFault", "Stack overflow",
    "stack overflow", "[E]", "Crash", "crashed", "out of memory",
)


def debug_elf(appid: str) -> str:
    """The unstripped ELF that matches the last build, for symbolising."""
    for path in (os.path.join(PROJECT, "dist", "debug", f"{appid}_d.elf"),
                 os.path.expanduser(f"~/.ufbt/build/{appid}_d.elf")):
        if os.path.exists(path):
            return path
    die(f"no debug ELF for '{appid}' - build first "
        "(tools/flipper/flipctl deploy --build-only)")


def toolchain_bin(name: str) -> str:
    hits = sorted(glob.glob(os.path.expanduser(f"~/.ufbt/toolchain/*/bin/{name}")))
    if not hits:
        die(f"{name} not found in the ufbt toolchain")
    return hits[0]


def cmd_sym(args):
    """Turn crash addresses into file:line.

    A .fap is relocated when it is loaded, so a runtime address is the ELF
    address plus wherever the loader put the app. The loader logs that base at
    debug level; pass it as --base and the addresses map straight through.
    """
    elf = debug_elf(args.appid)
    base = int(args.base, 0) if args.base else 0
    addrs = []
    for a in args.addrs:
        value = int(a, 0)
        addrs.append(hex(value - base))
    out = subprocess.run(
        [toolchain_bin("arm-none-eabi-addr2line"), "-e", elf, "-f", "-C", "-i"] + addrs,
        capture_output=True, text=True)
    print(f"# {os.path.relpath(elf, PROJECT) if elf.startswith(PROJECT) else elf}"
          f"{f' (base {args.base})' if args.base else ''}")
    print(out.stdout.strip() or out.stderr.strip())
    return 0


def cmd_size(args):
    """Report what the .fap costs in RAM.

    The whole .fap is loaded into the Flipper's 190 KB heap, so every ALLOC
    section is heap the app has spent before main() runs. Sections without the
    ALLOC flag - notably .fapassets, where the station table lives - are
    unpacked to the SD card by the firmware and never reach RAM.
    """
    elf = debug_elf(args.appid)
    fap = os.path.join(PROJECT, "dist", f"{args.appid}.fap")
    readelf = toolchain_bin("arm-none-eabi-readelf")
    target = fap if os.path.exists(fap) else elf
    out = subprocess.run([readelf, "-S", "-W", target], capture_output=True, text=True).stdout

    ram = 0
    rows = []
    for line in out.splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+\S+\s+\S+\s+([0-9a-f]+)\s+\S+\s+(\S*)",
                     line)
        if not m:
            continue
        name, kind, size_hex, flags = m.groups()
        size = int(size_hex, 16)
        if not size or name == "":
            continue
        alloc = "A" in flags
        if alloc:
            ram += size
        rows.append((name, size, alloc))

    print(f"{os.path.relpath(target, PROJECT)}  ({os.path.getsize(target):,} bytes on disk)")
    print(f"{'section':<20} {'bytes':>9}  in RAM")
    for name, size, alloc in sorted(rows, key=lambda r: -r[1]):
        print(f"{name:<20} {size:>9,}  {'yes' if alloc else 'no'}")
    print(f"{'TOTAL IN RAM':<20} {ram:>9,}  "
          f"({100 * ram / 190144:.0f}% of the 190 KB heap, before any allocation)")
    return 0


def cli_alive(port: str, wait: float = 1.2) -> bool:
    """Does the text CLI still answer? The one reliable liveness test.

    Opens with retries, because this is what tells a crash apart from a
    transient endpoint drop: answering "no" to the second one reports a fault
    that never happened. The retries cost nothing in the case that matters - a
    halted device still enumerates, so the open succeeds and the read is empty.

    Alive means a prompt comes back, not just bytes. On 2026-09-28, after a
    launch that never came up, the device answered this check while every
    command sent to it returned nothing at all - it passed the old "any bytes"
    test, and the empty output that followed was read as real answers.
    """
    try:
        s = open_serial(port, timeout=0.3, attempts=3)
        s.write(b"\x03\r\n")
        time.sleep(wait)
        reply = ANSI.sub(b"", s.read(65536))
        s.close()
        return b">:" in reply
    except Exception:
        return False


HALTED_ADVICE = (
    "The device is halted. That is what a furi_check / furi_assert failure does:\n"
    "  - the failure message is on the Flipper's screen, and nowhere else\n"
    "  - the CLI, RPC and `power reboot` are all dead, so nothing here can\n"
    "    read it or restart the device\n"
    "\nAsk the user to read the message off the screen, then to hold LEFT + BACK\n"
    "for about five seconds to reset it."
)


def cmd_crash(args):
    """Detect a crash, and capture the evidence that a crash actually leaves.

    Worth knowing before reading this: a crash prints **nothing** over USB. The
    handler runs with the RTOS already down and writes to the GPIO UART console,
    not the CDC port - a 40-second capture across a deliberate crash returned
    zero bytes. So there is no crash text to grep for.

    What is observable is the device going silent. The last log line before that
    silence is the best localisation available without a hardware debugger, so
    that is what this captures.
    """
    import serial

    port = find_port()
    require_free_port(port, force=args.force)

    if not args.watch:
        if cli_alive(port):
            print("The CLI is answering - the device is not in a crashed state.")
            f = Flipper(port)
            try:
                print(f"running: {f.app_running() or 'nothing (desktop)'}")
                show_heap(f)
            finally:
                f.close()
            return 0
        print(f"{port} is present but the CLI is silent.\n")
        print(HALTED_ADVICE)
        return 2

    if not cli_alive(port):
        print("The device is already halted - recover it before watching.\n")
        print(HALTED_ADVICE)
        return 2

    print(f"[flipctl] watching for a crash for {args.watch:g}s - reproduce it now",
          flush=True)
    def arm(handle):
        for _ in range(3):
            handle.write(b"\x03")
            time.sleep(0.2)
            handle.read(65536)
        handle.write(f"\r\nlog {args.level}\r\n".encode())
        time.sleep(0.6)
        handle.read(65536)

    s = open_serial(port)
    arm(s)

    recent: list[str] = []
    deadline = time.time() + args.watch
    buf = b""
    last_line_at = time.time()
    crashed = False
    while time.time() < deadline:
        try:
            chunk = s.read(512)
        except (serial.SerialException, OSError):
            # The port went away underneath us. That is what a crash looks like
            # - but it is also what the transient CDC drop looks like, and
            # calling that a crash sends the reader hunting a fault that never
            # happened. Ask the device instead: a halted Flipper does not
            # answer the CLI, a healthy one does.
            try:
                s.close()
            except Exception:
                pass
            if cli_alive(port):
                s = open_serial(port)
                arm(s)
                buf = b""
                continue
            crashed = True
            break
        if chunk:
            buf += chunk
            last_line_at = time.time()
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                text = ANSI.sub(b"", raw).decode("utf-8", "replace").replace("\r", "").strip()
                if text:
                    recent.append(text)
                    del recent[:-40]
                    if args.print_lines:
                        print(text, flush=True)
            continue
        time.sleep(0.05)
        # Silence alone means nothing - an idle app logs nothing either. Only
        # check for a halt once the device has had a quiet spell, and only by
        # asking the CLI on a second connection.
        if time.time() - last_line_at > 3.0:
            if not cli_alive(port, wait=0.8):
                crashed = True
                break
            last_line_at = time.time()
    try:
        s.write(b"\x03")
        s.close()
    except Exception:
        pass

    if not crashed:
        print("[flipctl] no crash in the window - the CLI is still answering.")
        if recent:
            print(f"[flipctl] last {min(len(recent), 5)} log line(s):")
            print("\n".join(recent[-5:]))
        return 0

    print("\n[flipctl] the device stopped answering: it crashed.\n")
    if recent:
        print("Last lines before the silence - the fault is after the last one "
              "of these:")
        print("\n".join(recent))
    else:
        print("Nothing was logged before the silence. Add FURI_LOG_D calls along "
              "the suspect path and reproduce: the last line that appears is the "
              "furthest the code got.")
    print()
    print(HALTED_ADVICE)
    print("\nOnce it is back, map any address from the screen with:\n"
          "  tools/flipper/flipctl sym 0x... [--base 0x...]")
    return 2


def build_parser():
    p = argparse.ArgumentParser(
        prog="flipctl", description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--force", action="store_true",
                   help="kill any process holding the serial port, not just known helpers")
    p.add_argument("--appid", default="flipso", help="application id (default: flipso)")
    sub = p.add_subparsers(dest="sub", required=True)

    s = sub.add_parser("doctor", help="check host tooling, the port and the device")
    s.set_defaults(func=cmd_doctor)

    s = sub.add_parser("ready", help="make sure the app is running and responds, "
                                     "recovering a wedged device")
    s.add_argument("--desktop", action="store_true",
                   help="only require a usable device, not a running app")
    s.add_argument("--no-reboot", action="store_true",
                   help="fail rather than rebooting a device that will not let go")
    s.set_defaults(func=cmd_ready)

    s = sub.add_parser("arm", help="start a scan and prove the NFC field is "
                                   "polling - the check to run before asking "
                                   "anyone to tap a card")
    s.add_argument("--shot", metavar="PATH",
                   help="also capture the armed screen as a PNG")
    s.add_argument("--timeout", type=float, default=5.0,
                   help="seconds to watch for NFC activity (default: 5)")
    s.add_argument("--tries", type=int, default=2,
                   help="relaunch-and-retry attempts (default: 2)")
    s.add_argument("--no-reboot", action="store_true",
                   help="fail rather than rebooting a device that will not let go")
    s.set_defaults(func=cmd_arm)

    s = sub.add_parser("scan", help="read a card already lying on the reader, "
                                    "following the read in the log")
    s.add_argument("--shot", metavar="PATH",
                   help="also capture the result screen as a PNG")
    s.add_argument("--timeout", type=float, default=8.0,
                   help="seconds to wait for a card to answer (default: 8)")
    s.add_argument("--tries", type=int, default=2,
                   help="relaunch-and-retry attempts (default: 2)")
    s.add_argument("--no-reboot", action="store_true",
                   help="fail rather than rebooting a device that will not let go")
    s.set_defaults(func=cmd_scan)

    s = sub.add_parser("cmd", help="run Flipper CLI commands")
    s.add_argument("command", nargs="+")
    s.add_argument("--timeout", type=float, default=20.0)
    s.set_defaults(func=cmd_cmd)

    s = sub.add_parser("keys", help="send key presses: up down ok back left right, "
                                    "'ok:long' for a long press, '@1.5' to wait")
    s.add_argument("keys", nargs="+")
    s.add_argument("--settle", type=float, default=0.3, help="pause between keys")
    s.add_argument("--check", action="store_true", help="print what is running afterwards")
    s.set_defaults(func=cmd_keys)

    s = sub.add_parser("close", help="back out of the running app to the desktop")
    s.set_defaults(func=cmd_close)

    s = sub.add_parser("deploy", help="close the app, build, upload and launch")
    s.add_argument("--no-close", action="store_true",
                   help="do not try to close a running app first")
    s.add_argument("--build-only", action="store_true", help="compile without uploading")
    s.add_argument("--no-reboot", action="store_true",
                   help="fail instead of rebooting when the app will not close")
    s.add_argument("--timeout", type=float, default=300.0, help="compile timeout")
    s.add_argument("--launch-timeout", type=float, default=None,
                   help="upload timeout before rebooting and retrying "
                        "(default: scaled from the .fap size)")
    s.set_defaults(func=cmd_deploy)

    s = sub.add_parser("reboot", help="reboot the device and wait for it to come back")
    s.set_defaults(func=cmd_reboot)

    s = sub.add_parser("log", help="stream the device log")
    s.add_argument("--level", default="debug",
                   choices=["error", "warn", "info", "debug", "trace", "default"])
    s.add_argument("--seconds", type=float, default=0, help="stop after N seconds (0 = forever)")
    s.add_argument("--all", action="store_true", help="do not filter to app/NFC/error lines")
    s.add_argument("--grep", help="only print lines matching this regex")
    s.add_argument("--keys", nargs="+", default=[],
                   help="key presses to send before streaming starts")
    s.add_argument("--settle", type=float, default=0.3)
    s.add_argument("--arm", action="store_true",
                   help="run `arm` first and refuse to stream unless the NFC "
                        "field is polling")
    s.add_argument("--shot", metavar="PATH",
                   help="with --arm, capture the armed screen as a PNG")
    s.add_argument("--launch", action="store_true",
                   help="open the app in the same session the stream starts in, "
                        "so its startup is logged (the app must not be running)")
    s.set_defaults(func=cmd_log)

    s = sub.add_parser("mem", help="heap snapshot, sampling, or the app's cost")
    s.add_argument("--samples", type=int, default=1)
    s.add_argument("--interval", type=float, default=2.0)
    s.add_argument("--cost", action="store_true",
                   help="measure what the running app costs (closes it)")
    s.set_defaults(func=cmd_mem)

    s = sub.add_parser("shot", help="capture the screen as a PNG")
    s.add_argument("out")
    s.add_argument("keys", nargs="*", help="keys to send before capturing")
    s.add_argument("--settle", type=float, default=0.3)
    s.add_argument("--scale", type=int, default=3)
    s.add_argument("--invert", action="store_true")
    s.add_argument("--timeout", type=float, default=20.0)
    s.set_defaults(func=cmd_shot)

    s = sub.add_parser("crash", help="check for, or catch, a device crash")
    s.add_argument("--watch", type=float, default=0,
                   help="watch for N seconds and report the lines before a crash")
    s.add_argument("--level", default="debug",
                   choices=["error", "warn", "info", "debug", "trace"])
    s.add_argument("--print-lines", action="store_true",
                   help="print log lines as they arrive, not just at the end")
    s.set_defaults(func=cmd_crash)

    s = sub.add_parser("sym", help="map crash addresses to file:line")
    s.add_argument("addrs", nargs="+")
    s.add_argument("--base", help="load address of the .fap, from the loader's debug log")
    s.set_defaults(func=cmd_sym)

    s = sub.add_parser("size", help="what the .fap costs in RAM, section by section")
    s.set_defaults(func=cmd_size)

    s = sub.add_parser("pull", help="copy a file off the SD card")
    s.add_argument("remote")
    s.add_argument("local")
    s.set_defaults(func=cmd_pull)

    s = sub.add_parser("push", help="copy a file onto the SD card")
    s.add_argument("local")
    s.add_argument("remote")
    s.set_defaults(func=cmd_push)

    s = sub.add_parser("ls", help="list an SD card directory")
    s.add_argument("path")
    s.set_defaults(func=cmd_ls)
    return p


def main():
    args = build_parser().parse_args()
    try:
        rc = args.func(args)
    except CliSilent as exc:
        print(f"flipctl: the CLI accepted `{exc}` and printed nothing back - it "
              "takes input without running it.\n", file=sys.stderr)
        print(HALTED_ADVICE, file=sys.stderr)
        rc = 2
    raise SystemExit(rc or 0)


if __name__ == "__main__":
    main()
