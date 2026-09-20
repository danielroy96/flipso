/*
 * Host-side test for the saved-card capture: the raw blocks a read yields, the
 * file they are written to, and the card they decode back into.
 *
 * The property that matters is that saving and loading changes nothing. A card
 * decoded straight from the blocks a read produced and the same card decoded
 * after a trip through the file must be identical, field for field - which is
 * checkable here because ItsoCard is a flat struct zeroed before every parse.
 */
#include "flipso_capture.h"
#include "itso.h"
#include "card_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(const char* what, int ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if(!ok) failures++;
}

/* The five product groups of the synthetic CMD7 card, assembled the way the
 * reader assembles them: each chained sector appended at a sector boundary. */
static uint8_t group1[128], group2[64], group3[128], group5[128];
static uint8_t group4[sizeof(card_sector4) + sizeof(card_sector10)];

static void build_groups(void) {
    memcpy(group1, card_sector1, sizeof(card_sector1));
    memcpy(group1 + 64, card_sector9, sizeof(card_sector9));

    memcpy(group2, card_sector2, sizeof(card_sector2));

    memcpy(group3, card_sector3, sizeof(card_sector3));
    memcpy(group3 + 64, card_sector11, sizeof(card_sector11));

    memcpy(group4, card_sector4, sizeof(card_sector4));
    memcpy(group4 + sizeof(card_sector4), card_sector10, sizeof(card_sector10));

    memcpy(group5, card_sector5, sizeof(card_sector5));
    memcpy(group5 + 64, card_sector12, sizeof(card_sector12));
}

typedef struct {
    const uint8_t* data;
    size_t len;
} Group;

static Group groups[5];

/** Decode the card the way flipso_read_card() does, with no capture involved. */
static void reference_decode(ItsoCard* card) {
    itso_card_reset(card);
    itso_parse_shell(card, card_shell, sizeof(card_shell));
    itso_parse_directory(card, card_dir, sizeof(card_dir));
    for(uint8_t i = 0; i < card->product_count && i < 5; i++) {
        itso_parse_ipe(
            &card->products[i], groups[i].data, groups[i].len, card->sector_size);
    }
    itso_parse_log(card, card_log, sizeof(card_log));
}

/** Fill a capture with the same blocks that read would have produced. */
static void fill(FlipsoCapture* capture, const ItsoCard* reference) {
    flipso_capture_add(capture, FlipsoBlockShell, 0, card_shell, sizeof(card_shell));
    flipso_capture_add(capture, FlipsoBlockDirectory, 0, card_dir, sizeof(card_dir));
    for(uint8_t i = 0; i < reference->product_count && i < 5; i++) {
        flipso_capture_add(
            capture, FlipsoBlockProduct, reference->products[i].dir_index, groups[i].data,
            groups[i].len);
    }
    flipso_capture_add(capture, FlipsoBlockLog, 0, card_log, sizeof(card_log));
    flipso_capture_set_time(capture, 1758400000u);
}

/** Render a capture to the lines of a saved file. Caller frees. */
static char** to_lines(const FlipsoCapture* capture, size_t* count) {
    *count = flipso_capture_lines(capture);
    char** lines = malloc(sizeof(char*) * *count);
    for(size_t i = 0; i < *count; i++) {
        lines[i] = malloc(FLIPSO_CAPTURE_LINE_MAX);
        if(!flipso_capture_line(capture, i, lines[i], FLIPSO_CAPTURE_LINE_MAX)) {
            check("every line fits FLIPSO_CAPTURE_LINE_MAX", 0);
            lines[i][0] = '\0';
        }
    }
    return lines;
}

static void free_lines(char** lines, size_t count) {
    for(size_t i = 0; i < count; i++) {
        free(lines[i]);
    }
    free(lines);
}

static void round_trip(void) {
    ItsoCard reference;
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

    ItsoCard direct;
    check("capture decodes", flipso_capture_decode(capture, &direct));
    check("capture decodes to the same card", memcmp(&direct, &reference, sizeof(ItsoCard)) == 0);

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

    ItsoCard reloaded;
    check("loaded capture decodes", flipso_capture_decode(loaded, &reloaded));
    check(
        "saving and loading changes nothing",
        memcmp(&reloaded, &reference, sizeof(ItsoCard)) == 0);

    /* Lines may arrive in any order: nothing in the format is positional. */
    FlipsoCapture* shuffled = flipso_capture_alloc();
    for(size_t i = count; i > 0; i--) {
        flipso_capture_parse_line(shuffled, lines[i - 1]);
    }
    ItsoCard backwards;
    flipso_capture_decode(shuffled, &backwards);
    check(
        "a file read backwards decodes the same",
        memcmp(&backwards, &reference, sizeof(ItsoCard)) == 0);

    free_lines(lines, count);
    flipso_capture_free(shuffled);
    flipso_capture_free(loaded);
    flipso_capture_free(capture);
}

/* A directory entry no product claims: written out, read back, and simply not
 * used by the decode. It must not take the rest of the file with it. */
static void spare_entry(void) {
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

static void partial(void) {
    ItsoCard reference;
    reference_decode(&reference);

    /* A card whose log never read: the taps go, nothing else does. */
    FlipsoCapture* capture = flipso_capture_alloc();
    flipso_capture_add(capture, FlipsoBlockShell, 0, card_shell, sizeof(card_shell));
    flipso_capture_add(capture, FlipsoBlockDirectory, 0, card_dir, sizeof(card_dir));
    ItsoCard card;
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

static void limits(void) {
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
        "a repeated block is refused",
        !flipso_capture_add(capture, FlipsoBlockShell, 0, big, 8));

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
    ItsoCard card;
    check("a capture of junk does not decode", !flipso_capture_decode(capture, &card));

    flipso_capture_reset(capture);
    check("reset empties it", !flipso_capture_valid(capture));
    check("and it can be filled again", flipso_capture_add(
        capture, FlipsoBlockShell, 0, card_shell, sizeof(card_shell)));
    flipso_capture_free(capture);
}

static void hostile_files(void) {
    FlipsoCapture* capture = flipso_capture_alloc();

    check(
        "a file of another type is refused",
        !flipso_capture_parse_line(capture, "Filetype: Flipper NFC device"));
    check(
        "our own filetype is accepted",
        flipso_capture_parse_line(capture, "Filetype: Flipso card"));
    /* Lines come off the stream with their newline still attached. */
    check(
        "and is still accepted with the line ending on it",
        flipso_capture_parse_line(capture, "Filetype: Flipso card\r\n"));
    check(
        "a filetype that merely starts the same is refused",
        !flipso_capture_parse_line(capture, "Filetype: Flipso cardboard"));
    check("a version from the future is refused",
          !flipso_capture_parse_line(capture, "Version: 99"));
    check("a version that is not a number is refused",
          !flipso_capture_parse_line(capture, "Version: banana"));
    check("our own version is accepted", flipso_capture_parse_line(capture, "Version: 1"));

    /* Everything below is junk that must be survivable rather than fatal: these
     * files sit in a directory the user can edit. */
    static const char* junk[] = {
        "",
        "\n",
        ":",
        ": 00 11",
        "Shell:",
        "Shell: ",
        "Shell: 0",
        "Shell: 0011223",
        "Shell: ZZ",
        "Shell: 00 11 ZZ 22",
        "Shell 00 11",
        "AVeryLongKeyIndeedThatNobodyWrote: 00",
        "Product: 00 11",
        "Product x: 00 11",
        "Product 0: 00 11",
        "Product 99: 00 11",
        "Product 256: 00 11",
        "Product 4294967296: 00",
        "Read at:",
        "Read at: not a number",
        "Read at: 99999999999999999999",
        "   Shell   :   00 11 22   ",
    };
    bool fatal = false;
    for(size_t i = 0; i < sizeof(junk) / sizeof(junk[0]); i++) {
        if(!flipso_capture_parse_line(capture, junk[i])) fatal = true;
    }
    check("no junk line is treated as fatal", !fatal);

    /* Only the last of those carried a usable Shell, padded though it is. */
    check("a padded key still lands", flipso_capture_valid(capture));

    flipso_capture_free(capture);

    /* A deterministic sweep of random bytes through the line parser. Under
     * ASan this is what catches a read past the end of a key or a hex pair. */
    uint32_t seed = 0x9E3779B9u;
    capture = flipso_capture_alloc();
    for(int round = 0; round < 20000; round++) {
        char line[64];
        size_t len = 0;
        seed = seed * 1103515245u + 12345u;
        size_t want = (seed >> 16) % (sizeof(line) - 1);
        while(len < want) {
            seed = seed * 1103515245u + 12345u;
            char c = (char)(" :ShellDirectoryProductLogVersionFiletypeReadat0123456789ABCDEFxz\t\r"
                            [(seed >> 16) % 66]);
            line[len++] = c;
        }
        line[len] = '\0';
        flipso_capture_parse_line(capture, line);
        /* Keep it from filling up and refusing everything after the first few
         * hundred rounds, which would stop the sweep exercising anything. */
        if((round % 64) == 0) flipso_capture_reset(capture);
    }
    check("random lines survive the parser", 1);
    flipso_capture_free(capture);
}

int main(void) {
    build_groups();
    groups[0] = (Group){group1, sizeof(group1)};
    groups[1] = (Group){group2, sizeof(group2)};
    groups[2] = (Group){group3, sizeof(group3)};
    groups[3] = (Group){group4, sizeof(group4)};
    groups[4] = (Group){group5, sizeof(group5)};

    printf("Save and load\n");
    round_trip();
    printf("\nIncomplete reads\n");
    partial();
    spare_entry();
    printf("\nLimits\n");
    limits();
    printf("\nHostile files\n");
    hostile_files();

    printf("\n%s\n", failures ? "FAILURES" : "All capture tests passed");
    return failures ? 1 : 0;
}
