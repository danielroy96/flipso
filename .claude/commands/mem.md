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
3. If a specific path changed, walk it: `tools/flipper/flipctl walk
   <scratch>/m --launch STEP...` prints the free heap, low-water mark and
   largest block after every step, beside a screenshot of each. The low-water
   mark is what says whether it ever pressured memory.
4. For a leak, get to the screen and then `flipctl walk <scratch>/leak ok back
   --repeat 5`: it reports the drift across the repeats.

Report measured numbers, not estimates, and say which device state they came
from — the idle baseline moves between boots. Follow the **flipper-memory**
skill.
