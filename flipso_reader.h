/**
 * @file flipso_reader.h
 * @brief Reads an ITSO Shell from a card and decodes it.
 *
 * ITSO defines several customer media, and they do not share a command set. The
 * reader therefore has more than one transport and tries them in turn: DESFire
 * first (CMD7 and CMD12), then ISO 7816 (CMD2, the generic micro-processor media
 * that SPT's Glasgow Subway card uses). Each transport needs its own poller, so
 * switching between them means stopping one and starting the next - see
 * flipso_reader_next_transport().
 *
 * The reader owns the NFC stack. Polling runs on the NFC worker thread; the
 * result callback is invoked from that thread, so it must only signal the UI
 * (for example via view_dispatcher_send_custom_event) and return promptly.
 */
#pragma once

#include "flipso_media.h"
#include "itso/itso.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Log the shell owner: the operator that issued the card, and so the one whose
 * branding titles it.
 *
 * Called from inside each transport's read, so that CMD2 logs it as well as
 * DESFire, and because the result callback is documented above to return
 * promptly.
 *
 * Logged by number: the number is what a user needs in order to add their card
 * to the operator table, and unlike the card number it identifies a scheme
 * rather than a holder.
 *
 * Note for anyone debugging with this line: a USB log line can arrive cut off
 * mid-word. That is the serial stream, not the app - the pre-existing per-IPE
 * lines truncate the same way, and the device is running normally afterwards.
 * Re-read rather than concluding the app stopped where the text does.
 */
void flipso_log_shell_owner(const ItsoCard* card);

typedef enum {
    FlipsoReaderStatusIdle,
    FlipsoReaderStatusSuccess, /**< A complete ITSO Shell was decoded. */
    FlipsoReaderStatusNotItso, /**< No ITSO application in this transport. */
    FlipsoReaderStatusBadShell, /**< The ITSO application is present but unreadable. */
    FlipsoReaderStatusCardError, /**< The card moved away or the read failed. */
    FlipsoReaderStatusOyster, /**< A TfL Oyster: known, and deliberately not decoded. */
} FlipsoReaderStatus;

typedef struct FlipsoReader FlipsoReader;

/** Invoked from the NFC worker thread once a card has been processed. */
typedef void (*FlipsoReaderCallback)(FlipsoReaderStatus status, void* context);

FlipsoReader* flipso_reader_alloc(void);
void flipso_reader_free(FlipsoReader* reader);

/**
 * Begin polling. The decoded card is written into @p card and, for a card that
 * turns out not to be an ITSO one, whatever it will say about itself is written
 * into @p media. Both must outlive the read. The callback fires once per card
 * presented.
 */
void flipso_reader_start(
    FlipsoReader* reader,
    ItsoCard* card,
    FlipsoMedia* media,
    FlipsoReaderCallback callback,
    void* context);

/** Stop polling. Safe to call when not started. Must not be called from the callback. */
void flipso_reader_stop(FlipsoReader* reader);

/**
 * Advance to the next transport after a FlipsoReaderStatusNotItso result.
 *
 * Call it from the same thread that starts and stops the reader, between a stop
 * and a start: the card is still on the reader, so the next transport picks it
 * up without the user doing anything.
 *
 * @return false once every transport has been tried, leaving the reader ready to
 *         start again from the first.
 */
bool flipso_reader_next_transport(FlipsoReader* reader);

/** Go back to the first transport, ready for a fresh scan. */
void flipso_reader_reset_transport(FlipsoReader* reader);

#ifdef __cplusplus
}
#endif
