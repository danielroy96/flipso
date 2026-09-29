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

/** True when @p needle appears, and only under the screen's Technical heading. */
static bool technical(const FuriString* text, const char* needle) {
    const char* heading = strstr(furi_string_get_cstr(text), "\e#Technical\n");
    const char* found = strstr(furi_string_get_cstr(text), needle);
    return heading && found && found > heading;
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

/*
 * The encodings Demo 7 carries from a real GWR Touch card, pinned by the lines
 * only they produce: a gate check-in and check-out in the revision 4 shapes a
 * rail gate writes, each naming the reader that wrote it; a revision 2 period
 * ticket with and without CPICC, and no value record; an ID with an empty
 * bitmap; and a Directory InstanceID with a 16-bit extended ISAM OID.
 */
static void demo_seven(const FlipsoFormat* f, const ItsoCard* card) {
    FuriString* text = furi_string_alloc();

    flipso_format_taps(text, f, card);
    check(
        "a check-in names the operator whose gate it was",
        shows(text, "Tap in\n  When: 18/09/2026 17:52\n  Tapped in with: Unknown (24585)\n"));
    /* The stub table knows no GWR stations, so the check is the shape, not the
     * names: a dated tap out with a destination and no fare line. */
    check(
        "a check-out with no amount is still a journey",
        shows(text, "Tap out (latest)\n  When: 18/09/2026 18:49\n  From: ") &&
            shows(text, "  To: ") && !shows(text, "  Fare: "));
    check("each record names the reader that wrote it", shows(text, "  Reader: FF00A3C7\n"));

    furi_string_reset(text);
    flipso_format_card(text, f, card, NULL, 0);
    check(
        "the directory's last writer is decoded from an extended ISAM",
        shows(text, "Last updated by machine: 004E30F3\n  Operator: Unknown (24585)\n"));
    check("160-byte sectors are the layout", shows(text, "Layout: 16 sectors of 160 bytes\n"));

    furi_string_reset(text);
    flipso_format_id(text, f, card);
    check("an ID with nothing optional says so", shows(text, "Name: Not stored\n"));
    check("and still has its language", shows(text, "Language: English\n"));

    for(uint8_t i = 0; i < card->product_count; i++) {
        const ItsoProduct* p = &card->products[i];
        if(p->typ != ItsoTypPeriodTicket) continue;
        furi_string_reset(text);
        flipso_format_product(text, f, card, p);
        check("a revision 2 season ticket has its price", shows(text, "Price paid: \xC2\xA3"));
        check("and its end time", shows(text, "Ends at: 04:30 the day after expiry\n"));
        check("and its validity code", shows(text, "Validity code: 17\n"));
        check(
            "its CPICC shows only when the bitmap says it is there",
            shows(text, "Issuer code: ") == ((p->bitmap & 0x10) != 0));
        check("its expiry comes from the directory", shows(text, "Expires: "));
    }
    furi_string_free(text);
}

/*
 * The two full-shell Type 2 cards: the chip named from the media definition,
 * the lock bytes judged against what TS 1000-10 clause 10.23.1 recommends, a
 * CMD9's Abacus, and a history that takes both copies of the value records.
 */
static void demo_type2_full(const FlipsoFormat* f, const ItsoCard* card, bool ntag) {
    FuriString* text = furi_string_alloc();

    flipso_format_card(text, f, card, NULL, 0);
    check("a full-shell tag has a card number of its own", shows(text, "Card number\n633597 "));
    check("its shell pages are locked", shows(text, "Locked pages: 4-11\n  Shell locked: Yes\n"));
    if(ntag) {
        check("CMD9 names its chip", shows(text, "Chip: NTAG215\n"));
        check("and its memory", shows(text, "Memory: 540 bytes\n"));
        check("and its media", shows(text, "Card type: NTAG (CMD9)\n"));
        check(
            "its Abacus counts down its uses", shows(text, "Uses left: 10\n  Abacus: 5 of 16\n"));
        check("64-byte sectors are the layout", shows(text, "Layout: 9 sectors of 64 bytes\n"));

        /* The same card with its Abacus run out (TS 1000-10 table 107). A copy
         * borrows the card's product and journey arrays, so it lives only as
         * long as this block and is never reset. */
        ItsoCard retired = *card;
        retired.chip_abacus = 16;
        furi_string_reset(text);
        flipso_format_card(text, f, &retired, NULL, 0);
        house_style("a retired CMD9's card screen", text);
        check("a retired CMD9 says so", shows(text, "Status: Retired\n"));
        check(
            "and has no uses left", shows(text, "Uses left: None, retired\n  Abacus: 16 of 16\n"));
        furi_string_reset(text);
        flipso_format_summary(text, f, &retired);
        check("its summary leads with it", shows(text, "Card: Retired\n"));
        furi_string_reset(text);
        flipso_format_card(text, f, card, NULL, 0);
    } else {
        check("CMD10 names its chip", shows(text, "Chip: Ultralight EV1\n"));
        check("and its media", shows(text, "Card type: Ultralight EV1 (CMD10)\n"));
        check("and has no Abacus", !shows(text, "Abacus"));
        check("its rotated shell keeps its MCRN", shows(text, "Card reference: 4917250331\n"));
    }

    furi_string_reset(text);
    flipso_format_product(text, f, card, &card->products[0]);
    check(
        ntag ? "CMD9 history reaches into the other copy" :
               "CMD10 history reaches into the other copy",
        shows(text, ntag ? "Rides left: 7\n" : "Passes left: 1\n"));

    furi_string_reset(text);
    flipso_format_taps(text, f, card);
    check("both log records are journeys", shows(text, "(latest)\n") && card->tap_count == 2);
    furi_string_free(text);
}

/** Render one product decoded from @p group as its product screen. */
static void product_screen(
    FuriString* text,
    const FlipsoFormat* f,
    const ItsoCard* card,
    ItsoProduct* p,
    uint8_t typ,
    bool vgp,
    const uint8_t* group,
    size_t len) {
    memset(p, 0, sizeof(*p));
    p->present = p->on_card = true;
    p->typ = typ;
    p->value_group = vgp;
    p->dir_index = 9;
    itso_parse_ipe(p, group, len, 64);
    furi_string_reset(text);
    flipso_format_product(text, f, card, p);
    house_style("review product", text);
}

/*
 * The lines the TS 1000-5 review (2026-09-29) added: elements that were on a
 * card and not shown, and ones that were shown wrong.
 */
static void spec_review(const FlipsoFormat* f, const ItsoCard* card) {
    FuriString* text = furi_string_alloc();
    static ItsoProduct p;

    furi_string_reset(text);
    flipso_format_product(text, f, card, &card->products[0]);
    check(
        "a purse's print flags are under Technical",
        shows(text, "Print ticket: Yes\nPrint receipt: Yes\n"));
    furi_string_reset(text);
    flipso_format_product(text, f, card, &card->products[1]);
    check(
        "an ID has PrintTicket and no PrintReceipt",
        shows(text, "Print ticket: No\n") && !shows(text, "Print receipt: "));
    furi_string_reset(text);
    flipso_format_product(text, f, card, &card->products[3]);
    check("a rail RouteCode reads as text", shows(text, "Route code: 00000\n"));
    furi_string_reset(text);
    flipso_format_product(text, f, card, &card->products[4]);
    check("loyalty has its owner's data", shows(text, "Owner data: 4660\n"));

    product_screen(
        text,
        f,
        card,
        &p,
        ItsoTypStoredTravelRights,
        true,
        purse_scaled_group,
        sizeof(purse_scaled_group));
    check(
        "a scaled purse's limit scales with its balance",
        shows(
            text,
            "Balance limit: \xC2\xA3"
            "900.00\n"));

    product_screen(
        text, f, card, &p, ItsoTypChargeToAccount1, true, charge1_group, sizeof(charge1_group));
    check(
        "a TYP 4 deposit has its VAT",
        shows(
            text,
            "Deposit: \xC2\xA3"
            "15.00\n  Paid by: Card\n  VAT: 17.50%\n"));

    product_screen(
        text, f, card, &p, ItsoTypChargeToAccount2, true, charge2_group, sizeof(charge2_group));
    check(
        "MaxValue5 in the value record's currency",
        shows(
            text,
            "Spending limit: \xE2\x82\xAC"
            "250.00\n"));
    check("a TYP 5 is used first", shows(text, "Used first: Yes\n"));
    check("and has a receipt printed", shows(text, "Print ticket: No\nPrint receipt: Yes\n"));

    product_screen(
        text,
        f,
        card,
        &p,
        ItsoTypEntitlement,
        false,
        entitlement_rev2_group,
        sizeof(entitlement_rev2_group));
    check(
        "an entitlement names its pass issuer, under Technical",
        technical(text, "Pass issuer code: 1620\n"));
    check("and its holder", technical(text, "Holder number: 11259375\n"));
    check("and how fares round", shows(text, "Fare rounding: Down to 5p\n"));
    check(
        "and its deposit",
        shows(
            text,
            "Deposit: \xC2\xA3"
            "10.00\n  Paid by: Card\n  VAT: 20.00%\n  Refundable: Yes\n"));
    check("and not as an Issuer code under Technical", !shows(text, "Issuer code: "));

    product_screen(
        text,
        f,
        card,
        &p,
        ItsoTypJourneyTicket,
        false,
        journey_rev3_group,
        sizeof(journey_rev3_group));
    check(
        "a revision 3 return and its limits",
        shows(
            text,
            "Ticket use: Return, journeys in pairs\n  Changes allowed: 1\n"
            "  Time between legs: 45 min 30 s\n"));
    check(
        "a ride's value in its own currency",
        shows(
            text,
            "Value of a ride: \xE2\x82\xAC"
            "60.00\n"));

    product_screen(
        text,
        f,
        card,
        &p,
        ItsoTypPeriodTicket,
        false,
        period_rev3_id_group,
        sizeof(period_rev3_id_group));
    check("a period ticket's identity document", shows(text, "Carry with it: ID RC123456\n"));
    check(
        "what a top-up does with expired passes", shows(text, "Expired passes at top-up: Kept\n"));

    product_screen(
        text,
        f,
        card,
        &p,
        ItsoTypPeriodTicket,
        false,
        period_rev3_long_id_group,
        sizeof(period_rev3_long_id_group));
    check(
        "a long identity number is hex, with what is not kept counted",
        shows(text, "Carry with it: ID 0102030405060708090A0B0C0D0E0F10 and 4 more bytes\n"));
    check(
        "a revision 3 period ticket's default",
        shows(text, "Expired passes at top-up: Written off\n"));

    furi_string_free(text);
}

/* Wording pinned against the demo card that carries every product type. */
static void demo_one(const FlipsoFormat* f, const ItsoCard* card) {
    FuriString* text = furi_string_alloc();

    flipso_format_summary(text, f, card);
    check("an ITSO ID is summed up by its concession", shows(text, "ITSO ID: Commuter\n"));

    furi_string_reset(text);
    flipso_format_taps(text, f, card);
    check(
        "the products a gate checked are one line",
        shows(text, "  Products checked: Journey ticket, Pay as you go, Period ticket\n"));
    check(
        "a reader names its machine, then its operator",
        shows(text, "  Tap-in reader: 01020304\n    Operator: "));
    check("passback is called passback", shows(text, "Passback timeout: 20 min\n"));

    /* The purse and the ID have menu rows of their own, so the product list
     * leaves them out - and their own screens carry the Technical section the
     * list's detail screen would have. */
    uint8_t listed = 0;
    for(uint8_t i = 0; i < card->product_count; i++) {
        if(flipso_product_listed(&card->products[i])) listed++;
    }
    check(
        "the product list leaves out the purse, the ID and the entitlement",
        listed == card->product_count - 3);
    ItsoProduct dropped = card->products[0];
    dropped.on_card = false;
    check("but lists a purse the card has dropped", flipso_product_listed(&dropped));
    furi_string_reset(text);
    flipso_format_payg(text, f, card);
    check(
        "the purse screen has its technical details",
        shows(text, "\e#Technical\nType code: 2.0\n"));
    furi_string_reset(text);
    flipso_format_id(text, f, card);
    check(
        "the ID screen has technical details for each product",
        shows(text, "Type code: 16.1\n") && shows(text, "Type code: 14.0\n"));

    /* An identity document that is another product names it, and a loyalty
     * scheme's own bytes are shown as they stand. */
    for(uint8_t i = 0; i < card->product_count; i++) {
        const ItsoProduct* p = &card->products[i];
        if(p->typ != ItsoTypPeriodTicket && p->typ != ItsoTypLoyalty1) continue;
        furi_string_reset(text);
        flipso_format_product(text, f, card, p);
        if(p->typ == ItsoTypPeriodTicket) {
            check(
                "a period ticket names the ID it needs", shows(text, "Carry with it: ITSO ID\n"));
        } else {
            check("loyalty shows its owner's data", shows(text, "Owner data: 321\n"));
        }
    }

    /* Its reserved journey is in use as far as the card's chain says, and out
     * of date as far as its expiry, 31/03/2026, says: the second is the one to
     * show. Checked on 2026-09-21, after that expiry. */
    FlipsoFormat after = *f;
    after.now = 1790000000u;
    for(uint8_t i = 0; i < card->product_count; i++) {
        if(card->products[i].typ != ItsoTypReservationTicket) continue;
        furi_string_reset(text);
        flipso_format_product(text, &after, card, &card->products[i]);
        check("an expired product is not called active", shows(text, "Status: Expired\n"));
        check("and says so once", !shows(text, "Status: Active\n"));
    }
    furi_string_free(text);
}

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
    spec_review(&f, &card);

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

    /* The Space Saving IPEs of a Type 2 paper ticket, which hold their facts in
     * places a full IPE does not - a counter, a place last used, day ticks. */
    {
        static ItsoCard t2;
        const struct {
            const char* name;
            const uint8_t* pages;
            size_t len;
            const char* expect[5];
        } tickets[] = {
            {"TYP 27 day ticket",
             cmd4_pages,
             sizeof(cmd4_pages),
             {"Area: Set by the operator\n  Fare code: 0\n",
              "Ends at: Set by the operator\n",
              "Off-peak only: No\n",
              "Event 2: Tap out\n",
              "  Operator: SPT (Strathclyde)\n"}},
            {"TYP 29 return",
             cmd4_return,
             sizeof(cmd4_return),
             {"Rides left: 1\n  Backup count: 1\n  Agrees: Yes\n",
              "Last got off: Hillhead\n",
              "Price paid: \xC2\xA3"
              "3.30\n",
              "Ends at: Set by the operator\n",
              "Weekdays only: No\n"}},
            {"TYP 28 carnet",
             cmd4_carnet,
             sizeof(cmd4_carnet),
             {"Passes left: 3\n",
              "Day used: ",
              "Valid on day of issue: Yes\n",
              "Valid on day of expiry: Yes\n",
              "Ends at: 23:59 on the expiry date\n"}},
            {"TYP 29 multi-leg",
             cmd4_multileg,
             sizeof(cmd4_multileg),
             {"Rides left: 7\n",
              "  Journeys that day: 2\n",
              "Journey began: ",
              "Daily journey limit: 4\n",
              "  Changes made: 1\n"}},
            {"TYP 29 unused single",
             cmd4_unused,
             sizeof(cmd4_unused),
             {"Last used: Never\n", "Rides left: 1\n", "Area: ", "Paid by: Cash\n", "Class: "}},
            {"TYP 27 by fare value",
             cmd4_fare_value,
             sizeof(cmd4_fare_value),
             {"Area: Set by fare value\n  Fare value: \xC2\xA3"
              "1.75\n",
              "Event 1: Tap in\n",
              "Photocard number: 424242\n",
              "Passback timeout: Set by the operator\n",
              "Travellers: 1 adult\n"}},
            {"TYP 27 by location",
             cmd4_location,
             sizeof(cmd4_location),
             {"Area: Zones 1,2,3\n",
              "Photocard number: None\n",
              "Last used: Never\n",
              "Event 1: Other\n",
              "Issued: "}},
            {"TYP 28 between stations",
             cmd4_journey_area,
             sizeof(cmd4_journey_area),
             {"From: London Waterloo\n",
              "To: Station 1444\n",
              "Passes left: 6\n",
              "Valid on day of issue: No\n",
              "Ends at: 23:59 on the expiry date\n"}},
            {"TYP 29 scaled backup",
             cmd4_backup_scaled,
             sizeof(cmd4_backup_scaled),
             {"Rides left: 10\n  Backup count: Up to 12\n  Backup step: 4\n  Agrees: Yes\n",
              "Price paid: \xC2\xA3"
              "15.00\n",
              "Last used: Never\n",
              "Area: Set by the operator\n",
              "Class: Standard\n"}},
            {"TYP 29 torn backup",
             cmd4_backup_torn,
             sizeof(cmd4_backup_torn),
             {"Rides left: 1\n  Backup count: 3\n  Agrees: No\n",
              "Last used: Never\n",
              "Weekdays only: No\n",
              "Area: Set by the operator\n",
              "Issued: "}},
        };
        for(size_t i = 0; i < COUNT_OF(tickets); i++) {
            itso_card_reset(&t2);
            check("a whole CMD4 decodes", itso_parse_type2(&t2, tickets[i].pages, tickets[i].len));
            every_screen(tickets[i].name, &f, &t2);
            furi_string_reset(text);
            flipso_format_product(text, &f, &t2, &t2.products[0]);
            for(size_t e = 0; e < COUNT_OF(tickets[i].expect); e++) {
                char what[160];
                snprintf(what, sizeof(what), "%s shows %s", tickets[i].name, tickets[i].expect[e]);
                check(what, shows(text, tickets[i].expect[e]));
            }
            /* The place a ticket was last used is not the start of a journey,
             * and a product with no Sector Chain Table claims no status. Only
             * an area recorded as a journey's two ends has a From line. */
            char what[160];
            if(!t2.space.area[1].valid) {
                snprintf(what, sizeof(what), "%s has no From line", tickets[i].name);
                check(what, !shows(text, "From: "));
            }
            snprintf(what, sizeof(what), "%s claims no status", tickets[i].name);
            check(what, !shows(text, "Status: "));
        }

        /* The Card screen of a paper ticket: its implied shell is not presented
         * as the card's own data, its UID is, and its state is its product's. */
        itso_card_reset(&t2);
        itso_parse_type2(&t2, cmd4_pages, sizeof(cmd4_pages));
        furi_string_reset(text);
        flipso_format_card(text, &f, &t2, NULL, 0);
        check(
            "a paper ticket's number is with its chip, for what it is",
            shows(text, "Card number: 633597 8189 0000 0003\n(Compact ITSO Shell Ticket)\n"));
        check("and is not the screen's headline", !shows(text, "Card number\n"));
        check("a paper ticket shows its UID", shows(text, "UID: 04A2B3C4D5E6F7\n"));
        check("and its chip maker", shows(text, "Maker: NXP\n"));
        check("and its memory", shows(text, "Memory: 64 bytes\n"));
        check(
            "and which pages are locked, as ITSO requires",
            shows(text, "Locked pages: 6-13\n  As ITSO requires: Yes\n"));
        check("and that no lock bits are frozen", shows(text, "Lock bits frozen: None\n"));
        check("a paper ticket names its card type", shows(text, "Card type: Ultralight (CMD4)\n"));
        check("a compact shell says so", shows(text, "Layout: Compact shell\n"));
        check(
            "a compact shell shows no implied geometry",
            !shows(text, "sectors") && !shows(text, "Directory: ") && !shows(text, "Key set: ") &&
                !shows(text, "Update count: "));
        check("a paper ticket shows no 2041 expiry", !shows(text, "2041"));
        check("an in-date paper ticket is active", shows(text, "Status: Active\n"));
        furi_string_reset(text);
        flipso_format_card(text, &later, &t2, NULL, 0);
        check("an expired paper ticket says so", shows(text, "Status: Expired "));
        furi_string_reset(text);
        flipso_format_summary(text, &later, &t2);
        check("and its summary says so, of a ticket", shows(text, "Ticket: Expired "));
        check("with no card expiry line", !shows(text, "Card expires"));

        /* A paper ticket's Summary answers what its holder asks: is it good,
         * how much is left on it, when or where it was last used, and what it
         * cost - all from its one product, since it keeps no log. */
        furi_string_reset(text);
        flipso_format_summary(text, &f, &t2);
        check(
            "a day ticket's summary has its state, last use and price",
            shows(
                text,
                "Ticket: Active\n"
                "Paper period ticket: Until 27/09/2026\n"
                "Last used: 27/09/2026 17:47\n"
                "Price paid: \xC2\xA3"
                "4.45\n"));

        itso_card_reset(&t2);
        itso_parse_type2(&t2, cmd4_return, sizeof(cmd4_return));
        furi_string_reset(text);
        flipso_format_summary(text, &f, &t2);
        check(
            "a return's summary has its rides left and where it was last used",
            shows(
                text,
                "Ticket: Active\n"
                "Multi-use ticket: Until 26/09/2026\n"
                "  Rides left: 1\n"
                "Last used: Hillhead\n"
                "Price paid: \xC2\xA3"
                "3.30\n"));
        check("a place with no time claims no time", !shows(text, "When: "));

        itso_card_reset(&t2);
        itso_parse_type2(&t2, cmd4_multileg, sizeof(cmd4_multileg));
        furi_string_reset(text);
        flipso_format_summary(text, &f, &t2);
        check(
            "a multi-leg ticket's summary has when it was last used",
            shows(text, "  Rides left: 7\nLast used: 21/09/2026 08:20\n"));

        itso_card_reset(&t2);
        itso_parse_type2(&t2, cmd4_location, sizeof(cmd4_location));
        furi_string_reset(text);
        flipso_format_summary(text, &f, &t2);
        check("a day ticket never used says so", shows(text, "Last used: Never\n"));

        itso_card_reset(&t2);
        itso_parse_type2(&t2, cmd4_unused, sizeof(cmd4_unused));
        furi_string_reset(text);
        flipso_format_summary(text, &f, &t2);
        check("a single never used says so", shows(text, "Last used: Never\n"));
        furi_string_reset(text);
        flipso_format_card(text, &f, &t2, NULL, 0);
        check("an Infineon chip is named", shows(text, "Maker: Infineon\n"));
        check(
            "a ticket locked short of ITSO's rule says what is still writable",
            shows(text, "Locked pages: 6-9\n  As ITSO requires: No\n  Still writable: 10-13\n"));

        itso_card_reset(&t2);
        itso_parse_type2(&t2, cmd4_fare_value, sizeof(cmd4_fare_value));
        furi_string_reset(text);
        flipso_format_card(text, &f, &t2, NULL, 0);
        check("frozen lock bits are listed", shows(text, "Lock bits frozen: 3-15\n"));

        itso_card_reset(&t2);
        itso_parse_type2(&t2, cmd4_spent, sizeof(cmd4_spent));
        furi_string_reset(text);
        flipso_format_summary(text, &f, &t2);
        check("a ticket with no rides left is used up", shows(text, "Ticket: Used up\n"));

        itso_card_reset(&t2);
        itso_parse_type2(&t2, cmd4_blocked, sizeof(cmd4_blocked));
        every_screen("blocked paper ticket", &f, &t2);
        furi_string_reset(text);
        flipso_format_summary(text, &f, &t2);
        check("a zero-Seal ticket is blocked", shows(text, "Ticket: Blocked\n"));
    }
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

    /* A GWR season ticket keeps its product one day past expiry. */
    {
        ItsoProduct ticket = card.products[0];
        ticket.has_remove_date = true;
        ticket.remove_date = 1;
        furi_string_reset(text);
        flipso_format_product(text, &f, &card, &ticket);
        check("one day is not days", shows(text, "Removable: 1 day after expiry\n"));
        ticket.remove_date = 30;
        furi_string_reset(text);
        flipso_format_product(text, &f, &card, &ticket);
        check("but thirty are", shows(text, "Removable: 30 days after expiry\n"));
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
            if(strncmp(entry->d_name, "Demo 1 ", 7) == 0) demo_one(&f, &demo_card);
            if(strncmp(entry->d_name, "Demo 8", 6) == 0) demo_type2_full(&f, &demo_card, true);
            if(strncmp(entry->d_name, "Demo 9", 6) == 0) demo_type2_full(&f, &demo_card, false);
            if(strncmp(entry->d_name, "Demo 7", 6) == 0) {
                /* Judged on the day after it was read, when both tickets ran. */
                FlipsoFormat read_day = f;
                read_day.now = 1790035200u; /* 2026-09-22 */
                demo_seven(&read_day, &demo_card);
            }
            f.media = NULL;
            FlipsoFormat expired = f;
            expired.now = FLIPSO_TEST_LATER;
            snprintf(what, sizeof(what), "%s, expired", entry->d_name);
            every_screen(what, &expired, &demo_card);
            flipso_capture_free(demo);
            cards++;
        }
        if(dir) closedir(dir);
        check("all ten demo cards were rendered", cards == 10);
        check("and a saved chip block was among them", chips > 0);
    }

    printf("\n%s\n", failures ? "FAILED" : "All screen text tests passed");
    return failures ? 1 : 0;
}
