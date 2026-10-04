/**
 * @file flipso_capture_merge.c
 * @brief Merging in what an earlier read of the same card saw.
 *
 * The card keeps only a rolling window of its history - two value records a
 * product, four taps - and drops a product when its entry is freed. A saved
 * card read again keeps what the file remembered from before as blocks of its
 * own, so the history is as long as every read of the card put together.
 */
#include "flipso_capture_i.h"

#include <string.h>

/**
 * A run of fixed-length records inside one block, for the merge to walk.
 *
 * The two histories differ in the length of a record and in how one is dated,
 * and in nothing else, so the walk below is written once against this.
 *
 * A run may continue in another: a product on a software anti-tear card keeps
 * its value records in two copies of the group, a sector or more apart, and
 * the history is only whole with both.
 */
typedef struct FlipsoRecordRun {
    const uint8_t* data;
    uint8_t count;
    const struct FlipsoRecordRun* next;
} FlipsoRecordRun;

/** The records of a block of back-to-back fixed-length records. */
static FlipsoRecordRun flipso_capture_run(
    const FlipsoCapture* capture,
    const FlipsoCaptureBlock* block,
    size_t record_len) {
    FlipsoRecordRun run = {NULL, 0, NULL};
    if(!block) return run;
    run.data = capture->bytes + block->offset;
    run.count = (uint8_t)(block->len / record_len);
    return run;
}

/** True when @p record appears byte for byte anywhere in @p run or its sequels. */
static bool flipso_run_holds(const FlipsoRecordRun* run, const uint8_t* record, size_t len) {
    for(; run; run = run->next) {
        for(uint8_t i = 0; i < run->count; i++) {
            if(memcmp(run->data + (size_t)i * len, record, len) == 0) return true;
        }
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

    for(; from; from = from->next) {
        for(uint8_t i = 0; i < from->count; i++) {
            const uint8_t* record = from->data + (size_t)i * record_len;
            if(!present(record, record_len)) continue;
            if(flipso_run_holds(live, record, record_len)) continue;
            if(also_live && flipso_run_holds(also_live, record, record_len)) continue;

            /* Already collected: the previous file holds its live log and its own
         * history, and a record can sit in both if it was written twice. */
            FlipsoRecordRun collected = {out, kept, NULL};
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
                memcpy(
                    out + (size_t)j * record_len, out + (size_t)(j - 1) * record_len, record_len);
            }
            memcpy(out + (size_t)pos * record_len, record, record_len);
        }
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
    for(; live; live = live->next) {
        for(uint8_t i = 0; i < live->count; i++) {
            const uint8_t* record = live->data + (size_t)i * record_len;
            if(!present(record, record_len)) continue;
            if(flipso_run_holds(previous, record, record_len)) continue;
            if(flipso_run_holds(previous_history, record, record_len)) continue;
            count++;
        }
    }
    return count;
}

/** A value record is written unless it is all zeros; a slot is blank until used. */
static bool flipso_value_record_present(const uint8_t* record, size_t len) {
    return !itso_is_blank(record, len);
}

/**
 * The value records inside one product block, as a run: the current copy of the
 * group, continuing into the previous copy where the card keeps one.
 *
 * @param previous_copy filled with the second copy's run and linked behind the
 *                      one returned, so it must live as long as that does.
 */
static FlipsoRecordRun flipso_capture_value_run(
    const FlipsoCapture* capture,
    const FlipsoCaptureBlock* group,
    uint8_t sector_size,
    FlipsoRecordRun* previous_copy) {
    FlipsoRecordRun run = {NULL, 0, NULL};
    *previous_copy = run;
    if(!group || sector_size == 0) return run;

    const uint8_t* bytes = capture->bytes + group->offset;
    size_t offset = 0;
    uint8_t records = itso_value_records(bytes, group->len, sector_size, &offset);
    if(records == 0) return run;
    run.data = bytes + offset;
    run.count = records;

    records = itso_previous_value_records(bytes, group->len, sector_size, &offset);
    if(records) {
        previous_copy->data = bytes + offset;
        previous_copy->count = records;
        run.next = previous_copy;
    }
    return run;
}

/** True when both captures describe directory entry @p index the same way. */
static bool
    flipso_capture_same_entry(const FlipsoCapture* a, const FlipsoCapture* b, uint8_t index) {
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
        const FlipsoCaptureBlock* group = flipso_capture_find(capture, FlipsoBlockProduct, entry);
        const FlipsoCaptureBlock* was_group =
            flipso_capture_find(previous, FlipsoBlockProduct, entry);
        if(!group && !was_group) continue;

        /* A directory entry that has changed hands is not the same product, and
         * its transactions are not this one's. The product that used to be
         * there is not lost by this: flipso_carry_gone_products() keeps it. */
        if(!flipso_capture_same_entry(capture, previous, entry)) continue;

        FlipsoRecordRun live_copy, was_copy;
        FlipsoRecordRun live = flipso_capture_value_run(capture, group, sector_size, &live_copy);
        FlipsoRecordRun was =
            flipso_capture_value_run(previous, was_group, was_sector_size, &was_copy);
        FlipsoRecordRun was_history = flipso_capture_run(
            previous,
            flipso_capture_find(previous, FlipsoBlockValueHistory, entry),
            ITSO_VALUE_RECORD_LEN);

        found->new_values = (uint8_t)(found->new_values + flipso_count_new(
                                                              &live,
                                                              &was,
                                                              &was_history,
                                                              ITSO_VALUE_RECORD_LEN,
                                                              flipso_value_record_present));

        uint8_t kept = flipso_collect_history(
            &was,
            &live,
            NULL,
            ITSO_VALUE_RECORD_LEN,
            flipso_value_record_present,
            itso_value_record_newer,
            FLIPSO_CAPTURE_MAX_VALUE_HISTORY,
            records);
        FlipsoRecordRun collected = {records, kept, NULL};
        kept += flipso_collect_history(
            &was_history,
            &live,
            &collected,
            ITSO_VALUE_RECORD_LEN,
            flipso_value_record_present,
            itso_value_record_newer,
            (uint8_t)(FLIPSO_CAPTURE_MAX_VALUE_HISTORY - kept),
            records + (size_t)kept * ITSO_VALUE_RECORD_LEN);

        if(kept) {
            found->kept_values = (uint8_t)(found->kept_values + kept);
            flipso_capture_add(
                capture,
                FlipsoBlockValueHistory,
                entry,
                records,
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
    ItsoUnixTime last_seen; /**< When the read that still saw it was. */
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

        const FlipsoCaptureBlock* was_dir = flipso_capture_find(previous, FlipsoBlockDirectory, 0);
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
            ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) |
                data[3],
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
        uint8_t index = (uint8_t)(FLIPSO_CAPTURE_HISTORY_BASE + i);
        uint8_t* out = flipso_capture_reserve(
            capture,
            FlipsoBlockProductHistory,
            index,
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

/**
 * The IPE half of a product block: everything before its value group, or the
 * whole block when it has none. Value records are counted on their own, so this
 * is the part whose change the counts would otherwise miss.
 */
static size_t flipso_capture_ipe_len(
    const FlipsoCapture* capture,
    const FlipsoCaptureBlock* group,
    uint8_t sector_size) {
    size_t offset = 0;
    if(sector_size &&
       itso_value_records(capture->bytes + group->offset, group->len, sector_size, &offset)) {
        return offset - 2; /* Back over the value group's two-byte header. */
    }
    return group->len;
}

/**
 * Count what this read holds that the record did not, beyond records: products
 * new to the card, and products whose own data has changed.
 */
static void flipso_count_product_changes(
    const FlipsoCapture* capture,
    const FlipsoCapture* previous,
    FlipsoCaptureDiff* found) {
    /* A Type 2 tag is one block of pages and one product. Any difference in it
     * is the ticket changing - a ride used, a gate passed. */
    const FlipsoCaptureBlock* pages = flipso_capture_find(capture, FlipsoBlockType2, 0);
    const FlipsoCaptureBlock* was_pages = flipso_capture_find(previous, FlipsoBlockType2, 0);
    if(pages && was_pages) {
        if(pages->len != was_pages->len ||
           memcmp(
               capture->bytes + pages->offset, previous->bytes + was_pages->offset, pages->len) !=
               0) {
            found->changed_products = 1;
        }
        return;
    }

    const FlipsoCaptureBlock* shell = flipso_capture_find(capture, FlipsoBlockShell, 0);
    const FlipsoCaptureBlock* was_shell = flipso_capture_find(previous, FlipsoBlockShell, 0);
    uint8_t sector_size =
        shell ? itso_shell_sector_size(capture->bytes + shell->offset, shell->len) : 0;
    uint8_t was_sector_size =
        was_shell ? itso_shell_sector_size(previous->bytes + was_shell->offset, was_shell->len) :
                    0;

    for(uint8_t entry = 1; entry <= ITSO_MAX_PRODUCTS; entry++) {
        /* Only entries this read found a product in; the last entry can be the
         * Log Directory Entry, and no reader writes a group for that. */
        const FlipsoCaptureBlock* group = flipso_capture_find(capture, FlipsoBlockProduct, entry);
        if(!group) continue;

        if(!flipso_capture_same_entry(capture, previous, entry)) {
            found->new_products++;
            continue;
        }

        const FlipsoCaptureBlock* was_group =
            flipso_capture_find(previous, FlipsoBlockProduct, entry);
        if(!was_group) continue;
        size_t len = flipso_capture_ipe_len(capture, group, sector_size);
        size_t was_len = flipso_capture_ipe_len(previous, was_group, was_sector_size);
        if(len != was_len ||
           memcmp(capture->bytes + group->offset, previous->bytes + was_group->offset, len) != 0) {
            found->changed_products++;
        }
    }
}

/** True for the blocks a merge adds, as opposed to the ones a read produced. */
static bool flipso_capture_is_history(uint8_t kind) {
    return kind == FlipsoBlockLogHistory || kind == FlipsoBlockValueHistory ||
           kind == FlipsoBlockProductHistory;
}

/**
 * Take out whatever an earlier merge added, closing up the arena behind it.
 *
 * Blocks are appended in the order they arrive, so their offsets rise with
 * their index and each one moves down into the space before it, never over a
 * block not yet moved.
 */
static void flipso_capture_drop_history(FlipsoCapture* capture) {
    size_t write = 0;
    uint8_t kept = 0;
    for(uint8_t i = 0; i < capture->count; i++) {
        FlipsoCaptureBlock block = capture->blocks[i];
        if(flipso_capture_is_history(block.kind)) continue;
        if(block.offset != write) {
            memmove(capture->bytes + write, capture->bytes + block.offset, block.len);
            block.offset = (uint16_t)write;
        }
        write += block.len;
        capture->blocks[kept++] = block;
    }
    capture->count = kept;
    capture->len = write;
}

void flipso_capture_merge_history(
    FlipsoCapture* capture,
    const FlipsoCapture* previous,
    FlipsoCaptureDiff* diff) {
    FlipsoCaptureDiff found;
    memset(&found, 0, sizeof(found));
    if(diff) *diff = found;
    if(!capture || !previous) return;

    /* The save screen merges before it asks, so a user who backs out and asks
     * again merges the same record into this capture a second time. Starting
     * from the read alone makes that come out exactly as the first merge did,
     * rather than finding every history slot already taken. */
    flipso_capture_drop_history(capture);

    /* The largest history the log can produce, on the stack rather than the
     * heap: 384 bytes against the app's 4 KB. */
    uint8_t records[FLIPSO_CAPTURE_MAX_LOG_HISTORY * ITSO_TAP_RECORD_LEN];

    /* --- the cyclic log, which belongs to the card rather than a product --- */
    FlipsoRecordRun live = flipso_capture_run(
        capture, flipso_capture_find(capture, FlipsoBlockLog, 0), ITSO_TAP_RECORD_LEN);
    FlipsoRecordRun was = flipso_capture_run(
        previous, flipso_capture_find(previous, FlipsoBlockLog, 0), ITSO_TAP_RECORD_LEN);
    FlipsoRecordRun was_history = flipso_capture_run(
        previous, flipso_capture_find(previous, FlipsoBlockLogHistory, 0), ITSO_TAP_RECORD_LEN);

    found.new_taps =
        flipso_count_new(&live, &was, &was_history, ITSO_TAP_RECORD_LEN, itso_tap_record_present);

    /* The previous file's live log first and its history after it, so that when
     * the two together overflow the cap it is the oldest that are dropped: what
     * was live then is newer than what it had already archived. Each run is
     * sorted within itself, which is all the cap needs - the decoder sorts the
     * whole history again when it reads the file back. */
    uint8_t kept = flipso_collect_history(
        &was,
        &live,
        NULL,
        ITSO_TAP_RECORD_LEN,
        itso_tap_record_present,
        itso_tap_record_newer,
        FLIPSO_CAPTURE_MAX_LOG_HISTORY,
        records);
    FlipsoRecordRun collected = {records, kept, NULL};
    kept += flipso_collect_history(
        &was_history,
        &live,
        &collected,
        ITSO_TAP_RECORD_LEN,
        itso_tap_record_present,
        itso_tap_record_newer,
        (uint8_t)(FLIPSO_CAPTURE_MAX_LOG_HISTORY - kept),
        records + (size_t)kept * ITSO_TAP_RECORD_LEN);

    if(kept) {
        found.kept_taps = kept;
        flipso_capture_add(
            capture, FlipsoBlockLogHistory, 0, records, (size_t)kept * ITSO_TAP_RECORD_LEN);
    }

    flipso_merge_value_records(capture, previous, &found);
    flipso_count_product_changes(capture, previous, &found);
    flipso_carry_gone_products(capture, previous, &found);

    if(diff) *diff = found;
}
