---
description: Profile Flipso's memory on the device and report what it costs
argument-hint: "[what changed]"
allowed-tools: Bash(tools/flipper/flipctl:*), Bash(tools/test/run.sh)
---

Profile Flipso's memory use. Context: $ARGUMENTS

Cover:

1. `tools/flipper/flipctl size` — what the `.fap` costs in RAM before it runs,
   and whether anything new landed in an ALLOC section.
2. `tools/flipper/flipctl mem` — free heap, the since-boot low-water mark, and
   the largest free block.
3. If a specific path changed, exercise it with `tools/flipper/flipctl keys`
   and re-sample: the low-water mark is what says whether it ever pressured
   memory.

Report measured numbers, not estimates, and say which device state they came
from — the idle baseline moves between boots. Follow the **flipper-memory**
skill.
