/**
 * @file flipso_type2.h
 * @brief NFC Type 2 tag transport for ITSO customer media definition 4.
 *
 * The compact page-based media (TS 1000-10 section 5): a MIFARE Ultralight or an
 * Infineon my-d, the family SPT's Glasgow Subway paper tickets use. Unlike the
 * DESFire and ISO 7816 media there is no application to select and no file system
 * to walk - the whole card is a flat run of 4-byte pages, read with the Type 2
 * READ command (0x30), and its ITSO data groups sit at fixed page offsets.
 *
 * The read is done over the raw ISO 14443-3A poller rather than the firmware's
 * MIFARE Ultralight poller on purpose: an Infineon my-d is Ultralight-command
 * compatible but is not always identified as an Ultralight, and a raw 0x30 read
 * works on any Type 2 tag whatever the chip turns out to be.
 */
#pragma once

#include "flipso_capture.h"
#include "flipso_reader.h"
#include "itso/itso.h"

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
 * The whole page memory is kept in @p capture as one FlipsoBlockType2 block, so
 * the card can be saved and replayed exactly as it was read, and a scanned
 * ticket is a test case as it stands. Must be called from inside the poller
 * callback.
 *
 * @return Success for a whole CMD4 ticket; CardError when the read stopped
 *         short of the 64 bytes every Type 2 tag has; Unsupported for an ITSO
 *         card on the other Type 2 media (CMD9, CMD10), with its shell decoded
 *         into @p card; NotItso when the tag carries no ITSO shell.
 */
FlipsoReaderStatus flipso_type2_read(
    FlipsoType2* type2,
    Iso14443_3aPoller* poller,
    ItsoCard* card,
    FlipsoCapture* capture);

#ifdef __cplusplus
}
#endif
