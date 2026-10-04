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

#include "../cards/flipso_capture.h"
#include "flipso_media.h"
#include "flipso_scan_session.h"
#include "../itso/itso.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlipsoReader FlipsoReader;

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
