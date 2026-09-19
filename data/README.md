# Shipped, but not packaged

`assets/` is bundled into the `.fap`. This directory is the other kind of
reference data: files that ship with the project ready-made, but are copied to
the Flipper's SD card rather than travelling inside the app.

- `naptan.dat` — the NaPTAN bus stop table, every active stop in Great Britain.
  Built by `tools/naptan/build_naptan.py`; see `tools/naptan/SOURCES.md` for the
  licensing and `tools/naptan/FORMAT.md` for the layout.

## Why not `assets/`

Anything in `assets/` goes into the `.fap`, and `ufbt launch` re-uploads the
whole `.fap` over USB on every install. At 21 MB the stop table takes upwards of
ten minutes each time, and a transfer that is interrupted leaves a half-written
`.fap` that the loader reports as **"invalid file"**. The station table is 79 KB
and has no such problem, which is why it stays packaged.

Copying this file to the SD card directly is seconds instead of minutes, and it
only has to happen when the table changes rather than on every deploy.

## Installing it

Fastest, and what to do for 21 MB: power the Flipper down, take the microSD card
out, and copy the file into `apps_data/flipso/` on it.

```
<SD card>/apps_data/flipso/naptan.dat
```

Over USB instead, which is the same transfer the `.fap` was doing and takes
about as long:

```bash
tools/flipper/flipctl push data/naptan.dat /ext/apps_data/flipso/naptan.dat
```

Either way the app reads it from `/ext/apps_data/flipso/naptan.dat`, in
preference to a packaged table, and simply shows bare stop codes when it is not
there.

`/ext/apps_data/` is left alone by the firmware. `/ext/apps_assets/` is not —
that is where `.fapassets` is unpacked, so the firmware owns its contents and a
file placed there by hand is not guaranteed to survive an install.
