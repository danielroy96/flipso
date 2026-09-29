"""Fail when a storage open's failure branch leaves without closing.

The Flipper storage service registers a path before it tries to open it, and
only storage_file_close() / storage_dir_close() / file_stream_close() takes the
registration back off - including after a failed open (the SDK says so on
storage_file_open). A registration left behind outlives the app: the next
launch's open of the same path is told it is already open and waits for ever.
Flipso hung that way on every second launch, behind the desktop, until
2026-09-26.

This checks the pattern that caused it: `if(!<open>(...)) {` whose block exits
with `return` or `continue` before any close. A close after the if, on the
shared path, is fine and is not looked at.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OPEN = re.compile(r"if\(!\s*(storage_file_open|storage_dir_open|file_stream_open|"
                  r"buffered_file_stream_open)\(")
CLOSE = re.compile(r"(storage_file_close|storage_dir_close|file_stream_close|"
                   r"buffered_file_stream_close)\(")
EXIT = re.compile(r"\b(return|continue|break|goto)\b")

problems = []
sources = [p for d in ("", "reader", "cards", "lookup", "format") for p in ROOT.glob(d + "/*.c" if d else "*.c")] + list(ROOT.glob("scenes/*.c")) + list(ROOT.glob("views/*.c"))
for path in sources:
    text = path.read_text()
    for m in OPEN.finditer(text):
        # Find the block the if opens: from its first '{' to the matching '}'.
        start = text.find("{", m.end())
        depth, i = 0, start
        while i < len(text):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        block = text[start:i]
        exit_at = EXIT.search(block)
        close_at = CLOSE.search(block)
        if exit_at and (not close_at or close_at.start() > exit_at.start()):
            line = text.count("\n", 0, m.start()) + 1
            problems.append(f"{path.relative_to(ROOT)}:{line}: failed {m.group(1)} "
                            "leaves without closing")

for p in problems:
    print(p)
print("FAILED" if problems else "All storage opens close on failure")
sys.exit(1 if problems else 0)
