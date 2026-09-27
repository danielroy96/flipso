/**
 * @file flipso_reader.h
 * @brief Reads an ITSO Shell from a card and decodes it.
 *
 * ITSO defines several customer media, and they do not share a command set. The
 * reader therefore has more than one transport and tries them in turn: for a card
 * that speaks ISO 14443-4, DESFire first (CMD7 and CMD12), then ISO 7816 (CMD2,
 * the generic micro-processor media that SPT's Glasgow Subway smartcard uses);
 * for a Type A card that does not, the Type 2 tag transport (CMD4, the page-based
 * media SPT's paper tickets use). In front of them is a detection stage, the
 * firmware's NFC scanner, which decides which of those a card could be before any
 * transport commits to it - a -4 poller started on a Type 2 tag would hang.
 * Each transport needs its own poller, so switching between them means stopping
 * one and starting the next; which comes next is flipso_scan_session.h's to say.
 *
 * The reader owns the NFC stack. Polling runs on the NFC worker thread; the
 * result callback is invoked from that thread, so it must only signal the UI
 * (for example via view_dispatcher_send_custom_event) and return promptly.
 */
#pragma once

#include "flipso_capture.h"
#include "flipso_media.h"
#include "flipso_scan_session.h"
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

typedef struct FlipsoReader FlipsoReader;

/**
 * Where a transport gets a card's data groups from.
 *
 * The two transports address sectors differently - a DESFire file number, a
 * CMD2 directory path - but once the shell and directory are in hand they walk
 * the products and the log the same way, and that walk lives in one place:
 * flipso_reader_read_groups().
 */
typedef struct {
    /** Read the sector chain starting at @p sector; the bytes are left in @p data. */
    size_t (*read_group)(void* context, uint8_t sector, const uint8_t** data);
    /** Read the cyclic log, or return 0 when the card keeps none. */
    size_t (*read_log)(void* context, const uint8_t** data);
    /** True once the card has stopped answering. */
    bool (*lost)(void* context);
    void* context;
} FlipsoGroupSource;

/**
 * Read and decode every product the directory lists, then the journey log.
 *
 * @param card    with the shell and directory already decoded into it.
 * @param capture keeps each group's bytes, so the card can be saved.
 * @return Success, or CardLost when the card left the field part way: a
 *         product that could not be read is not a product the card does not
 *         have, and finishing would show a card that looks read and is not.
 */
FlipsoReaderStatus flipso_reader_read_groups(
    ItsoCard* card,
    FlipsoCapture* capture,
    const FlipsoGroupSource* source);

/**
 * Invoked from the NFC worker thread each time a transport has reported. It
 * must only signal the UI thread, which then calls flipso_reader_advance().
 */
typedef void (*FlipsoReaderCallback)(void* context);

FlipsoReader* flipso_reader_alloc(void);
void flipso_reader_free(FlipsoReader* reader);

/**
 * Begin a scan, from the detect stage. The decoded card is written into
 * @p card and, for a card that turns out not to be an ITSO one, whatever it
 * will say about itself is written into @p media. The raw bytes behind the
 * decode are kept in @p capture, so that a card can be saved and read back
 * later; it is emptied at the start of every read attempt. All three must
 * outlive the scan. The callback fires each time a transport reports.
 */
void flipso_reader_start(
    FlipsoReader* reader,
    ItsoCard* card,
    FlipsoMedia* media,
    FlipsoCapture* capture,
    FlipsoReaderCallback callback,
    void* context);

/**
 * Act on the report the callback announced: stop that transport, and either
 * start the next read - another transport, or the same one again - or finish.
 *
 * Call from the thread that starts and stops the reader, never from the
 * callback: the poller cannot stop itself.
 *
 * @param[out] status the scan's result, when it is over.
 * @return true while the scan goes on, with the card still on the reader and
 *         nothing for the user to do; false when it is over.
 */
bool flipso_reader_advance(FlipsoReader* reader, FlipsoReaderStatus* status);

/** Stop polling. Safe to call when not started. Must not be called from the callback. */
void flipso_reader_stop(FlipsoReader* reader);

#ifdef __cplusplus
}
#endif
