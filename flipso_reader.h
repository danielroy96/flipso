/**
 * @file flipso_reader.h
 * @brief Reads an ITSO Shell from a card and decodes it.
 *
 * ITSO defines several customer media, and they do not share a command set. The
 * reader therefore has more than one transport and tries them in turn: DESFire
 * first (CMD7 and CMD12), then ISO 7816 (CMD2, the generic micro-processor media
 * that SPT's Glasgow Subway card uses). In front of both is a detection stage,
 * the firmware's NFC scanner, which says what kind of card is on the reader
 * before either transport commits to talking to it. Each transport needs its own poller, so
 * switching between them means stopping one and starting the next - see
 * flipso_reader_next_transport().
 *
 * The reader owns the NFC stack. Polling runs on the NFC worker thread; the
 * result callback is invoked from that thread, so it must only signal the UI
 * (for example via view_dispatcher_send_custom_event) and return promptly.
 */
#pragma once

#include "flipso_capture.h"
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
    /**
     * The card moved away part way through an ITSO read.
     *
     * Distinct from CardError because of what has already been proved by the
     * time it happens: the ITSO application selected, so this is an ITSO card
     * in this transport and there is no point asking another one. CardError can
     * arrive before anything is known about the card, which is why that one
     * does move on. Both are worth retrying.
     */
    FlipsoReaderStatusCardLost,
    FlipsoReaderStatusOyster, /**< A TfL Oyster: known, and deliberately not decoded. */
    /**
     * A card is on the reader and it does not speak ISO 14443-4, which every
     * ITSO medium Flipso reads does: a MIFARE Classic or Ultralight, a hotel
     * key, an older ITSO card type. Without this the scan would wait for ever
     * on a card that can never answer.
     */
    FlipsoReaderStatusUnsupported,
    /**
     * The first stage of a scan found an ISO 14443-4 card on the reader. Not a
     * result: advance to the next transport and start again, as for NotItso.
     */
    FlipsoReaderStatusFound,
} FlipsoReaderStatus;

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

/** Invoked from the NFC worker thread once a card has been processed. */
typedef void (*FlipsoReaderCallback)(FlipsoReaderStatus status, void* context);

FlipsoReader* flipso_reader_alloc(void);
void flipso_reader_free(FlipsoReader* reader);

/**
 * Begin polling. The decoded card is written into @p card and, for a card that
 * turns out not to be an ITSO one, whatever it will say about itself is written
 * into @p media. The raw bytes behind the decode are kept in @p capture, so
 * that a card can be saved and read back later; it is emptied at the start of
 * every read attempt. All three must outlive the read. The callback fires once
 * per card presented.
 */
void flipso_reader_start(
    FlipsoReader* reader,
    ItsoCard* card,
    FlipsoMedia* media,
    FlipsoCapture* capture,
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
