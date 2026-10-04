/**
 * @file test_round_trip.c
 * @brief Saving and loading changes nothing: every kind of card through the file and back.
 */
#include "test_capture.h"

void round_trip(void) {
    static ItsoCard reference;
    reference_decode(&reference);
    check("reference card has five products", reference.product_count == 5);
    check("reference card has taps", reference.tap_count > 0);

    FlipsoCapture* capture = flipso_capture_alloc();
    check("an empty capture is not worth saving", !flipso_capture_valid(capture));
    fill(capture, &reference);
    check("a filled capture is valid", flipso_capture_valid(capture));

    /* The identity a saved card is matched on, read without decoding the rest. */
    char isrn[ITSO_ISRN_DIGITS + 1];
    check("the capture knows its card number", flipso_capture_card_number(capture, isrn));
    check("and it is the one the decoder reads", strcmp(isrn, reference.isrn) == 0);

    static ItsoCard direct;
    check("capture decodes", flipso_capture_decode(capture, &direct));
    check("capture decodes to the same card", itso_card_equal(&direct, &reference));

    /* Write it out, read it back into a fresh capture, decode that. This is the
     * whole save/load path bar the file handle. */
    size_t count = 0;
    char** lines = to_lines(capture, &count);
    check("header plus one line per block", count == 3 + 8);

    FlipsoCapture* loaded = flipso_capture_alloc();
    bool ok = true;
    for(size_t i = 0; i < count; i++) {
        if(!flipso_capture_parse_line(loaded, lines[i])) ok = false;
    }
    check("every line of our own file is accepted", ok);
    check("the loaded capture is valid", flipso_capture_valid(loaded));
    check("the read time survives", flipso_capture_time(loaded) == 1758400000u);

    static ItsoCard reloaded;
    check("loaded capture decodes", flipso_capture_decode(loaded, &reloaded));
    check("saving and loading changes nothing", itso_card_equal(&reloaded, &reference));

    /* Lines may arrive in any order: nothing in the format is positional. */
    FlipsoCapture* shuffled = flipso_capture_alloc();
    for(size_t i = count; i > 0; i--) {
        flipso_capture_parse_line(shuffled, lines[i - 1]);
    }
    static ItsoCard backwards;
    flipso_capture_decode(shuffled, &backwards);
    check("a file read backwards decodes the same", itso_card_equal(&backwards, &reference));

    free_lines(lines, count);
    flipso_capture_free(shuffled);
    flipso_capture_free(loaded);
    flipso_capture_free(capture);
}

/* The chip's description: saved and loaded like any block, byte for byte, and
 * left alone by a merge, which only replaces the history blocks. */
void chip_block(void) {
    uint8_t chip[31];
    for(uint8_t i = 0; i < sizeof(chip); i++)
        chip[i] = (uint8_t)(0xA0 + i);

    FlipsoCapture* capture = flipso_capture_alloc();
    static ItsoCard reference;
    reference_decode(&reference);
    fill(capture, &reference);
    size_t len = 0;
    check("a read with no chip block has none", flipso_capture_chip(capture, &len) == NULL);
    check(
        "a chip block is kept",
        flipso_capture_add(capture, FlipsoBlockChip, 0, chip, sizeof(chip)));

    size_t count = 0;
    char** lines = to_lines(capture, &count);
    bool keyed = false;
    for(size_t i = 0; i < count; i++) {
        if(strncmp(lines[i], "Chip: A0 A1", 11) == 0) keyed = true;
    }
    check("it is written under its own key", keyed);

    FlipsoCapture* loaded = flipso_capture_alloc();
    for(size_t i = 0; i < count; i++)
        flipso_capture_parse_line(loaded, lines[i]);
    const uint8_t* back = flipso_capture_chip(loaded, &len);
    check(
        "and reads back byte for byte",
        back && len == sizeof(chip) && memcmp(back, chip, len) == 0);

    static ItsoCard decoded;
    flipso_capture_decode(loaded, &decoded);
    check("without changing the card it decodes to", itso_card_equal(&decoded, &reference));

    flipso_capture_merge_history(capture, loaded, NULL);
    back = flipso_capture_chip(capture, &len);
    check("a merge keeps it", back && len == sizeof(chip) && memcmp(back, chip, len) == 0);

    free_lines(lines, count);
    flipso_capture_free(loaded);
    flipso_capture_free(capture);
}

/* A Type 2 tag: its whole page memory is one block, and the card decodes from it
 * on its own - no shell/directory/product blocks. It survives the save/load round
 * trip and takes its identity from the chip UID, not the shared compact ISRN. */
void type2_card(void) {
    FlipsoCapture* capture = flipso_capture_alloc();
    check("an empty capture is not valid", !flipso_capture_valid(capture));
    check(
        "a Type 2 block is kept",
        flipso_capture_add(capture, FlipsoBlockType2, 0, cmd4_pages, sizeof(cmd4_pages)));
    check("a Type 2 capture is worth saving", flipso_capture_valid(capture));

    /* The identity is the UID (pages 0-1), not the compact ISRN every card shares:
     * "8189" then the seven serial bytes as hex. */
    char number[ITSO_ISRN_DIGITS + 1];
    check("a Type 2 capture has a card number", flipso_capture_card_number(capture, number));
    check(
        "it is the chip UID, not the shared ISRN",
        strcmp(
            number,
            "8189"
            "04A2B3C4D5E6F7") == 0);

    static ItsoCard direct;
    check("a Type 2 capture decodes", flipso_capture_decode(capture, &direct));
    check("it decodes to the SPT compact card", direct.shell_compact && direct.product_count == 1);

    size_t count = 0;
    char** lines = to_lines(capture, &count);
    check("header plus the one block", count == 3 + 1);
    bool keyed = false;
    for(size_t i = 0; i < count; i++) {
        if(strncmp(lines[i], "Type 2: 04 A2 B3", 16) == 0) keyed = true;
    }
    check("it is written under its own key", keyed);

    FlipsoCapture* loaded = flipso_capture_alloc();
    for(size_t i = 0; i < count; i++)
        flipso_capture_parse_line(loaded, lines[i]);
    static ItsoCard reloaded;
    check("the loaded Type 2 capture decodes", flipso_capture_decode(loaded, &reloaded));
    check("to the same card, byte for byte", itso_card_equal(&direct, &reloaded));

    free_lines(lines, count);
    flipso_capture_free(loaded);
    flipso_capture_free(capture);
}

void full_type2_card(void) {
    FlipsoCapture* capture = flipso_capture_alloc();
    capture_full(capture, cmd9_pages, sizeof(cmd9_pages));
    check("a CMD9 capture is worth saving", flipso_capture_valid(capture));

    /* A full shell has a number of its own, unlike a compact one. */
    char number[ITSO_ISRN_DIGITS + 1];
    check(
        "a CMD9 is known by its card number",
        flipso_capture_card_number(capture, number) && strcmp(number, EXPECT_CMD9_ISRN) == 0);

    static ItsoCard direct, loaded_card;
    check("a CMD9 capture decodes", flipso_capture_decode(capture, &direct));
    check(
        "to the NTAG215 with its Abacus",
        strcmp(itso_type2_chip_name(&direct), "NTAG215") == 0 && direct.chip_abacus == 3 &&
            direct.chip_uid_valid);
    check(
        "its journey ticket and both journeys",
        direct.product_count == 1 && direct.products[0].value_ts == 3 &&
            direct.products[0].value_history_count == 3 && direct.tap_count == 2);

    size_t count = 0;
    char** lines = to_lines(capture, &count);
    check("header plus shell, tag, directory, product and log", count == 3 + 5);
    bool keyed = false;
    for(size_t i = 0; i < count; i++) {
        if(strncmp(lines[i], "Tag: 04 19 09", 13) == 0) keyed = true;
    }
    check("the chip pages are written under their own key", keyed);

    FlipsoCapture* loaded = flipso_capture_alloc();
    for(size_t i = 0; i < count; i++) {
        flipso_capture_parse_line(loaded, lines[i]);
    }
    check("the saved CMD9 decodes", flipso_capture_decode(loaded, &loaded_card));
    check("exactly as the read did", itso_card_equal(&direct, &loaded_card));

    free_lines(lines, count);
    flipso_capture_free(loaded);
    flipso_capture_free(capture);
}

/* What is not a whole CMD9 or CMD10 is refused, and leaves nothing behind. */
void full_type2_refused(void) {
    static ItsoCard scratch;
    bool clean = true;
    for(size_t cut = 0; cut < sizeof(cmd9_pages); cut += 8) {
        uint8_t* exact = malloc(cut ? cut : 1);
        memcpy(exact, cmd9_pages, cut);
        FlipsoCapture* capture = flipso_capture_alloc();
        if(flipso_capture_add_type2_full(capture, &scratch, exact, cut) ||
           flipso_capture_valid(capture)) {
            clean = false;
        }
        flipso_capture_free(capture);
        free(exact);
    }
    check("a CMD9 short of its sixth sector is not kept", clean);

    FlipsoCapture* capture = flipso_capture_alloc();
    check(
        "nor is a CMD4 ticket",
        !flipso_capture_add_type2_full(capture, &scratch, cmd4_pages, sizeof(cmd4_pages)) &&
            !flipso_capture_valid(capture));
    flipso_capture_free(capture);
}
