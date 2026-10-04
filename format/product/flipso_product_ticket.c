/**
 * @file flipso_product_ticket.c
 * @brief The terms a TYP 22 period ticket, TYP 23 journey ticket or TYP 25 voucher was
 * sold on, and its price.
 */
#include "flipso_product_i.h"

/**
 * The terms a period or journey ticket was sold on: the days and times it is
 * good for, how long each pass lasts and how it renews, and who it covers.
 * What was paid for it is flipso_cat_ticket_price()'s, for the Purchase page.
 */
void flipso_cat_ticket_terms(FlipsoPages* p, const ItsoProduct* product) {
    const ItsoTicketTerms* t = itso_product_ticket(product);
    if(!t->valid) return;
    FuriString* rules = flipso_pages_at(p, FlipsoSlotRules);
    FuriString* left = flipso_pages_at(p, FlipsoSlotLeft);
    FuriString* who = flipso_pages_at(p, FlipsoSlotWho);

    char text[48];
    /* Day filters are a period ticket's; a journey ticket has none to show. */
    if(product->typ == ItsoTypPeriodTicket) {
        uint8_t days = itso_ticket_days(t->valid_days, t->flags);
        itso_format_days(days, text, sizeof(text));
        furi_string_cat_printf(rules, "Valid days: %s\n", text);
        itso_format_part_days(days, t->flags, text, sizeof(text));
        if(text[0]) furi_string_cat_printf(rules, "  Part days: %s\n", text);
        flipso_cat_flag(rules, "  ", "Public holidays", days & ITSO_DOW_SPECIAL);
        if(t->flags & ITSO_T22_OFF_PEAK_ONLY) flipso_cat_flag(rules, "", "Off-peak only", true);
    }

    /* TYP 23's mode group: how rides are counted, and what joins legs into one
     * journey (TS 1000-5 tables 35a and 35b) - one ride used per leg, one per
     * journey however many changes it takes within the limits below, none at
     * all for an ordinary single ticket, or from revision 3 journeys taken in
     * outward and return pairs under the same limits. */
    if(t->has_mode_group) {
        static const char* const modes[] = {
            "One ride per leg",
            "One ride per journey, changes included",
            "As a single ticket",
            "Return, journeys in pairs"};
        const size_t defined = product->format_rev >= 3 ? 4 : 3;
        furi_string_cat_printf(
            left, "Ticket use: %s\n", t->mode < defined ? modes[t->mode] : "Other");
        if(t->mode == ItsoJourneyModeStoredJourneys ||
           (t->mode == ItsoJourneyModeReturn && defined == 4)) {
            /* TimeLimit counts 30 second steps between the start of one leg
             * and the next. */
            furi_string_cat_printf(left, "  Changes allowed: %u\n", t->max_transfers);
            furi_string_cat_printf(
                left,
                "  Time between legs: %u min%s\n",
                t->time_limit / 2,
                (t->time_limit & 1) ? " 30 s" : "");
        }
        flipso_cat_money(left, "", "Value of a ride", &t->unit_value);
    }

    /* Below 1440 the time falls on the expiry date itself; from 1440 it is the
     * next morning, which is how a ticket covers the last buses after midnight.
     * Zero is left out: schemes use it for "the machine decides". */
    if(t->expiry_time) {
        uint16_t minutes = t->expiry_time;
        bool next_day = minutes >= 1440;
        if(next_day) minutes -= 1440;
        furi_string_cat_printf(
            rules,
            "Ends at: %02u:%02u %s\n",
            minutes / 60,
            minutes % 60,
            next_day ? "the day after expiry" : "on the expiry date");
    }

    if(t->has_pass_duration && t->pass_duration) {
        static const char* const units[] = {"day", "month", "quarter", "year"};
        const char* unit = t->duration_unit < COUNT_OF(units) ? units[t->duration_unit] : "unit";
        furi_string_cat_printf(
            left,
            "Pass length: %u %s%s\n",
            t->pass_duration,
            unit,
            t->pass_duration == 1 ? "" : "s");
    }

    /* AutoRenewQuantity1 counts passes in stored-pass mode and days otherwise
     * (rules 5 and 6 of TS 1000-5 clause 2.9.1.4). A voucher's counts uses,
     * and flipso_cat_product_details() shows it beside its auto-renew. */
    if(product->auto_renew && t->renew_quantity && product->typ != ItsoTypVoucher) {
        const bool one = t->renew_quantity == 1;
        furi_string_cat_printf(
            left,
            "Renewal adds: %u %s\n",
            t->renew_quantity,
            t->stored_passes ? (one ? "pass" : "passes") : (one ? "day" : "days"));
    }
    if(product->auto_renew && t->has_stock_duration && t->stock_duration) {
        furi_string_cat_printf(left, "  Unused passes last: %u more days\n", t->stock_duration);
    }
    /* Revision 3's TreatmentOfExpiredSP: what a top-up does with passes whose
     * stock has expired (rule 8 of TS 1000-5 clause 2.9.3.4). */
    if(product->typ == ItsoTypPeriodTicket && product->format_rev >= 3) {
        furi_string_cat_printf(
            left,
            "Expired passes at top-up: %s\n",
            (t->flags & ITSO_T22_KEEP_EXPIRED) ? "Kept" : "Written off");
    }

    if(t->adults || t->children || t->concessions) {
        furi_string_cat(who, "Travellers:");
        const char* sep = " ";
        if(t->adults) {
            furi_string_cat_printf(who, "%s%u adult%s", sep, t->adults, t->adults == 1 ? "" : "s");
            sep = ", ";
        }
        if(t->children) {
            furi_string_cat_printf(
                who, "%s%u child%s", sep, t->children, t->children == 1 ? "" : "ren");
            sep = ", ";
        }
        if(t->concessions) {
            furi_string_cat_printf(
                who, "%s%u concession%s", sep, t->concessions, t->concessions == 1 ? "" : "s");
        }
        furi_string_push_back(who, '\n');
    }

    const char* travel_class = itso_class_name(t->travel_class);
    if(travel_class) furi_string_cat_printf(who, "Class: %s\n", travel_class);
    if(product->typ == ItsoTypPeriodTicket && (t->flags & ITSO_T22_TRANSFERABLE)) {
        flipso_cat_flag(who, "", "Transferable", true);
    }
    if(t->photocard) {
        furi_string_cat_printf(who, "Photocard number: %lu\n", (unsigned long)t->photocard);
    }
}

/** When a ticket was issued and what was paid for it. */
void flipso_cat_ticket_price(FuriString* out, const ItsoProduct* product) {
    const ItsoTicketTerms* t = itso_product_ticket(product);
    if(!t->valid) return;
    if(t->issue_date) flipso_cat_date_line(out, "", "Issued", t->issue_date);
    if(t->amount_paid.valid) {
        flipso_cat_money(out, "", "Price paid", &t->amount_paid);
        if(t->paid_mop)
            furi_string_cat_printf(out, "  Paid by: %s\n", itso_payment_name(t->paid_mop));
        flipso_cat_vat(out, "  ", t->vat);
    }
}
