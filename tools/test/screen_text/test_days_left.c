/**
 * @file test_days_left.c
 * @brief How long is left on an expiry, and the date a period ticket's Summary goes by.
 */
#include "test_format.h"

#include <time.h>

/** "Label: dd/mm/yyyy\n" for @p date, as the screens write it. */
static const char* dated(const char* label, ItsoDate date) {
    static char line[64];
    const time_t when = (time_t)itso_date_to_unix(date);
    struct tm tm;
    gmtime_r(&when, &tm);
    snprintf(
        line,
        sizeof(line),
        "%s: %02d/%02d/%04d\n",
        label,
        tm.tm_mday,
        tm.tm_mon + 1,
        tm.tm_year + 1900);
    return line;
}

/** True when the screen has @p first with @p second on the line straight after it. */
static bool followed_by(const FuriString* text, const char* first, const char* second) {
    char want[128];
    snprintf(want, sizeof(want), "%s%s", first, second);
    return shows(text, want);
}

void days_left_screens(const FlipsoFormat* f, const ItsoCard* card, FuriString* text) {
    const ItsoProduct* period = NULL;
    for(uint8_t i = 0; i < card->product_count && !period; i++) {
        if(card->products[i].typ == ItsoTypPeriodTicket) period = &card->products[i];
    }
    check("the synthetic card has a period ticket", period != NULL);
    if(!period) return;

    /* The day f->now falls on, whatever its hour. */
    const ItsoDate today = (ItsoDate)((f->now - (itso_date_to_unix(1) - 86400)) / 86400);

    check("days left counts to the day", itso_date_days_left(today + 185, f->now) == 185);
    check("the last day has none left after it", itso_date_days_left(today, f->now) == 0);
    check("yesterday is behind us", itso_date_days_left(today - 1, f->now) == -1);
    /* Calendar days, not 24-hour spans: a minute before midnight, tomorrow is
     * still one day away, and so it is a minute after. */
    check(
        "late in the day, tomorrow is a day away",
        itso_date_days_left(today + 1, f->now + 86340) == 1);
    check(
        "and just past midnight, today is today",
        itso_date_days_left(today + 1, f->now + 86460) == 0);
    for(int32_t d = -2; d <= 2; d++) {
        const ItsoDate date = (ItsoDate)(today + d);
        check(
            "it is below zero exactly when the date has expired",
            (itso_date_days_left(date, f->now) < 0) == itso_date_expired(date, f->now));
    }

    ItsoProduct ticket = *period;
    ticket.status = ItsoProductStatusActive;
    ticket.count_kind = ItsoCountPasses;
    ticket.count = 5;
    ticket.expiry = today + 185;
    ticket.terms.ticket.has_current_expiry = true;
    ticket.terms.ticket.current_expiry = today + 12;
    ticket.terms.ticket.has_stored_expiry = true;
    ticket.terms.ticket.stored_expiry = today + 120;

    furi_string_reset(text);
    flipso_format_product(text, f, card, &ticket);
    printf("\n%s\n", furi_string_get_cstr(text));
    check(
        "the expiry says how long is left",
        followed_by(text, dated("Expires", ticket.expiry), "  Days left: 185\n"));
    check(
        "and so does the pass in use",
        followed_by(
            text,
            dated("Current pass until", ticket.terms.ticket.current_expiry),
            "  Days left: 12\n"));
    check(
        "but not the unused passes",
        !followed_by(
            text, dated("Unused passes until", ticket.terms.ticket.stored_expiry), "  Days"));

    ticket.terms.ticket.current_expiry = today + 1;
    ticket.expiry = today;
    furi_string_reset(text);
    flipso_format_product(text, f, card, &ticket);
    check(
        "a date tomorrow is a day away",
        followed_by(
            text,
            dated("Current pass until", ticket.terms.ticket.current_expiry),
            "  Days left: 1\n"));
    check(
        "and the last day says so",
        followed_by(text, dated("Expires", ticket.expiry), "  Days left: Today\n"));

    ticket.expiry = today - 1;
    ticket.terms.ticket.current_expiry = today - 3;
    furi_string_reset(text);
    flipso_format_product(text, f, card, &ticket);
    check("an expired date has its label", shows(text, dated("Expired", ticket.expiry)));
    check("and nothing counted down", !shows(text, "Days left"));

    ticket.expiry = 0;
    ticket.terms.ticket.has_current_expiry = false;
    furi_string_reset(text);
    flipso_format_product(text, f, card, &ticket);
    check(
        "no expiry has no days left",
        shows(text, "Expires: No expiry\n") && !shows(text, "Days left"));

    /* The card's own expiry, on the card screen. */
    static ItsoCard one;
    one = *card;
    one.expiry = today + 1000;
    furi_string_reset(text);
    flipso_format_card(text, f, &one, NULL, false, 0);
    check(
        "the card's expiry says how long is left",
        followed_by(text, dated("Expires", one.expiry), "  Days left: 1000\n"));
    one.expiry = 0x3FFF;
    furi_string_reset(text);
    flipso_format_card(text, f, &one, NULL, false, 0);
    check("a card that never expires counts nothing down", !shows(text, "Days left"));

    /* The Summary goes by the pass in use, and by the product once it ends. */
    one = *card;
    one.expiry = today + 1000;
    one.products = &ticket;
    one.product_count = 1;
    ticket.on_card = true;
    ticket.expiry = today + 185;
    ticket.terms.ticket.has_current_expiry = true;
    ticket.terms.ticket.current_expiry = today + 12;
    furi_string_reset(text);
    flipso_format_summary(text, f, &one);
    printf("\n%s\n", furi_string_get_cstr(text));
    char want[64];
    snprintf(
        want,
        sizeof(want),
        "Period ticket: Until %s",
        dated("", ticket.terms.ticket.current_expiry) + 2);
    check("the Summary gives the pass in use", shows(text, want));
    check("with the passes behind it", shows(text, "  Passes left: 5\n"));
    check("and does not count the days itself", !shows(text, "Days left"));

    ticket.terms.ticket.current_expiry = today - 1;
    furi_string_reset(text);
    flipso_format_summary(text, f, &one);
    snprintf(want, sizeof(want), "Period ticket: Until %s", dated("", ticket.expiry) + 2);
    check("once that pass has ended, the product's expiry", shows(text, want));

    ticket.terms.ticket.has_current_expiry = false;
    furi_string_reset(text);
    flipso_format_summary(text, f, &one);
    check("and the same with no pass in use", shows(text, want));

    ticket.terms.ticket.has_current_expiry = true;
    ticket.terms.ticket.current_expiry = today + 12;
    ticket.expiry = today - 1;
    furi_string_reset(text);
    flipso_format_summary(text, f, &one);
    snprintf(want, sizeof(want), "Period ticket: Expired %s", dated("", ticket.expiry) + 2);
    check("an expired product is expired, whatever its pass says", shows(text, want));
}
