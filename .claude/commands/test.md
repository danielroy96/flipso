---
description: Run the host decoder test suite under ASan and UBSan
allowed-tools: Bash(tools/test/run.sh), Bash(tools/test/run.sh -v), Bash(tools/test/replay.py:*)
---

Run `tools/test/run.sh`.

It builds and runs every host suite under ASan and UBSan - the decoder, the
scan session, saved cards, the station and stop tables, operators, every
screen of every demo card, card media, both views - plus the storage lint and
flipctl's own tests. No hardware needed; about 9 s.

On success it prints one line with the number of checks. On failure it prints
each `[FAIL]` with its got/wanted detail, then the last 40 lines of the log,
which is where a compiler error or a sanitiser report lands, and exits
non-zero. The full log's path is on the last line; `-v` streams everything.

Do not pipe it through `grep` or `tail` - that reports their exit status, not
the suite's. Report pass or fail. On a sanitiser report, give the stack and
the input that triggered it - those are real bugs, not test noise.
