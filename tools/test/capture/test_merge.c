/**
 * @file test_merge.c
 * @brief A card read twice: what the second read keeps of the first.
 */
#include "test_capture.h"

/*
 * A card read twice, a few journeys apart.
 *
 * The card keeps four journey slots and two value records per product, and
 * writes each new one over the oldest. So a second read of a card that has been
 * used since the first cannot see everything the first read saw - and the file
 * written then is the only place those records still exist.
 */
void merge_history(void) {
    static ItsoCard reference;
    reference_decode(&reference);

    /* Taps decode newest first, so the last is the one the card will overwrite
     * next. Both timestamps are taken from the card rather than written down:
     * a DTS is a signed count of minutes from an epoch in 2028, so every record
     * here is a negative number and the arithmetic reads backwards. */
    uint32_t oldest_dts = reference.taps[reference.tap_count - 1].dts;
    uint32_t new_tap_dts = reference.taps[0].dts + 1440; /* A day later. */
    uint32_t new_value_dts = new_tap_dts + 1;

    FlipsoCapture* previous = flipso_capture_alloc();
    fill(previous, &reference);

    /* The card as it is now: one journey and one purse transaction later, each
     * written over the oldest record of its kind. */
    static uint8_t log_now[sizeof(card_log)];
    memcpy(log_now, card_log, sizeof(card_log));
    restamp_tap(log_now + 3 * ITSO_TAP_RECORD_LEN, card_log, new_tap_dts);

    static uint8_t group1_now[sizeof(group1)];
    memcpy(group1_now, group1, sizeof(group1));
    restamp_value(group1_now + GROUP1_VALUE_0, group1 + GROUP1_VALUE_1, 102, new_value_dts, 900);

    FlipsoCapture* now = flipso_capture_alloc();
    fill_now(
        now,
        &reference,
        card_dir,
        sizeof(card_dir),
        group1_now,
        sizeof(group1_now),
        log_now,
        sizeof(log_now));

    /* What the card alone can say, before the file is consulted. */
    static ItsoCard live;
    flipso_capture_decode(now, &live);
    check("the card itself holds four journeys", live.tap_count == 4);
    check("and two value records on the purse", live.products[0].value_history_count == 2);
    check("the oldest journey has rolled off the card", !holds_tap(&live, oldest_dts));
    check("and the new one is there instead", holds_tap(&live, new_tap_dts));

    FlipsoCaptureDiff diff;
    flipso_capture_merge_history(now, previous, &diff);
    printf(
        "      +%u journeys, +%u transactions, kept %u and %u\n",
        diff.new_taps,
        diff.new_values,
        diff.kept_taps,
        diff.kept_values);
    check("one journey is new since the file was written", diff.new_taps == 1);
    check("one transaction is new", diff.new_values == 1);
    check("no product is new or changed", diff.new_products == 0 && diff.changed_products == 0);
    check("the journey that rolled off is kept", diff.kept_taps == 1);
    check("so is the value record that rolled off", diff.kept_values == 1);

    static ItsoCard merged;
    check("the merged capture decodes", flipso_capture_decode(now, &merged));
    check("the card now holds five journeys", merged.tap_count == 5);
    check("including the one only the file remembered", holds_tap(&merged, oldest_dts));
    check("and the new one", holds_tap(&merged, new_tap_dts));
    check("no journey is listed twice", ({
              bool duplicate = false;
              for(uint8_t i = 0; i < merged.tap_count; i++) {
                  for(uint8_t j = (uint8_t)(i + 1); j < merged.tap_count; j++) {
                      if(merged.taps[i].dts == merged.taps[j].dts &&
                         merged.taps[i].transaction_type == merged.taps[j].transaction_type) {
                          duplicate = true;
                      }
                  }
              }
              !duplicate;
          }));
    check("exactly one journey is the card's own latest", ({
              uint8_t latest = 0;
              for(uint8_t i = 0; i < merged.tap_count; i++) {
                  if(merged.taps[i].latest) latest++;
              }
              latest == 1;
          }));
    check("only the journey the file remembered is marked as from past reads", ({
              uint8_t past = 0;
              bool oldest_is_past = false;
              for(uint8_t i = 0; i < merged.tap_count; i++) {
                  if(!merged.taps[i].on_card) past++;
                  if(merged.taps[i].dts == oldest_dts) oldest_is_past = !merged.taps[i].on_card;
              }
              past == 1 && oldest_is_past;
          }));

    /* The balance series: three transactions where the card holds two, which is
     * the whole point of keeping the file's records. */
    const ItsoProduct* purse = &merged.products[0];
    check("the purse has three value records", purse->value_history_count == 3);
    check(
        "newest first, by TS#",
        purse->value_history[0].ts == 102 && purse->value_history[1].ts == 101 &&
            purse->value_history[2].ts == 100);
    check(
        "the balance series runs 9.00, 12.34, 15.60",
        purse->value_history[0].amount.value == 900 &&
            purse->value_history[1].amount.value == 1234 &&
            purse->value_history[2].amount.value == 1560);
    /* Which of them the card still holds is not a detail: the two records in
     * its group are a different claim from the one the file is the only copy
     * of, and the product screen shows them apart. */
    check(
        "the records still on the card say so",
        purse->value_history[0].on_card && purse->value_history[1].on_card);
    check("and the one only the file has does not", !purse->value_history[2].on_card);
    check(
        "and the live balance is the newest of them",
        itso_product_purse(purse)->balance.value == 900 && purse->value_ts == 102);

    /* Merging the same file again must change nothing: the records it holds are
     * already here, and a second copy would read as a second journey. */
    FlipsoCaptureDiff again;
    flipso_capture_merge_history(now, previous, &again);
    static ItsoCard twice;
    flipso_capture_decode(now, &twice);
    check("merging the same record twice changes nothing", itso_card_equal(&twice, &merged));
    /* The save screen merges before it asks, so backing out and asking again
     * does exactly this - and has to report what it did the first time. */
    check("and reports the same counts", memcmp(&again, &diff, sizeof(diff)) == 0);

    /* The file: new keys, same Version, so a build that predates them loses the
     * history and reads the rest. */
    size_t count = 0;
    char** lines = to_lines(now, &count);
    bool version_unchanged = false, has_log_history = false, has_value_history = false;
    for(size_t i = 0; i < count; i++) {
        if(strcmp(lines[i], "Version: 1") == 0) version_unchanged = true;
        if(strncmp(lines[i], "Log history:", 12) == 0) has_log_history = true;
        if(strncmp(lines[i], "Value history 1:", 16) == 0) has_value_history = true;
    }
    check("the history is written under its own keys", has_log_history && has_value_history);
    check("and the file version is unchanged", version_unchanged);

    FlipsoCapture* loaded = flipso_capture_alloc();
    for(size_t i = 0; i < count; i++) {
        flipso_capture_parse_line(loaded, lines[i]);
    }
    static ItsoCard reloaded;
    check("a merged file loads", flipso_capture_decode(loaded, &reloaded));
    check("and decodes to the same card", itso_card_equal(&reloaded, &merged));

    free_lines(lines, count);
    flipso_capture_free(loaded);
    flipso_capture_free(now);
    flipso_capture_free(previous);
}

/*
 * A directory entry that has changed hands.
 *
 * Entry numbers are reused: a ticket that expires and is replaced leaves its
 * slot to another product, and the transactions of the old one are not the new
 * one's. The entry bytes - owner, type, subtype, expiry - are what say so.
 */
void merge_replaced_product(void) {
    static ItsoCard reference;
    reference_decode(&reference);

    /* E1 as a product that has only just been created: two records of its own,
     * numbered from one, where the file remembers TS# 100 and 101. */
    static uint8_t group1_now[sizeof(group1)];
    memcpy(group1_now, group1, sizeof(group1));
    restamp_value(group1_now + GROUP1_VALUE_0, group1 + GROUP1_VALUE_1, 1, 0xEE0000u, 500);
    restamp_value(group1_now + GROUP1_VALUE_1, group1 + GROUP1_VALUE_1, 2, 0xEE0001u, 400);

    /* The same card, except that E1's entry now carries a different expiry. */
    static uint8_t dir_now[sizeof(card_dir)];
    memcpy(dir_now, card_dir, sizeof(card_dir));
    dir_now[6] = (uint8_t)(dir_now[6] ^ 0x0F);

    FlipsoCapture* previous = flipso_capture_alloc();
    fill(previous, &reference);
    FlipsoCapture* now = flipso_capture_alloc();
    fill_now(
        now,
        &reference,
        dir_now,
        sizeof(dir_now),
        group1_now,
        sizeof(group1_now),
        card_log,
        sizeof(card_log));

    FlipsoCaptureDiff diff;
    flipso_capture_merge_history(now, previous, &diff);
    static ItsoCard merged;
    flipso_capture_decode(now, &merged);
    check("a replaced product keeps no history", diff.kept_values == 0);
    check("the product in its slot counts as new", diff.new_products == 1);
    /* Its records are not the new product's, but the product itself is still
     * one the card used to carry, so it is kept beside the one in its slot. */
    check("but the product itself is kept", diff.kept_products == 1);
    {
        /* Asking twice, as the save screen does after a Cancel, still reports
         * the product rather than finding its slot already taken. */
        FlipsoCaptureDiff again;
        flipso_capture_merge_history(now, previous, &again);
        check("a second merge still reports the kept product", again.kept_products == 1);
        static ItsoCard twice;
        flipso_capture_decode(now, &twice);
        check("and still decodes to six products", twice.product_count == 6);
    }
    check(
        "as a sixth product the card does not list",
        merged.product_count == 6 && !merged.products[5].on_card &&
            merged.products[5].dir_index == 1);
    check(
        "so its records are only its own",
        merged.products[0].value_history_count == 2 &&
            merged.products[0].value_history[0].ts == 2 &&
            merged.products[0].value_history[1].ts == 1);
    /* The journey log belongs to the card rather than to a product, so it is
     * unaffected by what happened to an entry. */
    check("the journey log is unaffected", merged.tap_count == 4);

    /* The control: the same two reads with the entry bytes left alone. Now the
     * records the file holds do belong to the product in that slot, and all
     * four are kept - which is what says the guard above is the entry and not
     * something else about these blocks. */
    FlipsoCapture* same = flipso_capture_alloc();
    fill_now(
        same,
        &reference,
        card_dir,
        sizeof(card_dir),
        group1_now,
        sizeof(group1_now),
        card_log,
        sizeof(card_log));
    FlipsoCaptureDiff unchanged;
    flipso_capture_merge_history(same, previous, &unchanged);
    static ItsoCard kept;
    flipso_capture_decode(same, &kept);
    check("an unchanged entry keeps its history", unchanged.kept_values == 2);
    check("and shows all four records", kept.products[0].value_history_count == 4);
    check("and nothing has left the card", unchanged.kept_products == 0);

    flipso_capture_free(same);
    flipso_capture_free(now);
    flipso_capture_free(previous);
}

/*
 * The cap. A history cannot grow without bound, so the oldest records fall off
 * the end of the file the way they fell off the card - and what the decoder has
 * room to show is what bounds what the file bothers to keep.
 */
void merge_cap(void) {
    static ItsoCard reference;
    reference_decode(&reference);
    uint32_t oldest_dts = reference.taps[reference.tap_count - 1].dts;

    /* A file saved from an earlier read: its own live log, plus a full history
     * of records older than any of them. */
    FlipsoCapture* previous = flipso_capture_alloc();
    fill(previous, &reference);

    static uint8_t history[FLIPSO_CAPTURE_MAX_LOG_HISTORY * ITSO_TAP_RECORD_LEN];
    for(uint8_t i = 0; i < FLIPSO_CAPTURE_MAX_LOG_HISTORY; i++) {
        restamp_tap(
            history + (size_t)i * ITSO_TAP_RECORD_LEN, card_log, oldest_dts - 1440u * (i + 1));
    }
    check(
        "a full history is a block a capture will hold",
        flipso_capture_add(previous, FlipsoBlockLogHistory, 0, history, sizeof(history)));

    /* One more journey since, so the oldest of the card's own four rolls off
     * and there are nine records competing for eight places. */
    static uint8_t log_now[sizeof(card_log)];
    memcpy(log_now, card_log, sizeof(card_log));
    restamp_tap(log_now + 3 * ITSO_TAP_RECORD_LEN, card_log, reference.taps[0].dts + 1440);

    FlipsoCapture* now = flipso_capture_alloc();
    fill_now(
        now,
        &reference,
        card_dir,
        sizeof(card_dir),
        group1,
        sizeof(group1),
        log_now,
        sizeof(log_now));

    FlipsoCaptureDiff diff;
    flipso_capture_merge_history(now, previous, &diff);
    check("the kept history stops at the cap", diff.kept_taps == FLIPSO_CAPTURE_MAX_LOG_HISTORY);

    static ItsoCard merged;
    flipso_capture_decode(now, &merged);
    printf(
        "      %u journeys decoded, of %u the card holds plus %u kept\n",
        merged.tap_count,
        4,
        diff.kept_taps);
    check("the decoded card fills its own array exactly", merged.tap_count == ITSO_MAX_TAPS);

    /* What was dropped has to be the oldest of them, which is only true if the
     * whole candidate set was ordered before the cap was applied. */
    check("the journey that just rolled off the card is kept", holds_tap(&merged, oldest_dts));
    check(
        "the oldest invented record is the one dropped",
        !holds_tap(&merged, oldest_dts - 1440u * FLIPSO_CAPTURE_MAX_LOG_HISTORY));

    bool descending = true;
    for(uint8_t i = 1; i < merged.tap_count; i++) {
        if(itso_dts_to_unix(merged.taps[i].dts) > itso_dts_to_unix(merged.taps[i - 1].dts)) {
            descending = false;
        }
    }
    check("the whole history is ordered newest first", descending);

    flipso_capture_free(now);
    flipso_capture_free(previous);
}

/*
 * A read that lost the directory is not a card that has shed its products. The
 * distinction is worth a test because the two look alike from here: in both
 * cases this read has no entry to compare the old one against.
 */
/*
 * A product sold since the record was written, in an entry the record had
 * empty. It brings no journey and, here, no transaction of its own that the
 * record lacks - so without counting products the save screen would call the
 * card unchanged.
 */
void merge_new_product(void) {
    static ItsoCard reference;
    reference_decode(&reference);

    static uint8_t dir_then[sizeof(card_dir)];
    memcpy(dir_then, card_dir, sizeof(card_dir));
    memset(dir_then + 2 + ITSO_DIR_ENTRY_LEN, 0, ITSO_DIR_ENTRY_LEN); /* Entry 2 empty. */

    FlipsoCapture* previous = flipso_capture_alloc();
    flipso_capture_add(previous, FlipsoBlockShell, 0, card_shell, sizeof(card_shell));
    flipso_capture_add(previous, FlipsoBlockDirectory, 0, dir_then, sizeof(dir_then));
    for(uint8_t i = 0; i < reference.product_count && i < 5; i++) {
        if(reference.products[i].dir_index == 2) continue;
        flipso_capture_add(
            previous,
            FlipsoBlockProduct,
            reference.products[i].dir_index,
            groups[i].data,
            groups[i].len);
    }
    flipso_capture_add(previous, FlipsoBlockLog, 0, card_log, sizeof(card_log));

    FlipsoCapture* now = flipso_capture_alloc();
    fill(now, &reference);

    FlipsoCaptureDiff diff;
    flipso_capture_merge_history(now, previous, &diff);
    check("a product sold since is counted as new", diff.new_products == 1);
    check("and nothing else is said to have changed", diff.changed_products == 0);
    check("nor is any journey new", diff.new_taps == 0);

    /* The same card again, identical: nothing to report at all. */
    FlipsoCapture* again = flipso_capture_alloc();
    fill(again, &reference);
    FlipsoCapture* same = flipso_capture_alloc();
    fill(same, &reference);
    flipso_capture_merge_history(same, again, &diff);
    check(
        "an unchanged card reports nothing new",
        !diff.new_taps && !diff.new_values && !diff.new_products && !diff.changed_products);

    flipso_capture_free(same);
    flipso_capture_free(again);
    flipso_capture_free(now);
    flipso_capture_free(previous);
}

/*
 * A paper ticket read again after a ride. It has no journey log and no value
 * records, so the only sign of the ride is that its pages differ.
 */
void merge_type2(void) {
    FlipsoCapture* previous = flipso_capture_alloc();
    flipso_capture_add(previous, FlipsoBlockType2, 0, cmd4_pages, sizeof(cmd4_pages));

    static uint8_t pages_now[sizeof(cmd4_pages)];
    memcpy(pages_now, cmd4_pages, sizeof(cmd4_pages));
    pages_now[16] ^= 0x01; /* Rewritable dynamic data, where a ride is counted. */
    FlipsoCapture* now = flipso_capture_alloc();
    flipso_capture_add(now, FlipsoBlockType2, 0, pages_now, sizeof(pages_now));

    FlipsoCaptureDiff diff;
    flipso_capture_merge_history(now, previous, &diff);
    check("a paper ticket whose pages changed is reported changed", diff.changed_products == 1);

    FlipsoCapture* same = flipso_capture_alloc();
    flipso_capture_add(same, FlipsoBlockType2, 0, cmd4_pages, sizeof(cmd4_pages));
    flipso_capture_merge_history(same, previous, &diff);
    check("and one read again unchanged is not", diff.changed_products == 0);

    flipso_capture_free(same);
    flipso_capture_free(now);
    flipso_capture_free(previous);
}

/*
 * A CMD9 read before and after one ride. Its value records alternate between
 * two copies of the group, so a record the last read saw in the current copy
 * is in the previous copy now - still on the card, and neither new nor rolled
 * off. Only TS#3 is new, and nothing needs keeping.
 */
void merge_full_type2(void) {
    FlipsoCapture* previous = flipso_capture_alloc();
    capture_full(previous, cmd9_before, sizeof(cmd9_before));
    FlipsoCapture* now = flipso_capture_alloc();
    capture_full(now, cmd9_pages, sizeof(cmd9_pages));

    FlipsoCaptureDiff diff;
    flipso_capture_merge_history(now, previous, &diff);
    check("one new transaction, across both copies", diff.new_values == 1);
    check("and none carried forward that the card still holds", diff.kept_values == 0);
    check("one new journey", diff.new_taps == 1 && diff.kept_taps == 0);
    check("the ticket is the same product", diff.new_products == 0 && diff.kept_products == 0);

    flipso_capture_free(now);
    flipso_capture_free(previous);
}
