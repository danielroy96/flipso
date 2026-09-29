/*
 * Host-side test for the scan policy: which transport a scan tries next, when it
 * tries the same one again, and what it concludes. Each case is the sequence of
 * reports a real card produces, fed through as the reader would feed them.
 */
#include "reader/flipso_scan_session.h"

#include <stdio.h>

static int failures = 0;

static void check(const char* what, int ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if(!ok) failures++;
}

/**
 * Feed @p reports through a fresh session. Each report goes to whichever
 * transport the session is on; @p path records those transports in order.
 *
 * @return the verdict, or FlipsoReaderStatusIdle if the scan was still going
 *         when the reports ran out.
 */
static FlipsoReaderStatus
    run(bool iso4,
        const FlipsoReaderStatus* reports,
        size_t count,
        FlipsoTransport* path,
        size_t* steps) {
    FlipsoScanSession session;
    flipso_scan_session_begin(&session);
    session.detected_iso4 = iso4;
    *steps = 0;
    for(size_t i = 0; i < count; i++) {
        path[(*steps)++] = session.transport;
        FlipsoReaderStatus status = reports[i];
        if(!flipso_scan_session_step(&session, &status)) return status;
    }
    return FlipsoReaderStatusIdle;
}

#define RUN(iso4, ...)                                                                    \
    do {                                                                                  \
        const FlipsoReaderStatus reports[] = {__VA_ARGS__};                               \
        verdict = run(iso4, reports, sizeof(reports) / sizeof(reports[0]), path, &steps); \
    } while(0)

int main(void) {
    printf("Scan session\n");
    FlipsoTransport path[16];
    size_t steps;
    FlipsoReaderStatus verdict;

    RUN(true, FlipsoReaderStatusFound, FlipsoReaderStatusSuccess);
    check("a DESFire card reads on the second step", verdict == FlipsoReaderStatusSuccess);
    check(
        "detect, then DESFire",
        steps == 2 && path[0] == FlipsoTransportDetect && path[1] == FlipsoTransportDesfire);

    RUN(true, FlipsoReaderStatusFound, FlipsoReaderStatusNotItso, FlipsoReaderStatusSuccess);
    check("a CMD2 card reads on the ISO 7816 transport", verdict == FlipsoReaderStatusSuccess);
    check("after DESFire found nothing", steps == 3 && path[2] == FlipsoTransportIso7816);

    RUN(false, FlipsoReaderStatusFound, FlipsoReaderStatusSuccess);
    check("a Type 2 tag goes straight to its transport", path[1] == FlipsoTransportType2);
    check("and reads", verdict == FlipsoReaderStatusSuccess);

    RUN(true, FlipsoReaderStatusFound, FlipsoReaderStatusNotItso, FlipsoReaderStatusNotItso);
    check("a -4 card no transport knows is not ITSO", verdict == FlipsoReaderStatusNotItso);

    RUN(false, FlipsoReaderStatusFound, FlipsoReaderStatusNotItso);
    check("nor is a Type 2 tag with no shell", verdict == FlipsoReaderStatusNotItso);

    RUN(true, FlipsoReaderStatusUnsupported);
    check("an unsupported card ends the scan at once", verdict == FlipsoReaderStatusUnsupported);

    RUN(true, FlipsoReaderStatusFound, FlipsoReaderStatusOyster);
    check("an Oyster ends the scan", verdict == FlipsoReaderStatusOyster);

    /* Dropping out: the same transport is tried again, twice. */
    RUN(true,
        FlipsoReaderStatusFound,
        FlipsoReaderStatusCardLost,
        FlipsoReaderStatusCardLost,
        FlipsoReaderStatusSuccess);
    check("a card lost mid-read is read again", verdict == FlipsoReaderStatusSuccess);
    check(
        "on the same transport",
        path[1] == FlipsoTransportDesfire && path[2] == FlipsoTransportDesfire &&
            path[3] == FlipsoTransportDesfire);

    RUN(true,
        FlipsoReaderStatusFound,
        FlipsoReaderStatusCardLost,
        FlipsoReaderStatusCardLost,
        FlipsoReaderStatusCardLost);
    check("a card lost three times is a failed read", verdict == FlipsoReaderStatusCardLost);
    check("without moving to another transport", steps == 4);

    /* CardError, unlike CardLost, moves on once the retries are spent: a card
     * that answers a command set it lacks with silence. */
    RUN(true,
        FlipsoReaderStatusFound,
        FlipsoReaderStatusCardError,
        FlipsoReaderStatusCardError,
        FlipsoReaderStatusCardError,
        FlipsoReaderStatusSuccess);
    check("a silent card is tried on the next transport", path[4] == FlipsoTransportIso7816);
    check("and read there", verdict == FlipsoReaderStatusSuccess);

    /* Dropping out along the way and then finding nothing is a failed read,
     * not a verdict on the card. */
    RUN(true,
        FlipsoReaderStatusFound,
        FlipsoReaderStatusCardError,
        FlipsoReaderStatusNotItso,
        FlipsoReaderStatusNotItso);
    check("not ITSO after a drop is a failed read", verdict == FlipsoReaderStatusCardError);

    /* The retry budget is per transport. */
    RUN(true,
        FlipsoReaderStatusFound,
        FlipsoReaderStatusCardError,
        FlipsoReaderStatusNotItso,
        FlipsoReaderStatusCardLost,
        FlipsoReaderStatusCardLost,
        FlipsoReaderStatusSuccess);
    check("each transport gets its own retries", verdict == FlipsoReaderStatusSuccess);

    /* A new scan starts from detect, whatever the last one was doing. */
    FlipsoScanSession session;
    flipso_scan_session_begin(&session);
    session.detected_iso4 = true;
    FlipsoReaderStatus status = FlipsoReaderStatusFound;
    flipso_scan_session_step(&session, &status);
    flipso_scan_session_begin(&session);
    check(
        "beginning again returns to the detect stage",
        session.transport == FlipsoTransportDetect && !session.detected_iso4 && !session.dropped &&
            session.retries == 0);

    printf("\n%s\n", failures ? "FAILURES" : "All scan session tests passed");
    return failures ? 1 : 0;
}
