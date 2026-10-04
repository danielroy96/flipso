/**
 * @file test_house_style.c
 * @brief The house style every line of every screen is held to.
 */
#include "test_format.h"

#include <ctype.h>

/* Values that are names, and so keep the case they are given: an operator's
 * brand, and a folder on the SD card. */
static bool flipso_test_is_name(const char* value) {
    return strncmp(value, "c2c", 3) == 0 || strncmp(value, "apps_data/", 10) == 0;
}

/**
 * Hold every line of @p text to the conventions, reporting the first line
 * that breaks each one. @p where names the screen for the report.
 */
void house_style(const char* where, const FuriString* text) {
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
void every_screen(const char* name, const FlipsoFormat* f, const ItsoCard* card) {
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
