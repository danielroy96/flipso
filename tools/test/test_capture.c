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

/* The chip's description: saved and loaded like any block, byte for byte, and
 * left alone by a merge, which only replaces the history blocks. */
static void chip_block(void) {
    uint8_t chip[31];
    for(uint8_t i = 0; i < sizeof(chip); i++) chip[i] = (uint8_t)(0xA0 + i);

    FlipsoCapture* capture = flipso_capture_alloc();
    ItsoCard reference;
    reference_decode(&reference);
    fill(capture, &reference);
    size_t len = 0;
    check("a read with no chip block has none", flipso_capture_chip(capture, &len) == NULL);
    check("a chip block is kept", flipso_capture_add(capture, FlipsoBlockChip, 0, chip, sizeof(chip)));

    size_t count = 0;
    char** lines = to_lines(capture, &count);
    bool keyed = false;
    for(size_t i = 0; i < count; i++) {
        if(strncmp(lines[i], "Chip: A0 A1", 11) == 0) keyed = true;
    }
    check("it is written under its own key", keyed);

    FlipsoCapture* loaded = flipso_capture_alloc();
    for(size_t i = 0; i < count; i++) flipso_capture_parse_line(loaded, lines[i]);
    const uint8_t* back = flipso_capture_chip(loaded, &len);
    check("and reads back byte for byte", back && len == sizeof(chip) && memcmp(back, chip, len) == 0);

    ItsoCard decoded;
    flipso_capture_decode(loaded, &decoded);
    check("without changing the card it decodes to", memcmp(&decoded, &reference, sizeof(ItsoCard)) == 0);

    flipso_capture_merge_history(capture, loaded, NULL);
    back = flipso_capture_chip(capture, &len);
    check("a merge keeps it", back && len == sizeof(chip) && memcmp(back, chip, len) == 0);

    free_lines(lines, count);
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

/* ------------------------------------------------------------------ */
/* Merging in what an earlier read of the same card saw                 */
/* ------------------------------------------------------------------ */

/** A copy of tap record @p from, restamped, which is a different journey. */
static void restamp_tap(uint8_t* out, const uint8_t* from, uint32_t dts) {
    memcpy(out, from, ITSO_TAP_RECORD_LEN);
    /* DateTimeStamp is 24 bits at bit 32, so bytes 4 to 6. */
    out[4] = (uint8_t)(dts >> 16);
    out[5] = (uint8_t)(dts >> 8);
    out[6] = (uint8_t)dts;
}

/** A copy of value record @p from with a new TS#, timestamp and balance. */
static void restamp_value(
    uint8_t* out,
    const uint8_t* from,
    uint16_t ts,
    uint32_t dts,
    int16_t amount) {
    memcpy(out, from, ITSO_VALUE_RECORD_LEN);
    /* TS# is 12 bits at bit 4 and the DTS 24 bits at bit 16; a purse keeps its
     * balance in the two bytes at 10 (TS 1000-5 table 4). */
    out[0] = (uint8_t)((out[0] & 0xF0) | ((ts >> 8) & 0x0F));
    out[1] = (uint8_t)ts;
    out[2] = (uint8_t)(dts >> 16);
    out[3] = (uint8_t)(dts >> 8);
    out[4] = (uint8_t)dts;
    out[10] = (uint8_t)((uint16_t)amount >> 8);
    out[11] = (uint8_t)amount;
}

/** The purse's value records: after its IPE sector, past the two-byte header. */
#define GROUP1_VALUE_0 (64 + 2)
#define GROUP1_VALUE_1 (64 + 2 + ITSO_VALUE_RECORD_LEN)

/** True when a decoded card holds a journey stamped @p dts. */
static bool holds_tap(const ItsoCard* card, uint32_t dts) {
    for(uint8_t i = 0; i < card->tap_count; i++) {
        if(card->taps[i].dts == dts) return true;
    }
    return false;
}

/**
 * Fill a capture with the card as it is "now": every block as the read found
 * it, except that entry 1 and the log are whatever the caller has rewritten.
 */
static void fill_now(
    FlipsoCapture* capture,
    const ItsoCard* reference,
    const uint8_t* dir,
    size_t dir_len,
    const uint8_t* group_one,
    size_t group_one_len,
    const uint8_t* log,
    size_t log_len) {
    flipso_capture_add(capture, FlipsoBlockShell, 0, card_shell, sizeof(card_shell));
    flipso_capture_add(capture, FlipsoBlockDirectory, 0, dir, dir_len);
    flipso_capture_add(capture, FlipsoBlockProduct, 1, group_one, group_one_len);
    for(uint8_t i = 1; i < reference->product_count && i < 5; i++) {
        flipso_capture_add(
            capture, FlipsoBlockProduct, reference->products[i].dir_index, groups[i].data,
            groups[i].len);
    }
    flipso_capture_add(capture, FlipsoBlockLog, 0, log, log_len);
}

/*
 * A card read twice, a few journeys apart.
 *
 * The card keeps four journey slots and two value records per product, and
 * writes each new one over the oldest. So a second read of a card that has been
 * used since the first cannot see everything the first read saw - and the file
 * written then is the only place those records still exist.
 */
static void merge_history(void) {
    ItsoCard reference;
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
    restamp_value(
        group1_now + GROUP1_VALUE_0, group1 + GROUP1_VALUE_1, 102, new_value_dts, 900);

    FlipsoCapture* now = flipso_capture_alloc();
    fill_now(
        now, &reference, card_dir, sizeof(card_dir), group1_now, sizeof(group1_now), log_now,
        sizeof(log_now));

    /* What the card alone can say, before the file is consulted. */
    ItsoCard live;
    flipso_capture_decode(now, &live);
    check("the card itself holds four journeys", live.tap_count == 4);
    check("and two value records on the purse", live.products[0].value_history_count == 2);
    check("the oldest journey has rolled off the card", !holds_tap(&live, oldest_dts));
    check("and the new one is there instead", holds_tap(&live, new_tap_dts));

    FlipsoCaptureDiff diff;
    flipso_capture_merge_history(now, previous, &diff);
    printf("      +%u journeys, +%u transactions, kept %u and %u\n", diff.new_taps,
           diff.new_values, diff.kept_taps, diff.kept_values);
    check("one journey is new since the file was written", diff.new_taps == 1);
    check("one transaction is new", diff.new_values == 1);
    check("the journey that rolled off is kept", diff.kept_taps == 1);
    check("so is the value record that rolled off", diff.kept_values == 1);

    ItsoCard merged;
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
    check("newest first, by TS#",
          purse->value_history[0].ts == 102 && purse->value_history[1].ts == 101 &&
              purse->value_history[2].ts == 100);
    check("the balance series runs 9.00, 12.34, 15.60",
          purse->value_history[0].amount.value == 900 &&
              purse->value_history[1].amount.value == 1234 &&
              purse->value_history[2].amount.value == 1560);
    /* Which of them the card still holds is not a detail: the two records in
     * its group are a different claim from the one the file is the only copy
     * of, and the product screen shows them apart. */
    check("the records still on the card say so",
          purse->value_history[0].on_card && purse->value_history[1].on_card);
    check("and the one only the file has does not", !purse->value_history[2].on_card);
    check("and the live balance is the newest of them",
          purse->balance.value == 900 && purse->value_ts == 102);

    /* Merging the same file again must change nothing: the records it holds are
     * already here, and a second copy would read as a second journey. */
    FlipsoCaptureDiff again;
    flipso_capture_merge_history(now, previous, &again);
    ItsoCard twice;
    flipso_capture_decode(now, &twice);
    check("merging the same record twice changes nothing",
          memcmp(&twice, &merged, sizeof(ItsoCard)) == 0);
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
    ItsoCard reloaded;
    check("a merged file loads", flipso_capture_decode(loaded, &reloaded));
    check("and decodes to the same card", memcmp(&reloaded, &merged, sizeof(ItsoCard)) == 0);

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
static void merge_replaced_product(void) {
    ItsoCard reference;
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
        now, &reference, dir_now, sizeof(dir_now), group1_now, sizeof(group1_now), card_log,
        sizeof(card_log));

    FlipsoCaptureDiff diff;
    flipso_capture_merge_history(now, previous, &diff);
    ItsoCard merged;
    flipso_capture_decode(now, &merged);
    check("a replaced product keeps no history", diff.kept_values == 0);
    /* Its records are not the new product's, but the product itself is still
     * one the card used to carry, so it is kept beside the one in its slot. */
    check("but the product itself is kept", diff.kept_products == 1);
    {
        /* Asking twice, as the save screen does after a Cancel, still reports
         * the product rather than finding its slot already taken. */
        FlipsoCaptureDiff again;
        flipso_capture_merge_history(now, previous, &again);
        check("a second merge still reports the kept product", again.kept_products == 1);
        ItsoCard twice;
        flipso_capture_decode(now, &twice);
        check("and still decodes to six products", twice.product_count == 6);
    }
    check("as a sixth product the card does not list",
          merged.product_count == 6 && !merged.products[5].on_card &&
              merged.products[5].dir_index == 1);
    check("so its records are only its own",
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
        same, &reference, card_dir, sizeof(card_dir), group1_now, sizeof(group1_now),
        card_log, sizeof(card_log));
    FlipsoCaptureDiff unchanged;
    flipso_capture_merge_history(same, previous, &unchanged);
    ItsoCard kept;
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
static void merge_cap(void) {
    ItsoCard reference;
    reference_decode(&reference);
    uint32_t oldest_dts = reference.taps[reference.tap_count - 1].dts;

    /* A file saved from an earlier read: its own live log, plus a full history
     * of records older than any of them. */
    FlipsoCapture* previous = flipso_capture_alloc();
    fill(previous, &reference);

    static uint8_t history[FLIPSO_CAPTURE_MAX_LOG_HISTORY * ITSO_TAP_RECORD_LEN];
    for(uint8_t i = 0; i < FLIPSO_CAPTURE_MAX_LOG_HISTORY; i++) {
        restamp_tap(
            history + (size_t)i * ITSO_TAP_RECORD_LEN, card_log,
            oldest_dts - 1440u * (i + 1));
    }
    check("a full history is a block a capture will hold",
          flipso_capture_add(
              previous, FlipsoBlockLogHistory, 0, history, sizeof(history)));

    /* One more journey since, so the oldest of the card's own four rolls off
     * and there are nine records competing for eight places. */
    static uint8_t log_now[sizeof(card_log)];
    memcpy(log_now, card_log, sizeof(card_log));
    restamp_tap(log_now + 3 * ITSO_TAP_RECORD_LEN, card_log, reference.taps[0].dts + 1440);

    FlipsoCapture* now = flipso_capture_alloc();
    fill_now(
        now, &reference, card_dir, sizeof(card_dir), group1, sizeof(group1), log_now,
        sizeof(log_now));

    FlipsoCaptureDiff diff;
    flipso_capture_merge_history(now, previous, &diff);
    check("the kept history stops at the cap",
          diff.kept_taps == FLIPSO_CAPTURE_MAX_LOG_HISTORY);

    ItsoCard merged;
    flipso_capture_decode(now, &merged);
    printf("      %u journeys decoded, of %u the card holds plus %u kept\n",
           merged.tap_count, 4, diff.kept_taps);
    check("the decoded card fills its own array exactly",
          merged.tap_count == ITSO_MAX_TAPS);

    /* What was dropped has to be the oldest of them, which is only true if the
     * whole candidate set was ordered before the cap was applied. */
    check("the journey that just rolled off the card is kept",
          holds_tap(&merged, oldest_dts));
    check("the oldest invented record is the one dropped",
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
 * A product that has left the card altogether.
 *
 * A directory entry is freed when a ticket expires and is removed, so the
 * card's account of what it carries is only ever the present tense: read it
 * again and the ticket is not there, and the file written while it was is the
 * only place it still exists.
 */
static void merge_gone_product(void) {
    ItsoCard reference;
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
            now, FlipsoBlockProduct, reference.products[i].dir_index, groups[i].data,
            groups[i].len);
    }
    flipso_capture_add(now, FlipsoBlockLog, 0, card_log, sizeof(card_log));
    flipso_capture_set_time(now, 1758500000u);

    ItsoCard live;
    flipso_capture_decode(now, &live);
    check("the card itself no longer lists the product", live.product_count == 4);

    FlipsoCaptureDiff diff;
    flipso_capture_merge_history(now, previous, &diff);
    check("so it is carried out of the record", diff.kept_products == 1);

    ItsoCard merged;
    check("the merged capture decodes", flipso_capture_decode(now, &merged));
    check("and shows it again", merged.product_count == 5);

    const ItsoProduct* gone = &merged.products[4];
    const ItsoProduct* was = &reference.products[0];
    check("after the ones the card still has", merged.products[0].on_card && !gone->on_card);
    check("in the entry it held then", gone->dir_index == 1);
    check("as the product it was", gone->typ == was->typ && gone->oid == was->oid);
    check(
        "decoded from its own group",
        gone->body_parsed && gone->balance.value == was->balance.value);
    check(
        "dated by the read that last saw it",
        gone->last_seen == flipso_capture_time(previous));

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
    ItsoCard reloaded;
    check("a file holding one loads", flipso_capture_decode(loaded, &reloaded));
    check("and decodes to the same card", memcmp(&reloaded, &merged, sizeof(ItsoCard)) == 0);
    free_lines(lines, count);

    /* Saving the same read again must not stack copies of it up. */
    FlipsoCaptureDiff again;
    flipso_capture_merge_history(now, previous, &again);
    ItsoCard twice;
    flipso_capture_decode(now, &twice);
    check(
        "merging the same record twice changes nothing",
        memcmp(&twice, &merged, sizeof(ItsoCard)) == 0);

    flipso_capture_free(loaded);
    flipso_capture_free(now);
    flipso_capture_free(previous);
}

/*
 * The cap on those, which is what the product list has room to show: a card
 * wiped clean has more past than the screen does.
 */
static void gone_product_cap(void) {
    ItsoCard reference;
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

    ItsoCard merged;
    flipso_capture_decode(now, &merged);
    check(
        "and the card shows exactly those",
        merged.product_count == ITSO_MAX_HISTORIC_PRODUCTS);
    bool any_on_card = false;
    for(uint8_t i = 0; i < merged.product_count; i++) {
        if(merged.products[i].on_card) any_on_card = true;
    }
    check("none of them as a product it holds", !any_on_card);

    flipso_capture_free(now);
    flipso_capture_free(previous);
}

/*
 * A read that lost the directory is not a card that has shed its products. The
 * distinction is worth a test because the two look alike from here: in both
 * cases this read has no entry to compare the old one against.
 */
static void gone_needs_a_directory(void) {
    ItsoCard reference;
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

int main(void) {
    build_groups();
    groups[0] = (Group){group1, sizeof(group1)};
    groups[1] = (Group){group2, sizeof(group2)};
    groups[2] = (Group){group3, sizeof(group3)};
    groups[3] = (Group){group4, sizeof(group4)};
    groups[4] = (Group){group5, sizeof(group5)};

    printf("Save and load\n");
    round_trip();
    chip_block();
    printf("\nIncomplete reads\n");
    partial();
    spare_entry();
    printf("\nLimits\n");
    limits();
    printf("\nHostile files\n");
    hostile_files();
    printf("\nA card read twice\n");
    merge_history();
    merge_replaced_product();
    merge_cap();
    printf("\nProducts the card has dropped\n");
    merge_gone_product();
    gone_product_cap();
    gone_needs_a_directory();

    printf("\n%s\n", failures ? "FAILURES" : "All capture tests passed");
    return failures ? 1 : 0;
}
