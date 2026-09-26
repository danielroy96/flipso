/**
 * @file flipso_capture.c
 * @brief Holds the raw blocks read off a card, and turns them back into a card.
 *
 * See flipso_capture.h for why a saved card is raw bytes rather than decoded
 * fields. The storage here is one arena that grows as blocks arrive, plus a
 * small index over it: a typical CMD7 card yields about 800 bytes across ten
 * blocks, and sizing for the 9.5 KB worst case up front would spend a twentieth
 * of the Flipper's heap on slack that almost no card uses.
 */
#include "flipso_capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Bumped only when a file this build writes would mislead an older one. */
#define FLIPSO_CAPTURE_VERSION 1

#define FLIPSO_CAPTURE_FILETYPE "Flipso card"

/* First allocation, and the step the arena doubles from. Covers a whole CMD7
 * card in two grows, and the tiny cards in one. */
#define FLIPSO_CAPTURE_CHUNK 512

typedef struct {
    uint8_t kind;
    uint8_t index; /**< Directory entry, for FlipsoBlockProduct. */
    uint16_t offset; /**< Into FlipsoCapture::bytes. */
    uint16_t len;
} FlipsoCaptureBlock;

struct FlipsoCapture {
    uint8_t* bytes;
    size_t len;
    size_t capacity;
    FlipsoCaptureBlock blocks[FLIPSO_CAPTURE_MAX_BLOCKS];
    uint8_t count;
    uint32_t timestamp;
};

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
static const FlipsoCaptureBlock*
    flipso_capture_find(const FlipsoCapture* capture, FlipsoBlockKind kind, uint8_t index) {
    for(uint8_t i = 0; i < capture->count; i++) {
        const FlipsoCaptureBlock* block = &capture->blocks[i];
        if(block->kind != (uint8_t)kind) continue;
        /* Only the per-product blocks carry an index - a directory entry, or a
         * history slot for a product that has left the card; the shell, the
         * directory and the two log blocks occur once per card. */
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
static uint8_t* flipso_capture_reserve(
    FlipsoCapture* capture,
    FlipsoBlockKind kind,
    uint8_t index,
    size_t len) {
    if(!capture || len == 0 || len > ITSO_MAX_GROUP_LEN) return NULL;
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

bool flipso_capture_valid(const FlipsoCapture* capture) {
    return capture && flipso_capture_find(capture, FlipsoBlockShell, 0) != NULL;
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
    if(!shell) return false;
    return itso_shell_card_number(capture->bytes + shell->offset, shell->len, out);
}

bool flipso_capture_decode(const FlipsoCapture* capture, ItsoCard* card) {
    if(!capture || !card) return false;

    itso_card_reset(card);

    const FlipsoCaptureBlock* shell = flipso_capture_find(capture, FlipsoBlockShell, 0);
    if(!shell) return false;
    if(!itso_parse_shell(card, capture->bytes + shell->offset, shell->len)) return false;

    /* From here the sequence mirrors flipso_read_card(), including what it does
     * when a group is missing: a directory that will not parse still leaves the
     * card number and expiry on screen, and a product with no block keeps
     * whatever its directory entry said about it. */
    const FlipsoCaptureBlock* dir = flipso_capture_find(capture, FlipsoBlockDirectory, 0);
    if(dir) itso_parse_directory(card, capture->bytes + dir->offset, dir->len);

    for(uint8_t i = 0; i < card->product_count; i++) {
        ItsoProduct* product = &card->products[i];
        const FlipsoCaptureBlock* group =
            flipso_capture_find(capture, FlipsoBlockProduct, product->dir_index);
        if(group) {
            itso_parse_ipe(
                product, capture->bytes + group->offset, group->len, card->sector_size);
        }
    }

    const FlipsoCaptureBlock* log = flipso_capture_find(capture, FlipsoBlockLog, 0);
    if(log) itso_parse_log(card, capture->bytes + log->offset, log->len);

    /* Then whatever earlier reads of this card saw, which only a saved card
     * carries. It goes in after the live blocks on purpose: the card is the
     * authority on what it holds now, and these only fill in what has since
     * rolled off the end of its own rolling windows. */
    for(uint8_t i = 0; i < card->product_count; i++) {
        ItsoProduct* product = &card->products[i];
        const FlipsoCaptureBlock* history =
            flipso_capture_find(capture, FlipsoBlockValueHistory, product->dir_index);
        if(history) {
            itso_parse_value_history(
                product, capture->bytes + history->offset, history->len);
        }
    }

    /* And the products the card has dropped altogether, appended after the ones
     * it still lists so that a screen walking the array shows the card before
     * it shows the card's past. Each carries the directory entry that described
     * it, because the directory in this file no longer does. */
    for(uint8_t slot = 0; slot < ITSO_MAX_HISTORIC_PRODUCTS; slot++) {
        uint8_t index = (uint8_t)(FLIPSO_CAPTURE_HISTORY_BASE + slot);
        const FlipsoCaptureBlock* gone =
            flipso_capture_find(capture, FlipsoBlockProductHistory, index);
        if(!gone || gone->len < FLIPSO_PRODUCT_HISTORY_HEADER) continue;

        const uint8_t* data = capture->bytes + gone->offset;
        uint32_t last_seen = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                             ((uint32_t)data[2] << 8) | data[3];

        ItsoProduct* product = itso_card_add_product(card, data + 5, data[4]);
        if(!product) break;

        itso_parse_ipe(
            product, data + FLIPSO_PRODUCT_HISTORY_HEADER,
            gone->len - FLIPSO_PRODUCT_HISTORY_HEADER, card->sector_size);

        /* Its own value history is keyed by the same slot, not by the entry it
         * used to hold: that entry may belong to a live product by now. */
        const FlipsoCaptureBlock* history =
            flipso_capture_find(capture, FlipsoBlockValueHistory, index);
        if(history) {
            itso_parse_value_history(
                product, capture->bytes + history->offset, history->len);
        }

        /* Last, so that the records the group itself held are marked too: they
         * were live when it was captured, and are not now. */
        itso_product_off_card(product, last_seen);
    }

    const FlipsoCaptureBlock* log_history =
        flipso_capture_find(capture, FlipsoBlockLogHistory, 0);
    if(log_history) {
        itso_parse_log_history(card, capture->bytes + log_history->offset, log_history->len);
    }

    return true;
}

/* ------------------------------------------------------------------ */
/* Merging in what an earlier read of the same card saw                 */
/* ------------------------------------------------------------------ */

/**
 * A run of fixed-length records inside one block, for the merge to walk.
 *
 * The two histories differ in the length of a record and in how one is dated,
 * and in nothing else, so the walk below is written once against this.
 */
typedef struct {
    const uint8_t* data;
    uint8_t count;
} FlipsoRecordRun;

/** The records of a block of back-to-back fixed-length records. */
static FlipsoRecordRun flipso_capture_run(
    const FlipsoCapture* capture,
    const FlipsoCaptureBlock* block,
    size_t record_len) {
    FlipsoRecordRun run = {NULL, 0};
    if(!block) return run;
    run.data = capture->bytes + block->offset;
    run.count = (uint8_t)(block->len / record_len);
    return run;
}

/** True when @p record appears byte for byte anywhere in @p run. */
static bool flipso_run_holds(const FlipsoRecordRun* run, const uint8_t* record, size_t len) {
    for(uint8_t i = 0; i < run->count; i++) {
        if(memcmp(run->data + (size_t)i * len, record, len) == 0) return true;
    }
    return false;
}

/**
 * Collect the records of @p from that @p live and @p also_live do not hold,
 * newest first, into @p out.
 *
 * @param newer   orders two records of this kind.
 * @return how many were collected, at most @p max.
 */
static uint8_t flipso_collect_history(
    const FlipsoRecordRun* from,
    const FlipsoRecordRun* live,
    const FlipsoRecordRun* also_live,
    size_t record_len,
    bool (*present)(const uint8_t* record, size_t len),
    bool (*newer)(const uint8_t* a, const uint8_t* b),
    uint8_t max,
    uint8_t* out) {
    uint8_t kept = 0;

    for(uint8_t i = 0; i < from->count; i++) {
        const uint8_t* record = from->data + (size_t)i * record_len;
        if(!present(record, record_len)) continue;
        if(flipso_run_holds(live, record, record_len)) continue;
        if(also_live && flipso_run_holds(also_live, record, record_len)) continue;

        /* Already collected: the previous file holds its live log and its own
         * history, and a record can sit in both if it was written twice. */
        FlipsoRecordRun collected = {out, kept};
        if(flipso_run_holds(&collected, record, record_len)) continue;

        /* Insertion sort, newest first, so that a cap keeps the newest. */
        uint8_t pos = kept;
        for(uint8_t j = 0; j < kept; j++) {
            if(newer(record, out + (size_t)j * record_len)) {
                pos = j;
                break;
            }
        }
        if(pos >= max) continue;

        if(kept < max) kept++;
        for(uint8_t j = kept - 1; j > pos; j--) {
            memcpy(out + (size_t)j * record_len, out + (size_t)(j - 1) * record_len, record_len);
        }
        memcpy(out + (size_t)pos * record_len, record, record_len);
    }

    return kept;
}

/** Records of @p live that @p previous and its history have never seen. */
static uint8_t flipso_count_new(
    const FlipsoRecordRun* live,
    const FlipsoRecordRun* previous,
    const FlipsoRecordRun* previous_history,
    size_t record_len,
    bool (*present)(const uint8_t* record, size_t len)) {
    uint8_t count = 0;
    for(uint8_t i = 0; i < live->count; i++) {
        const uint8_t* record = live->data + (size_t)i * record_len;
        if(!present(record, record_len)) continue;
        if(flipso_run_holds(previous, record, record_len)) continue;
        if(flipso_run_holds(previous_history, record, record_len)) continue;
        count++;
    }
    return count;
}

/** A value record is written unless it is all zeros; a slot is blank until used. */
static bool flipso_value_record_present(const uint8_t* record, size_t len) {
    return !itso_is_blank(record, len);
}

/** The value records inside one product block, as a run. */
static FlipsoRecordRun flipso_capture_value_run(
    const FlipsoCapture* capture,
    const FlipsoCaptureBlock* group,
    uint8_t sector_size) {
    FlipsoRecordRun run = {NULL, 0};
    if(!group || sector_size == 0) return run;

    size_t offset = 0;
    uint8_t records =
        itso_value_records(capture->bytes + group->offset, group->len, sector_size, &offset);
    if(records == 0) return run;

    run.data = capture->bytes + group->offset + offset;
    run.count = records;
    return run;
}

/** True when both captures describe directory entry @p index the same way. */
static bool flipso_capture_same_entry(
    const FlipsoCapture* a,
    const FlipsoCapture* b,
    uint8_t index) {
    const FlipsoCaptureBlock* dir_a = flipso_capture_find(a, FlipsoBlockDirectory, 0);
    const FlipsoCaptureBlock* dir_b = flipso_capture_find(b, FlipsoBlockDirectory, 0);
    if(!dir_a || !dir_b) return false;

    const uint8_t* entry_a = itso_dir_entry(a->bytes + dir_a->offset, dir_a->len, index);
    const uint8_t* entry_b = itso_dir_entry(b->bytes + dir_b->offset, dir_b->len, index);
    if(!entry_a || !entry_b) return false;

    return memcmp(entry_a, entry_b, ITSO_DIR_ENTRY_LEN) == 0;
}

/**
 * Fold in each product's value records, entry by entry.
 *
 * Its own function because the log above shares nothing with it but the helpers
 * - and because the geometry it needs may be missing, which must not cost the
 * caller the counts it has already made.
 */
static void flipso_merge_value_records(
    FlipsoCapture* capture,
    const FlipsoCapture* previous,
    FlipsoCaptureDiff* found) {
    const FlipsoCaptureBlock* shell = flipso_capture_find(capture, FlipsoBlockShell, 0);
    if(!shell) return;
    uint8_t sector_size = itso_shell_sector_size(capture->bytes + shell->offset, shell->len);
    if(sector_size == 0) return;

    const FlipsoCaptureBlock* was_shell = flipso_capture_find(previous, FlipsoBlockShell, 0);
    if(!was_shell) return;
    uint8_t was_sector_size =
        itso_shell_sector_size(previous->bytes + was_shell->offset, was_shell->len);
    if(was_sector_size == 0) return;

    uint8_t records[FLIPSO_CAPTURE_MAX_VALUE_HISTORY * ITSO_VALUE_RECORD_LEN];

    for(uint8_t entry = 1; entry <= ITSO_MAX_PRODUCTS; entry++) {
        const FlipsoCaptureBlock* group =
            flipso_capture_find(capture, FlipsoBlockProduct, entry);
        const FlipsoCaptureBlock* was_group =
            flipso_capture_find(previous, FlipsoBlockProduct, entry);
        if(!group && !was_group) continue;

        /* A directory entry that has changed hands is not the same product, and
         * its transactions are not this one's. The product that used to be
         * there is not lost by this: flipso_carry_gone_products() keeps it. */
        if(!flipso_capture_same_entry(capture, previous, entry)) continue;

        FlipsoRecordRun live = flipso_capture_value_run(capture, group, sector_size);
        FlipsoRecordRun was = flipso_capture_value_run(previous, was_group, was_sector_size);
        FlipsoRecordRun was_history = flipso_capture_run(
            previous, flipso_capture_find(previous, FlipsoBlockValueHistory, entry),
            ITSO_VALUE_RECORD_LEN);

        found->new_values = (uint8_t)(
            found->new_values + flipso_count_new(
                                    &live, &was, &was_history, ITSO_VALUE_RECORD_LEN,
                                    flipso_value_record_present));

        uint8_t kept = flipso_collect_history(
            &was, &live, NULL, ITSO_VALUE_RECORD_LEN, flipso_value_record_present,
            itso_value_record_newer, FLIPSO_CAPTURE_MAX_VALUE_HISTORY, records);
        FlipsoRecordRun collected = {records, kept};
        kept += flipso_collect_history(
            &was_history, &live, &collected, ITSO_VALUE_RECORD_LEN,
            flipso_value_record_present, itso_value_record_newer,
            (uint8_t)(FLIPSO_CAPTURE_MAX_VALUE_HISTORY - kept),
            records + (size_t)kept * ITSO_VALUE_RECORD_LEN);

        if(kept) {
            found->kept_values = (uint8_t)(found->kept_values + kept);
            flipso_capture_add(
                capture, FlipsoBlockValueHistory, entry, records,
                (size_t)kept * ITSO_VALUE_RECORD_LEN);
        }
    }
}

/**
 * One product an earlier read found and this one did not, waiting to be written
 * into @p capture. Everything points into the previous capture, which outlives
 * the merge.
 */
typedef struct {
    uint32_t last_seen; /**< Unix time of the read that still saw it. */
    uint8_t entry; /**< Directory entry it held then. */
    const uint8_t* dir_entry; /**< ITSO_DIR_ENTRY_LEN bytes that described it. */
    const uint8_t* group; /**< Its IPE and value record groups, chained. */
    uint16_t group_len;
    const uint8_t* values; /**< Value records archived for it before, or NULL. */
    uint16_t values_len;
} FlipsoGoneProduct;

/** Keep @p gone among the @p max newest, newest first. @return the new count. */
static uint8_t flipso_keep_gone(
    FlipsoGoneProduct* kept,
    uint8_t count,
    uint8_t max,
    const FlipsoGoneProduct* gone) {
    uint8_t pos = count;
    for(uint8_t i = 0; i < count; i++) {
        if(gone->last_seen > kept[i].last_seen) {
            pos = i;
            break;
        }
    }
    if(pos >= max) return count;

    if(count < max) count++;
    for(uint8_t i = (uint8_t)(count - 1); i > pos; i--) {
        kept[i] = kept[i - 1];
    }
    kept[pos] = *gone;
    return count;
}

/**
 * Carry forward the products @p previous saw and @p capture's card no longer
 * lists.
 *
 * A directory entry is freed when a ticket expires and is removed, and reused
 * when the next one is sold, so the card's own account of what it carries is
 * only ever the present tense. The file is the only place a ticket that ran out
 * last month still exists.
 *
 * What counts as gone is the entry bytes changing, which is the same test the
 * value records use: same owner, type, subtype, VGP and expiry or it is not the
 * same product. So a renewed season ticket leaves its old self behind here, in
 * the same way a replaced one does - both are products the card used to hold
 * and does not now, which is exactly what these blocks are for.
 */
static void flipso_carry_gone_products(
    FlipsoCapture* capture,
    const FlipsoCapture* previous,
    FlipsoCaptureDiff* found) {
    /* Without a directory there is nothing to have left: a read that lost that
     * block would otherwise look like a card that had shed every product. */
    if(!flipso_capture_find(capture, FlipsoBlockDirectory, 0)) return;

    FlipsoGoneProduct kept[ITSO_MAX_HISTORIC_PRODUCTS];
    uint8_t count = 0;

    /* The products the previous read found in the directory, which this read
     * either still finds in the same entry or does not find at all. */
    for(uint8_t entry = 1; entry <= ITSO_MAX_PRODUCTS; entry++) {
        const FlipsoCaptureBlock* was_group =
            flipso_capture_find(previous, FlipsoBlockProduct, entry);
        /* A Product block is the guard against carrying something that was
         * never a product: the last directory entry can be the Log Directory
         * Entry, and no reader writes a group for that. */
        if(!was_group) continue;
        if(flipso_capture_same_entry(capture, previous, entry)) continue;

        const FlipsoCaptureBlock* was_dir =
            flipso_capture_find(previous, FlipsoBlockDirectory, 0);
        if(!was_dir) continue;
        const uint8_t* dir_entry =
            itso_dir_entry(previous->bytes + was_dir->offset, was_dir->len, entry);
        if(!dir_entry) continue;

        const FlipsoCaptureBlock* was_values =
            flipso_capture_find(previous, FlipsoBlockValueHistory, entry);

        FlipsoGoneProduct gone = {
            previous->timestamp,
            entry,
            dir_entry,
            previous->bytes + was_group->offset,
            was_group->len,
            was_values ? previous->bytes + was_values->offset : NULL,
            was_values ? was_values->len : 0,
        };
        count = flipso_keep_gone(kept, count, ITSO_MAX_HISTORIC_PRODUCTS, &gone);
    }

    /* And the ones it had already carried, each keeping the date it left by. */
    for(uint8_t slot = 0; slot < ITSO_MAX_HISTORIC_PRODUCTS; slot++) {
        uint8_t index = (uint8_t)(FLIPSO_CAPTURE_HISTORY_BASE + slot);
        const FlipsoCaptureBlock* block =
            flipso_capture_find(previous, FlipsoBlockProductHistory, index);
        if(!block || block->len < FLIPSO_PRODUCT_HISTORY_HEADER) continue;

        const uint8_t* data = previous->bytes + block->offset;
        const FlipsoCaptureBlock* was_values =
            flipso_capture_find(previous, FlipsoBlockValueHistory, index);

        FlipsoGoneProduct gone = {
            ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                ((uint32_t)data[2] << 8) | data[3],
            data[4],
            data + 5,
            data + FLIPSO_PRODUCT_HISTORY_HEADER,
            (uint16_t)(block->len - FLIPSO_PRODUCT_HISTORY_HEADER),
            was_values ? previous->bytes + was_values->offset : NULL,
            was_values ? was_values->len : 0,
        };
        count = flipso_keep_gone(kept, count, ITSO_MAX_HISTORIC_PRODUCTS, &gone);
    }

    /* Renumbered into slots as they are written: the slot a product had in the
     * previous file means nothing beyond keeping its blocks together. */
    for(uint8_t i = 0; i < count; i++) {
        const FlipsoGoneProduct* gone = &kept[i];
        /* The header has to fit inside the same block as the group, and a block
         * is capped at one sector chain. A group that long is longer than any
         * geometry the decoder assembles, so this is a bound rather than a case
         * worth handling. */
        if((size_t)gone->group_len + FLIPSO_PRODUCT_HISTORY_HEADER > ITSO_MAX_GROUP_LEN) {
            continue;
        }

        uint8_t index = (uint8_t)(FLIPSO_CAPTURE_HISTORY_BASE + i);
        uint8_t* out = flipso_capture_reserve(
            capture, FlipsoBlockProductHistory, index,
            (size_t)gone->group_len + FLIPSO_PRODUCT_HISTORY_HEADER);
        /* Written in place rather than through a buffer: the sources point into
         * the previous capture, which the growth of this one cannot move. */
        if(!out) continue;
        out[0] = (uint8_t)(gone->last_seen >> 24);
        out[1] = (uint8_t)(gone->last_seen >> 16);
        out[2] = (uint8_t)(gone->last_seen >> 8);
        out[3] = (uint8_t)gone->last_seen;
        out[4] = gone->entry;
        memcpy(out + 5, gone->dir_entry, ITSO_DIR_ENTRY_LEN);
        memcpy(out + FLIPSO_PRODUCT_HISTORY_HEADER, gone->group, gone->group_len);

        if(gone->values && gone->values_len) {
            flipso_capture_add(
                capture, FlipsoBlockValueHistory, index, gone->values, gone->values_len);
        }

        found->kept_products++;
    }
}

void flipso_capture_merge_history(
    FlipsoCapture* capture,
    const FlipsoCapture* previous,
    FlipsoCaptureDiff* diff) {
    FlipsoCaptureDiff found;
    memset(&found, 0, sizeof(found));
    if(diff) *diff = found;
    if(!capture || !previous) return;

    /* The largest history the log can produce, on the stack rather than the
     * heap: 384 bytes against the app's 4 KB. */
    uint8_t records[FLIPSO_CAPTURE_MAX_LOG_HISTORY * ITSO_TAP_RECORD_LEN];

    /* --- the cyclic log, which belongs to the card rather than a product --- */
    FlipsoRecordRun live =
        flipso_capture_run(capture, flipso_capture_find(capture, FlipsoBlockLog, 0),
                           ITSO_TAP_RECORD_LEN);
    FlipsoRecordRun was =
        flipso_capture_run(previous, flipso_capture_find(previous, FlipsoBlockLog, 0),
                           ITSO_TAP_RECORD_LEN);
    FlipsoRecordRun was_history = flipso_capture_run(
        previous, flipso_capture_find(previous, FlipsoBlockLogHistory, 0),
        ITSO_TAP_RECORD_LEN);

    found.new_taps =
        flipso_count_new(&live, &was, &was_history, ITSO_TAP_RECORD_LEN,
                         itso_tap_record_present);

    /* The previous file's live log first and its history after it, so that when
     * the two together overflow the cap it is the oldest that are dropped: what
     * was live then is newer than what it had already archived. Each run is
     * sorted within itself, which is all the cap needs - the decoder sorts the
     * whole history again when it reads the file back. */
    uint8_t kept = flipso_collect_history(
        &was, &live, NULL, ITSO_TAP_RECORD_LEN, itso_tap_record_present,
        itso_tap_record_newer, FLIPSO_CAPTURE_MAX_LOG_HISTORY, records);
    FlipsoRecordRun collected = {records, kept};
    kept += flipso_collect_history(
        &was_history, &live, &collected, ITSO_TAP_RECORD_LEN, itso_tap_record_present,
        itso_tap_record_newer, (uint8_t)(FLIPSO_CAPTURE_MAX_LOG_HISTORY - kept),
        records + (size_t)kept * ITSO_TAP_RECORD_LEN);

    if(kept) {
        found.kept_taps = kept;
        flipso_capture_add(
            capture, FlipsoBlockLogHistory, 0, records,
            (size_t)kept * ITSO_TAP_RECORD_LEN);
    }

    flipso_merge_value_records(capture, previous, &found);
    flipso_carry_gone_products(capture, previous, &found);

    if(diff) *diff = found;
}

/* ------------------------------------------------------------------ */
/* The saved file, a line at a time                                    */
/* ------------------------------------------------------------------ */

/** Header lines that come before the blocks. */
#define FLIPSO_CAPTURE_HEADER_LINES 3

/** Write the key a block is stored under, e.g. "Product 3". */
static void flipso_capture_key(const FlipsoCaptureBlock* block, char* out, size_t out_len) {
    switch((FlipsoBlockKind)block->kind) {
    case FlipsoBlockShell:
        snprintf(out, out_len, "Shell");
        break;
    case FlipsoBlockDirectory:
        snprintf(out, out_len, "Directory");
        break;
    case FlipsoBlockLog:
        snprintf(out, out_len, "Log");
        break;
    case FlipsoBlockLogHistory:
        snprintf(out, out_len, "Log history");
        break;
    case FlipsoBlockValueHistory:
        snprintf(out, out_len, "Value history %u", block->index);
        break;
    case FlipsoBlockProductHistory:
        snprintf(out, out_len, "Product history %u", block->index);
        break;
    case FlipsoBlockProduct:
    default:
        snprintf(out, out_len, "Product %u", block->index);
        break;
    }
}

size_t flipso_capture_lines(const FlipsoCapture* capture) {
    if(!capture) return 0;
    return FLIPSO_CAPTURE_HEADER_LINES + capture->count;
}

bool flipso_capture_line(const FlipsoCapture* capture, size_t index, char* out, size_t out_len) {
    if(!capture || !out || out_len == 0) return false;
    if(index >= flipso_capture_lines(capture)) return false;

    if(index == 0) {
        return (size_t)snprintf(out, out_len, "Filetype: %s", FLIPSO_CAPTURE_FILETYPE) < out_len;
    }
    if(index == 1) {
        return (size_t)snprintf(out, out_len, "Version: %u", FLIPSO_CAPTURE_VERSION) < out_len;
    }
    if(index == 2) {
        return (size_t)snprintf(out, out_len, "Read at: %lu", (unsigned long)capture->timestamp) <
               out_len;
    }

    const FlipsoCaptureBlock* block = &capture->blocks[index - FLIPSO_CAPTURE_HEADER_LINES];
    char key[FLIPSO_CAPTURE_KEY_MAX];
    flipso_capture_key(block, key, sizeof(key));

    /* Space-separated hex, as the firmware's own files write a byte array. */
    size_t pos = (size_t)snprintf(out, out_len, "%s:", key);
    if(pos >= out_len) return false;
    for(uint16_t i = 0; i < block->len; i++) {
        if(pos + 4 > out_len) return false;
        pos += (size_t)snprintf(
            out + pos, out_len - pos, " %02X", capture->bytes[block->offset + i]);
    }
    return true;
}

/** Value of one hex digit, or -1. */
static int flipso_capture_nibble(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool flipso_capture_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/**
 * Count the bytes a hex value carries.
 * @return -1 for anything that is not whitespace-separated pairs of hex digits.
 */
static int flipso_capture_hex_len(const char* value) {
    int count = 0;
    while(*value) {
        if(flipso_capture_space(*value)) {
            value++;
            continue;
        }
        if(flipso_capture_nibble(value[0]) < 0 || flipso_capture_nibble(value[1]) < 0) return -1;
        value += 2;
        count++;
    }
    return count;
}

/** Fill @p out with the bytes counted by flipso_capture_hex_len(). */
static void flipso_capture_hex(const char* value, uint8_t* out, size_t len) {
    for(size_t i = 0; i < len; i++) {
        while(flipso_capture_space(*value)) {
            value++;
        }
        out[i] = (uint8_t)(
            (flipso_capture_nibble(value[0]) << 4) | flipso_capture_nibble(value[1]));
        value += 2;
    }
}

/**
 * True when @p key is @p prefix followed by a number, which goes to @p index.
 *
 * Both the per-product blocks are keyed this way - "Product 3" and "Value
 * history 3" - so the entry number is parsed once.
 */
static bool
    flipso_capture_indexed_key(const char* key, const char* prefix, uint8_t* index) {
    size_t prefix_len = strlen(prefix);
    if(strncmp(key, prefix, prefix_len) != 0) return false;

    const char* digits = key + prefix_len;
    if(*digits < '0' || *digits > '9') return false;

    /* Bounded by what the field can hold rather than by how many products a
     * shell may have: an entry number no product claims simply goes unused when
     * the card is decoded, and refusing it here would silently drop a block
     * that the writing side was perfectly willing to write. */
    unsigned value = 0;
    while(*digits >= '0' && *digits <= '9') {
        value = value * 10 + (unsigned)(*digits++ - '0');
        if(value > UINT8_MAX) return false;
    }
    if(*digits != '\0') return false;

    *index = (uint8_t)value;
    return true;
}

bool flipso_capture_parse_line(FlipsoCapture* capture, const char* line) {
    if(!capture || !line) return false;

    const char* colon = strchr(line, ':');
    if(!colon) return true; /* Blank line, or a comment: nothing to take from it. */

    /* Split into a trimmed key and the value after it. The key is bounded by
     * the longest one we write, so a line with a runaway key is simply not one
     * of ours. */
    char key[FLIPSO_CAPTURE_KEY_MAX];
    size_t key_len = (size_t)(colon - line);
    while(key_len && flipso_capture_space(line[0])) {
        line++;
        key_len--;
    }
    while(key_len && flipso_capture_space(line[key_len - 1])) {
        key_len--;
    }
    if(key_len == 0 || key_len >= sizeof(key)) return true;
    memcpy(key, line, key_len);
    key[key_len] = '\0';

    const char* value = colon + 1;
    while(flipso_capture_space(*value)) {
        value++;
    }
    /* Lines arrive from the file with their newline still on them, and the
     * fields below are compared or scanned to the end of the value. */
    const char* value_end = value + strlen(value);
    while(value_end > value && flipso_capture_space(value_end[-1])) {
        value_end--;
    }

    if(strcmp(key, "Filetype") == 0) {
        size_t len = sizeof(FLIPSO_CAPTURE_FILETYPE) - 1;
        return (size_t)(value_end - value) == len &&
               memcmp(value, FLIPSO_CAPTURE_FILETYPE, len) == 0;
    }

    if(strcmp(key, "Version") == 0) {
        unsigned parsed = 0;
        if(*value < '0' || *value > '9') return false;
        while(*value >= '0' && *value <= '9') {
            parsed = parsed * 10 + (unsigned)(*value++ - '0');
            if(parsed > FLIPSO_CAPTURE_VERSION) return false;
        }
        /* A file from a later build may place blocks this one would misread, so
         * it is refused rather than half-loaded. */
        return parsed == FLIPSO_CAPTURE_VERSION;
    }

    if(strcmp(key, "Read at") == 0) {
        unsigned long parsed = 0;
        while(*value >= '0' && *value <= '9') {
            parsed = parsed * 10 + (unsigned long)(*value++ - '0');
        }
        capture->timestamp = (uint32_t)parsed;
        return true;
    }

    FlipsoBlockKind kind;
    uint8_t index = 0;
    if(strcmp(key, "Shell") == 0) {
        kind = FlipsoBlockShell;
    } else if(strcmp(key, "Directory") == 0) {
        kind = FlipsoBlockDirectory;
    } else if(strcmp(key, "Log") == 0) {
        kind = FlipsoBlockLog;
    } else if(strcmp(key, "Log history") == 0) {
        kind = FlipsoBlockLogHistory;
    } else if(flipso_capture_indexed_key(key, "Product history ", &index)) {
        kind = FlipsoBlockProductHistory;
    } else if(flipso_capture_indexed_key(key, "Product ", &index)) {
        kind = FlipsoBlockProduct;
    } else if(flipso_capture_indexed_key(key, "Value history ", &index)) {
        kind = FlipsoBlockValueHistory;
    } else {
        /* A key from a later build. Skipping it loses that block and no more,
         * which is why the blocks are keyed rather than positional. */
        return true;
    }

    int len = flipso_capture_hex_len(value);
    /* A block whose hex is malformed is dropped whole rather than truncated:
     * half a sector chain would decode to plausible nonsense, while a missing
     * one decodes to a product the card shows without its details. */
    if(len <= 0) return true;

    uint8_t* out = flipso_capture_reserve(capture, kind, index, (size_t)len);
    if(out) flipso_capture_hex(value, out, (size_t)len);
    return true;
}
