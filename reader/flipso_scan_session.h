/**
 * @file flipso_scan_session.h
 * @brief What a scan does next, once a transport has reported on a card.
 *
 * A scan is several reads. The detect stage says which command sets the card
 * might answer; each transport is then tried in turn while the card stays on
 * the reader; a card that drops out mid-read is tried again; and the verdict at
 * the end depends on whether the card was ever read cleanly. That policy is the
 * most intricate logic in the app and has nothing to do with NFC, so it lives
 * here, apart from the pollers, where the host tests can drive it through every
 * path a real card takes: tools/test/test_scan_session.c.
 *
 * flipso_reader.c owns one of these and consults it each time a transport
 * reports. Pure C, no firmware headers.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/** The command sets the reader knows, tried in this order. */
typedef enum {
    /**
     * Not a command set: the firmware's scanner, which waits for a card and
     * says which protocols it answers. A poller cannot do that job - with no
     * card present it reports the same timeout a card that will not activate
     * does - and a scan needs it done, or a MIFARE Classic on the reader is a
     * scan that never ends.
     */
    FlipsoTransportDetect,
    FlipsoTransportDesfire, /**< CMD7 and CMD12: native DESFire commands. */
    FlipsoTransportIso7816, /**< CMD2: an ISO 7816-4 file system. */
    FlipsoTransportType2, /**< CMD4: a page-based NFC Type 2 tag. */
    FlipsoTransportCount,
} FlipsoTransport;

/*
 * Reads to give up on before deciding a transport has nothing to say.
 *
 * A card that drops out of the field mid-read is nearly always a fumbled tap,
 * not a card we cannot read, so the same transport is simply tried again. It
 * costs nothing while no card is present: the poller sits waiting rather than
 * failing, so the budget is only spent on cards that are there and dropping out.
 */
#define FLIPSO_SCAN_CARD_ERROR_RETRIES 2

typedef struct {
    /** The transport the next read uses. */
    FlipsoTransport transport;
    /** The detected card speaks ISO 14443-4: it is a DESFire or ISO 7816
     *  candidate rather than a Type 2 tag. Set from the detect stage, and read
     *  by flipso_scan_session_next_transport() to choose which to try. */
    bool detected_iso4;
    /** Reads lost to the card leaving the field on the current transport. */
    uint8_t retries;
    /**
     * The card dropped out at least once during this scan, on any transport.
     *
     * Unlike @c retries this is not cleared when the scan moves on to the next
     * transport, because what it is for is the verdict at the end: a scan that
     * never once got a clean look at the card cannot conclude anything about
     * what the card is.
     */
    bool dropped;
} FlipsoScanSession;

/** Start a scan afresh: the detect stage, no retries spent, nothing dropped. */
void flipso_scan_session_begin(FlipsoScanSession* session);

/**
 * Advance to the next transport, with the card still on the reader.
 *
 * Not a straight walk down the list: the detect stage decides whether the card
 * is an ISO 14443-4 one, and only then are the two -4 transports worth trying.
 * A Type 2 tag skips them for the one transport that fits it - being tried a -4
 * transport would hang it.
 *
 * @return false once every transport on the card's path has been tried, leaving
 *         the session back on the detect stage.
 */
bool flipso_scan_session_next_transport(FlipsoScanSession* session);

/**
 * Decide what follows a transport's report.
 *
 * @param[in,out] status what the transport reported; on a false return, the
 *                       scan's verdict, which may differ from it - see the
 *                       notes in flipso_scan_session.c.
 * @return true to read again, with @c transport saying which transport; false
 *         when the scan is over and @p status is the result to show.
 */
bool flipso_scan_session_step(FlipsoScanSession* session, FlipsoReaderStatus* status);

#ifdef __cplusplus
}
#endif
