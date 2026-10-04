/**
 * @file flipso_capture.c
 * @brief Holds the raw blocks read off a card, as they arrive.
 *
 * See flipso_capture.h for why a saved card is raw bytes rather than decoded
 * fields. The storage here is one arena that grows as blocks arrive, plus a
 * small index over it: a typical CMD7 card yields about 800 bytes across ten
 * blocks, and sizing for the 9.5 KB worst case up front would spend a twentieth
 * of the Flipper's heap on slack that almost no card uses.
 *
 * What is done with the blocks is beside it: flipso_capture_decode.c turns them
 * back into a card, flipso_capture_merge.c folds in an earlier read's history,
 * and flipso_capture_file.c writes and parses the saved file.
 */
#include "flipso_capture_i.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* First allocation, and the step the arena doubles from. Covers a whole CMD7
 * card in two grows, and the tiny cards in one. */
#define FLIPSO_CAPTURE_CHUNK 512

FlipsoCapture* flipso_capture_alloc(void) {
    FlipsoCapture* capture = malloc(sizeof(FlipsoCapture));
    memset(capture, 0, sizeof(FlipsoCapture));
    return capture;
}

void flipso_capture_free(FlipsoCapture* capture) {
    if(!capture) return;
    free(capture->bytes);
    free(capture);
}

void flipso_capture_reset(FlipsoCapture* capture) {
    if(!capture) return;
    /* The arena goes back too rather than being kept for the next card: between
     * scans the app sits on the idle screen, where holding a kilobyte of the
     * last card's bytes buys nothing. */
    free(capture->bytes);
    capture->bytes = NULL;
    capture->len = 0;
    capture->capacity = 0;
    capture->count = 0;
    capture->timestamp = 0;
}

/** The block of this kind and directory entry, or NULL. */
const FlipsoCaptureBlock*
    flipso_capture_find(const FlipsoCapture* capture, FlipsoBlockKind kind, uint8_t index) {
    for(uint8_t i = 0; i < capture->count; i++) {
        const FlipsoCaptureBlock* block = &capture->blocks[i];
        if(block->kind != (uint8_t)kind) continue;
        /* Only the per-product blocks carry an index - a directory entry, or a
         * history slot for a product that has left the card; the shell, the
         * directory, the two log blocks and the chip occur once per card. */
        if((kind == FlipsoBlockProduct || kind == FlipsoBlockValueHistory ||
            kind == FlipsoBlockProductHistory) &&
           block->index != index) {
            continue;
        }
        return block;
    }
    return NULL;
}

const uint8_t*
    flipso_capture_product_group(const FlipsoCapture* capture, uint8_t dir_index, size_t* len) {
    const FlipsoCaptureBlock* block = flipso_capture_find(capture, FlipsoBlockProduct, dir_index);
    if(!block) return NULL;
    *len = block->len;
    return capture->bytes + block->offset;
}

/**
 * Make room for one block and index it.
 * @return where to write @p len bytes, or NULL when the block cannot be kept.
 */
uint8_t* flipso_capture_reserve(
    FlipsoCapture* capture,
    FlipsoBlockKind kind,
    uint8_t index,
    size_t len) {
    /* A block is at most one sector chain, and a dropped product's is that
     * behind its header: a chain can fill ITSO_MAX_GROUP_LEN exactly - an
     * Ultralight EV1's IPE over two 128-byte sectors and both copies of its
     * value records - and still has to be kept when the card lets it go. */
    size_t max = kind == FlipsoBlockProductHistory ?
                     ITSO_MAX_GROUP_LEN + FLIPSO_PRODUCT_HISTORY_HEADER :
                     ITSO_MAX_GROUP_LEN;
    if(!capture || len == 0 || len > max) return NULL;
    if(capture->count >= FLIPSO_CAPTURE_MAX_BLOCKS) return NULL;
    /* A repeat means a malformed file, or a reader reading the same sector
     * twice; either way the first answer is the one to keep. */
    if(flipso_capture_find(capture, kind, index)) return NULL;
    if(capture->len + len > FLIPSO_CAPTURE_MAX_BYTES) return NULL;

    if(capture->len + len > capture->capacity) {
        size_t capacity = capture->capacity ? capture->capacity : FLIPSO_CAPTURE_CHUNK;
        while(capacity < capture->len + len) {
            capacity *= 2;
        }
        if(capacity > FLIPSO_CAPTURE_MAX_BYTES) capacity = FLIPSO_CAPTURE_MAX_BYTES;

        uint8_t* grown = realloc(capture->bytes, capacity);
        if(!grown) return NULL;
        capture->bytes = grown;
        capture->capacity = capacity;
    }

    FlipsoCaptureBlock* block = &capture->blocks[capture->count++];
    block->kind = (uint8_t)kind;
    block->index = index;
    block->offset = (uint16_t)capture->len;
    block->len = (uint16_t)len;
    capture->len += len;
    return capture->bytes + block->offset;
}

bool flipso_capture_add(
    FlipsoCapture* capture,
    FlipsoBlockKind kind,
    uint8_t index,
    const uint8_t* data,
    size_t len) {
    if(!data) return false;
    uint8_t* out = flipso_capture_reserve(capture, kind, index, len);
    if(!out) return false;
    memcpy(out, data, len);
    return true;
}

/**
 * Read a sector chain straight into a new block of the arena, which is then
 * trimmed to what the chain held - so no chain-sized scratch buffer is needed
 * beside the one the capture already has.
 */
static void flipso_capture_add_chain(
    FlipsoCapture* capture,
    FlipsoBlockKind kind,
    uint8_t index,
    size_t (*gather)(ItsoType2Pages* source, const uint8_t* dir, uint8_t start, uint8_t* out),
    ItsoType2Pages* source,
    const uint8_t* dir,
    uint8_t start) {
    uint8_t* out = flipso_capture_reserve(capture, kind, index, ITSO_MAX_GROUP_LEN);
    if(!out) return;
    size_t got = gather(source, dir, start, out);
    FlipsoCaptureBlock* block = &capture->blocks[capture->count - 1];
    capture->len -= block->len - got;
    block->len = (uint16_t)got;
    if(got == 0) capture->count--; /* The last block, so nothing moves. */
}

static size_t flipso_capture_gather_group(
    ItsoType2Pages* source,
    const uint8_t* dir,
    uint8_t start,
    uint8_t* out) {
    return itso_read_chain(
        source->card,
        dir,
        ITSO_TYPE2_DIR_LEN,
        start,
        itso_type2_read_sector,
        source,
        out,
        ITSO_MAX_GROUP_LEN);
}

static size_t flipso_capture_gather_log(
    ItsoType2Pages* source,
    const uint8_t* dir,
    uint8_t start,
    uint8_t* out) {
    (void)start;
    return itso_read_log_sectors(
        source->card,
        dir,
        ITSO_TYPE2_DIR_LEN,
        itso_type2_read_sector,
        source,
        out,
        ITSO_MAX_GROUP_LEN);
}

bool flipso_capture_add_type2_full(
    FlipsoCapture* capture,
    ItsoCard* card,
    const uint8_t* pages,
    size_t len) {
    if(!capture || !card || itso_type2_kind(pages, len) != ItsoType2FullShell) return false;

    uint8_t shell[ITSO_TYPE2_FULL_SHELL_LEN];
    itso_card_reset(card);
    itso_type2_full_shell(pages, len, shell);
    if(!itso_parse_shell(card, shell, sizeof(shell))) return false;
    size_t needed = itso_type2_full_len(card);
    if(needed == 0 || needed > len) return false;

    flipso_capture_add(capture, FlipsoBlockShell, 0, shell, sizeof(shell));
    flipso_capture_add(capture, FlipsoBlockTag, 0, pages, ITSO_TYPE2_TAG_LEN);

    /* As on the other media, a directory that will not parse still leaves the
     * shell worth keeping: the card number and expiry are in it. */
    const uint8_t* dir = itso_type2_directory(card, pages, len);
    if(!dir || !itso_parse_directory(card, dir, ITSO_TYPE2_DIR_LEN)) return true;
    flipso_capture_add(capture, FlipsoBlockDirectory, 0, dir, ITSO_TYPE2_DIR_LEN);

    ItsoType2Pages source = {card, pages, len};
    for(uint8_t i = 0; i < card->product_count; i++) {
        uint8_t entry = card->products[i].dir_index;
        flipso_capture_add_chain(
            capture, FlipsoBlockProduct, entry, flipso_capture_gather_group, &source, dir, entry);
    }
    /* Two records, one per sector, linked from the log entry's own sector
     * (TS 1000-10 figure 4.1: Log File A in sector 2, B in sector 3). */
    if(card->log_dir_index) {
        flipso_capture_add_chain(
            capture, FlipsoBlockLog, 0, flipso_capture_gather_log, &source, dir, 0);
    }
    return true;
}

const uint8_t* flipso_capture_chip(const FlipsoCapture* capture, size_t* len) {
    const FlipsoCaptureBlock* block = capture ? flipso_capture_find(capture, FlipsoBlockChip, 0) :
                                                NULL;
    if(!block) return NULL;
    *len = block->len;
    return capture->bytes + block->offset;
}

bool flipso_capture_valid(const FlipsoCapture* capture) {
    if(!capture) return false;
    /* A Type 2 tag holds its whole card in one page-memory block instead of a
     * separate shell, so either is enough to have something worth saving. */
    return flipso_capture_find(capture, FlipsoBlockShell, 0) != NULL ||
           flipso_capture_find(capture, FlipsoBlockType2, 0) != NULL;
}

/**
 * The identity of a Type 2 tag: its chip UID, as eighteen filename-safe
 * characters, so two of them are told apart when saved.
 *
 * A compact shell carries no per-card number - every card of the CMD shares the
 * implied one (633597 8189 0...) - so the ISRN cannot be the identity here the
 * way it is for a full shell. The chip's 7-byte UID can: it sits in pages 0-1 of
 * the page memory (byte 3 is BCC0 and skipped), and is the only thing that
 * differs between one paper ticket and the next. Rendered as hex behind the
 * compact OID so it is the right length for the field and safe in a file name.
 */
static bool flipso_capture_type2_identity(const FlipsoCapture* capture, char* out) {
    const FlipsoCaptureBlock* block = flipso_capture_find(capture, FlipsoBlockType2, 0);
    if(!block || block->len < 8) return false;
    const uint8_t* p = capture->bytes + block->offset;
    const uint8_t uid[7] = {p[0], p[1], p[2], p[4], p[5], p[6], p[7]};
    int n = snprintf(
        out,
        ITSO_ISRN_DIGITS + 1,
        "8189%02X%02X%02X%02X%02X%02X%02X",
        uid[0],
        uid[1],
        uid[2],
        uid[3],
        uid[4],
        uid[5],
        uid[6]);
    return n == ITSO_ISRN_DIGITS;
}

uint32_t flipso_capture_time(const FlipsoCapture* capture) {
    return capture ? capture->timestamp : 0;
}

void flipso_capture_set_time(FlipsoCapture* capture, uint32_t timestamp) {
    if(capture) capture->timestamp = timestamp;
}

bool flipso_capture_card_number(const FlipsoCapture* capture, char* out) {
    if(!capture || !out) return false;

    const FlipsoCaptureBlock* shell = flipso_capture_find(capture, FlipsoBlockShell, 0);
    if(shell) return itso_shell_card_number(capture->bytes + shell->offset, shell->len, out);

    /* A Type 2 tag has no full shell to take a number from, and its compact one is
     * the same for every card. Its identity is the chip UID instead, so that two
     * paper tickets do not save over each other. */
    return flipso_capture_type2_identity(capture, out);
}
