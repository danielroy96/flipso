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
#include "format/flipso_format.h"
#include "views/flipso_text_view.h"
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

/** True when @p c is a heading's icon number rather than the start of its text. */
static bool is_icon(char c) {
    return (unsigned char)c > FLIPSO_TEXT_ICON_BASE && (unsigned char)c < '0';
}

/** Where the page titled @p title starts, at its "\e#", or NULL. */
static const char* find_page(const FuriString* text, const char* title) {
    const size_t want = strlen(title);
    for(const char* p = furi_string_get_cstr(text); p;
        p = strchr(p, '\f') ? strchr(p, '\f') + 1 : NULL) {
        if(p[0] != '\e' || p[1] != '#') continue;
        const char* t = is_icon(p[2]) ? p + 3 : p + 2;
        if(strncmp(t, title, want) == 0 && t[want] == '\n') return p;
    }
    return NULL;
}

/**
 * The lines of the page titled @p title, less its title row, or NULL when the
 * screen has no such page. A heading's icon byte is skipped, so "History"
 * finds the page whatever icon it carries. Four buffers, so a check can
 * compare a few pages at once.
 */
static const char* page_of(const FuriString* text, const char* title) {
    static char pages[4][4096];
    static unsigned next;
    const size_t want = strlen(title);
    for(const char* p = furi_string_get_cstr(text); p;
        p = strchr(p, '\f') ? strchr(p, '\f') + 1 : NULL) {
        if(p[0] != '\e' || p[1] != '#') continue;
        const char* t = p + 2;
        if(is_icon(*t)) t++;
        if(strncmp(t, title, want) != 0 || t[want] != '\n') continue;
        const char* body = t + want + 1;
        const char* end = strchr(body, '\f');
        size_t n = end ? (size_t)(end - body) : strlen(body);
        char* buf = pages[next++ % 4];
        if(n >= sizeof(pages[0])) n = sizeof(pages[0]) - 1;
        memcpy(buf, body, n);
        buf[n] = '\0';
        return buf;
    }
    return NULL;
}

/** True when the screen's pages are titled @p want, in order, as "One|Two|Three". */
static bool titles_are(const FuriString* text, const char* want) {
    char got[512] = "";
    for(const char* p = furi_string_get_cstr(text); p;
        p = strchr(p, '\f') ? strchr(p, '\f') + 1 : NULL) {
        if(p[0] != '\e' || p[1] != '#') continue;
        const char* t = p + 2;
        if(is_icon(*t)) t++;
        const char* nl = strchr(t, '\n');
        size_t n = nl ? (size_t)(nl - t) : strlen(t);
        if(got[0]) strncat(got, "|", sizeof(got) - strlen(got) - 1);
        strncat(got, t, n < sizeof(got) - strlen(got) - 1 ? n : sizeof(got) - strlen(got) - 1);
    }
    if(strcmp(got, want) != 0) printf("    pages: %s\n    want:  %s\n", got, want);
    return strcmp(got, want) == 0;
}

/** True when @p needle is on the page titled @p title. */
static bool on_page(const FuriString* text, const char* title, const char* needle) {
    const char* page = page_of(text, title);
    return page && strstr(page, needle);
}

/** True when the page titled @p title opens with @p needle. */
static bool page_starts(const FuriString* text, const char* title, const char* needle) {
    const char* page = page_of(text, title);
    return page && strncmp(page, needle, strlen(needle)) == 0;
}

/** Pages whose title carries @p icon: a journeys screen's tap pages carry the taps icon. */
static int pages_with_icon(const FuriString* text, FlipsoIcon icon) {
    char mark[4] = {'\e', '#', (char)(FLIPSO_TEXT_ICON_BASE + icon), '\0'};
    int n = 0;
    for(const char* p = furi_string_get_cstr(text); (p = strstr(p, mark)) != NULL; p++) {
        n++;
    }
    return n;
}

/** True when @p needle appears on a page before Technical. */
static bool before_technical(const FuriString* text, const char* needle) {
    const char* found = strstr(furi_string_get_cstr(text), needle);
    const char* heading = find_page(text, "Technical");
    return found && (!heading || found < heading);
}

/** True when @p needle appears, and only under the screen's Technical heading. */
static bool technical(const FuriString* text, const char* needle) {
    const char* heading = find_page(text, "Technical");
    const char* found = strstr(furi_string_get_cstr(text), needle);
    return heading && found && found > heading;
}

/* Values that are names, and so keep the case they are given: an operator's
 * brand, and a folder on the SD card. */
static bool flipso_test_is_name(const char* value) {
    return strncmp(value, "c2c", 3) == 0 || strncmp(value, "apps_data/", 10) == 0;
}

/**
 * Hold every line of @p text to the conventions, reporting the first line
 * that breaks each one. @p where names the screen for the report.
 */
static void house_style(const char* where, const FuriString* text) {
    /* The pages: each opens with its title, none is empty or ends in a blank
     * line, the codes come last, and nothing fell through to the page that
     * catches lines a product kind has nowhere for. */
    const char* all = furi_string_get_cstr(text);
    bool titled = strncmp(all, "\e#", 2) == 0, filled = true, tidy = true, last = true;
    bool iconed = true;
    char untitled[64] = "";
    for(const char* p = all; p; p = strchr(p, '\f') ? strchr(p, '\f') + 1 : NULL) {
        if(p != all && strncmp(p, "\e#", 2) != 0) titled = false;
        /* Every page's title carries an icon, as every menu row does. */
        const unsigned char icon = (unsigned char)p[2];
        if(strncmp(p, "\e#", 2) == 0 &&
           (icon <= FLIPSO_TEXT_ICON_BASE || icon >= FLIPSO_TEXT_ICON_BASE + FlipsoIconCount)) {
            if(iconed)
                snprintf(untitled, sizeof(untitled), "%.*s", (int)strcspn(p + 2, "\n"), p + 2);
            iconed = false;
        }
        const char* nl = strchr(p, '\n');
        const char* end = strchr(p, '\f');
        if(!end) end = p + strlen(p);
        if(!nl || nl + 1 >= end) filled = false;
        if(nl && nl + 1 < end &&
           (nl[1] == '\n' || (end - p >= 2 && end[-1] == '\n' && end[-2] == '\n')))
            tidy = false;
        if(p == find_page(text, "Technical") && strchr(p, '\f')) last = false;
    }
    char what_pages[320];
    snprintf(what_pages, sizeof(what_pages), "%s: every page opens with its title", where);
    check(what_pages, titled);
    snprintf(
        what_pages,
        sizeof(what_pages),
        "%s: every page's title has an icon%s%s",
        where,
        iconed ? "" : " - ",
        untitled);
    check(what_pages, iconed);
    snprintf(what_pages, sizeof(what_pages), "%s: no page is empty", where);
    check(what_pages, filled);
    snprintf(
        what_pages, sizeof(what_pages), "%s: no page starts or ends with a blank line", where);
    check(what_pages, tidy);
    snprintf(what_pages, sizeof(what_pages), "%s: Technical is the last page", where);
    check(what_pages, last);
    snprintf(what_pages, sizeof(what_pages), "%s: every line has a page of its own", where);
    check(what_pages, page_of(text, "More") == NULL);

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

        bool heading = buf[0] == '\e' || (buf[0] == '\f' && buf[1] == '\e');
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
    flipso_format_card(text, f, card, "A name", false, 1758400000u);
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
 * The encodings Demo 07 carries from a real GWR Touch card, pinned by the lines
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
        page_starts(text, "Tap in", "When: 18/09/2026 17:52\n") &&
            on_page(text, "Tap in", "Tapped in with: Unknown (24585)\n"));
    /* The stub table knows no GWR stations, so the check is the shape, not the
     * names: a dated tap out with a destination and no fare line. */
    check(
        "a check-out with no amount is still a journey",
        page_starts(text, "Tap out", "When: 18/09/2026 18:49\nFrom: ") &&
            on_page(text, "Tap out", "\nTo: ") && !shows(text, "Fare: "));
    check(
        "each record names the reader that wrote it, under Technical",
        technical(text, "Tap out\n  When: 18/09/2026 18:49\n  Reader: FF00A3C7\n"));

    furi_string_reset(text);
    flipso_format_card(text, f, card, NULL, false, 0);
    check(
        "the directory's last writer is decoded from an extended ISAM",
        technical(text, "Last updated by machine: 004E30F3\n  Operator: Unknown (24585)\n"));
    check("160-byte sectors are the layout", shows(text, "Layout: 16 sectors of 160 bytes\n"));

    /* Its ID never expires and nor does the entitlement on it, in the other
     * of the two encodings of "never": one line says so, not two. */
    furi_string_reset(text);
    flipso_format_id(text, f, card);
    check(
        "an entitlement that never ends is not a second No expiry",
        shows(text, "Expires: No expiry\n") && !shows(text, "Entitlement until: "));

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

    flipso_format_card(text, f, card, NULL, false, 0);
    check("a full-shell tag has a card number of its own", page_starts(text, "Card", "633597 "));
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
        flipso_format_card(text, f, &retired, NULL, false, 0);
        house_style("a retired CMD9's card screen", text);
        check("a retired CMD9 says so", shows(text, "Status: Retired\n"));
        check(
            "and has no uses left", shows(text, "Uses left: None, retired\n  Abacus: 16 of 16\n"));
        furi_string_reset(text);
        flipso_format_summary(text, f, &retired);
        check("its summary leads with it", shows(text, "Card: Retired\n"));
        furi_string_reset(text);
        flipso_format_card(text, f, card, NULL, false, 0);
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
    check(
        "both log records are journeys, a page each",
        card->tap_count == 2 &&
            pages_with_icon(text, FlipsoIconTaps) == 2 + (card->log_entry_valid ? 1 : 0));
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
    check(
        "a rail journey ticket was sold by a station",
        on_page(text, "Journey ticket", "Operator: South Western Railway\n") &&
            on_page(text, "Purchase", "Sold by: Station 5631\n"));
    furi_string_reset(text);
    flipso_format_product(text, f, card, &card->products[2]);
    check(
        "a period ticket's retailer-only OID is still an operator",
        shows(text, "Sold by: Unknown (57345)\n"));
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
    check("a period ticket's identity document", shows(text, "Valid only with: ID RC123456\n"));
    check("a rail period ticket was sold by a station", shows(text, "Sold by: London Waterloo\n"));
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
        shows(text, "Valid only with: ID 0102030405060708090A0B0C0D0E0F10 and 4 more bytes\n"));
    check(
        "a revision 3 period ticket's default",
        shows(text, "Expired passes at top-up: Written off\n"));

    furi_string_free(text);
}

/* Wording pinned against the demo card that carries every product type. */
/**
 * A TYP 24 reserved journey's screen: the ticket's terms from ItsoProduct, the
 * rest of its dataset and its reserved legs decoded on demand from the capture,
 * and its codes under Technical.
 */
static void reservation_screen(const FlipsoFormat* f, const ItsoCard* card) {
    FuriString* text = furi_string_alloc();
    static ItsoProduct p;

    /* The product decodes from the capture, as the screen's on-demand part
     * does: directory slot 9, where the synthetic card has nothing. */
    FlipsoCapture* capture = flipso_capture_alloc();
    flipso_capture_add(
        capture, FlipsoBlockProduct, 9, reservation_group, sizeof(reservation_group));
    FlipsoFormat with = *f;
    with.capture = capture;
    product_screen(
        text,
        &with,
        card,
        &p,
        ItsoTypReservationTicket,
        true,
        reservation_group,
        sizeof(reservation_group));
    printf("\n%s\n", furi_string_get_cstr(text));

    check(
        "a test ticket says so first",
        strncmp(furi_string_get_cstr(text), "\e#", 2) == 0 &&
            strstr(strchr(furi_string_get_cstr(text), '\n') + 1, "Test ticket: Yes\n") ==
                strchr(furi_string_get_cstr(text), '\n') + 1);
    check(
        "the railcard it is not valid without, on the first page, with the number it carries",
        on_page(
            text,
            "Reserved journey",
            "Valid only with: 16-25 Railcard\n  Railcard number: Ends 4567\nOperator: "));
    check(
        "the journeys it has left lead its details",
        page_starts(text, "Details", "Journeys left: 1\n"));
    check("a rail retailer is the station that sold it", shows(text, "Sold by: Station 5685\n"));
    check("a return of two journeys", shows(text, "Sold as: Return\n  Journeys sold: 2\n"));
    check(
        "the outward portion and its last day",
        shows(text, "Outward: 01/10/2026 to 02/10/2026\n"));
    check("the return portion", shows(text, "Return: 03/10/2026 to 02/11/2026\n"));
    check("its own start is not a second Valid from", !shows(text, "Valid from: "));
    check(
        "only the flags that are set, each on its page",
        on_page(text, "Purchase", "Duplicate: Yes\n") &&
            on_page(text, "Restrictions", "Seat reservation required: Yes\n") &&
            on_page(text, "Details", "Part-way through a leg: Yes\n") &&
            !shows(text, "Replacement: Yes"));
    check(
        "the clear ones under Technical",
        technical(
            text,
            "Replacement: No\nFollow-on renewal: No\nUnfulfilled warrant: No\nCarnet: No\n"
            "Companion allowed: No\n") &&
            !technical(text, "Test ticket: No"));
    check("rail's 511 transfers are unlimited", shows(text, "Transfers left: Unlimited\n"));
    check("the renewal window", shows(text, "Renews until: 14 days after expiry\n"));
    check("the passenger", shows(text, "Passenger: A N OTHER\n  Gender: Female\n"));
    check("the alternative origin", shows(text, "Or from: Station 0035\n"));
    check("a null alternative is left out", !shows(text, "Or to: "));
    check("the days", shows(text, "Valid days: Mon-Fri\n  Public holidays: No\n"));
    check("the restricted days", shows(text, "Restrictions apply: Sat Sun\n"));
    check("one operator only", shows(text, "Only on operator: GR\n"));
    check("the railcard IPE it was sold with", shows(text, "Part of this ticket: ITSO ID\n"));
    check("a via", shows(text, "Via: Station 1555\n"));
    check(
        "an out-of-station interchange",
        shows(
            text,
            "Change stations at: London Bridge\n  Continue from: Station 5143\n"
            "  Time allowed: 45 min\n"));
    check("break of journey, transfer type 2", shows(text, "Break of journey: Allowed\n"));
    check(
        "valid times, and the journeys they apply to",
        shows(text, "Valid times: Outside 07:00-09:30\n  Applies to: Outward departures\n"));
    check(
        "a train it may not be used on",
        shows(text, "Not valid on train: GR1234\n  From: Station 1444\n  Departs: 18:30\n"));
    check(
        "where it was sold, the retailer's station, is not said twice", !shows(text, "Sold at: "));
    check(
        "the price",
        shows(
            text,
            "Price paid: \xC2\xA3"
            "89.50\n  Paid by: "));
    check(
        "the booking reference leads the purchase",
        page_starts(text, "Purchase", "Booking reference: ABC12345\n"));
    check(
        "the last validation leads the history",
        page_starts(text, "History", "Last validated: 01/10/2026 08:02\n  At: London Waterloo\n"));
    check(
        "the outward leg, its padding gone and its window named, coach and seat first",
        page_starts(
            text,
            "Leg 1",
            "Departs: 01/10/2026 08:30\nFrom: London Waterloo\nTo: Station 1444\nCoach: C\n"
            "Seat: 42\nReserved: Seat\nFacing: Forwards\nFeature: Window\nTrain: GR1234\n"));
    check(
        "the return leg, a shared upper berth with an attribute as it stands",
        on_page(
            text,
            "Leg 2",
            "Coach: D\nBerth: 17A\nReserved: Sleeper berth\nFacing: Airline style\n"
            "Feature: ZQXV\nBunk: Upper\n  Cabin shared: Yes\n"));
    check(
        "the legs come straight after the first page",
        find_page(text, "Leg 1") && find_page(text, "Leg 1") < find_page(text, "Restrictions"));
    check("every leg read", !shows(text, "Not read: "));
    check(
        "no type code left on the screen",
        !shows(text, "Type code: 0") && !shows(text, "Type code: 1\n"));
    check("the ticket number, under Technical", technical(text, "Ticket number: 123456\n"));
    check("the fare type", technical(text, "Fare type: SOR\nRestriction code: OP\nID type: 1\n"));
    check(
        "the discount's code, rail's whole percent and its type under Technical",
        technical(
            text,
            "Discount code: YNG\n  Percentage: 33%\n  Code type: Status code\nSupplement: SLP\n"));
    check("the route code", technical(text, "Route code: 00700\n"));

    /* Sold through another station's retailer: VendorLoc says where. */
    p.retailer = 0x8000 | (1 << 10) | 72;
    furi_string_reset(text);
    flipso_format_product(text, &with, card, &p);
    check(
        "where it was sold, when the retailer is elsewhere",
        shows(text, "Sold at: Station 5685\n"));

    /* A discount that is not a card to carry has no railcard to number. */
    memcpy(p.ticket.discount, "GS3  ", sizeof(p.ticket.discount));
    furi_string_reset(text);
    flipso_format_product(text, &with, card, &p);
    check(
        "a GroupSave's ID is not a railcard",
        shows(text, "Discount: GroupSave\nRailcard or photocard number: Ends 4567\n"));
    house_style("reservation, GroupSave", text);

    /* Read again without its capture: what ItsoProduct holds still shows, and
     * nothing claims the reservations are there. */
    product_screen(
        text,
        f,
        card,
        &p,
        ItsoTypReservationTicket,
        true,
        reservation_group,
        sizeof(reservation_group));
    check("without the capture, the portions still show", shows(text, "Outward: 01/10/2026"));
    check("and the legs cannot be read", shows(text, "Reservations: Could not be read\n"));

    /* The summary carries the test flag under the product's line. */
    ItsoCard one = *card;
    one.products = &p;
    one.product_count = 1;
    furi_string_reset(text);
    flipso_format_summary(text, f, &one);
    check(
        "the summary says journeys, the railcard, and that it is a test",
        shows(text, "  Journeys left: 1\n  Valid only with: 16-25 Railcard\n  Test ticket: Yes\n"));
    house_style("reservation summary", text);

    flipso_capture_free(capture);
    furi_string_free(text);
}

static void demo_one(const FlipsoFormat* f, const ItsoCard* card) {
    FuriString* text = furi_string_alloc();

    /* Its pages, which between them have every kind of product but paper, in
     * the order the holder reads them. */
    static const struct {
        uint8_t typ;
        const char* pages;
    } kinds[] = {
        {ItsoTypPeriodTicket, "Period ticket|Passes|Conditions|Purchase|History|Technical"},
        {ItsoTypJourneyTicket, "Journey ticket|Rides|Conditions|Purchase|History|Technical"},
        {ItsoTypReservationTicket,
         "Reserved journey|Leg 1|Leg 2|Restrictions|Route|Details|Purchase|History|Technical"},
        {ItsoTypChargeToAccount2, "Charge to account|Account|History|Technical"},
        {ItsoTypVoucher, "Voucher|History|Technical"},
        {ItsoTypLoyalty1, "Loyalty|History|Technical"},
        {ItsoTypTolling, "Toll pass|Technical"},
    };
    for(size_t k = 0; k < COUNT_OF(kinds); k++) {
        for(uint8_t i = 0; i < card->product_count; i++) {
            if(card->products[i].typ != kinds[k].typ) continue;
            furi_string_reset(text);
            flipso_format_product(text, f, card, &card->products[i]);
            char what[128];
            snprintf(what, sizeof(what), "a %s has its pages in order", kinds[k].pages);
            check(what, titles_are(text, kinds[k].pages));
        }
    }
    furi_string_reset(text);
    flipso_format_payg(text, f, card);
    check("the purse's pages", titles_are(text, "Pay as you go|Top-up|History|Technical"));
    check(
        "a purse names who sold it after whose it is",
        on_page(text, "Pay as you go", "Operator: Southeastern\nSold by: National Rail purse\n"));
    {
        /* Two purses: each one's pages, and one Technical page for both, last. */
        const ItsoProduct* purse = flipso_find_product(card, ItsoTypStoredTravelRights);
        static ItsoProduct two[2];
        two[0] = two[1] = *purse;
        ItsoCard purses = *card;
        purses.products = two;
        purses.product_count = 2;
        furi_string_reset(text);
        flipso_format_payg(text, f, &purses);
        house_style("two purses", text);
        check(
            "two purses share one Technical page, last",
            titles_are(
                text, "Pay as you go|Top-up|History|Pay as you go|Top-up|History|Technical") &&
                on_page(text, "Technical", "\e#Pay as you go\nType code: 2.0\n") &&
                on_page(text, "Technical", "\n\n\e#Pay as you go\nType code: 2.0\n"));
    }
    furi_string_reset(text);
    flipso_format_id(text, f, card);
    check(
        "the ID and the entitlement, then their codes on one page",
        titles_are(text, "ITSO ID|Holder|ID terms|Entitlement|Entitlement terms|Technical") &&
            on_page(text, "Technical", "\e#ITSO ID\nType code: 16.1\n") &&
            on_page(text, "Technical", "\n\n\e#Entitlement\nType code: 14.0\n"));
    check(
        "the ID's first page is who and what the holder is",
        page_starts(text, "ITSO ID", "Name: JAMIE OKONKWO-LEE\nStatus: Active\n") &&
            on_page(text, "ITSO ID", "Operator: SEFT Central Products\n"));
    check("the holder's page", page_starts(text, "Holder", "Born: 14/05/1978\nGender: Male\n"));
    furi_string_reset(text);
    flipso_format_card(text, f, card, "Demo 01", true, 0);
    check("the card's pages, the codes last", titles_are(text, "Card|Chip|Demo card|Technical"));
    check(
        "the card page has the number, the state and the issuer",
        page_starts(
            text,
            "Card",
            "633597 0289 0100 0016\nStatus: Active\nExpires: 31/08/2031\n"
            "Operator: Southeastern\n"));
    furi_string_reset(text);
    flipso_format_summary(text, f, card);
    check("the summary's pages", titles_are(text, "Summary|Tickets|Not valid"));
    check(
        "the card and the holder lead it, with their money and their pass",
        page_starts(
            text,
            "Summary",
            "Card: Active\nCard expires: 31/08/2031\nHolder: JAMIE OKONKWO-LEE\n"
            "Pay as you go: \xC2\xA3"
            "24.15\nITSO ID: Commuter\n") &&
            on_page(text, "Summary", "Last tap: London Bridge\n"));
    check(
        "the tickets that can be used today are a page",
        page_starts(text, "Tickets", "Period ticket: Until 31/03/2027\n"));
    check(
        "and the ones that cannot, another",
        on_page(text, "Not valid", "Loyalty: Blocked\n") &&
            !on_page(text, "Tickets", "Loyalty: Blocked\n"));
    furi_string_reset(text);
    flipso_format_taps(text, f, card);
    check(
        "the journeys: the last tap, a page a journey, then the readers",
        titles_are(text, "Last tap|Tap in|Tap out|Tap out|Tap out|Technical"));

    flipso_format_summary(text, f, card);
    check("an ITSO ID is summed up by its concession", shows(text, "ITSO ID: Commuter\n"));

    furi_string_reset(text);
    flipso_format_taps(text, f, card);
    check(
        "the products a gate checked are one line",
        shows(text, "\nProducts checked: Period ticket, Pay as you go, Journey ticket\n"));
    check(
        "the last tap says where it was",
        page_starts(
            text,
            "Last tap",
            "Inside ticket gates: Yes\nWhen: 21/09/2026 17:46\nAt: London Bridge\n"));
    {
        /* The place belongs to the newest record, so it is only said when that
         * record is the entry's: not after an update that wrote none, and not
         * when the entry is newer than the record. A copy borrows the card's
         * arrays, so it is never reset. */
        ItsoCard unrecorded = *card;
        unrecorded.log_normal_mode = false;
        FuriString* other = furi_string_alloc();
        flipso_format_taps(other, f, &unrecorded);
        check(
            "an entry with no journey record names no place",
            !on_page(other, "Last tap", "At: ") &&
                on_page(other, "Last tap", "Journey details: Not recorded\n"));
        ItsoCard later = *card;
        later.log_dts += 60;
        furi_string_reset(other);
        flipso_format_taps(other, f, &later);
        check("nor one newer than the newest record", !on_page(other, "Last tap", "At: "));
        furi_string_free(other);
    }
    check(
        "a journey's page leads with its times, in the order they happened",
        strstr(
            furi_string_get_cstr(text),
            "\e#\x14Tap out\nIn: 21/09/2026 07:12\nOut: 21/09/2026 08:03\nJourney time: 51 min\n") !=
            NULL);
    check(
        "a reader names its machine, then its operator, under Technical",
        technical(text, "  Tap-in reader: 01020304\n    Operator: "));
    check(
        "and the journey it belongs to keeps only where and when",
        !before_technical(text, "Tapped in on: ") && !before_technical(text, "reader: "));
    check("passback is called passback", shows(text, "Passback timeout: 20 min\n"));
    /* The gates wrote ITSO's own IIN, in BCD as every IIN is: ITSO's network,
     * not one outside it. */
    check("a record from ITSO's own network does not say otherwise", !shows(text, "Outside ITSO"));

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
        page_starts(text, "Technical", "Type code: 2.0\n"));
    furi_string_reset(text);
    flipso_format_id(text, f, card);
    check(
        "the ID screen has technical details for each product",
        shows(text, "Type code: 16.1\n") && shows(text, "Type code: 14.0\n"));

    /* An identity document that is another product names it, a loyalty
     * scheme's own bytes are shown as they stand, and an owner numbered by
     * another network says so beside its number. */
    for(uint8_t i = 0; i < card->product_count; i++) {
        const ItsoProduct* p = &card->products[i];
        furi_string_reset(text);
        flipso_format_product(text, f, card, p);
        if(p->typ == ItsoTypPeriodTicket) {
            check(
                "a period ticket names the ID it needs, near the top",
                shows(text, "Valid only with: ITSO ID\nOperator: "));
            check(
                "its passback is an instruction to the gate, under Technical",
                technical(text, "Passback timeout: Set by the operator\n"));
        } else if(p->typ == ItsoTypLoyalty1) {
            check("loyalty shows its owner's data", shows(text, "Owner data: 321\n"));
        } else if(p->typ == ItsoTypJourneyTicket) {
            check(
                "an owner on another network is a detail of its number",
                technical(text, "Operator number: 289\n  Network: Not the card's own\n"));
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
        /* A revision 2 TYP 24, chained across five sectors, its two seats in
         * the VGXRef 3 extension after its one value record. */
        check(
            "the reserved journey is a return with a day out and a month back",
            on_page(text, "Details", "Sold as: Return\n  Journeys sold: 2\n") &&
                on_page(
                    text,
                    "Reserved journey",
                    "Outward: 13/03/2026 only\nReturn: 13/03/2026 to 12/04/2026\n"));
        check(
            "it is valid only with its railcard, on the first page",
            on_page(
                text,
                "Reserved journey",
                "Valid only with: Disabled Persons Railcard\n"
                "  Railcard number: Ends 1372\nOperator: Southeastern\n") &&
                page_starts(text, "Details", "Journeys left: 0\n") &&
                on_page(text, "Purchase", "Sold by: Station 5230\n"));
        check(
            "it names the railcard product it goes with",
            shows(text, "Part of this ticket: Entitlement\n"));
        check(
            "and its two seats, a page each",
            page_starts(text, "Purchase", "Booking reference: 8KQ2TX4M\n") &&
                page_starts(text, "Leg 1", "Departs: 13/03/2026 10:00\n") &&
                page_starts(text, "Leg 2", "Departs: 20/03/2026 14:00\n") &&
                !page_of(text, "Leg 3") && !shows(text, "Not read: "));
        check(
            "with the discount's code and percentage under Technical",
            technical(text, "Discount code: DIS\n  Percentage: 33%\n"));
        check(
            "and its seats' positions in words",
            on_page(text, "Leg 1", "Feature: Table\n") &&
                on_page(text, "Leg 2", "Feature: Aisle\n"));
    }
    furi_string_free(text);
}

/** How many times @p needle appears in @p text. */
static int occurrences_of(const FuriString* text, const char* needle) {
    int n = 0;
    for(const char* p = furi_string_get_cstr(text); (p = strstr(p, needle)) != NULL; p++) {
        n++;
    }
    return n;
}

/* A paper carnet's pages, and its summary on one page as a ticket's is. */
static void demo_fourteen(const FlipsoFormat* f, const ItsoCard* card) {
    FuriString* text = furi_string_alloc();
    flipso_format_product(text, f, card, &card->products[0]);
    check(
        "a paper ticket's pages",
        titles_are(text, "Book of tickets|Conditions|Use|Purchase|Technical"));
    check("the days a carnet was used are its use", page_starts(text, "Use", "Day used: "));
    check("its operator ends its first page", on_page(text, "Book of tickets", "Operator: SPT"));
    furi_string_reset(text);
    flipso_format_summary(text, f, card);
    check("a paper ticket's summary is one page", titles_are(text, "Summary"));
    furi_string_free(text);
}

/* A card whose saved file remembers journeys the card has dropped. */
static void demo_four(const FlipsoFormat* f, const ItsoCard* card) {
    FuriString* text = furi_string_alloc();
    flipso_format_taps(text, f, card);
    uint8_t on_card = 0, past = 0;
    for(uint8_t i = 0; i < card->tap_count; i++) {
        if(card->taps[i].on_card) {
            on_card++;
        } else {
            past++;
        }
    }
    check(
        "each journey the card holds has a page in the taps icon",
        past > 0 && pages_with_icon(text, FlipsoIconTaps) == on_card + 1);
    check(
        "and each the file remembers one in the clock",
        pages_with_icon(text, FlipsoIconPast) == past);
    /* And says so in words, as a dropped product does. */
    int said = 0;
    for(const char* p = furi_string_get_cstr(text); (p = strchr(p, '\f')) != NULL; p++) {
        if(p[1] == '\e' && p[2] == '#' && p[3] == (char)(FLIPSO_TEXT_ICON_BASE + FlipsoIconPast) &&
           strstr(p, "\nOn card: No longer\n") == strchr(p, '\n')) {
            said++;
        }
    }
    check("every journey the file remembers says it is no longer on the card", said == past);
    check("and none the card holds does", occurrences_of(text, "On card: No longer\n") == past);
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
    check(
        "earlier transactions have a page of their own",
        page_of(text, "History") && on_page(text, "History", "\n  When: "));
    check("each one says when", shows(text, "  When: "));

    furi_string_reset(text);
    flipso_format_taps(text, &f, &card);
    printf("\n%s\n", furi_string_get_cstr(text));
    check("the in/out state is where the holder is", shows(text, "Inside ticket gates: "));
    check("not a bare IN or OUT", !shows(text, ": IN\n") && !shows(text, ": OUT\n"));
    check("a tap's time is labelled as every time is", shows(text, "\nWhen: "));
    check("with no other word for it", !shows(text, "\nTime: "));
    check("a tap out says which time is which", shows(text, "\nOut: ") && shows(text, "\nIn: "));
    check("and how long the journey took", shows(text, "\nJourney time: "));
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
    reservation_screen(&f, &card);

    furi_string_reset(text);
    flipso_format_card(text, &f, &card, NULL, false, 0);
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
             {"Fare code: 0\n",
              "Ends at: Set by the operator\n",
              "Off-peak only: No\n",
              "Event 2: Tap out\n",
              "  Operator: SPT (Strathclyde)\n"}},
            {"TYP 29 return",
             cmd4_return,
             sizeof(cmd4_return),
             {"Backup count: 1\n  Agrees with rides left: Yes\n",
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
             {"Backup count: Up to 12\n  Step: 4\n  Agrees with rides left: Yes\n",
              "Price paid: \xC2\xA3"
              "15.00\n",
              "Last used: Never\n",
              "Area: Set by the operator\n",
              "Class: Standard\n"}},
            {"TYP 29 torn backup",
             cmd4_backup_torn,
             sizeof(cmd4_backup_torn),
             {"Backup count: 3\n  Agrees with rides left: No\n",
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
            /* The owner's codes and the backup's cross-check are there for
             * whoever is debugging the ticket, not for its holder. */
            {
                char what[160];
                snprintf(
                    what, sizeof(what), "%s keeps its codes under Technical", tickets[i].name);
                check(
                    what,
                    (!shows(text, "Fare code: ") || technical(text, "Fare code: ")) &&
                        (!shows(text, "Backup count: ") || technical(text, "Backup count: ")));
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
        flipso_format_card(text, &f, &t2, NULL, false, 0);
        check(
            "a paper ticket's number is under Technical, for what it is",
            technical(
                text,
                "Layout: Compact shell\n  Implied card number: 633597 8189 0000 0003\n"
                "  Shell operator number: 8189\n"));
        check(
            "and its issuer's number is with them",
            technical(text, "Operator number: 8323\n") && !shows(text, "(compact)"));
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
        flipso_format_card(text, &later, &t2, NULL, false, 0);
        check("an expired paper ticket says so", shows(text, "Status: Expired "));
        furi_string_reset(text);
        flipso_format_summary(text, &later, &t2);
        check(
            "and its summary says so once, of its ticket",
            shows(text, "\nPaper period ticket: Expired ") && !shows(text, "Ticket: Expired "));
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
        flipso_format_card(text, &f, &t2, NULL, false, 0);
        check("an Infineon chip is named", shows(text, "Maker: Infineon\n"));
        check(
            "a ticket locked short of ITSO's rule says what is still writable",
            shows(text, "Locked pages: 6-9\n  As ITSO requires: No\n  Still writable: 10-13\n"));

        itso_card_reset(&t2);
        itso_parse_type2(&t2, cmd4_fare_value, sizeof(cmd4_fare_value));
        furi_string_reset(text);
        flipso_format_card(text, &f, &t2, NULL, false, 0);
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
        check(
            "a zero-Seal ticket is blocked, said once",
            shows(text, "ticket: Blocked\n") && !shows(text, "\nTicket: Blocked\n"));
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

    /* A ticket good within a set of zones has an area, not a journey. */
    {
        static const uint8_t zones[] = {204, 3, 0x07, 0x00, 0x00};
        ItsoProduct ticket = card.products[0];
        itso_parse_location(zones, sizeof(zones), ItsoLocStructLoc1, &ticket.from);
        ticket.to.valid = false;
        furi_string_reset(text);
        flipso_format_product(text, &f, &card, &ticket);
        check(
            "a ticket's zone map is where it is valid",
            shows(text, "Valid in: Zones 1,2,3\n") && !shows(text, "From: Zones"));
    }

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

    /* A GWR season ticket keeps its product one day past expiry; RemoveDate
     * says how long any machine must wait, or that none may. */
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
        ticket.remove_date = 0;
        furi_string_reset(text);
        flipso_format_product(text, &f, &card, &ticket);
        check("and none is on expiry", shows(text, "Removable: Once expired\n"));
        ticket.remove_date = 255;
        furi_string_reset(text);
        flipso_format_product(text, &f, &card, &ticket);
        check(
            "255 is the operator's to remove, not the holder's",
            shows(text, "Removable: Only by the operator\n"));
    }

    /* About, with every table present and with none. */
    furi_string_reset(text);
    flipso_format_about(text, "1.0", 4009, 400000, 3);
    house_style("about", text);
    check(
        "about is a page to each thing it says",
        titles_are(text, "Flipso|Station names|Bus stop names|Operator names|Saved cards"));
    furi_string_reset(text);
    flipso_format_about(text, NULL, 0, 0, 0);
    house_style("about, nothing installed", text);

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
    check(
        "contents wrap between groups of bytes, a page to each file",
        on_page(text, "File 0", "Contents: DEADBEEF 01020304\n"));
    check(
        "the files' application is named with the others",
        on_page(text, "Applications", "Files read from: Oyster\n"));
    check(
        "a file the card would not describe says so",
        page_starts(text, "File 2", "Details: Locked\n"));
    check("a value file has its range", page_starts(text, "File 1", "Type: Value\nRange: "));

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
                flipso_format_card(screen, &with_chip, &demo_card, "A name", false, 0);
                snprintf(what, sizeof(what), "%s shows its saved chip", entry->d_name);
                check(what, shows(screen, "Chip: MIFARE DESFire EV1\n"));
                furi_string_free(screen);
                chips++;
            }
            if(strncmp(entry->d_name, "Demo 01", 7) == 0) {
                /* Opened from the About menu rather than from Saved cards. */
                FuriString* screen = furi_string_alloc();
                flipso_format_card(screen, &f, &demo_card, "Demo 01", true, 0);
                check(
                    "a demo card says it is one",
                    shows(screen, "Demo card\nName: Demo 01\n") && !shows(screen, "Saved card"));
                furi_string_free(screen);
            }
            f.media = &demo_media;
            every_screen(entry->d_name, &f, &demo_card);
            if(strncmp(entry->d_name, "Demo 01", 7) == 0) demo_one(&f, &demo_card);
            if(strncmp(entry->d_name, "Demo 04", 7) == 0) demo_four(&f, &demo_card);
            if(strncmp(entry->d_name, "Demo 14", 7) == 0) demo_fourteen(&f, &demo_card);
            if(strncmp(entry->d_name, "Demo 08", 7) == 0) demo_type2_full(&f, &demo_card, true);
            if(strncmp(entry->d_name, "Demo 09", 7) == 0) demo_type2_full(&f, &demo_card, false);
            if(strncmp(entry->d_name, "Demo 07", 7) == 0) {
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
        check("all fourteen demo cards were rendered", cards == 14);
        check("and a saved chip block was among them", chips > 0);
    }

    printf("\n%s\n", failures ? "FAILED" : "All screen text tests passed");
    return failures ? 1 : 0;
}
