# Read CMD9 and CMD10 cards (full ITSO shell on NFC Type 2 tags)

## Goal

ITSO defines full-shell media on Type 2 tags: **CMD9** (NXP NTAG21x) and
**CMD10** (MIFARE Ultralight EV1). Flipso detects them and stops:

- `itso_type2_kind()` (`itso/itso_parse.c`) returns `ItsoType2FullShell` when
  the shell at `ITSO_TYPE2_SHELL_OFFSET` is a full one.
- `flipso_type2_read()` (`flipso_type2.c`) then parses the shell, logs the FVC
  and returns `FlipsoReaderStatusUnsupported`; the error screen says "on a kind
  of NFC tag that Flipso cannot read yet" with `Reason: Media type CMD9`.

## What is already there

- Page reading over the raw ISO 14443-3A poller (`flipso_type2_read_pages()`),
  capped at `FLIPSO_TYPE2_MAX_BYTES` (256). NTAG213/215/216 are 180/540/924
  bytes, so the cap must grow — the buffer lives in `FlipsoType2`, allocated
  only when the Type 2 transport runs; mind the heap (`flipctl mem`).
- Directory, SCT and chained product reads are media-independent:
  `itso_parse_directory()`, `itso_read_chain()` (takes a sector-read callback —
  write one that maps a logical sector to its pages), `flipso_reader_read_groups()`
  with a `FlipsoGroupSource`. CMD2 (`flipso_cmd2.c`) is the model to copy.
- Saved cards: a new transport should capture Shell/Directory/Product/Log blocks
  like DESFire does (not the Type 2 page block), so saved files replay through
  `flipso_capture_decode()` unchanged.

## What needs research first

Read **TS 1000-10** (linked from the README) for CMD9 and CMD10:

- Page layout: where each logical sector starts, the sector size B and count S
  the shell will state, and whether sectors are contiguous pages.
- Where the directory and the log live (DESFire reserves S-2/S-1; CMD2 keeps two
  directory copies for anti-tear — check which model these follow).
- Lock/OTP pages and any password-protected areas (NTAG PWD_AUTH) — reads of
  protected pages NAK, which the page reader currently treats as end of memory.
- Whether the tag answers READ past its end by wrapping (Ultralight) or NAKing
  (NTAG), since that decides how the page reader detects the end.

Record the findings in `docs/PROTOCOL.md` with clause citations.

## Tests

- Build a synthetic CMD9 card in `tools/test/build_card.py` / `itso_build.py`
  (the CMD4 fixtures are the model) and decode it on the host.
- Add a demo card in `tools/demo/build_demo_cards.py` so the screens can be seen
  on the device without owning such a card.
- A real card, if the user has one, only after a green `flipctl arm`.

## Done when

- A synthetic CMD9 and CMD10 card decode on the host and on the device via a
  demo file; the Unsupported path remains only for genuinely unknown FVCs.
- The error text's CMD9/CMD10 case is removed or narrowed accordingly.
