/*
 * Host-side test for the text of the detail screens.
 *
 * Two kinds of check. The synthetic card from build_card.py is rendered and
 * specific lines looked for, which pins the wording of the things a review
 * found wrong: a doubled label, a tap-out's two unlabelled times, "Tapped:
 * OUT" on a bus card. Then every screen of every demo card - which between
 * them reach every screen, every product type and both command sets - is held
 * to the house style flipso_format.h sets out, line by line:
 *
 *   - a value after "Label: " starts with a capital or a digit;
 *   - an indented line is itself "Label: Value";
 *   - no label appears twice on one line;
 *   - money is "£", never "GBP".
 *
 *     test_format <directory of demo .flipso files>
 */
#include "flipso_format.h"
#include "itso/itso_operators.h"
#include "card_data.h"
#include "itso_i.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(const char* what, int ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if(!ok) failures++;
}

/* ---- lookups: the operator table is real, the SD card tables are faked --- */

const char* flipso_operators_name(const FlipsoOperators* instance, uint16_t oid) {
    (void)instance;
    return itso_operator_name(oid);
}

const char* flipso_operators_brand(const FlipsoOperators* instance, uint16_t oid) {
    (void)instance;
    return itso_operator_brand(oid);
}

const char* flipso_stations_name(FlipsoStations* instance, const char* nlc) {
    (void)instance;
    if(strcmp(nlc, "1072") == 0) return "London Waterloo";
    if(strcmp(nlc, "5148") == 0) return "London Bridge";
    return NULL;
}

const char* flipso_naptan_stop(FlipsoNaptan* instance, const char* digits) {
    (void)instance;
    if(strcmp(digits, "00062624") == 0) return "High Street";
    return NULL;
}

const char* flipso_naptan_atco(FlipsoNaptan* instance, const char* atco) {
    (void)instance;
    (void)atco;
    return NULL;
}

/* ---- the synthetic card, assembled as test_capture.c does it ------------- */

static uint8_t group1[64 + sizeof(card_sector9)];
static uint8_t group3[64 + sizeof(card_sector11)];
static uint8_t group4[sizeof(card_sector4) + sizeof(card_sector10)];
static uint8_t group5[64 + sizeof(card_sector12)];

static FlipsoCapture* synthetic_capture(void) {
    memcpy(group1, card_sector1, sizeof(card_sector1));
    memcpy(group1 + 64, card_sector9, sizeof(card_sector9));
    memcpy(group3, card_sector3, sizeof(card_sector3));
    memcpy(group3 + 64, card_sector11, sizeof(card_sector11));
    memcpy(group4, card_sector4, sizeof(card_sector4));
    memcpy(group4 + sizeof(card_sector4), card_sector10, sizeof(card_sector10));
    memcpy(group5, card_sector5, sizeof(card_sector5));
    memcpy(group5 + 64, card_sector12, sizeof(card_sector12));

    FlipsoCapture* capture = flipso_capture_alloc();
    flipso_capture_add(capture, FlipsoBlockShell, 0, card_shell, sizeof(card_shell));
    flipso_capture_add(capture, FlipsoBlockDirectory, 0, card_dir, sizeof(card_dir));
    flipso_capture_add(capture, FlipsoBlockProduct, 1, group1, sizeof(group1));
    flipso_capture_add(capture, FlipsoBlockProduct, 2, card_sector2, sizeof(card_sector2));
    flipso_capture_add(capture, FlipsoBlockProduct, 3, group3, sizeof(group3));
    flipso_capture_add(capture, FlipsoBlockProduct, 4, group4, sizeof(group4));
    flipso_capture_add(capture, FlipsoBlockProduct, 5, group5, sizeof(group5));
    flipso_capture_add(capture, FlipsoBlockLog, 0, card_log, sizeof(card_log));
    flipso_capture_set_time(capture, 1758400000u);
    return capture;
}

/* ---- house style --------------------------------------------------------- */

static bool shows(const FuriString* text, const char* needle) {
    return strstr(furi_string_get_cstr(text), needle) != NULL;
}

/* Values that are names, and so keep the case they are given. */
static bool flipso_test_is_name(const char* value) {
    return strncmp(value, "c2c", 3) == 0;
}

/**
 * Hold every line of @p text to the conventions, reporting the first line
 * that breaks each one. @p where names the screen for the report.
 */
static void house_style(const char* where, const FuriString* text) {
    const char* line = furi_string_get_cstr(text);
    char buf[256];
    bool lower = false, unlabelled = false, doubled = false, gbp = false;
    char first_lower[256] = "", first_unlabelled[256] = "", first_doubled[256] = "";

    while(*line) {
        const char* nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);
        if(len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, line, len);
        buf[len] = '\0';

        bool heading = buf[0] == '\e';
        const char* colon = strstr(buf, ": ");

        if(!heading && colon) {
            const char* value = colon + 2;
            if(islower((unsigned char)value[0]) && !flipso_test_is_name(value)) {
                if(!lower) snprintf(first_lower, sizeof(first_lower), "%s", buf);
                lower = true;
            }
            if(strstr(value, ": ")) {
                if(!doubled) snprintf(first_doubled, sizeof(first_doubled), "%s", buf);
                doubled = true;
            }
        }
        if(!heading && buf[0] == ' ' && !colon) {
            if(!unlabelled) snprintf(first_unlabelled, sizeof(first_unlabelled), "%s", buf);
            unlabelled = true;
        }
        if(strstr(buf, "GBP")) gbp = true;

        if(!nl) break;
        line = nl + 1;
    }

    char what[320];
    snprintf(
        what,
        sizeof(what),
        "%s: values are capitalised%s%s",
        where,
        lower ? " - " : "",
        first_lower);
    check(what, !lower);
    snprintf(
        what,
        sizeof(what),
        "%s: indented lines are labelled%s%s",
        where,
        unlabelled ? " - " : "",
        first_unlabelled);
    check(what, !unlabelled);
    snprintf(
        what,
        sizeof(what),
        "%s: no label twice on a line%s%s",
        where,
        doubled ? " - " : "",
        first_doubled);
    check(what, !doubled);
    snprintf(what, sizeof(what), "%s: money is in pounds, not GBP", where);
    check(what, !gbp);
}

/** Render every screen of @p card, holding each to the house style. */
static void every_screen(const char* name, const FlipsoFormat* f, const ItsoCard* card) {
    FuriString* text = furi_string_alloc();
    char where[128];

    flipso_format_summary(text, f, card);
    snprintf(where, sizeof(where), "%s summary", name);
    house_style(where, text);

    furi_string_reset(text);
    flipso_format_card(text, f, card, "A name", 1758400000u);
    snprintf(where, sizeof(where), "%s card", name);
    house_style(where, text);

    furi_string_reset(text);
    flipso_format_payg(text, f, card);
    snprintf(where, sizeof(where), "%s purse", name);
    house_style(where, text);

    furi_string_reset(text);
    flipso_format_id(text, f, card);
    snprintf(where, sizeof(where), "%s ID", name);
    house_style(where, text);

    furi_string_reset(text);
    flipso_format_taps(text, f, card);
    snprintf(where, sizeof(where), "%s journeys", name);
    house_style(where, text);

    for(uint8_t i = 0; i < card->product_count; i++) {
        furi_string_reset(text);
        flipso_format_product(text, f, card, &card->products[i]);
        snprintf(
            where,
            sizeof(where),
            "%s product %u (%s)",
            name,
            i,
            flipso_product_title(&card->products[i]));
        house_style(where, text);
    }
    furi_string_free(text);
}

/** Load a saved card file into @p capture. */
static bool load(FlipsoCapture* capture, const char* path) {
    FILE* file = fopen(path, "r");
    if(!file) return false;
    static char line[FLIPSO_CAPTURE_LINE_MAX + 2];
    bool ok = true;
    while(ok && fgets(line, sizeof(line), file)) {
        ok = flipso_capture_parse_line(capture, line);
    }
    fclose(file);
    return ok && flipso_capture_valid(capture);
}

/* 2060-01-01: past the expiry of every card and product the tests build. */
#define FLIPSO_TEST_LATER 2840140800u

int main(int argc, char** argv) {
    printf("Screen text\n");

    /* A fixed "now", so what has expired does not depend on the day the test
     * runs: 2026-09-20. */
    FlipsoFormat f = {.now = 1758326400u};

    /* --- The synthetic card. --- */
    FlipsoCapture* capture = synthetic_capture();
    static ItsoCard card;
    check("the synthetic card decodes", flipso_capture_decode(capture, &card));
    f.capture = capture;

    FuriString* text = furi_string_alloc();

    flipso_format_payg(text, &f, &card);
    printf("\n%s\n", furi_string_get_cstr(text));
    check(
        "the balance is in pounds",
        shows(
            text,
            "Balance: \xC2\xA3"
            "12.34"));
    check("the purse terms are labelled", shows(text, "Auto top-up: "));
    check("a top-up's detail is indented and labelled", shows(text, "  When below: \xC2\xA3"));
    check("earlier transactions have a heading", shows(text, "Earlier on card"));
    check("each one says when", shows(text, "  When: "));

    furi_string_reset(text);
    flipso_format_taps(text, &f, &card);
    printf("\n%s\n", furi_string_get_cstr(text));
    check("the in/out state is where the holder is", shows(text, "Inside ticket gates: "));
    check("not a bare IN or OUT", !shows(text, ": IN\n") && !shows(text, ": OUT\n"));
    check("a tap's time is labelled as every time is", shows(text, "  When: "));
    check("with no other word for it", !shows(text, "  At: ") && !shows(text, "\nTime: "));
    check("a tap out says which time is which", shows(text, "  Out: ") && shows(text, "  In: "));
    check("and how long the journey took", shows(text, "  Journey time: "));
    check("stations are named", shows(text, "London Waterloo"));

    furi_string_reset(text);
    flipso_format_id(text, &f, &card);
    printf("\n%s\n", furi_string_get_cstr(text));
    check("the ID has its holder", shows(text, "Name: "));
    check(
        "the photo flag is capitalised",
        shows(text, "Photo on card: Yes") || shows(text, "Photo on card: No"));
    check("an entitlement's area is not a journey's end", !shows(text, "\nFrom: "));

    furi_string_reset(text);
    flipso_format_card(text, &f, &card, NULL, 0);
    printf("\n%s\n", furi_string_get_cstr(text));
    check("the card number is grouped", shows(text, "633597 1234 0012 3458"));
    check("the checksum is stated", shows(text, "Checksum: Correct"));
    check("the card type is named", shows(text, "Card type: DESFire (CMD7)"));
    check("no saved card section for a card just read", !shows(text, "Saved card"));

    furi_string_reset(text);
    flipso_format_summary(text, &f, &card);
    printf("\n%s\n", furi_string_get_cstr(text));
    check("the summary leads with the card's state", shows(text, "Card: "));
    check(
        "the summary has the balance",
        shows(
            text,
            "Pay as you go: \xC2\xA3"
            "12.34"));
    check("the summary has the last tap", shows(text, "Last tap: ") && shows(text, "  When: "));

    every_screen("synthetic", &f, &card);

    /* The same card decades on, when it and everything on it has expired: the
     * wording changes with the clock, and the house style has to hold for both.
     * Pinning one date alone once hid a "Card: Expired: ..." on every card. */
    FlipsoFormat later = f;
    later.now = FLIPSO_TEST_LATER;
    every_screen("synthetic, expired", &later, &card);
    furi_string_reset(text);
    flipso_format_summary(text, &later, &card);
    check("an expired card's summary says so", shows(text, "Card: Expired "));

    /* The product list's tags. */
    check("an in-date product has no tag", flipso_product_tag(&card.products[0], f.now) == NULL);
    ItsoProduct gone = card.products[0];
    gone.on_card = false;
    check(
        "a dropped product says so first",
        strcmp(flipso_product_tag(&gone, f.now), "Off card") == 0);

    /* Heading icons are one byte after the markup, above '\n'. */
    furi_string_reset(text);
    flipso_cat_heading(text, FlipsoIconPast, "Off card");
    const char* heading = furi_string_get_cstr(text);
    check("a heading's icon byte is never a newline", heading[2] != '\n');
    check("and the heading is one line", strchr(heading, '\n') == heading + strlen(heading) - 1);

    /* A location listing several stops, the first of which the stop table
     * names: the name replaces the code, and the others are still counted. */
    {
        static const uint8_t stops[] = {
            212, 12, 0x00, 0x06, 0x26, 0x24, 0x12, 0x34, 0x56, 0x78, 0x87, 0x65, 0x43, 0x21};
        ItsoProduct ticket = card.products[0];
        itso_parse_location(stops, sizeof(stops), ItsoLocStructLoc1, &ticket.from);
        furi_string_reset(text);
        flipso_format_product(text, &f, &card, &ticket);
        check(
            "a named stop keeps the count of the others", shows(text, "High Street and 2 more\n"));
    }

    /* The card details screen for a DESFire Flipso cannot decode, which is
     * held to the same style as the ITSO screens. */
    static const uint8_t chip[FLIPSO_MEDIA_CHIP_LEN] = {
        0x04, 0x01, 0x01, 0x01, 0x00, 0x16, 0x05, 0x04, 0x01, 0x01, 0x01,
        0x03, 0x16, 0x05, 0x04, 0x8B, 0x1F, 0xF1, 0xAD, 0x26, 0x80, 0xBA,
        0x34, 0xCD, 0x56, 0xEF, 0x42, 0x08, 0xE0, 0x04, 0x00,
    };
    static FlipsoMedia media;
    flipso_media_reset(&media);
    furi_string_reset(text);
    flipso_format_media(text, &media);
    house_style("card details, undescribed", text);
    flipso_media_parse_chip(&media, chip, sizeof(chip));
    flipso_media_add_app(&media, FLIPSO_AID_OYSTER);
    flipso_media_add_app(&media, 0xABCDEFu);
    media.has_files = true;
    media.selected_aid = FLIPSO_AID_OYSTER;
    media.file_count = 3;
    media.files[0] = (FlipsoMediaFile){.id = 0, .settings_valid = true, .access = 0xEEEE};
    media.files[0].data.size = 8;
    media.files[0].data_len = 8;
    memcpy(media.data, "\xDE\xAD\xBE\xEF\x01\x02\x03\x04", 8);
    media.data_len = 8;
    media.files[1] = (FlipsoMediaFile){
        .id = 1, .settings_valid = true, .type = FLIPSO_FILE_VALUE, .access = 0x1111};
    media.files[2] = (FlipsoMediaFile){.id = 2};
    furi_string_reset(text);
    flipso_format_media(text, &media);
    printf("\n%s\n", furi_string_get_cstr(text));
    house_style("card details", text);
    check("the details open on the chip", shows(text, "Chip: MIFARE DESFire EV1"));
    check("contents wrap between groups of bytes", shows(text, "  Contents: DEADBEEF 01020304\n"));

    furi_string_free(text);
    flipso_capture_free(capture);

    /* --- Every demo card, every screen. --- */
    if(argc > 1) {
        DIR* dir = opendir(argv[1]);
        check("the demo cards are there", dir != NULL);
        struct dirent* entry;
        int cards = 0, chips = 0;
        while(dir && (entry = readdir(dir))) {
            const char* ext = strrchr(entry->d_name, '.');
            if(!ext || strcmp(ext, ".flipso") != 0) continue;
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", argv[1], entry->d_name);
            FlipsoCapture* demo = flipso_capture_alloc();
            static ItsoCard demo_card;
            char what[600];
            snprintf(what, sizeof(what), "%s loads", entry->d_name);
            check(what, load(demo, path) && flipso_capture_decode(demo, &demo_card));
            f.capture = demo;
            /* What the saved-card scene does with a card's chip block. */
            static FlipsoMedia demo_media;
            flipso_media_reset(&demo_media);
            size_t chip_len = 0;
            const uint8_t* chip = flipso_capture_chip(demo, &chip_len);
            if(chip) {
                flipso_media_parse_chip(&demo_media, chip, chip_len);
                FuriString* screen = furi_string_alloc();
                FlipsoFormat with_chip = f;
                with_chip.media = &demo_media;
                flipso_format_card(screen, &with_chip, &demo_card, "A name", 0);
                snprintf(what, sizeof(what), "%s shows its saved chip", entry->d_name);
                check(what, shows(screen, "Chip: MIFARE DESFire EV1\n"));
                furi_string_free(screen);
                chips++;
            }
            f.media = &demo_media;
            every_screen(entry->d_name, &f, &demo_card);
            f.media = NULL;
            FlipsoFormat expired = f;
            expired.now = FLIPSO_TEST_LATER;
            snprintf(what, sizeof(what), "%s, expired", entry->d_name);
            every_screen(what, &expired, &demo_card);
            flipso_capture_free(demo);
            cards++;
        }
        if(dir) closedir(dir);
        check("all four demo cards were rendered", cards == 4);
        check("and a saved chip block was among them", chips > 0);
    }

    printf("\n%s\n", failures ? "FAILED" : "All screen text tests passed");
    return failures ? 1 : 0;
}
