/**
 * @file test_history_memory.c
 * @brief Value histories on the heap: allocated to fit, owned by one product,
 * and given back with the card.
 *
 * A product's value records are allocated as they are decoded rather than kept
 * in a fixed array, so every place a card is decoded again, reset or freed is a
 * place one could leak or be read after it has gone. ASan catches the second;
 * the first it only reports at exit on Linux, so these count the heap directly.
 */
#include "test_capture.h"

/* ASan's count of the bytes allocated and not yet freed, and the size of one
 * block it handed out. Declared here rather than taken from
 * <sanitizer/allocator_interface.h>, which not every compiler installs: every
 * suite is built with -fsanitize=address, whose runtime defines both. */
size_t __sanitizer_get_current_allocated_bytes(void);
size_t __sanitizer_get_allocated_size(const volatile void* p);

/** Purse records TS# @p first_ts and down, a day apart, before the card's own. */
static void older_records(uint8_t* out, uint8_t count, uint16_t first_ts, ItsoDts before_dts) {
    for(uint8_t i = 0; i < count; i++) {
        restamp_value(
            out + (size_t)i * ITSO_VALUE_RECORD_LEN,
            group1 + GROUP1_VALUE_1,
            (uint16_t)(first_ts - i),
            before_dts - 1440u * (i + 1),
            (int16_t)(2000 + i));
    }
}

/*
 * A card read now, one purse transaction after a file that remembers as many
 * of the purse's earlier records as a file keeps, merged with that file.
 */
static FlipsoCapture* merged_capture(void) {
    static ItsoCard reference;
    reference_decode(&reference);
    ItsoDts oldest_dts = reference.products[0].value_history[1].dts;

    FlipsoCapture* previous = flipso_capture_alloc();
    fill(previous, &reference);
    static uint8_t history[FLIPSO_CAPTURE_MAX_VALUE_HISTORY * ITSO_VALUE_RECORD_LEN];
    older_records(history, FLIPSO_CAPTURE_MAX_VALUE_HISTORY, 99, oldest_dts);
    flipso_capture_add(previous, FlipsoBlockValueHistory, 1, history, sizeof(history));

    /* TS# 102 written over 100, as merge_history() has it. */
    static uint8_t group1_now[sizeof(group1)];
    memcpy(group1_now, group1, sizeof(group1));
    restamp_value(
        group1_now + GROUP1_VALUE_0,
        group1 + GROUP1_VALUE_1,
        102,
        reference.products[0].value_history[0].dts + 1,
        900);

    FlipsoCapture* now = flipso_capture_alloc();
    fill_now(
        now,
        &reference,
        card_dir,
        sizeof(card_dir),
        group1_now,
        sizeof(group1_now),
        card_log,
        sizeof(card_log));
    FlipsoCaptureDiff diff;
    flipso_capture_merge_history(now, previous, &diff);
    flipso_capture_free(previous);
    return now;
}

/*
 * More history than the card holds: two records on the card, a file's worth
 * behind them, and a product with room for no more than ITSO_MAX_VALUE_RECORDS
 * whichever way they arrive.
 */
void history_past_the_card(void) {
    FlipsoCapture* now = merged_capture();
    ItsoCard card;
    itso_card_init(&card);
    check("a card merged with a long history decodes", flipso_capture_decode(now, &card));

    const ItsoProduct* purse = &card.products[0];
    check("the purse keeps as many records as it has room for", ({
              bool newest_first = purse->value_history_count == ITSO_MAX_VALUE_RECORDS;
              for(uint8_t i = 0; newest_first && i < purse->value_history_count; i++) {
                  newest_first = purse->value_history[i].ts == 102 - i;
              }
              newest_first;
          }));
    check(
        "the two the card holds first, and only they on the card",
        purse->value_history[0].on_card && purse->value_history[1].on_card &&
            !purse->value_history[2].on_card);
    check(
        "allocated to fit them",
        __sanitizer_get_allocated_size(purse->value_history) ==
            (size_t)purse->value_history_count * sizeof(ItsoValueRecord));
    check(
        "a purse's records hold a balance rather than a count",
        !purse->value_history[0].has_count && purse->value_history[0].amount.value == 900 &&
            !purse->value_history[7].has_count && purse->value_history[7].amount.value == 2004);
    check(
        "a journey ticket's hold a count",
        card.products[3].value_history_count == 2 && card.products[3].value_history[1].has_count &&
            card.products[3].value_history[1].count == 1);
    check(
        "an ID, which has no value records, allocates none",
        card.products[1].value_history_count == 0 && card.products[1].value_history == NULL);

    /* A file with more than a file keeps - a hostile one, or a later build's -
     * still leaves the product at its cap, holding the newest. */
    static uint8_t long_history[(ITSO_MAX_VALUE_RECORDS + 4) * ITSO_VALUE_RECORD_LEN];
    older_records(
        long_history,
        ITSO_MAX_VALUE_RECORDS + 4,
        99,
        card.products[0].value_history[ITSO_MAX_VALUE_RECORDS - 1].dts);
    FlipsoCapture* hostile = flipso_capture_alloc();
    flipso_capture_add(hostile, FlipsoBlockShell, 0, card_shell, sizeof(card_shell));
    flipso_capture_add(hostile, FlipsoBlockDirectory, 0, card_dir, sizeof(card_dir));
    flipso_capture_add(hostile, FlipsoBlockProduct, 1, group1, sizeof(group1));
    flipso_capture_add(hostile, FlipsoBlockValueHistory, 1, long_history, sizeof(long_history));
    check("a history past the cap decodes", flipso_capture_decode(hostile, &card));
    check(
        "to the cap, newest first",
        card.products[0].value_history_count == ITSO_MAX_VALUE_RECORDS &&
            card.products[0].value_history[0].ts == 101 &&
            card.products[0].value_history[ITSO_MAX_VALUE_RECORDS - 1].ts ==
                101 - (ITSO_MAX_VALUE_RECORDS - 1));

    itso_card_free(&card);
    flipso_capture_free(hostile);
    flipso_capture_free(now);
}

/*
 * Each card owns its products' histories. Two cards decoded from the same
 * blocks are equal and share nothing, and a product carried from one read into
 * the next - a ticket the card has since dropped - outlives the card it was
 * first decoded into.
 */
void history_owned_by_each_card(void) {
    FlipsoCapture* now = merged_capture();
    ItsoCard a, b;
    itso_card_init(&a);
    itso_card_init(&b);
    flipso_capture_decode(now, &a);
    flipso_capture_decode(now, &b);
    check(
        "two cards decoded from one capture are equal, each with its own history",
        itso_card_equal(&a, &b) && a.products[0].value_history != b.products[0].value_history);
    b.products[0].value_history[5].amount.value++;
    check("and a history that differs makes the cards differ", !itso_card_equal(&a, &b));
    b.products[0].value_history[5].amount.value--;
    check("until it is put back", itso_card_equal(&a, &b));

    /* Entry 1 freed on the card, so the purse comes back only from the file. */
    static uint8_t dir_now[sizeof(card_dir)];
    memcpy(dir_now, card_dir, sizeof(card_dir));
    memset(dir_now + 2, 0, ITSO_DIR_ENTRY_LEN);
    FlipsoCapture* later = flipso_capture_alloc();
    flipso_capture_add(later, FlipsoBlockShell, 0, card_shell, sizeof(card_shell));
    flipso_capture_add(later, FlipsoBlockDirectory, 0, dir_now, sizeof(dir_now));
    for(uint8_t i = 1; i < 5; i++) {
        flipso_capture_add(
            later, FlipsoBlockProduct, a.products[i].dir_index, groups[i].data, groups[i].len);
    }
    flipso_capture_add(later, FlipsoBlockLog, 0, card_log, sizeof(card_log));
    FlipsoCaptureDiff diff;
    flipso_capture_merge_history(later, now, &diff);

    ItsoCard c;
    itso_card_init(&c);
    flipso_capture_decode(later, &c);
    const ItsoProduct* gone = &c.products[c.product_count - 1];
    check(
        "the purse the card dropped is carried into the next read",
        diff.kept_products == 1 && !gone->on_card && gone->dir_index == 1);

    /* Read through after the cards it came from are freed: shared records
     * would be ASan's use-after-free here. */
    static ItsoValueRecord was[ITSO_MAX_VALUE_RECORDS];
    uint8_t was_count = a.products[0].value_history_count;
    memcpy(was, a.products[0].value_history, was_count * sizeof(ItsoValueRecord));
    itso_card_free(&a);
    itso_card_free(&b);
    check("with its own copy of every record, which outlives theirs", ({
              bool same = gone->value_history_count == was_count;
              for(uint8_t i = 0; same && i < was_count; i++) {
                  same = gone->value_history[i].ts == was[i].ts &&
                         gone->value_history[i].dts == was[i].dts &&
                         gone->value_history[i].amount.value == was[i].amount.value &&
                         !gone->value_history[i].on_card;
              }
              same;
          }));

    itso_card_free(&c);
    flipso_capture_free(later);
    flipso_capture_free(now);
}

/*
 * A card decoded again over itself - a second scan, the next saved card - and
 * a directory decoded over products it has already filled give back what the
 * last decode allocated, slots past the new count included.
 */
void history_reset_and_reread(void) {
    FlipsoCapture* now = merged_capture();
    ItsoCard card;
    itso_card_init(&card);
    size_t before = __sanitizer_get_current_allocated_bytes();

    flipso_capture_decode(now, &card);
    size_t once = __sanitizer_get_current_allocated_bytes();
    flipso_capture_decode(now, &card);
    check(
        "a card decoded again holds what it held the first time",
        __sanitizer_get_current_allocated_bytes() == once);

    /* No reset between: the slots are reused, and the one past a directory
     * that now lists a product fewer is left as it was until the card goes. */
    static uint8_t dir_now[sizeof(card_dir)];
    memcpy(dir_now, card_dir, sizeof(card_dir));
    memset(dir_now + 2, 0, ITSO_DIR_ENTRY_LEN);
    itso_parse_directory(&card, dir_now, sizeof(dir_now));
    for(uint8_t i = 0; i < card.product_count; i++) {
        itso_parse_ipe(&card.products[i], groups[i + 1].data, groups[i + 1].len, card.sector_size);
    }
    check(
        "a shorter directory over a card fills fewer products",
        card.product_count == 4 && card.product_capacity == 5);
    itso_card_reset(&card);
    check(
        "and a reset gives back every history, past the count too",
        __sanitizer_get_current_allocated_bytes() == before);

    itso_card_free(&card);
    flipso_capture_free(now);
}

/* The save screen's whole round: read, merge, write, load, free. */
static void load_merge_free(void) {
    FlipsoCapture* now = merged_capture();
    size_t count = 0;
    char** lines = to_lines(now, &count);
    FlipsoCapture* loaded = flipso_capture_alloc();
    for(size_t i = 0; i < count; i++) {
        flipso_capture_parse_line(loaded, lines[i]);
    }
    ItsoCard card;
    itso_card_init(&card);
    flipso_capture_decode(loaded, &card);
    check(
        "a merged card loads with its whole history",
        card.products[0].value_history_count == ITSO_MAX_VALUE_RECORDS);
    itso_card_free(&card);
    free_lines(lines, count);
    flipso_capture_free(loaded);
    flipso_capture_free(now);
}

/* Twice, because a leak that only the second round shows is a slot or a
 * history left behind by the first. */
void history_load_merge_free_twice(void) {
    /* merged_capture() keeps its reference card from one call to the next. */
    flipso_capture_free(merged_capture());
    size_t before = __sanitizer_get_current_allocated_bytes();
    load_merge_free();
    check(
        "loading, merging and freeing a card leaves the heap as it was",
        __sanitizer_get_current_allocated_bytes() == before);
    load_merge_free();
    check("and so does a second time", __sanitizer_get_current_allocated_bytes() == before);
}
