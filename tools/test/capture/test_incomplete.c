/**
 * @file test_incomplete.c
 * @brief Reads that stopped short, and the limits of what a capture holds.
 */
#include "test_capture.h"

/* A directory entry no product claims: written out, read back, and simply not
 * used by the decode. It must not take the rest of the file with it. */
void spare_entry(void) {
    FlipsoCapture* capture = flipso_capture_alloc();
    flipso_capture_parse_line(capture, "Filetype: Flipso card");
    flipso_capture_parse_line(capture, "Version: 1");
    flipso_capture_parse_line(capture, "Product 200: 01 02 03");

    char line[FLIPSO_CAPTURE_LINE_MAX];
    check("an entry past the directory is kept", flipso_capture_lines(capture) == 4);
    check(
        "and renders back to the same key",
        flipso_capture_line(capture, 3, line, sizeof(line)) &&
            strcmp(line, "Product 200: 01 02 03") == 0);
    flipso_capture_free(capture);
}

void partial(void) {
    static ItsoCard reference;
    reference_decode(&reference);

    /* A card whose log never read: the taps go, nothing else does. */
    FlipsoCapture* capture = flipso_capture_alloc();
    flipso_capture_add(capture, FlipsoBlockShell, 0, card_shell, sizeof(card_shell));
    flipso_capture_add(capture, FlipsoBlockDirectory, 0, card_dir, sizeof(card_dir));
    static ItsoCard card;
    check("a capture with no products still decodes", flipso_capture_decode(capture, &card));
    check("the card number survives", strcmp(card.isrn, reference.isrn) == 0);
    check("the directory still lists the products", card.product_count == 5);
    check("but none of them has a body", !card.products[0].body_parsed);
    check("and there are no taps", card.tap_count == 0);
    flipso_capture_free(capture);

    /* A capture with no shell has nothing to place the other blocks against. */
    capture = flipso_capture_alloc();
    flipso_capture_add(capture, FlipsoBlockDirectory, 0, card_dir, sizeof(card_dir));
    check("a capture with no shell is not valid", !flipso_capture_valid(capture));
    check("and does not decode", !flipso_capture_decode(capture, &card));
    char isrn[ITSO_ISRN_DIGITS + 1];
    check("and has no card number", !flipso_capture_card_number(capture, isrn));
    flipso_capture_free(capture);

    /* A shell block of junk: valid() only says a block is there, so the number
     * is where a capture that cannot be matched gets found out. */
    capture = flipso_capture_alloc();
    static const uint8_t junk[32] = {0xAA};
    flipso_capture_add(capture, FlipsoBlockShell, 0, junk, sizeof(junk));
    check("a shell of junk has no card number", !flipso_capture_card_number(capture, isrn));
    flipso_capture_free(capture);
}

void limits(void) {
    FlipsoCapture* capture = flipso_capture_alloc();
    static uint8_t big[ITSO_MAX_GROUP_LEN + 1];
    memset(big, 0xA5, sizeof(big));

    check("an empty block is refused", !flipso_capture_add(capture, FlipsoBlockShell, 0, big, 0));
    check(
        "a block longer than a sector chain is refused",
        !flipso_capture_add(capture, FlipsoBlockShell, 0, big, sizeof(big)));
    check(
        "the longest legal block fits",
        flipso_capture_add(capture, FlipsoBlockShell, 0, big, ITSO_MAX_GROUP_LEN));
    check(
        "a repeated block is refused", !flipso_capture_add(capture, FlipsoBlockShell, 0, big, 8));

    /* Fill every remaining slot, then one more. */
    int added = 0;
    for(unsigned i = 1; i <= FLIPSO_CAPTURE_MAX_BLOCKS + 4; i++) {
        if(flipso_capture_add(capture, FlipsoBlockProduct, (uint8_t)i, big, ITSO_MAX_GROUP_LEN)) {
            added++;
        }
    }
    check("the block index cannot be overrun", added == FLIPSO_CAPTURE_MAX_BLOCKS - 1);

    /* Every one of those is the longest a block can be, so this is also the
     * arena's ceiling being reached without overrunning it. */
    static ItsoCard card;
    check("a capture of junk does not decode", !flipso_capture_decode(capture, &card));

    flipso_capture_reset(capture);
    check("reset empties it", !flipso_capture_valid(capture));
    check(
        "and it can be filled again",
        flipso_capture_add(capture, FlipsoBlockShell, 0, card_shell, sizeof(card_shell)));
    flipso_capture_free(capture);
}
