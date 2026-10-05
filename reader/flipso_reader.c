/**
 * @file flipso_reader.c
 * @brief NFC transports for the ITSO decoder: which one runs, and the pollers
 * that run it.
 *
 * Not every ITSO card is a DESFire one, and the others do not answer DESFire
 * commands at all. Each command set has a transport of its own - DESFire in
 * flipso_desfire.c, ISO 7816 in flipso_cmd2.c, Type 2 tags in flipso_type2.c,
 * and what they share in flipso_transport.c - and this file starts whichever
 * one the scan session (flipso_scan_session.h) is on, and hands it the card
 * when the poller reports one.
 */
#include "flipso_reader.h"
#include "flipso_cmd2.h"
#include "flipso_desfire.h"
#include "flipso_type2.h"

#include <furi.h>
#include <nfc/nfc.h>
#include <nfc/nfc_poller.h>
#include <nfc/nfc_scanner.h>
#include <nfc/protocols/iso14443_4a/iso14443_4a_poller.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a_poller.h>

#define TAG "Flipso"

struct FlipsoReader {
    Nfc* nfc;
    NfcPoller* poller;
    NfcScanner* scanner; /**< Running instead of a poller in the detect stage. */
    /** The scanner has reported its card; it repeats itself until stopped. */
    bool detected;
    bool running;
    /** Which transport runs next, and what the scan has learned so far. */
    FlipsoScanSession session;

    /** The DESFire transport, which most cards are read by. */
    FlipsoDesfire* desfire;
    /** Allocated the first time the ISO 7816 transport is used. */
    FlipsoCmd2* cmd2;
    /** Allocated the first time the Type 2 transport is used. */
    FlipsoType2* type2;

    ItsoCard* card;
    FlipsoMedia* media;
    FlipsoCapture* capture;
    FlipsoReaderCallback callback;
    void* context;
    FlipsoReaderStatus status;
};

/* ------------------------------------------------------------------ */
/* Poller callbacks                                                    */
/* ------------------------------------------------------------------ */

/**
 * The detect stage: work out which command set the card on the reader might
 * answer, so the right transport is tried and no transport is tried that would
 * hang on it. Runs on the scanner's thread, and only ever reports once - the
 * scanner repeats itself for as long as it runs.
 *
 * A card that speaks ISO 14443-4 is a DESFire (CMD7/12) or ISO 7816 (CMD2)
 * candidate. A Type A card that does not - a MIFARE Ultralight or Infineon my-d -
 * may be a Type 2 tag (CMD4), which is read a different way entirely. Starting a
 * -4 poller on a Type 2 tag would send RATS it can never answer and poll for
 * ever, so the two are kept apart here rather than tried in turn.
 *
 * A MIFARE Classic is Type A but not a medium Flipso reads (the obsolete CMD1
 * and CMD3, TS 1000-10 clauses 2 and 4), so it is called unsupported rather
 * than fed to the Type 2 transport.
 */
static void flipso_reader_scanner_callback(NfcScannerEvent event, void* context) {
    FlipsoReader* reader = context;
    if(event.type != NfcScannerEventTypeDetected || reader->detected) return;
    reader->detected = true;

    bool iso4 = false, type_a = false, classic = false;
    for(size_t i = 0; i < event.data.protocol_num; i++) {
        NfcProtocol protocol = event.data.protocols[i];
        if(protocol == NfcProtocolIso14443_4a ||
           nfc_protocol_has_parent(protocol, NfcProtocolIso14443_4a)) {
            iso4 = true;
        }
        if(protocol == NfcProtocolIso14443_3a ||
           nfc_protocol_has_parent(protocol, NfcProtocolIso14443_3a)) {
            type_a = true;
        }
        if(protocol == NfcProtocolMfClassic) classic = true;
    }

    reader->session.detected_iso4 = iso4;
    bool supported = iso4 || (type_a && !classic);
    FURI_LOG_I(
        TAG,
        "Card detected: %u protocol(s), %s",
        (unsigned)event.data.protocol_num,
        iso4      ? "ISO 14443-4" :
        supported ? "Type 2 tag" :
                    "unsupported");

    reader->status = supported ? FlipsoReaderStatusFound : FlipsoReaderStatusUnsupported;
    reader->callback(reader->context);
}

/**
 * DESFire transport. Started in extended mode, so the events arriving here come
 * from the parent ISO14443-4A poller, which has already activated the card by
 * the time it reports Ready.
 */
static NfcCommand flipso_desfire_callback(NfcGenericEventEx event, void* context) {
    FlipsoReader* reader = context;
    const Iso14443_4aPollerEvent* iso_event = event.parent_event_data;

    if(iso_event->type != Iso14443_4aPollerEventTypeReady) {
        /* Activation failed: most likely nothing is in the field yet, or the card
         * was pulled away. Keep polling so the user can simply try again. */
        return NfcCommandContinue;
    }

    reader->status = flipso_desfire_read(
        reader->desfire, event.poller, reader->card, reader->media, reader->capture);
    reader->callback(reader->context);
    return NfcCommandStop;
}

/**
 * ISO 7816 transport. Started in plain mode rather than extended, so that the
 * ISO14443-4A poller runs its own activation: as well as sending RATS, that is
 * what takes the frame waiting time from the card's ATS. Driving activation by
 * hand leaves the poller timing out every APDU after a few milliseconds.
 */
static NfcCommand flipso_iso7816_callback(NfcGenericEvent event, void* context) {
    FlipsoReader* reader = context;
    furi_assert(event.protocol == NfcProtocolIso14443_4a);

    const Iso14443_4aPollerEvent* iso_event = event.event_data;
    if(iso_event->type != Iso14443_4aPollerEventTypeReady) return NfcCommandContinue;

    reader->status = flipso_cmd2_read(reader->cmd2, event.instance, reader->card, reader->capture);
    reader->callback(reader->context);
    return NfcCommandStop;
}

/**
 * Type 2 tag transport. Started in plain mode: the ISO 14443-3A poller activates
 * the card (anticollision and select, no RATS) and reports Ready, after which the
 * tag's pages are read with raw 0x30 commands.
 */
static NfcCommand flipso_type2_poller_callback(NfcGenericEvent event, void* context) {
    FlipsoReader* reader = context;
    furi_assert(event.protocol == NfcProtocolIso14443_3a);

    const Iso14443_3aPollerEvent* iso_event = event.event_data;
    if(iso_event->type != Iso14443_3aPollerEventTypeReady) return NfcCommandContinue;

    reader->status =
        flipso_type2_read(reader->type2, event.instance, reader->card, reader->capture);
    reader->callback(reader->context);
    return NfcCommandStop;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

FlipsoReader* flipso_reader_alloc(void) {
    FlipsoReader* reader = malloc(sizeof(FlipsoReader));
    memset(reader, 0, sizeof(FlipsoReader));
    reader->nfc = nfc_alloc();
    reader->desfire = flipso_desfire_alloc();
    reader->status = FlipsoReaderStatusIdle;
    flipso_scan_session_begin(&reader->session);
    return reader;
}

void flipso_reader_free(FlipsoReader* reader) {
    furi_assert(reader);
    flipso_reader_stop(reader);
    if(reader->cmd2) flipso_cmd2_free(reader->cmd2);
    if(reader->type2) flipso_type2_free(reader->type2);
    flipso_desfire_free(reader->desfire);
    nfc_free(reader->nfc);
    free(reader);
}

/** Start whichever transport the session is on. */
static void flipso_reader_start_transport(FlipsoReader* reader) {
    reader->status = FlipsoReaderStatusIdle;

    const FlipsoTransport transport = reader->session.transport;
    if(transport == FlipsoTransportDetect) {
        reader->detected = false;
        reader->scanner = nfc_scanner_alloc(reader->nfc);
        nfc_scanner_start(reader->scanner, flipso_reader_scanner_callback, reader);
    } else if(transport == FlipsoTransportIso7816) {
        /* Only cards that are not DESFire get this far, so the buffers it owns
         * are worth allocating late rather than for every read. */
        if(!reader->cmd2) reader->cmd2 = flipso_cmd2_alloc();
        reader->poller = nfc_poller_alloc(reader->nfc, NfcProtocolIso14443_4a);
        nfc_poller_start(reader->poller, flipso_iso7816_callback, reader);
    } else if(transport == FlipsoTransportType2) {
        /* Read the raw Type 2 tag over the base ISO 14443-3A poller: it activates
         * with anticollision and select but no RATS, which is all a Type 2 tag
         * answers. Allocated late, like the CMD2 buffers, and freed as soon as
         * the poller stops. */
        if(!reader->type2) reader->type2 = flipso_type2_alloc();
        reader->poller = nfc_poller_alloc(reader->nfc, NfcProtocolIso14443_3a);
        nfc_poller_start(reader->poller, flipso_type2_poller_callback, reader);
    } else {
        reader->poller = nfc_poller_alloc(reader->nfc, NfcProtocolMfDesfire);
        nfc_poller_start_ex(reader->poller, flipso_desfire_callback, reader);
    }
    reader->running = true;
}

void flipso_reader_start(
    FlipsoReader* reader,
    ItsoCard* card,
    FlipsoMedia* media,
    FlipsoCapture* capture,
    FlipsoReaderCallback callback,
    void* context) {
    furi_assert(reader);
    furi_assert(card);
    furi_assert(media);
    furi_assert(capture);
    furi_assert(callback);
    if(reader->running) return;

    reader->card = card;
    reader->capture = capture;
    reader->media = media;
    reader->callback = callback;
    reader->context = context;

    /* Every scan starts at the detect stage, including one started again after
     * Back stopped the last part way through: a -4 transport left over from
     * that scan would hang on a Type 2 tag. */
    flipso_scan_session_begin(&reader->session);
    flipso_reader_start_transport(reader);
}

bool flipso_reader_advance(FlipsoReader* reader, FlipsoReaderStatus* status) {
    furi_assert(reader);
    furi_assert(status);

    /* Stop polling from this thread: the poller cannot stop itself. */
    flipso_reader_stop(reader);

    *status = reader->status;
    if(flipso_scan_session_step(&reader->session, status)) {
        flipso_reader_start_transport(reader);
        return true;
    }
    return false;
}

void flipso_reader_stop(FlipsoReader* reader) {
    furi_assert(reader);
    if(!reader->running) return;

    if(reader->scanner) {
        nfc_scanner_stop(reader->scanner);
        nfc_scanner_free(reader->scanner);
        reader->scanner = NULL;
    } else {
        nfc_poller_stop(reader->poller);
        nfc_poller_free(reader->poller);
        reader->poller = NULL;
    }
    /* The Type 2 page buffer is nearly a kilobyte, and nothing reads it once
     * the poller has stopped: the card and its blocks are copied out by then.
     * A retry allocates it again. */
    if(reader->type2) {
        flipso_type2_free(reader->type2);
        reader->type2 = NULL;
    }
    reader->running = false;
}
