/**
 * @file flipso_type2.c
 * @brief NFC Type 2 tag transport for ITSO CMD4 (see flipso_type2.h).
 */
#include "flipso_type2.h"

#include <furi.h>
#include <toolbox/bit_buffer.h>

#define TAG "Flipso"

/* The Type 2 READ command returns the addressed page and the three after it, so
 * one command brings back sixteen bytes (TS 1000-10 5.1.1, the U1 spec). */
#define FLIPSO_TYPE2_READ_CMD   0x30
#define FLIPSO_TYPE2_PAGE_BYTES 4
#define FLIPSO_TYPE2_READ_BYTES 16

/* As many bytes as any Type 2 tag Flipso reads carries usefully: a CMD4
 * Ultralight is 64, an Infineon my-d and an Ultralight EV1 a little more. Capped
 * well under ITSO_MAX_GROUP_LEN so the whole page memory fits one capture block,
 * and a tag that answers reads past its end (by wrapping rather than refusing)
 * cannot spin the read forever. */
#define FLIPSO_TYPE2_MAX_BYTES 256

/* Response timeout in carrier cycles: about 4.4 ms at 13.56 MHz, which a page
 * read returns inside many times over - it is a ceiling on a stalled card, not a
 * measure of a healthy one. */
#define FLIPSO_TYPE2_FWT_FC 60000

struct FlipsoType2 {
    BitBuffer* tx;
    BitBuffer* rx;
    uint8_t pages[FLIPSO_TYPE2_MAX_BYTES];
};

FlipsoType2* flipso_type2_alloc(void) {
    FlipsoType2* type2 = malloc(sizeof(FlipsoType2));
    memset(type2, 0, sizeof(FlipsoType2));
    type2->tx = bit_buffer_alloc(2);
    type2->rx = bit_buffer_alloc(FLIPSO_TYPE2_READ_BYTES + 2); /* Data plus its CRC. */
    return type2;
}

void flipso_type2_free(FlipsoType2* type2) {
    furi_assert(type2);
    bit_buffer_free(type2->tx);
    bit_buffer_free(type2->rx);
    free(type2);
}

/**
 * Read the tag's pages into @p type2->pages, page 0 first.
 *
 * Reads run in blocks of four pages until one is refused, which is the end of
 * memory, or the buffer is full. A card that leaves the field mid-read stops the
 * same way; the caller tells the two apart by how much arrived.
 *
 * @return the number of bytes read.
 */
static size_t flipso_type2_read_pages(FlipsoType2* type2, Iso14443_3aPoller* poller) {
    size_t total = 0;

    for(uint8_t page = 0; total + FLIPSO_TYPE2_READ_BYTES <= FLIPSO_TYPE2_MAX_BYTES;
        page += FLIPSO_TYPE2_READ_BYTES / FLIPSO_TYPE2_PAGE_BYTES) {
        const uint8_t command[] = {FLIPSO_TYPE2_READ_CMD, page};
        bit_buffer_reset(type2->tx);
        bit_buffer_append_bytes(type2->tx, command, sizeof(command));

        Iso14443_3aError error = iso14443_3a_poller_send_standard_frame(
            poller, type2->tx, type2->rx, FLIPSO_TYPE2_FWT_FC);
        if(error != Iso14443_3aErrorNone) break;

        /* A refusal is a short frame - a 4-bit NAK - rather than an error on some
         * cards, so a response too short to be four pages ends the read too. */
        if(bit_buffer_get_size_bytes(type2->rx) < FLIPSO_TYPE2_READ_BYTES) break;

        memcpy(type2->pages + total, bit_buffer_get_data(type2->rx), FLIPSO_TYPE2_READ_BYTES);
        total += FLIPSO_TYPE2_READ_BYTES;
    }

    return total;
}

FlipsoReaderStatus flipso_type2_read(
    FlipsoType2* type2,
    Iso14443_3aPoller* poller,
    ItsoCard* card,
    FlipsoCapture* capture) {
    furi_assert(type2);
    furi_assert(poller);
    furi_assert(card);
    furi_assert(capture);

    itso_card_reset(card);
    flipso_capture_reset(capture);

    size_t len = flipso_type2_read_pages(type2, poller);

    switch(itso_type2_kind(type2->pages, len)) {
    case ItsoType2Incomplete:
        /* Every Type 2 tag has at least the 64 bytes of an Ultralight, so fewer
         * means the tag left the field mid-read: a fumbled tap to retry, not a
         * verdict on the card - and not a half-read ticket to show or save. */
        FURI_LOG_D(TAG, "Type 2: read stopped after %u bytes", (unsigned)len);
        return FlipsoReaderStatusCardError;

    case ItsoType2FullShell:
        /* An ITSO card, but a CMD9 or CMD10 with a real directory rather than the
         * CMD4 layout. The shell is decoded so the error screen can say which. */
        itso_parse_shell(
            card, type2->pages + ITSO_TYPE2_SHELL_OFFSET, len - ITSO_TYPE2_SHELL_OFFSET);
        FURI_LOG_I(TAG, "Type 2: full ITSO shell, FVC %u - not read", card->fvc);
        return FlipsoReaderStatusUnsupported;

    case ItsoType2NotItso:
        FURI_LOG_D(TAG, "Type 2: no ITSO shell in %u bytes", (unsigned)len);
        return FlipsoReaderStatusNotItso;

    case ItsoType2Compact:
        break;
    }

    itso_parse_type2(card, type2->pages, len);

    /* Keep the whole page memory, not the decoded groups: a saved Type 2 card is
     * the raw pages, so it replays through the same decoder and doubles as a test
     * case. */
    flipso_capture_add(capture, FlipsoBlockType2, 0, type2->pages, len);
    FURI_LOG_I(
        TAG,
        "Type 2 ITSO card: FVC %u, %u bytes, product owner %u",
        card->fvc,
        (unsigned)len,
        itso_card_issuer_oid(card));
    return FlipsoReaderStatusSuccess;
}
