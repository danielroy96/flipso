# Reference data

Flipso comes with the following optional datasets:

- `naptan.dat` — the NaPTAN bus stop table, every active stop in Great Britain.
  Built by `tools/naptan/build_naptan.py`; see `tools/naptan/SOURCES.md` for the
  licensing and `tools/naptan/FORMAT.md` for the layout.

## Why do I need to install this manually?

Unfortunately anything in `assets/` goes into the `.fap`. At 21 MB the stop 
table takes upwards of ten minutes to install.

Copying this file to the SD card directly is seconds instead of minutes and
I don't much enjoy watching paint dry.

## Installing it

Power the Flipper down, take the microSD card out, and copy the file 
into `apps_data/flipso/` on it. Flipso will load it when next started. You can
check the install status in the About menu.

```
<SD card>/apps_data/flipso/naptan.dat
```