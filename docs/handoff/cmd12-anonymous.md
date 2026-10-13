# Read what an Anonymous CMD12 card leaves readable

## Goal

CMD12 has two variants (TS 1000-10 clause 13.5). **Standard** is an ordinary
DESFire whose files are all freely readable. **Anonymous** is meant for phones
and other media that must not be trackable: the chip presents a random MID
(UID) at every tap, and the Shell and the IPE sector files need a key to read
(table 119: file 0 Key 2, files 1 to S-3 Key 1). But:

> The Directory and Log contain dynamic data and cannot be used to fingerprint
> a device. These files retain free read permission, thus enabling ISAM-less
> ticket inspection devices to verify the existence of ticket types and the
> holder's check-in/check-out status. (clause 13.5)

Flipso is exactly an ISAM-less inspection device. On an Anonymous card it
could show which operator issued it, what kinds of product it holds with their
owners, expiry and status, and the journey log - and today it shows "Card not
readable", because the shell read is refused and nothing else is tried.

No Anonymous card is known to have been seen. Build it from the spec with a
synthetic card. Do `cmd12-check.md` first: it settles how CMD12 is selected
and read at all.

Size: medium, two to three days.

## What to build

1. **Recognise it.** On CMD12 the SELECT's FCI (also file 31, freely readable)
   carries C0, a shell, and on Anonymous media C4, an eight-byte pseudo-MID
   (clause 13.8.1). C4 present means Anonymous. The C0 there is a
   **pseudo-shell**: "an operator specific pseudo-shell common to all CMs
   issued by that operator", with the issuer's OID and, recommended, ISSN 0,
   no MCRN, 0x18 bytes.
2. **Take the geometry and the issuer from the pseudo-shell.** It is a real
   shell structure, so `itso_parse_shell()` reads it; its OID is the issuing
   operator (titles the menu, as `card->oid` does), and B, S and E place the
   directory (S-1) and the log (S-2). Check against table 120 that the
   geometry is one of the eight permitted; refuse anything else, as the CMD9
   and CMD10 path does.
3. **Read the directory and the log**, which are free. Do not read file 0 or
   the sector files: they will be refused, and each refusal is a round trip.
   The card then has: directory entries (owner OID, TYP, PTYP, VGP, expiry,
   and status from the Sector Chain Table - unused, used, blocked) and the
   journeys. No product bodies, no value records, no holder.
4. **Say what is hidden, once.** The Card screen: "Card type: DESFire,
   anonymous (CMD12)", the card number shown as hidden (the pseudo-shell's
   number is not this card's), and one line explaining that the card keeps
   its details for ticket machines. Products show their directory-entry facts
   and a line that the rest is hidden, rather than a page of empties - the
   product screens already handle a product with no body
   (`ItsoProduct::body_parsed` false), so check what they say and reword it
   for this case.
5. **Saving.** Every Anonymous card of one operator has the same pseudo-shell
   number and a different UID at every tap, so neither identifies the card.
   Saving must never match an existing record (that would merge strangers'
   histories). Either disable Save for these, or always save as new with no
   merge - pick one and say why in PROTOCOL.md's Saved cards section.
6. **The UID** shown on the Card screen is random; say so rather than
   presenting it as the chip's serial.

## Constraints

- `flipso_desfire_read()` (`reader/flipso_desfire.c`) treats a shell it cannot
  read as `FlipsoReaderStatusBadShell` after sweeping all 32 files; on an
  Anonymous card that sweep is 32 refused reads. Detect C4 first and skip it.
- The DESFire chip description (`flipso_desfire_describe()`) may also be
  restricted on such media; treat a refusal there as nothing to show.
- PROTOCOL.md: a section under "How it reads the card", and the Limitations
  list updated.

## Tests

- A synthetic Anonymous card: the FCI with C0 (pseudo-shell, ISSN 0) and C4,
  a directory and a log, in `tools/test/` as byte arrays exactly as long as
  the data. The decode shows the issuer, each product's type, owner, expiry
  and status, and the journeys, and no card number.
- Saving such a card twice produces two records, or none, as decided.
- A demo card (`tools/demo/build_demo_cards.py`) so the screens can be walked
  on the device.

## Done when

- An Anonymous CMD12 card shows its issuer, its products' directory facts and
  its journeys, says plainly what the card keeps private, and is never merged
  with another card's record.
- `tools/test/run.sh` passes and `ufbt lint` is clean.
- This brief and its row in `README.md` here are deleted.
