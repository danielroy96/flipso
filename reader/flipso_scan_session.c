/**
 * @file flipso_scan_session.c
 * @brief The scan policy: which transport next, when to retry, what to conclude.
 */
#include "flipso_scan_session.h"

void flipso_scan_session_begin(FlipsoScanSession* session) {
    session->transport = FlipsoTransportDetect;
    session->detected_iso4 = false;
    session->retries = 0;
    session->dropped = false;
}

bool flipso_scan_session_next_transport(FlipsoScanSession* session) {
    switch(session->transport) {
    case FlipsoTransportDetect:
        session->transport = session->detected_iso4 ? FlipsoTransportDesfire :
                                                      FlipsoTransportType2;
        return true;
    case FlipsoTransportDesfire:
        session->transport = FlipsoTransportIso7816;
        return true;
    default: /* Iso7816 and Type2 are each the last of their path. */
        session->transport = FlipsoTransportDetect;
        return false;
    }
}

bool flipso_scan_session_step(FlipsoScanSession* session, FlipsoReaderStatus* status) {
    /* The detect stage found a card one of the transports might read - an
     * ISO 14443-4 card or a Type 2 tag: on to them, with the card still on the
     * reader. */
    if(*status == FlipsoReaderStatusFound && flipso_scan_session_next_transport(session)) {
        return true;
    }

    const bool dropped = *status == FlipsoReaderStatusCardError ||
                         *status == FlipsoReaderStatusCardLost;
    if(dropped) session->dropped = true;

    /* The card left the field part way through. Try the same transport again
     * rather than concluding anything from it: a half-finished read says
     * nothing about what the card is. */
    if(dropped && session->retries < FLIPSO_SCAN_CARD_ERROR_RETRIES) {
        session->retries++;
        return true;
    }

    /* A card with no ITSO application in this command set may still be an ITSO
     * card in another one, and it is still sitting on the reader. Move to the
     * next transport without telling the user anything: as far as they are
     * concerned this is all one scan.
     *
     * A card that kept dropping out gets the same treatment, because some cards
     * answer a command set they do not implement with silence rather than with
     * an error - and a CMD2 card that did that would never be read if a timeout
     * ended the scan here.
     *
     * FlipsoReaderStatusCardLost is deliberately not in that list. It means the
     * ITSO application had already selected when the card went, so this
     * transport is the right one and the next could only report that it found
     * no ITSO application - which is how a card that had just named its
     * operator came to be called "Not an ITSO card". */
    if((*status == FlipsoReaderStatusNotItso || *status == FlipsoReaderStatusCardError) &&
       flipso_scan_session_next_transport(session)) {
        session->retries = 0;
        return true;
    }

    /* Every transport has had its turn and the last one found no ITSO
     * application. That is only worth saying of a card we managed to ask
     * properly: if the card was dropping out of the field along the way, the
     * more likely story is that it left before it could answer, and "Not an
     * ITSO card" is a verdict on a card nobody ever read. Say the read failed,
     * which is both true and the one the user can act on. */
    if(*status == FlipsoReaderStatusNotItso && session->dropped) {
        *status = FlipsoReaderStatusCardError;
    }
    return false;
}
