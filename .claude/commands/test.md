---
description: Run the host decoder test suite under ASan and UBSan
allowed-tools: Bash(tools/test/run.sh), Bash(tools/test/replay.py:*)
---

Run `tools/test/run.sh`.

It builds and runs four suites on this machine under ASan and UBSan: the ITSO
decoder, the station table, card media, and the icon list view. No hardware
needed.

Report pass or fail. On a sanitiser report, give the stack and the input that
triggered it — those are real bugs, not test noise. On a plain assertion
failure, show the expected and actual values.
