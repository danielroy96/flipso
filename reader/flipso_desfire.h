/**
 * @file flipso_desfire.h
 * @brief The DESFire transport (ITSO CMD7 and CMD12), and what a DESFire that is
 * not an ITSO card says about itself.
 */
#pragma once

#include "flipso_reader.h"
#include "../itso/itso.h"

#include <nfc/protocols/mf_desfire/mf_desfire.h>
#include <nfc/protocols/mf_desfire/mf_desfire_poller.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlipsoDesfire FlipsoDesfire;

FlipsoDesfire* flipso_desfire_alloc(void);
void flipso_desfire_free(FlipsoDesfire* desfire);

/**
 * The whole read sequence, run on the NFC worker thread once a card responds:
 * the ITSO application's shell, directory, products and log into @p card and
 * @p capture, and what the chip says about itself into @p media. A DESFire with
 * no ITSO application is described instead, by flipso_desfire_read_other().
 */
FlipsoReaderStatus flipso_desfire_read(
    FlipsoDesfire* desfire,
    MfDesfirePoller* poller,
    ItsoCard* card,
    FlipsoMedia* media,
    FlipsoCapture* capture);

/* --- flipso_desfire_media.c --- */

/**
 * PICC-level description: what chip this is, when it was made, what is on it,
 * into @p media, and the chip block into @p capture. None of it needs a key.
 */
void flipso_desfire_describe(MfDesfirePoller* poller, FlipsoMedia* media, FlipsoCapture* capture);

/**
 * Describe a DESFire card that carries no ITSO application, naming it when it
 * is an Oyster.
 *
 * @return FlipsoReaderStatusOyster or FlipsoReaderStatusNotItso.
 */
FlipsoReaderStatus
    flipso_desfire_read_other(MfDesfirePoller* poller, FlipsoMedia* media, FlipsoCapture* capture);

#ifdef __cplusplus
}
#endif
