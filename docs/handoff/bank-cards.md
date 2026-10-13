# Recognise contactless bank cards and phone wallets

## Goal

UK buses and trains take contactless bank cards, so people will hold their
debit card or phone against a Flipper running a "UK travel card" app. Today
that ends on "Not an ITSO card / This card has no ITSO travel data on it"
(`scenes/flipso_scene_error.c`), which is true and unhelpful: the holder wants
to know why a card they travel on every day shows nothing.

Recognise an EMV payment card or wallet, name the scheme, and explain where
its journeys live instead.

Size: small, a day.

## What to build

1. **Detect it without reading anything personal.** In the ISO 14443-4 path,
   after the CMD2 transport has failed to select ITSO and before it returns
   `FlipsoReaderStatusNotItso` (`reader/flipso_cmd2.c`, start of
   `flipso_cmd2_read()`), send one SELECT of the contactless payment directory,
   PPSE: `00 A4 04 00 0E` + `"2PAY.SYS.DDF01"` + `00`. A 9000 with an FCI is a
   payment card or wallet. From the FCI take only:
   - the application identifiers (tag `4F`) - map the RID prefix to a scheme:
     `A000000003` Visa, `A000000004` Mastercard (incl. Maestro), `A000000025`
     American Express, `A000000065` JCB, `A000000152` Discover/Diners,
     `A000000333` UnionPay;
   - the application label (tag `50`), as a fallback name.
   **Never send GET PROCESSING OPTIONS or READ RECORD**: those return the
   card number. The select alone is enough, and keeping to it keeps Flipso
   from reading payment data at all.
2. **A status** `FlipsoReaderStatusBankCard` (`reader/flipso_scan_session.h`),
   with the scheme kept somewhere small on the reader result (a byte for the
   scheme, not the label).
3. **The error screen** (`flipso_scene_error.c`), in the style of the Oyster
   case: title "Contactless bank card" (or "Visa contactless" when known), and
   a detail along the lines of: pay-as-you-go fares are worked out by the
   operator's back office, nothing about journeys is stored on the card, and
   the operator's website or app shows them. Keep each line within 128 px as
   the other details are. One button, "Scan again".
4. A phone wallet (Apple Pay, Google Wallet) answers PPSE the same way; say
   "card or phone" in the words rather than trying to tell them apart.

## Constraints

- The scan session (`reader/flipso_scan_session.c`, host-tested by
  `tools/test/test_scan_session.c`) decides which transport runs next and what
  ends a scan; the new status must end it, not trigger a retry.
- Keep it out of the capture: there is nothing to save, and nothing from a
  payment card should reach the SD card.
- The scan's sound: a bank card is not a failed read, so use the plain end
  tone, not the error one - decide by trying both.

## Tests

- `tools/test/test_scan_session.c`: the new status ends the session.
- A host test of the FCI parser with a Visa and a Mastercard PPSE response
  (published examples are in EMV Book 1; write them as byte arrays exactly as
  long as the data).
- On the device: a real debit card and a phone wallet. Ask the user once,
  after a green `flipctl arm`, as CLAUDE.md requires.

## Done when

- A bank card or phone shows a screen that says what it is and why there is
  nothing to show, having sent nothing but the PPSE select.
- `tools/test/run.sh` passes and `ufbt lint` is clean.
- This brief and its row in `README.md` here are deleted.
