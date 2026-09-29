/**
 * @file flipso_cmd2.h
 * @brief ISO 7816 transport for ITSO customer media definition 2.
 *
 * CMD2 is ITSO's "generic micro-processor" media: an ISO 7816-4 file system
 * reached over ISO 14443-4, rather than the DESFire command set that CMD7 and
 * CMD12 use. Cards issued on it include the SPT Glasgow Subway smartcard.
 *
 * The context owns its scratch buffers so that nothing large lands on the NFC
 * worker's stack. Allocate it once, then call flipso_cmd2_read() from inside a
 * poller callback for each card presented.
 */
#pragma once

#include "flipso_reader.h"
#include "../itso/itso.h"

#include <nfc/protocols/iso14443_4a/iso14443_4a_poller.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlipsoCmd2 FlipsoCmd2;

FlipsoCmd2* flipso_cmd2_alloc(void);
void flipso_cmd2_free(FlipsoCmd2* cmd2);

/**
 * Read an ITSO Shell from a CMD2 card.
 *
 * @param cmd2   context holding the scratch buffers.
 * @param poller an activated ISO14443-4A poller; only valid inside its callback.
 * @param card   decoded into; reset before anything is written.
 * @param capture keeps the raw bytes behind that decode; likewise reset first.
 */
FlipsoReaderStatus flipso_cmd2_read(
    FlipsoCmd2* cmd2,
    Iso14443_4aPoller* poller,
    ItsoCard* card,
    FlipsoCapture* capture);

#ifdef __cplusplus
}
#endif
