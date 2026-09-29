/**
 * @file flipso_type2.h
 * @brief NFC Type 2 tag transport for ITSO customer media definitions 4, 9 and 10.
 *
 * The page-based media: a MIFARE Ultralight or an Infineon my-d with a compact
 * shell (CMD4, TS 1000-10 section 5), the family SPT's Glasgow Subway paper
 * tickets use; and an NTAG215/216 (CMD9) or Ultralight EV1 (CMD10) with a full
 * shell and logical sectors (sections 10 and 11). Unlike the DESFire and ISO
 * 7816 media there is no application to select and no file system to walk -
 * the whole card is a flat run of 4-byte pages, read with the Type 2 READ
 * command (0x30), and its ITSO data groups sit at fixed page offsets.
 *
 * The read is done over the raw ISO 14443-3A poller rather than the firmware's
 * MIFARE Ultralight poller on purpose: an Infineon my-d is Ultralight-command
 * compatible but is not always identified as an Ultralight, and a raw 0x30 read
 * works on any Type 2 tag whatever the chip turns out to be.
 */
#pragma once

#include "../cards/flipso_capture.h"
#include "flipso_reader.h"
#include "../itso/itso.h"

#include <nfc/protocols/iso14443_3a/iso14443_3a_poller.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlipsoType2 FlipsoType2;

FlipsoType2* flipso_type2_alloc(void);
void flipso_type2_free(FlipsoType2* type2);

/**
 * Read a Type 2 tag's pages and decode the ITSO data groups within them.
 *
 * A CMD4 ticket is kept in @p capture as one FlipsoBlockType2 block of its whole
 * page memory. A CMD9 or CMD10 card is kept as the Shell, Directory, Product
 * and Log blocks a smartcard is, plus a FlipsoBlockTag of its chip pages, so
 * that it saves, merges and replays the way a smartcard does. Either way a
 * scanned card is a test case as it stands. Must be called from inside the
 * poller callback.
 *
 * @return Success for a whole card; CardError when the read stopped short of
 *         the 64 bytes every Type 2 tag has, CardLost when a CMD9 or CMD10 left
 *         before its sectors were read; BadShell for a full shell that would
 *         not decode or states a geometry its media does not have; Unsupported
 *         for a full shell of a media definition this build does not know, with
 *         the shell decoded into @p card; NotItso when the tag carries no ITSO
 *         shell.
 */
FlipsoReaderStatus flipso_type2_read(
    FlipsoType2* type2,
    Iso14443_3aPoller* poller,
    ItsoCard* card,
    FlipsoCapture* capture);

#ifdef __cplusplus
}
#endif
