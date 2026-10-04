/**
 * @file test_gone.c
 * @brief Products the card has dropped since a file was written.
 */
#include "test_capture.h"

/*
 * A product that has left the card altogether.
 *
 * A directory entry is freed when a ticket expires and is removed, so the
 * card's account of what it carries is only ever the present tense: read it
 * again and the ticket is not there, and the file written while it was is the
 * only place it still exists.
 */
void merge_gone_product(void) {
    static ItsoCard reference;
    reference_decode(&reference);

    FlipsoCapture* previous = flipso_capture_alloc();
    fill(previous, &reference);

    /* The same card with entry 1 freed: nothing in the directory, and no group
     * for the read to have found. */
    static uint8_t dir_now[sizeof(card_dir)];
    memcpy(dir_now, card_dir, sizeof(card_dir));
    memset(dir_now + 2, 0, ITSO_DIR_ENTRY_LEN);

    FlipsoCapture* now = flipso_capture_alloc();
    flipso_capture_add(now, FlipsoBlockShell, 0, card_shell, sizeof(card_shell));
    flipso_capture_add(now, FlipsoBlockDirectory, 0, dir_now, sizeof(dir_now));
    for(uint8_t i = 1; i < reference.product_count && i < 5; i++) {
        flipso_capture_add(
            now,
            FlipsoBlockProduct,
            reference.products[i].dir_index,
            groups[i].data,
            groups[i].len);
    }
    flipso_capture_add(now, FlipsoBlockLog, 0, card_log, sizeof(card_log));
    flipso_capture_set_time(now, 1758500000u);

    static ItsoCard live;
    flipso_capture_decode(now, &live);
    check("the card itself no longer lists the product", live.product_count == 4);

    FlipsoCaptureDiff diff;
    flipso_capture_merge_history(now, previous, &diff);
    check("so it is carried out of the record", diff.kept_products == 1);

    static ItsoCard merged;
    check("the merged capture decodes", flipso_capture_decode(now, &merged));
    check("and shows it again", merged.product_count == 5);

    const ItsoProduct* gone = &merged.products[4];
    const ItsoProduct* was = &reference.products[0];
    check("after the ones the card still has", merged.products[0].on_card && !gone->on_card);
    check("in the entry it held then", gone->dir_index == 1);
    check("as the product it was", gone->typ == was->typ && gone->oid == was->oid);
    check(
        "decoded from its own group",
        gone->body_parsed &&
            itso_product_purse(gone)->balance.value == itso_product_purse(was)->balance.value);
    check("dated by the read that last saw it", gone->last_seen == flipso_capture_time(previous));

    check("keeping its transactions", gone->value_history_count == was->value_history_count);
    bool claimed_live = false;
    for(uint8_t i = 0; i < gone->value_history_count; i++) {
        if(gone->value_history[i].on_card) claimed_live = true;
    }
    check("none of them claimed to be on the card", !claimed_live);

    /* Through the file, under a key of its own that an older build would skip
     * the way it skips the record histories. */
    size_t count = 0;
    char** lines = to_lines(now, &count);
    bool keyed = false;
    for(size_t i = 0; i < count; i++) {
        if(strncmp(lines[i], "Product history 100:", 20) == 0) keyed = true;
    }
    check("written under a key of its own", keyed);

    FlipsoCapture* loaded = flipso_capture_alloc();
    for(size_t i = 0; i < count; i++) {
        flipso_capture_parse_line(loaded, lines[i]);
    }
    static ItsoCard reloaded;
    check("a file holding one loads", flipso_capture_decode(loaded, &reloaded));
    check("and decodes to the same card", itso_card_equal(&reloaded, &merged));
    free_lines(lines, count);

    /* Saving the same read again must not stack copies of it up. */
    FlipsoCaptureDiff again;
    flipso_capture_merge_history(now, previous, &again);
    static ItsoCard twice;
    flipso_capture_decode(now, &twice);
    check("merging the same record twice changes nothing", itso_card_equal(&twice, &merged));

    flipso_capture_free(loaded);
    flipso_capture_free(now);
    flipso_capture_free(previous);
}

/*
 * The cap on those, which is what the product list has room to show: a card
 * wiped clean has more past than the screen does.
 */
void gone_product_cap(void) {
    static ItsoCard reference;
    reference_decode(&reference);

    FlipsoCapture* previous = flipso_capture_alloc();
    fill(previous, &reference);

    /* Every entry freed at once - a shell reissued, which is the most a single
     * read can find missing. */
    static uint8_t dir_empty[sizeof(card_dir)];
    memcpy(dir_empty, card_dir, sizeof(card_dir));
    memset(dir_empty + 2, 0, (size_t)reference.product_count * ITSO_DIR_ENTRY_LEN);

    FlipsoCapture* now = flipso_capture_alloc();
    flipso_capture_add(now, FlipsoBlockShell, 0, card_shell, sizeof(card_shell));
    flipso_capture_add(now, FlipsoBlockDirectory, 0, dir_empty, sizeof(dir_empty));
    flipso_capture_add(now, FlipsoBlockLog, 0, card_log, sizeof(card_log));

    FlipsoCaptureDiff diff;
    flipso_capture_merge_history(now, previous, &diff);
    check("five products gone, four kept", diff.kept_products == ITSO_MAX_HISTORIC_PRODUCTS);

    static ItsoCard merged;
    flipso_capture_decode(now, &merged);
    check("and the card shows exactly those", merged.product_count == ITSO_MAX_HISTORIC_PRODUCTS);
    bool any_on_card = false;
    for(uint8_t i = 0; i < merged.product_count; i++) {
        if(merged.products[i].on_card) any_on_card = true;
    }
    check("none of them as a product it holds", !any_on_card);

    flipso_capture_free(now);
    flipso_capture_free(previous);
}

/*
 * A product whose chain fills a group exactly - an Ultralight EV1's IPE over two
 * 128-byte sectors and both value record copies - still has to be kept when the
 * card lets it go, header and all.
 */
void gone_full_chain(void) {
    static uint8_t chain[ITSO_MAX_GROUP_LEN];
    memcpy(chain, group1, sizeof(group1));

    FlipsoCapture* previous = flipso_capture_alloc();
    flipso_capture_add(previous, FlipsoBlockShell, 0, card_shell, sizeof(card_shell));
    flipso_capture_add(previous, FlipsoBlockDirectory, 0, card_dir, sizeof(card_dir));
    flipso_capture_add(previous, FlipsoBlockProduct, 1, chain, sizeof(chain));
    flipso_capture_set_time(previous, 1790000000u);

    /* The card now lists something else in E1: a different expiry. */
    static uint8_t dir_now[sizeof(card_dir)];
    memcpy(dir_now, card_dir, sizeof(card_dir));
    dir_now[2 + 4] ^= 0x01;
    FlipsoCapture* now = flipso_capture_alloc();
    flipso_capture_add(now, FlipsoBlockShell, 0, card_shell, sizeof(card_shell));
    flipso_capture_add(now, FlipsoBlockDirectory, 0, dir_now, sizeof(dir_now));
    flipso_capture_add(now, FlipsoBlockProduct, 1, group1, sizeof(group1));

    FlipsoCaptureDiff diff;
    flipso_capture_merge_history(now, previous, &diff);
    check("a dropped product with a full chain is kept", diff.kept_products == 1);

    /* And it survives the file, whose longest line is now that block's. */
    size_t count = 0;
    char** lines = to_lines(now, &count);
    size_t longest = 0;
    for(size_t i = 0; i < count; i++) {
        if(strlen(lines[i]) > longest) longest = strlen(lines[i]);
    }
    check("its line fits FLIPSO_CAPTURE_LINE_MAX", longest + 1 <= FLIPSO_CAPTURE_LINE_MAX);
    FlipsoCapture* loaded = flipso_capture_alloc();
    for(size_t i = 0; i < count; i++) {
        flipso_capture_parse_line(loaded, lines[i]);
    }
    static ItsoCard card;
    check(
        "and it loads back as a product off the card",
        flipso_capture_decode(loaded, &card) && card.product_count == 6 &&
            !card.products[5].on_card && card.products[5].dir_index == 1);

    free_lines(lines, count);
    flipso_capture_free(loaded);
    flipso_capture_free(now);
    flipso_capture_free(previous);
}

void gone_needs_a_directory(void) {
    static ItsoCard reference;
    reference_decode(&reference);

    FlipsoCapture* previous = flipso_capture_alloc();
    fill(previous, &reference);

    FlipsoCapture* now = flipso_capture_alloc();
    flipso_capture_add(now, FlipsoBlockShell, 0, card_shell, sizeof(card_shell));

    FlipsoCaptureDiff diff;
    flipso_capture_merge_history(now, previous, &diff);
    check("a read with no directory carries nothing forward", diff.kept_products == 0);

    flipso_capture_free(now);
    flipso_capture_free(previous);
}
