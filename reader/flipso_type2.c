/**
 * @file flipso_type2.c
 * @brief NFC Type 2 tag transport for ITSO CMD4, CMD9 and CMD10 (see flipso_type2.h).
 */
#include "flipso_type2.h"
#include "flipso_transport.h"

#include <furi.h>
#include <toolbox/bit_buffer.h>

#define TAG "Flipso"

/* The Type 2 READ command returns the addressed page and the three after it, so
 * one command brings back sixteen bytes (TS 1000-10 5.1.1, the U1 spec). */
#define FLIPSO_TYPE2_READ_CMD   0x30
#define FLIPSO_TYPE2_PAGE_BYTES 4
#define FLIPSO_TYPE2_READ_BYTES 16

/* How far the first pass reads before it knows what the tag is: past the 64
 * bytes of a CMD4 Ultralight and the little more an Infineon my-d or Ultralight
 * EV1 carries, and past the 128 bytes of shell and directories a CMD9 or CMD10
 * keeps ahead of its sectors. Capped, because a tag that answers reads past its
 * end by wrapping rather than refusing would otherwise spin the read forever. */
#define FLIPSO_TYPE2_PROBE_BYTES 256

/* The whole of a CMD9 or CMD10 card's ITSO data, which a second pass reads to
 * once the shell has said where it ends. */
#define FLIPSO_TYPE2_MAX_BYTES ITSO_TYPE2_FULL_MAX_LEN
_Static_assert(
    FLIPSO_TYPE2_MAX_BYTES >= FLIPSO_TYPE2_PROBE_BYTES,
    "the full read continues the probe in the same buffer");

/* Response timeout in carrier cycles: about 4.4 ms at 13.56 MHz, which a page
 * read returns inside many times over - it is a ceiling on a stalled card, not a
 * measure of a healthy one. */
#define FLIPSO_TYPE2_FWT_FC 60000

struct FlipsoType2 {
    BitBuffer* tx;
    BitBuffer* rx;
    uint8_t pages[FLIPSO_TYPE2_MAX_BYTES];
    size_t len; /**< Bytes of @c pages read so far. */
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
 * Read the tag's pages into @p type2->pages, from where the last read stopped
 * up to @p limit bytes.
 *
 * Reads run in blocks of four pages until one is refused, which is the end of
 * memory, or the limit is reached. A card that leaves the field mid-read stops
 * the same way; the caller tells the two apart by how much arrived.
 *
 * @return the number of bytes held in all.
 */
static size_t
    flipso_type2_read_pages(FlipsoType2* type2, Iso14443_3aPoller* poller, size_t limit) {
    while(type2->len + FLIPSO_TYPE2_READ_BYTES <= limit) {
        const uint8_t command[] = {
            FLIPSO_TYPE2_READ_CMD, (uint8_t)(type2->len / FLIPSO_TYPE2_PAGE_BYTES)};
        bit_buffer_reset(type2->tx);
        bit_buffer_append_bytes(type2->tx, command, sizeof(command));

        Iso14443_3aError error = iso14443_3a_poller_send_standard_frame(
            poller, type2->tx, type2->rx, FLIPSO_TYPE2_FWT_FC);
        if(error != Iso14443_3aErrorNone) break;

        /* A refusal is a short frame - a 4-bit NAK - rather than an error on some
         * cards, so a response too short to be four pages ends the read too. */
        if(bit_buffer_get_size_bytes(type2->rx) < FLIPSO_TYPE2_READ_BYTES) break;

        memcpy(type2->pages + type2->len, bit_buffer_get_data(type2->rx), FLIPSO_TYPE2_READ_BYTES);
        type2->len += FLIPSO_TYPE2_READ_BYTES;
    }

    return type2->len;
}

/* ------------------------------------------------------------------ */
/* CMD9 and CMD10: a full shell over logical sectors                   */
/* ------------------------------------------------------------------ */

/**
 * Read and decode a CMD9 or CMD10 card, whose first 128 bytes are in hand.
 *
 * The card is decoded from the blocks it is saved as rather than walked twice,
 * so the screens of a live read and of the saved file are the same by
 * construction; flipso_capture_add_type2_full() says why those blocks.
 */
static FlipsoReaderStatus flipso_type2_read_full(
    FlipsoType2* type2,
    Iso14443_3aPoller* poller,
    ItsoCard* card,
    FlipsoCapture* capture) {
    uint8_t shell[ITSO_TYPE2_FULL_SHELL_LEN];
    itso_type2_full_shell(type2->pages, type2->len, shell);
    if(!itso_parse_shell(card, shell, sizeof(shell))) {
        FURI_LOG_W(TAG, "Type 2: full shell did not parse (reject %u)", card->shell_reject);
        return FlipsoReaderStatusBadShell;
    }
    flipso_log_shell_owner(card);

    /* The media fix their geometry (TS 1000-10 clauses 10.11.5, 11.14.5), and
     * the sector map is only true of that geometry: a shell that states another
     * is not one whose sectors could be found. */
    size_t needed = itso_type2_full_len(card);
    FURI_LOG_D(
        TAG,
        "Type 2 full shell: FVC %u, B=%u S=%u E=%u SCTL=%u, %u bytes of sectors",
        card->fvc,
        card->sector_size,
        card->sector_count,
        card->dir_entries,
        card->sct_len,
        (unsigned)needed);
    if(needed == 0 || needed > sizeof(type2->pages)) {
        card->shell_reject = ItsoShellRejectGeometry;
        card->shell_valid = false;
        return FlipsoReaderStatusBadShell;
    }

    /* The rest of the sectors. Every one is on the chip - the media hold all six
     * whether or not they are used - so a read that stops short is a card that
     * left the field, and a half-read card is neither shown nor saved. */
    if(flipso_type2_read_pages(type2, poller, needed) < needed) {
        FURI_LOG_W(
            TAG, "Type 2: read stopped at %u of %u bytes", (unsigned)type2->len, (unsigned)needed);
        return FlipsoReaderStatusCardLost;
    }

    if(!flipso_capture_add_type2_full(capture, card, type2->pages, type2->len) ||
       !flipso_capture_decode(capture, card)) {
        return FlipsoReaderStatusBadShell;
    }

    /* The lines the smartcard transports log for each product, for the same
     * reason: they are what to watch while someone taps a card. */
    for(uint8_t i = 0; i < card->product_count; i++) {
        const ItsoProduct* product = &card->products[i];
        FURI_LOG_D(
            TAG,
            "E%u: TYP %u.%u, rev %u, bitmap 0x%02X",
            product->dir_index,
            product->typ,
            product->ptyp,
            product->format_rev,
            product->bitmap);
    }
    FURI_LOG_I(
        TAG,
        "Type 2 ITSO card: FVC %u, directory %s, DIRS# %u, %u tap(s)",
        card->fvc,
        card->dir_valid ? "read" : "unreadable",
        card->dir_sequence,
        card->tap_count);
    return FlipsoReaderStatusSuccess;
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

    type2->len = 0;
    size_t len = flipso_type2_read_pages(type2, poller, FLIPSO_TYPE2_PROBE_BYTES);

    switch(itso_type2_kind(type2->pages, len)) {
    case ItsoType2Incomplete:
        /* Every Type 2 tag has at least the 64 bytes of an Ultralight, so fewer
         * means the tag left the field mid-read: a fumbled tap to retry, not a
         * verdict on the card - and not a half-read ticket to show or save. */
        FURI_LOG_D(TAG, "Type 2: read stopped after %u bytes", (unsigned)len);
        return FlipsoReaderStatusCardError;

    case ItsoType2FullShell:
        return flipso_type2_read_full(type2, poller, card, capture);

    case ItsoType2OtherShell: {
        /* ITSO, laid out like CMD9 and CMD10 but under a media definition this
         * build does not know. The shell is decoded so the error screen can say
         * which. */
        uint8_t shell[ITSO_TYPE2_FULL_SHELL_LEN];
        itso_type2_full_shell(type2->pages, len, shell);
        itso_parse_shell(card, shell, sizeof(shell));
        FURI_LOG_I(TAG, "Type 2: full ITSO shell, FVC %u - not read", card->fvc);
        return FlipsoReaderStatusUnsupported;
    }

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
