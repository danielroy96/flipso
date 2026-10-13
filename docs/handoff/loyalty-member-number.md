# Show a loyalty product's membership number

## Goal

ITSO's two loyalty IPEs have no field for an account number. Instead the spec
says how to make one (TS 1000-5 clause 2.3.1.3 rule 1 for TYP 3, clause 2.8.1.2
for TYP 17, worded identically):

> a concatenation of IIN, OID, TYP, PTYP, creating ISAM serial number and
> creating ISAM sequence number shall be used, where the IPE identity elements
> IIN, OID, TYP and PTYP shall identify the individual loyalty scheme, and IPE
> Instance information shall identify the member.

Flipso reads every one of those already, so it can show the number a scheme
would know its member by. Today a loyalty product shows its points (TYP 3,
`itso/ipe/itso_ipe_loyalty.c`) or nothing beyond its directory entry (TYP 17,
which carries only the common elements - TS 1000-5 table 25).

Size: small, half a day.

## What to build

1. **The parts**, all on `ItsoProduct` (`itso/ipe/itso_product.h`):
   - IIN: the product's own (`has_iin`/`iin`, IPEBitMap bit 0) when present,
     otherwise the shell's (`card->iin`) - the IIN that owns the OID;
   - OID: `product->oid` (after any EF extension);
   - TYP and PTYP: `product->typ`, `product->ptyp`;
   - the creating ISAM and its sequence number: the IPE InstanceID,
     `isam_id` and `isam_seq`, only when `instance_valid`.
2. **"ISAM serial number"**: the ISAM ID is the registered OID in its top
   bits and a serial in the rest (TS 1000-2 annex B, `itso_isam_oid()` in
   `itso/itso_util.h`). Decide whether the spec means the whole 32-bit ISAM ID
   or only its serial part - search TS 1000-2 annex B for "serial" - and write
   the reading taken into PROTOCOL.md.
3. **The format.** The spec gives none. Something a person can read out:
   decimal groups as the card number is shown, e.g.
   `633597 0289 03 01 60090128 000001` (IIN, OID, TYP, PTYP, ISAM in hex as
   the Technical pages already show ISAMs, sequence). Check the width: it is
   longer than a row, so it belongs on its own line, `Member number:` with the
   number on a detail line, or grouped to wrap between groups.
4. **Where**: the loyalty product's first page, since it is what identifies
   the holder to the scheme - for TYP 3 under the points, for TYP 17 as the
   page's main line ("Member of: <operator>" and the number). Both are
   `FlipsoKindOther` in `format/product/flipso_product_pages.c`.
5. A product without an InstanceID (a CMD11 card redacts it - see
   `cmd11-pim.md`) shows no number rather than a partial one.

## Tests

- `tools/test/build_card.py` already builds TYP 3 and TYP 17 products for
  Demo 01 (products 5 and 9). `tools/test/screen_text/`: both show the number,
  built from their known InstanceIDs; one without `instance_valid` shows none.

## Done when

- Every loyalty product with an InstanceID shows the membership number the
  spec defines, and PROTOCOL.md says how it is built.
- `tools/test/run.sh` passes and `ufbt lint` is clean.
- This brief and its row in `README.md` here are deleted.
