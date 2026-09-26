/**
 * @file flipso_format.c
 * @brief The text of every detail screen. See flipso_format.h.
 */
#include "flipso_format.h"
#include "itso/itso_operators.h"
#include "views/flipso_text_view.h"

#include <datetime/datetime.h>
#include <locale/locale.h>
#include <string.h>

/* Room for any amount itso_format_money() writes. */
#define FLIPSO_MONEY_LEN 24

/* ------------------------------------------------------------------ */
/* Dates and times                                                     */
/* ------------------------------------------------------------------ */

static void flipso_cat_datetime_struct(FuriString* out, const DateTime* dt, bool with_time) {
    FuriString* formatted = furi_string_alloc();
    locale_format_date(formatted, dt, locale_get_date_format(), "/");
    furi_string_cat(out, formatted);

    if(with_time) {
        furi_string_reset(formatted);
        locale_format_time(formatted, dt, locale_get_time_format(), false);
        furi_string_cat_printf(out, " %s", furi_string_get_cstr(formatted));
    }

    furi_string_free(formatted);
}

/** Split a Unix timestamp and append it in the user's configured date format. */
static void flipso_cat_timestamp(FuriString* out, uint32_t timestamp, bool with_time) {
    DateTime dt;
    datetime_timestamp_to_datetime(timestamp, &dt);
    flipso_cat_datetime_struct(out, &dt, with_time);
}

void flipso_cat_date(FuriString* out, uint16_t date) {
    flipso_cat_timestamp(out, itso_date_to_unix(date), false);
}

void flipso_cat_short_date(FuriString* out, uint16_t date) {
    FuriString* full = furi_string_alloc();
    flipso_cat_date(full, date);
    const char* text = furi_string_get_cstr(full);
    size_t len = strlen(text);
    /* The locale writes the year as four digits, first or last; the century
     * is the pair that goes. */
    if(len == 10 && locale_get_date_format() == LocaleDateFormatYMD) {
        furi_string_cat(out, text + 2);
    } else if(len == 10) {
        furi_string_cat_printf(out, "%.6s%s", text, text + 8);
    } else {
        furi_string_cat(out, text);
    }
    furi_string_free(full);
}

void flipso_cat_time(FuriString* out, uint32_t timestamp) {
    flipso_cat_timestamp(out, timestamp, true);
}

/** Append "dd/mm/yyyy hh:mm" for an ITSO DTS. */
static void flipso_cat_datetime(FuriString* out, uint32_t dts) {
    flipso_cat_timestamp(out, itso_dts_to_unix(dts), true);
}

/* ------------------------------------------------------------------ */
/* Lines                                                               */
/* ------------------------------------------------------------------ */

void flipso_cat_heading(FuriString* out, FlipsoIcon icon, const char* title) {
    if(icon > FlipsoIconNone && icon < FlipsoIconCount) {
        furi_string_cat_printf(out, "\e#%c%s\n", (char)(FLIPSO_TEXT_ICON_BASE + icon), title);
    } else {
        furi_string_cat_printf(out, "\e#%s\n", title);
    }
}

/** "Label: Yes" or "Label: No". */
static void flipso_cat_flag(FuriString* out, const char* indent, const char* label, bool value) {
    furi_string_cat_printf(out, "%s%s: %s\n", indent, label, value ? "Yes" : "No");
}

/** "Label: £1.23", or nothing when the amount was not decoded. */
static void flipso_cat_money(
    FuriString* out,
    const char* indent,
    const char* label,
    const ItsoMoney* money) {
    if(!money->valid) return;
    char text[FLIPSO_MONEY_LEN];
    itso_format_money(money, text, sizeof(text));
    furi_string_cat_printf(out, "%s%s: %s\n", indent, label, text);
}

/** "Label: dd/mm/yyyy". */
static void
    flipso_cat_date_line(FuriString* out, const char* indent, const char* label, uint16_t date) {
    furi_string_cat_printf(out, "%s%s: ", indent, label);
    flipso_cat_date(out, date);
    furi_string_push_back(out, '\n');
}

/** "Label: dd/mm/yyyy hh:mm" for a DTS. */
static void flipso_cat_datetime_line(
    FuriString* out,
    const char* indent,
    const char* label,
    uint32_t dts) {
    furi_string_cat_printf(out, "%s%s: ", indent, label);
    flipso_cat_datetime(out, dts);
    furi_string_push_back(out, '\n');
}

/**
 * An expiry date, whose label changes once it has passed.
 *
 * Changing the label rather than appending a marker keeps the line to what fits
 * across the screen. A stored zero is EN1545's maximum date (10/11/2041), which
 * is how schemes say "no expiry".
 */
static void flipso_cat_expiry(
    FuriString* out,
    const char* indent,
    const char* label,
    const char* past_label,
    uint16_t date,
    uint32_t now) {
    if(date == 0) {
        furi_string_cat_printf(out, "%s%s: No expiry\n", indent, label);
        return;
    }
    flipso_cat_date_line(out, indent, itso_date_expired(date, now) ? past_label : label, date);
}

/** "VAT: 20.00%", from a rate in 0.01% steps. Nothing for a rate of zero. */
static void flipso_cat_vat(FuriString* out, const char* indent, uint16_t vat) {
    if(vat) furi_string_cat_printf(out, "%sVAT: %u.%02u%%\n", indent, vat / 100, vat % 100);
}

/**
 * "Label: Southeastern", or the operator's number where it has no name.
 *
 * ITSO's operator register is not public, so an unknown number is expected
 * rather than exceptional; the number is what lets the user add it to their
 * operators file.
 */
static void flipso_cat_operator(
    FuriString* out,
    const FlipsoFormat* f,
    const char* indent,
    const char* label,
    uint16_t oid) {
    const char* name = flipso_operators_name(f->operators, oid);
    if(name) {
        furi_string_cat_printf(out, "%s%s: %s\n", indent, label, name);
    } else {
        furi_string_cat_printf(out, "%s%s: Unknown (%u)\n", indent, label, oid);
    }
}

/**
 * The operator a machine's ISAM is registered to, then the machine itself.
 *
 * TS 1000-2 annex B builds an ISAM ID from the OID of the operator it belongs
 * to, so every ISAM on a card names an operator. Zero is what a record holds
 * until a machine first writes it (TS 1000-2 clause 7.2.4.4), not operator
 * zero, so it gets nothing.
 */
static void flipso_cat_machine(
    FuriString* out,
    const FlipsoFormat* f,
    const char* indent,
    const char* label,
    uint32_t isam) {
    if(!isam) return;
    flipso_cat_operator(out, f, indent, label, itso_isam_oid(isam));
    furi_string_cat_printf(out, "%s  Machine ID: %08lX\n", indent, (unsigned long)isam);
}

/**
 * "Label: place", or nothing when the location is absent.
 *
 * Rail codes are resolved to station names and bus stop codes to stop names,
 * where the tables that hold them are on the SD card; anything else falls back
 * to the text the decoder rendered from the code itself.
 */
static void flipso_cat_location(
    FuriString* out,
    const FlipsoFormat* f,
    const char* indent,
    const char* label,
    const ItsoLocation* location) {
    if(!location->valid) return;

    const char* place = NULL;
    switch(itso_location_code_kind(location)) {
    case ItsoLocCodeNlc:
        place = flipso_stations_name(f->stations, location->code);
        break;
    case ItsoLocCodeNaptan:
        place = flipso_naptan_stop(f->naptan, location->code);
        break;
    case ItsoLocCodeAtco:
        place = flipso_naptan_atco(f->naptan, location->code);
        break;
    default:
        break;
    }

    /* LocDefType 216 is a route and a stop together (TS 1000-1 table 42c),
     * which itso_render_location() joins with an '@'. The route half is kept
     * either way; the stop half is named where the table can, and reads as a
     * stop number where it cannot. */
    const char* at = location->def_type == 216 ? strchr(location->text, '@') : NULL;
    if(at) {
        int route_len = (int)(at - location->text);
        if(place) {
            furi_string_cat_printf(
                out, "%s%s: %.*s at %s\n", indent, label, route_len, location->text, place);
        } else {
            furi_string_cat_printf(
                out, "%s%s: %.*s, stop %s\n", indent, label, route_len, location->text, at + 1);
        }
        return;
    }

    if(place && location->more) {
        /* Named, the first stop no longer carries the count the decoder's own
         * text gave it, so it is put back. */
        furi_string_cat_printf(
            out, "%s%s: %s and %u more\n", indent, label, place, location->more);
        return;
    }
    furi_string_cat_printf(out, "%s%s: %s\n", indent, label, place ? place : location->text);
}

/* ------------------------------------------------------------------ */
/* Products in general                                                 */
/* ------------------------------------------------------------------ */

const char* flipso_product_title(const ItsoProduct* product) {
    /* PTYP is a scheme-private subtype; showing the bare number next to the name
     * reads as part of the name ("ITSO ID 29"), so it lives on the detail
     * screen's technical section. */
    return itso_typ_name(product->typ);
}

FlipsoIcon flipso_product_icon(const ItsoProduct* product) {
    switch(product->typ) {
    case ItsoTypStoredTravelRights:
        return FlipsoIconPurse;

    case ItsoTypChargeToAccount1:
    case ItsoTypChargeToAccount2:
        /* Spent now and paid for later: a bill rather than a purse. */
        return FlipsoIconAccount;

    case ItsoTypId:
    case ItsoTypEntitlement:
        return FlipsoIconId;

    case ItsoTypPeriodTicket:
    case ItsoTypPeriodCompact:
        /* Bounded by dates rather than by rides, so a calendar rather than a
         * ticket: that is the distinction a holder cares about. */
        return FlipsoIconPass;

    case ItsoTypJourneyTicket:
    case ItsoTypReservationTicket:
    case ItsoTypCarnet:
    case ItsoTypMultiUse:
    case ItsoTypVoucher:
    case ItsoTypTolling:
        return FlipsoIconTicket;

    case ItsoTypLoyalty1:
    case ItsoTypLoyalty2:
        return FlipsoIconStar;

    default:
        return FlipsoIconTag;
    }
}

const char* flipso_product_tag(const ItsoProduct* product, uint32_t now) {
    /* Off the card comes first, because it is the one thing not true of the
     * card in front of the user: whether it was blocked or expired when it
     * left is the detail screen's to tell - it is history either way. */
    if(!product->on_card) return "Off card";
    if(product->status == ItsoProductStatusBlocked) return "Blocked";
    if(itso_date_expired(product->expiry, now)) return "Expired";
    if(product->status == ItsoProductStatusUnused) return "Unused";
    return NULL;
}

const ItsoProduct* flipso_find_product(const ItsoCard* card, uint8_t typ) {
    for(uint8_t i = 0; i < card->product_count; i++) {
        /* On the card only. The screens this drives - the purse, the ID - are
         * about the card as it is, and a purse the card threw away last year
         * shown under "Pay as you go" would read as the balance it holds now.
         * The product list is where those are reachable, labelled as what they
         * are. */
        if(!card->products[i].on_card) continue;
        if(card->products[i].typ == typ) return &card->products[i];
    }
    return NULL;
}

static bool flipso_product_is_identity(const ItsoProduct* product) {
    return product->typ == ItsoTypId || product->typ == ItsoTypEntitlement;
}

/** "Label: Pay as you go", naming the product in directory entry @p dir_index. */
static void flipso_cat_product_ref(
    FuriString* out,
    const ItsoCard* card,
    const char* indent,
    const char* label,
    uint8_t dir_index) {
    if(dir_index == 0) return;

    /* The card's own products come first in the array, so an entry that has
     * been freed and reused names the product in it now rather than the one a
     * saved card remembers holding it. */
    for(uint8_t i = 0; i < card->product_count; i++) {
        const ItsoProduct* product = &card->products[i];
        if(product->dir_index != dir_index) continue;
        furi_string_cat_printf(out, "%s%s: %s\n", indent, label, flipso_product_title(product));
        return;
    }

    furi_string_cat_printf(out, "%s%s: Directory slot %u\n", indent, label, dir_index);
}

/* ------------------------------------------------------------------ */
/* The parts of a product                                              */
/* ------------------------------------------------------------------ */

/** A deposit, how it was paid, and whether it comes back. */
static void flipso_cat_deposit(
    FuriString* out,
    const char* label,
    const ItsoMoney* amount,
    uint8_t mop,
    uint16_t vat,
    bool refundable) {
    flipso_cat_money(out, "", label, amount);
    if(mop) furi_string_cat_printf(out, "  Paid by: %s\n", itso_payment_name(mop));
    flipso_cat_vat(out, "  ", vat);
    furi_string_cat(
        out, refundable ? "  Refundable: Yes\n" : "  Refundable: If the operator agrees\n");
}

/**
 * The terms a period or journey ticket was sold on: the days and times it is
 * good for, how long each pass lasts, who it covers, and what was paid for it.
 */
static void flipso_cat_ticket_terms(FuriString* out, const ItsoProduct* product) {
    const ItsoTicketTerms* t = &product->ticket;
    if(!t->valid) return;

    char text[48];
    /* Day filters are a period ticket's; a journey ticket has none to show. */
    if(product->typ == ItsoTypPeriodTicket) {
        uint8_t days = itso_ticket_days(t->valid_days, t->flags);
        itso_format_days(days, text, sizeof(text));
        furi_string_cat_printf(out, "Valid days: %s\n", text);
        itso_format_part_days(days, t->flags, text, sizeof(text));
        if(text[0]) furi_string_cat_printf(out, "  Part days: %s\n", text);
        flipso_cat_flag(out, "  ", "Public holidays", days & ITSO_DOW_SPECIAL);
        if(t->flags & ITSO_T22_OFF_PEAK_ONLY) flipso_cat_flag(out, "", "Off-peak only", true);
    }

    /* TYP 23's mode group: how rides are counted, and what joins legs into one
     * journey (TS 1000-5 table 35a). */
    if(t->has_mode_group) {
        static const char* const modes[] = {"Per ride", "Per journey", "As one ticket"};
        furi_string_cat_printf(
            out, "Rides counted: %s\n", t->mode < COUNT_OF(modes) ? modes[t->mode] : "Other");
        if(t->mode == ItsoJourneyModeStoredJourneys) {
            /* TimeLimit counts 30 second steps between the start of one leg
             * and the next. */
            furi_string_cat_printf(out, "  Changes allowed: %u\n", t->max_transfers);
            furi_string_cat_printf(out, "  Time between legs: %u min\n", t->time_limit / 2);
        }
        flipso_cat_money(out, "", "Value of a ride", &t->ride_value);
    }

    /* Below 1440 the time falls on the expiry date itself; from 1440 it is the
     * next morning, which is how a ticket covers the last buses after midnight.
     * Zero is left out: schemes use it for "the machine decides". */
    if(t->expiry_time) {
        uint16_t minutes = t->expiry_time;
        bool next_day = minutes >= 1440;
        if(next_day) minutes -= 1440;
        furi_string_cat_printf(
            out, "Ends at: %02u:%02u %s\n", minutes / 60, minutes % 60,
            next_day ? "the day after expiry" : "on the expiry date");
    }

    if(t->has_pass_duration && t->pass_duration) {
        static const char* const units[] = {"day", "month", "quarter", "year"};
        const char* unit = t->duration_unit < COUNT_OF(units) ? units[t->duration_unit] : "unit";
        furi_string_cat_printf(
            out, "Pass length: %u %s%s\n", t->pass_duration, unit,
            t->pass_duration == 1 ? "" : "s");
    }

    /* AutoRenewQuantity1 counts passes in stored-pass mode and days otherwise
     * (rules 5 and 6 of TS 1000-5 clause 2.9.1.4). */
    if(product->auto_renew && t->renew_quantity) {
        const bool one = t->renew_quantity == 1;
        furi_string_cat_printf(
            out, "Renewal adds: %u %s\n", t->renew_quantity,
            product->stored_passes ? (one ? "pass" : "passes") : (one ? "day" : "days"));
    }
    if(product->auto_renew && t->has_stock_duration && t->stock_duration) {
        furi_string_cat_printf(out, "  Unused passes last: %u more days\n", t->stock_duration);
    }

    if(t->adults || t->children || t->concessions) {
        furi_string_cat(out, "Travellers:");
        const char* sep = " ";
        if(t->adults) {
            furi_string_cat_printf(out, "%s%u adult%s", sep, t->adults, t->adults == 1 ? "" : "s");
            sep = ", ";
        }
        if(t->children) {
            furi_string_cat_printf(
                out, "%s%u child%s", sep, t->children, t->children == 1 ? "" : "ren");
            sep = ", ";
        }
        if(t->concessions) {
            furi_string_cat_printf(
                out, "%s%u concession%s", sep, t->concessions, t->concessions == 1 ? "" : "s");
        }
        furi_string_push_back(out, '\n');
    }

    const char* travel_class = itso_class_name(t->travel_class);
    if(travel_class) furi_string_cat_printf(out, "Class: %s\n", travel_class);
    if(product->typ == ItsoTypPeriodTicket && (t->flags & ITSO_T22_TRANSFERABLE)) {
        flipso_cat_flag(out, "", "Transferable", true);
    }
    if(t->photocard) {
        furi_string_cat_printf(out, "Photocard number: %lu\n", (unsigned long)t->photocard);
    }

    if(t->issue_date) flipso_cat_date_line(out, "", "Issued", t->issue_date);
    if(t->amount_paid.valid) {
        flipso_cat_money(out, "", "Price paid", &t->amount_paid);
        if(t->paid_mop) furi_string_cat_printf(out, "  Paid by: %s\n", itso_payment_name(t->paid_mop));
        flipso_cat_vat(out, "  ", t->vat);
    }
}

/**
 * The parts of an ITSO ID beyond name and entitlement: issuer and holder
 * numbers, language, valid periods, fare rounding and deposits.
 */
static void flipso_cat_id_details(FuriString* out, const ItsoProduct* product) {
    if(product->typ != ItsoTypId || !product->body_parsed) return;

    /* On an English, Scottish or Welsh concessionary pass this is the pass
     * issuer - the council - by the schemes' own numbering, which is not
     * published; elsewhere it is whatever the owner uses it for. */
    if(product->has_cpicc) furi_string_cat_printf(out, "Pass issuer code: %u\n", product->cpicc);
    if(product->has_holder_id) {
        furi_string_cat_printf(out, "Holder number: %lu\n", (unsigned long)product->holder_id);
    }
    if(product->has_secondary_holder && product->secondary_holder_id) {
        furi_string_cat_printf(
            out, "Second holder number: %lu\n", (unsigned long)product->secondary_holder_id);
    }

    char code[3];
    if(itso_language_code(product->language, code)) {
        const char* name = itso_language_name(product->language);
        if(name) {
            furi_string_cat_printf(out, "Language: %s\n", name);
        } else {
            /* ISO 639-1, upper cased so it reads as a code rather than a word. */
            furi_string_cat_printf(out, "Language: %c%c\n", code[0] - 32, code[1] - 32);
        }
    }

    /* HalfDayOfWeek: two network-defined periods per day (annex A.10). A zero
     * mask selects nothing, which on a real card means the element is unused. */
    if(product->has_half_days && product->half_days) {
        char days[40];
        uint8_t mask = itso_half_days_mask(product->half_days);
        itso_format_days(mask, days, sizeof(days));
        furi_string_cat_printf(out, "Valid days: %s\n", days);
        static const char* const names[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
        for(int day = 0; day < 7; day++) {
            uint8_t pair = (product->half_days >> (14 - 2 * day)) & 0x03;
            if(pair == 0x02) furi_string_cat_printf(out, "  %s: First period only\n", names[day]);
            if(pair == 0x01) furi_string_cat_printf(out, "  %s: Second period only\n", names[day]);
        }
        if(mask & ITSO_DOW_SPECIAL) flipso_cat_flag(out, "  ", "Special days", true);
    }

    /* How a machine rounds a half or proportional fare for this holder. */
    if(product->rounding & ITSO_ROUNDING_ENABLED) {
        furi_string_cat_printf(
            out, "Fare rounding: %s to %s\n",
            (product->rounding & ITSO_ROUNDING_FLAG) ? "Up" : "Down",
            (product->rounding & ITSO_ROUNDING_VALUE) ? "5p" : "1p");
    }

    /* IDFlags bits 3, 6 and 7 (TS 1000-5 table 24). */
    if(product->has_id_flags && (product->id_flags & 0x08)) {
        furi_string_cat(out, "More details: In another app on the card\n");
    }
    if(product->has_deposit) {
        flipso_cat_deposit(
            out, "Deposit", &product->deposit, product->deposit_mop, product->deposit_vat,
            (product->id_flags & 0x40) != 0);
    }
    if(product->has_shell_deposit) {
        flipso_cat_deposit(
            out, "Card deposit", &product->shell_deposit, product->shell_deposit_mop,
            product->shell_deposit_vat, (product->id_flags & 0x80) != 0);
    }
}

/**
 * The commercial terms of a purse or charge-to-account product: its ceiling,
 * any overdraft, the auto-top-up rule and the deposit paid for it.
 */
static void flipso_cat_purse_terms(FuriString* out, const ItsoProduct* product) {
    /* An ID's deposits are its own, and flipso_cat_id_details() shows them with
     * what the card says about getting them back. */
    if(flipso_product_is_identity(product)) return;

    if(product->has_limits && product->max_value.valid && product->max_value.value) {
        flipso_cat_money(
            out, "", product->typ == ItsoTypStoredTravelRights ? "Balance limit" : "Spending limit",
            &product->max_value);
    }
    if(product->max_negative.valid && product->max_negative.value) {
        flipso_cat_money(out, "", "Overdraft limit", &product->max_negative);
    }

    if(product->has_top_up) {
        furi_string_cat_printf(out, "Auto top-up: %s\n", product->auto_top_up ? "On" : "Off");
        flipso_cat_money(out, "  ", "Amount", &product->top_up_amount);
        flipso_cat_money(out, "  ", "When below", &product->top_up_threshold);
        if(product->auto_top_up_internal) flipso_cat_flag(out, "  ", "From another purse", true);
        /* On a purse the start date gates auto-top-up rather than the product:
         * TS 1000-5 table 2 is explicit that stored travel rights may be spent
         * at any time. */
        if(product->typ == ItsoTypStoredTravelRights && product->has_start) {
            flipso_cat_date_line(out, "  ", "Not before", product->start);
        }
    }

    if(product->has_deposit) {
        flipso_cat_money(out, "", "Deposit", &product->deposit);
        if(product->deposit_mop) {
            furi_string_cat_printf(out, "  Paid by: %s\n", itso_payment_name(product->deposit_mop));
        }
        flipso_cat_vat(out, "  ", product->deposit_vat);
    }
}

/**
 * A product's fare-capping progress, decoded from the capture on demand.
 *
 * Decoded here rather than held in ItsoProduct: four locations make it the
 * largest thing a product could carry, and at most one product has one.
 */
static void
    flipso_cat_capping(FuriString* out, const FlipsoFormat* f, const ItsoCard* card, const ItsoProduct* product) {
    if(product->vgx_ref != 1 && product->vgx_ref != 2) return;
    if(!product->on_card || !f->capture) return;
    size_t len = 0;
    const uint8_t* group = flipso_capture_product_group(f->capture, product->dir_index, &len);
    if(!group) return;

    ItsoCapping* cap = malloc(sizeof(ItsoCapping));
    uint8_t valc = product->balance.valid ? product->balance.currency : 0;
    if(itso_parse_capping(group, len, card->sector_size, valc, cap)) {
        furi_string_cat(out, "\n");
        flipso_cat_heading(out, FlipsoIconNone, "Fare capping");
        bool any = false;
        static const char* const rules[] = {NULL, "Daily", "Short period", "Long period"};
        for(uint8_t a = 0; a < ITSO_CAP_ACCUMULATORS; a++) {
            const ItsoCapAccumulator* acc = &cap->acc[a];
            if(acc->rule == ItsoCapRuleNone) continue;
            any = true;
            furi_string_cat_printf(
                out, "Cap %u: %s\n", a + 1, acc->rule < COUNT_OF(rules) ? rules[acc->rule] : "Other");
            if(acc->rule == ItsoCapRuleDay) {
                flipso_cat_money(out, "  ", "Spent today", &acc->day);
            } else {
                flipso_cat_money(out, "  ", "Spent so far", &acc->multiday);
                if(acc->day_count) furi_string_cat_printf(out, "  Days in: %u\n", acc->day_count);
            }
            flipso_cat_money(out, "  ", "Without capping", &acc->uncapped);
            if(acc->last_fare.valid && acc->last_fare.value) {
                flipso_cat_money(out, "  ", "Last fare", &acc->last_fare);
            }
            if(acc->last_txn) {
                furi_string_cat_printf(
                    out, "  Last fare type: %s\n", itso_transaction_name(acc->last_txn));
            }
            if(acc->cap_dts) flipso_cat_datetime_line(out, "  ", "Last capped", acc->cap_dts);
            /* The reduced form keeps one location for all four sets, in the
             * first; the full form keeps one per set. */
            flipso_cat_location(out, f, "  ", "Capped at", &acc->location);
        }
        /* An unused product holds the structure with every rule at zero. */
        if(!any) furi_string_cat(out, "Status: Not used yet\n");
        if(cap->strategy) furi_string_cat_printf(out, "Capping rules: %u\n", cap->strategy);
    }
    free(cap);
}

/** What the product's newest value record says the last transaction was. */
static void flipso_cat_last_transaction(FuriString* out, const ItsoProduct* product) {
    if(!product->value_parsed) return;
    furi_string_cat_printf(
        out, "Last transaction: %s\n", itso_transaction_name(product->value_txn));
    if(product->value_dts) flipso_cat_datetime_line(out, "  ", "When", product->value_dts);
}

/** One transaction, as three short lines. */
static void flipso_cat_value_record(
    FuriString* out,
    const ItsoProduct* product,
    const ItsoValueRecord* record) {
    /* Three short lines rather than one wide one: a date and time is sixteen
     * characters, which leaves nothing for what happened or for what the
     * balance became. */
    furi_string_cat_printf(out, "%s\n", itso_transaction_name(record->txn));
    flipso_cat_datetime_line(out, "  ", "When", record->dts);

    if(record->amount.valid) {
        flipso_cat_money(
            out, "  ", product->balance_is_spend ? "Spent so far" : "Balance", &record->amount);
    } else if(record->has_count) {
        /* The counter means whatever the product's type says it means, and it
         * means the same thing in every record. */
        const char* label = itso_count_name(product->count_kind);
        if(label) furi_string_cat_printf(out, "  %s: %lu\n", label, (unsigned long)record->count);
    }
}

/**
 * The transactions before the live one, newest first.
 *
 * Index 0 is the live record, which the screen has already shown as the
 * balance or the counter, so a product whose group holds one written record
 * has no history to show rather than a section with one line in it. Unless
 * there is no live record: a saved card whose product group did not read still
 * has whatever records the file kept, and every one of those is earlier by
 * definition.
 */
static void flipso_cat_value_history(FuriString* out, const ItsoProduct* product) {
    uint8_t first = product->value_parsed ? 1 : 0;
    if(product->value_history_count <= first) return;

    /* Two sections rather than one. A card keeps two value records and writes
     * each new one over the oldest, so anything before them survives only
     * because a file remembered it - and running the two together would present
     * what the file knows as what the card says. A product the card has dropped
     * has already said so for the whole screen, and every record it has is from
     * a file, so it keeps one list rather than being told the same thing twice. */
    const bool split = product->on_card;

    for(uint8_t section = 0; section < (split ? 2 : 1); section++) {
        const bool on_card = (section == 0);
        bool headed = false;

        for(uint8_t i = first; i < product->value_history_count; i++) {
            const ItsoValueRecord* record = &product->value_history[i];
            if(split && record->on_card != on_card) continue;

            if(!headed) {
                furi_string_cat(out, "\n");
                flipso_cat_heading(
                    out, FlipsoIconPast, on_card ? "Earlier on card" : "Off card");
                headed = true;
            }
            flipso_cat_value_record(out, product, record);
        }
    }
}

/**
 * Everything a screen says about one product, in the order a holder asks it:
 * whose it is, what it is worth, whose it is, when it is good for, where, and
 * then the terms and the history behind that.
 *
 * Shared by the purse, ID and product screens, so a product reads the same
 * wherever it is reached from.
 */
static void flipso_cat_product_details(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product) {
    const uint32_t now = f->now;
    const bool identity = flipso_product_is_identity(product);

    /* --- Who it belongs to (TYP 14 / TYP 16). --- */
    if(product->has_name) {
        furi_string_cat_printf(out, "Name: %s\n", product->name);
    } else if(product->typ == ItsoTypId && product->body_parsed) {
        /* The name fields are optional and often left off cards that carry a
         * printed photo ID instead. */
        furi_string_cat(out, "Name: Not stored\n");
    }
    if(product->has_dob) {
        /* A Datef rather than a DATE, so it can predate the DATE epoch, and is
         * formatted from its parts rather than through a timestamp. */
        DateTime dob = {
            .day = product->dob_day, .month = product->dob_month, .year = product->dob_year};
        furi_string_cat(out, "Born: ");
        flipso_cat_datetime_struct(out, &dob, false);
        furi_string_push_back(out, '\n');
    }
    if(product->has_id_flags) {
        const char* gender = itso_gender_name(product->id_flags);
        if(gender) furi_string_cat_printf(out, "Gender: %s\n", gender);
    }

    /* --- What it is worth. --- */
    if(product->balance.valid) {
        flipso_cat_money(
            out, "", product->balance_is_spend ? "Spent so far" : "Balance", &product->balance);
    } else if(product->typ == ItsoTypStoredTravelRights) {
        furi_string_cat(out, "Balance: Not readable\n");
    }
    const char* count_label = itso_count_name(product->count_kind);
    if(count_label) {
        furi_string_cat_printf(out, "%s: %lu\n", count_label, (unsigned long)product->count);
    }
    if(product->count_kind == ItsoCountTransactions && product->has_charge_period) {
        furi_string_cat_printf(
            out, "  Allowance: %u every %u week%s\n", product->max_transactions,
            product->weeks_per_period, product->weeks_per_period == 1 ? "" : "s");
    }
    if(product->has_last_reset && product->last_reset) {
        flipso_cat_date_line(out, "  ", "Count last reset", product->last_reset);
    }

    /* --- Whose it is. --- */
    flipso_cat_operator(out, f, "", "Operator", product->oid);
    /* The retailer is only worth a row when it differs from the owner; on most
     * products the operator sells its own product and the two are the same. */
    if(product->has_retailer && product->retailer != product->oid) {
        flipso_cat_operator(out, f, "", "Sold by", product->retailer);
    }

    /* --- When it is good for. --- */
    if(product->status != ItsoProductStatusUnknown) {
        furi_string_cat_printf(out, "Status: %s\n", itso_status_name(product->status));
    }
    flipso_cat_expiry(out, "", "Expires", "Expired", product->expiry, now);

    /* A purse's start date gates auto-top-up, and is shown with it. */
    if(product->has_start && product->typ != ItsoTypStoredTravelRights) {
        furi_string_cat(out, "Valid from: ");
        flipso_cat_date(out, product->start);
        if(product->ticket.has_start_time && product->ticket.start_time) {
            furi_string_cat_printf(
                out, " %02u:%02u", product->ticket.start_time / 60,
                product->ticket.start_time % 60);
        }
        furi_string_push_back(out, '\n');
    } else if(product->ticket.valid_from_dts) {
        /* Revisions 1 and 2 of a period ticket hold a DTS here, not a DATE. */
        flipso_cat_datetime_line(out, "", "Valid from", product->ticket.valid_from_dts);
    }
    if(product->has_end_date && product->end_date != product->expiry) {
        flipso_cat_expiry(out, "", "Valid to", "Ended", product->end_date, now);
    }
    if(product->has_sub_expiry && product->sub_expiry != product->expiry) {
        flipso_cat_expiry(
            out, "", "Entitlement until", "Entitlement ended", product->sub_expiry, now);
    }
    /* The pass in use and the stock of unused passes expire separately, so a
     * season ticket can be live while the passes behind it have lapsed. */
    if(product->has_current_expiry) {
        flipso_cat_expiry(
            out, "", "Current pass until", "Current pass ended", product->current_expiry, now);
    }
    if(product->has_stored_expiry && product->stored_expiry != product->expiry) {
        const bool rides = product->typ == ItsoTypJourneyTicket;
        flipso_cat_expiry(
            out, "", rides ? "Unused rides until" : "Unused passes until",
            rides ? "Unused rides expired" : "Unused passes expired", product->stored_expiry, now);
    }

    /* --- Where. An entitlement's two locations are areas it is good in, not
     * the ends of a journey. --- */
    flipso_cat_location(out, f, "", identity ? "Valid in" : "From", &product->from);
    flipso_cat_location(out, f, "", identity ? "Also valid in" : "To", &product->to);
    /* A period ticket may leave both locations out, and then it is good wherever
     * its owner has configured that product type to be accepted - an operator's
     * whole network, typically. The card cannot say more than that, and saying
     * nothing reads as though Flipso had failed to decode them. */
    if(product->typ == ItsoTypPeriodTicket && product->ticket.valid && !product->from.valid &&
       !product->to.valid) {
        furi_string_cat(out, "Area: Set by the operator\n");
    }

    /* --- What it entitles the holder to. --- */
    if(product->has_entitlement) {
        furi_string_cat_printf(
            out, "Entitlement: %s\n", itso_entitlement_name(product->entitlement_code));
        /* Profile code zero is "unspecified", which tells the holder nothing. */
        if(product->concession_class) {
            furi_string_cat_printf(
                out, "Concession: %s\n", itso_profile_name(product->concession_class));
        }
    }
    if(product->has_id_flags) {
        /* CompanionAllowed: a companion travels at the holder's own rate with
         * no entitlement of their own (TS 1000-5 table 24). Not "free": that
         * is only what it means where the holder's rate is. */
        if(itso_id_companion(product->id_flags)) {
            furi_string_cat(out, "Companion: Travels at the same rate\n");
        }
        if(identity) flipso_cat_flag(out, "", "Photo on card", itso_id_personalised(product->id_flags));
    }
    /* PassbackTime: how long a gate refuses the same pass after it has been
     * used, so it cannot be handed back through for a second person. */
    if(product->has_passback && product->passback) {
        furi_string_cat_printf(out, "Re-use wait: %u min\n", product->passback);
    }

    /* --- Its state. --- */
    if(product->ticket_used) flipso_cat_flag(out, "", "Used", true);
    if(product->auto_renew) furi_string_cat(out, "Auto-renew: On\n");
    if(product->priority_override) flipso_cat_flag(out, "", "Used before other products", true);
    /* A journey in progress: legs taken so far and the fare accumulated across
     * them, which is what a capped or multi-leg discount is computed from. */
    if(product->has_journey && (product->journey_legs || product->cumulative_fare.value)) {
        furi_string_cat_printf(
            out, "Current journey: %u leg%s\n", product->journey_legs,
            product->journey_legs == 1 ? "" : "s");
        flipso_cat_money(out, "  ", "Fare so far", &product->cumulative_fare);
    }
    if(product->has_transfers && product->transfers) {
        furi_string_cat_printf(out, "Changes made: %u\n", product->transfers);
    }

    /* --- The terms behind it, then what has happened to it. --- */
    flipso_cat_ticket_terms(out, product);
    flipso_cat_id_details(out, product);
    flipso_cat_last_transaction(out, product);
    flipso_cat_purse_terms(out, product);
    flipso_cat_capping(out, f, card, product);

    if(product->value_group && !product->value_parsed) {
        furi_string_cat(out, "Value record: Unreadable\n");
    }
    if(product->foreign_iin) furi_string_cat(out, "Operator's network: Not ITSO\n");

    /* Last, because the terms are what the product is and the history is what
     * has happened to it: a card read more than once can carry several
     * screenfuls of the latter. */
    flipso_cat_value_history(out, product);
}

/* ------------------------------------------------------------------ */
/* What a DESFire says about itself                                    */
/* ------------------------------------------------------------------ */

static void flipso_cat_hex(FuriString* out, const uint8_t* data, size_t len) {
    for(size_t i = 0; i < len; i++) {
        furi_string_cat_printf(out, "%02X", data[i]);
    }
}

/** Two BCD digits, which is how the production date is stored. */
static uint8_t flipso_bcd(uint8_t value) {
    return (uint8_t)(((value >> 4) & 0x0F) * 10 + (value & 0x0F));
}

/**
 * The few lines of chip description worth showing beside a decoded ITSO card:
 * what chip it is, its UID, storage and when it was made.
 */
static void flipso_cat_chip_summary(FuriString* out, const FlipsoMedia* media) {
    const char* name = flipso_media_chip_name(media->chip);
    if(name) {
        furi_string_cat_printf(out, "Chip: %s\n", name);
    } else {
        /* An unrecognised generation still has a readable version number, and
         * that is what would identify it. */
        furi_string_cat_printf(
            out, "Chip: Unknown (%02X.%02X)\n", media->hw_type, media->hw_major);
    }

    bool exact = true;
    uint32_t bytes = flipso_media_storage_bytes(media->hw_storage, &exact);
    if(bytes) {
        furi_string_cat_printf(
            out, "Storage: %s%lu bytes\n", exact ? "" : "Up to ", (unsigned long)bytes);
    }
    if(media->free_memory_valid) {
        furi_string_cat_printf(out, "Free space: %lu bytes\n", (unsigned long)media->free_memory);
    }

    furi_string_cat(out, "UID: ");
    flipso_cat_hex(out, media->uid, sizeof(media->uid));
    furi_string_push_back(out, '\n');

    /* Week 0 is what a card that does not carry a date reports. */
    uint8_t week = flipso_bcd(media->prod_week);
    if(week >= 1 && week <= 53) {
        furi_string_cat_printf(
            out, "Made: Week %u of 20%02u\n", week, flipso_bcd(media->prod_year));
    }
}

static const char* flipso_file_type_name(uint8_t type) {
    switch(type) {
    case FLIPSO_FILE_STANDARD:
        return "Standard";
    case FLIPSO_FILE_BACKUP:
        return "Backup";
    case FLIPSO_FILE_VALUE:
        return "Value";
    case FLIPSO_FILE_LINEAR_RECORD:
        return "Linear record";
    case FLIPSO_FILE_CYCLIC_RECORD:
        return "Cyclic record";
    case FLIPSO_FILE_TRANSACTION:
        return "Transaction MAC";
    default:
        return "Unknown type";
    }
}

static const char* flipso_file_comm_name(uint8_t comm) {
    switch(comm) {
    case 0:
        return "None";
    case 1:
        return "Signed";
    case 3:
        return "Encrypted";
    default:
        return "Unknown";
    }
}

/** "Anyone", "Nobody" or "Key 3": who may do a thing to a file. */
static void flipso_cat_file_right(FuriString* out, const char* label, uint8_t key) {
    if(key == FLIPSO_ACCESS_FREE) {
        furi_string_cat_printf(out, "    %s: Anyone\n", label);
    } else if(key == FLIPSO_ACCESS_NEVER) {
        furi_string_cat_printf(out, "    %s: Nobody\n", label);
    } else {
        furi_string_cat_printf(out, "    %s: Key %u\n", label, key);
    }
}

/** One file, as a line naming it and its details indented under it. */
static void
    flipso_cat_media_file(FuriString* out, const FlipsoMedia* media, const FlipsoMediaFile* file) {
    if(!file->settings_valid) {
        /* The card named the file and would not describe it without a key. */
        furi_string_cat_printf(out, "File %u: Details locked\n", file->id);
        return;
    }
    furi_string_cat_printf(out, "File %u: %s\n", file->id, flipso_file_type_name(file->type));

    switch(file->type) {
    case FLIPSO_FILE_LINEAR_RECORD:
    case FLIPSO_FILE_CYCLIC_RECORD:
        furi_string_cat_printf(
            out, "  Records: %lu of %lu, %lu bytes each\n", (unsigned long)file->record.cur,
            (unsigned long)file->record.max, (unsigned long)file->record.size);
        break;
    case FLIPSO_FILE_VALUE:
        /* A value file has no length: it holds one counter, and what the card
         * will say about it without a key is the range it is kept within. */
        furi_string_cat_printf(
            out, "  Range: %ld to %ld\n", (long)(int32_t)file->value.lo_limit,
            (long)(int32_t)file->value.hi_limit);
        break;
    case FLIPSO_FILE_TRANSACTION:
        /* A transaction MAC file has no length of its own to report. */
        break;
    default:
        furi_string_cat_printf(out, "  Size: %lu bytes\n", (unsigned long)file->data.size);
        break;
    }

    furi_string_cat_printf(out, "  Encryption: %s\n", flipso_file_comm_name(file->comm));
    /* The word as the card gave it, then what it means. */
    furi_string_cat_printf(out, "  Access rights: %04X\n", file->access);
    flipso_cat_file_right(out, "Read", FLIPSO_ACCESS_READ(file->access));
    flipso_cat_file_right(out, "Write", FLIPSO_ACCESS_WRITE(file->access));

    if(file->data_len) {
        /* Four bytes to a group, so a long run wraps between groups rather than
         * in the middle of a byte. */
        furi_string_cat(out, "  Contents:");
        for(uint8_t i = 0; i < file->data_len; i += 4) {
            uint8_t run = (uint8_t)(file->data_len - i);
            if(run > 4) run = 4;
            furi_string_push_back(out, ' ');
            flipso_cat_hex(out, media->data + file->data_offset + i, run);
        }
        furi_string_push_back(out, '\n');
    } else if(flipso_media_file_free_read(file)) {
        /* The rights said anyone could read it and the read still failed, which
         * is worth distinguishing from a file that is simply locked. */
        furi_string_cat(out, "  Contents: Could not be read\n");
    } else {
        furi_string_cat(out, "  Contents: Locked\n");
    }
}

void flipso_format_media(FuriString* out, const FlipsoMedia* media) {
    flipso_cat_heading(out, FlipsoIconCard, "Chip");
    if(!media->valid) {
        furi_string_cat(out, "The card did not describe itself.\n");
        return;
    }

    flipso_cat_chip_summary(out, media);
    furi_string_cat(out, "Batch: ");
    flipso_cat_hex(out, media->batch, sizeof(media->batch));
    furi_string_push_back(out, '\n');
    /* In hex, because NXP's version numbers are codes rather than counts: the
     * EV3's hardware major version is 0x33, and printing that as 51 invites the
     * reader to make something of a number that does not mean anything. */
    furi_string_cat_printf(
        out, "Hardware: %02X.%02X\nSoftware: %02X.%02X\nVendor code: %02X\nProtocol: %02X\n",
        media->hw_major, media->hw_minor, media->sw_major, media->sw_minor, media->hw_vendor,
        media->hw_proto);

    furi_string_cat(out, "\n");
    flipso_cat_heading(out, FlipsoIconNone, "Applications");
    if(!media->app_list_valid) {
        /* A card may keep its directory behind the master key, in which case
         * all we know of is whatever we went looking for by name. */
        furi_string_cat(out, "Listed by the card: No\n");
    } else if(media->app_count == 0) {
        furi_string_cat(out, "Applications: None\n");
    }
    for(uint8_t i = 0; i < media->app_count; i++) {
        const char* name = flipso_media_app_name(media->apps[i]);
        furi_string_cat_printf(
            out, "%06lX: %s\n", (unsigned long)media->apps[i], name ? name : "Unknown");
    }
    if(media->apps_truncated) furi_string_cat(out, "More: Too many to list\n");

    if(!media->has_files) return;

    /* Named where we can: the application list above has already paired the
     * name with its number, so repeating the number here says nothing. */
    furi_string_cat(out, "\n");
    const char* app = flipso_media_app_name(media->selected_aid);
    FuriString* title = furi_string_alloc();
    if(app) {
        furi_string_printf(title, "Files in %s", app);
    } else {
        furi_string_printf(title, "Files in %06lX", (unsigned long)media->selected_aid);
    }
    flipso_cat_heading(out, FlipsoIconNone, furi_string_get_cstr(title));
    furi_string_free(title);

    if(media->file_count == 0) {
        furi_string_cat(out, "Files: None listed\n");
        return;
    }
    for(uint8_t i = 0; i < media->file_count; i++) {
        /* A blank line between files: each is a block of indented details,
         * and the gap is what shows where one ends. */
        if(i) furi_string_push_back(out, '\n');
        flipso_cat_media_file(out, media, &media->files[i]);
    }
    if(media->files_truncated) furi_string_cat(out, "\nMore files: Too many to list\n");
}

/* ------------------------------------------------------------------ */
/* Screens                                                             */
/* ------------------------------------------------------------------ */

/** The newest tap the card itself holds, or NULL. */
static const ItsoTap* flipso_latest_tap(const ItsoCard* card) {
    /* Newest first, so the first on-card record is the newest. */
    for(uint8_t i = 0; i < card->tap_count; i++) {
        if(card->taps[i].on_card) return &card->taps[i];
    }
    return NULL;
}

/** One product as a summary line: "Period ticket: Until 31/03/2027". */
static void flipso_summary_product(FuriString* out, const ItsoProduct* product, uint32_t now) {
    const char* title = flipso_product_title(product);

    if(product->status == ItsoProductStatusBlocked) {
        furi_string_cat_printf(out, "%s: Blocked\n", title);
        return;
    }
    if(product->balance.valid) {
        char money[FLIPSO_MONEY_LEN];
        itso_format_money(&product->balance, money, sizeof(money));
        furi_string_cat_printf(
            out, "%s: %s%s\n", title, money, product->balance_is_spend ? " spent" : "");
    } else if(product->has_entitlement) {
        furi_string_cat_printf(
            out, "%s: %s\n", title, itso_entitlement_name(product->entitlement_code));
    } else {
        furi_string_cat_printf(out, "%s: ", title);
        if(product->expiry == 0) {
            furi_string_cat(out, "No expiry");
        } else {
            furi_string_cat(out, itso_date_expired(product->expiry, now) ? "Expired " : "Until ");
            flipso_cat_date(out, product->expiry);
        }
        furi_string_push_back(out, '\n');
    }

    /* Whatever the product counts down is the other half of what it is worth. */
    const char* count_label = itso_count_name(product->count_kind);
    if(count_label) {
        furi_string_cat_printf(out, "  %s: %lu\n", count_label, (unsigned long)product->count);
    }
    if(product->balance.valid || product->has_entitlement) {
        if(product->expiry && itso_date_expired(product->expiry, now)) {
            flipso_cat_date_line(out, "  ", "Expired", product->expiry);
        }
    }
}

void flipso_format_summary(FuriString* out, const FlipsoFormat* f, const ItsoCard* card) {
    flipso_cat_heading(out, FlipsoIconInfo, "Summary");

    /* The card's own state first: a blocked or expired card is the headline,
     * whatever its products say. */
    const bool expired = card->expiry && itso_date_expired(card->expiry, f->now);
    if(card->shell_blocked) {
        furi_string_cat(out, "Card: Blocked by its issuer\n");
    } else if(expired) {
        /* One label and a value that says both things, as a product's summary
         * line does: "Card: Expired 30/06/2030". */
        furi_string_cat(out, "Card: Expired ");
        flipso_cat_date(out, card->expiry);
        furi_string_push_back(out, '\n');
    } else {
        if(card->dir_valid) furi_string_cat(out, "Card: Active\n");
        flipso_cat_expiry(out, "", "Card expires", "Card expired", card->expiry, f->now);
    }

    for(uint8_t i = 0; i < card->product_count; i++) {
        const ItsoProduct* product = &card->products[i];
        if(product->on_card && product->has_name) {
            furi_string_cat_printf(out, "Holder: %s\n", product->name);
            break;
        }
    }

    uint8_t shown = 0, past = 0;
    for(uint8_t i = 0; i < card->product_count; i++) {
        const ItsoProduct* product = &card->products[i];
        if(!product->on_card) {
            past++;
            continue;
        }
        flipso_summary_product(out, product, f->now);
        shown++;
    }
    if(card->dir_valid && !shown) furi_string_cat(out, "Products: None\n");
    if(!card->dir_valid) furi_string_cat(out, "Products: Could not be read\n");

    const ItsoTap* tap = flipso_latest_tap(card);
    if(tap) {
        /* Where it ended, if it was a tap out; where it began otherwise - and
         * what it was, where the card does not say where. The time goes on a
         * line of its own, which is the only way a date and time fit beside a
         * label. */
        const ItsoLocation* where = tap->destination.valid ? &tap->destination : &tap->origin;
        if(where->valid) {
            flipso_cat_location(out, f, "", "Last tap", where);
        } else {
            furi_string_cat_printf(
                out, "Last tap: %s\n", itso_transaction_name(tap->transaction_type));
        }
        flipso_cat_datetime_line(out, "  ", "When", tap->dts);
    }

    if(past) {
        furi_string_cat_printf(out, "Products off card: %u\n", past);
    }
}

void flipso_format_card(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const char* saved_name,
    uint32_t read_at) {
    /* The blocking indicator is a property of the whole shell, so it comes
     * before anything else on the screen: once it is set a machine rejects the
     * card, however valid the products further down still look.
     * TS 1000-2 clause 5.1.2. */
    if(card->shell_blocked) {
        flipso_cat_heading(out, FlipsoIconWarning, "Blocked");
        furi_string_cat(
            out,
            "This card has been stopped by its issuer. Readers will reject it, "
            "even where the products on it are still in date.\n\n");
    }

    flipso_cat_heading(out, FlipsoIconCard, "Card number");
    /* The ISRN prints as issuer, operator, then serial: 633597 1234 0012 3458. */
    static const uint8_t groups[] = {6, 4, 4, 4};
    size_t pos = 0;
    for(size_t g = 0; g < COUNT_OF(groups); g++) {
        if(g) furi_string_push_back(out, ' ');
        for(uint8_t i = 0; i < groups[g] && card->isrn[pos]; i++) {
            furi_string_push_back(out, card->isrn[pos++]);
        }
    }
    furi_string_push_back(out, '\n');
    if(!card->isrn_check_ok) furi_string_cat(out, "Check digit: Does not match\n");

    furi_string_cat(out, "\n");
    flipso_cat_heading(out, FlipsoIconPass, "Validity");
    flipso_cat_expiry(out, "", "Expires", "Expired", card->expiry, f->now);
    /* The good case is stated rather than left to silence, because nothing else
     * on the screen separates a card the issuer is happy with from one whose
     * directory was never read. */
    if(card->shell_blocked) {
        furi_string_cat(out, "Status: Blocked\n");
    } else if(card->expiry && itso_date_expired(card->expiry, f->now)) {
        furi_string_cat(out, "Status: Expired\n");
    } else if(card->dir_valid) {
        furi_string_cat(out, "Status: Active\n");
    }

    furi_string_cat(out, "\n");
    flipso_cat_heading(out, FlipsoIconNone, "Issuer");
    /* The shell owner is the operator that issued the card and so the one that
     * brands it. Its number is shown whether or not it has a name, because the
     * number is what a user needs to add their card to the operators file - on
     * a line of its own for a named one, and in the "Unknown (1234)" that
     * stands in for the name otherwise. */
    flipso_cat_operator(out, f, "", "Operator", card->oid);
    if(flipso_operators_name(f->operators, card->oid)) {
        furi_string_cat_printf(out, "Operator number: %u\n", card->oid);
    }
    /* Every ITSO shell carries ITSO's own issuer number, so this only earns a
     * line when it is something else. */
    if(!itso_iin_name(card->iin)) furi_string_cat_printf(out, "Network: %06lu\n", (unsigned long)card->iin);
    if(card->mcrn_present && card->mcrn[0]) {
        furi_string_cat_printf(out, "Card reference: %s\n", card->mcrn);
    }
    /* The directory is rewritten by every transaction, so the machine that
     * last sealed it is the last one to change anything on the card
     * (TS 1000-2 table 8, annex B). */
    if(card->dir_instance_valid) flipso_cat_machine(out, f, "", "Last updated by", card->dir_isam);

    /* What the chip says about itself, which only a live DESFire read asks. */
    if(f->media && f->media->valid) {
        furi_string_cat(out, "\n");
        flipso_cat_heading(out, FlipsoIconNone, "Chip");
        flipso_cat_chip_summary(out, f->media);
    }

    furi_string_cat(out, "\n");
    flipso_cat_heading(out, FlipsoIconNone, "Technical");
    /* FVC is the number of the customer media definition the shell follows. */
    switch(card->fvc) {
    case 2:
        furi_string_cat(out, "Card type: Smartcard (CMD2)\n");
        break;
    case 7:
        furi_string_cat(out, "Card type: DESFire (CMD7)\n");
        break;
    case 12:
        furi_string_cat(out, "Card type: DESFire (CMD12)\n");
        break;
    default:
        furi_string_cat_printf(out, "Card type: CMD%u\n", card->fvc);
        break;
    }
    furi_string_cat_printf(out, "Layout version: %u\n", card->format_rev);
    /* The shell's own checksum, which is the only thing on the card Flipso can
     * actually verify: the data groups are sealed with keys it does not have,
     * so everything else on these screens is reported on the card's word. Both
     * outcomes are stated, and a mismatch shows its numbers, because the useful
     * thing to do with one is to report the card. TS 1000-2 clause 4.1.15. */
    if(card->secrc_checked) {
        if(card->secrc_valid) {
            furi_string_cat(out, "Checksum: Correct\n");
        } else {
            furi_string_cat_printf(
                out, "Checksum: Wrong\n  Stored: %04X\n  Worked out: %04X\n", card->secrc_stored,
                card->secrc_computed);
        }
    }
    furi_string_cat_printf(out, "Key set: %u, version %u\n", card->ksc, card->kvc);
    furi_string_cat_printf(
        out, "Layout: %u sectors of %u bytes\n", card->sector_count, card->sector_size);
    furi_string_cat_printf(out, "Directory: %u slots\n", card->dir_entries);
    /* The directory sequence number counts every change made to the shell,
     * modulo 256; it is also how a CMD2 card's two directory copies are told
     * apart. */
    if(card->dir_valid) {
        furi_string_cat_printf(out, "Update count: %u\n", card->dir_sequence);
        if(card->dir_instance_valid && card->shell_iteration) {
            /* INS#: bumped to bring a stopped card back into use. */
            furi_string_cat_printf(out, "Times reinstated: %u\n", card->shell_iteration);
        }
    }

    /* Where this came from, for a card opened off the SD card. The read time
     * matters more than it looks: a balance is only true as of the tap that
     * wrote it, and a saved card carries no hint of its own age otherwise. */
    if(saved_name) {
        furi_string_cat(out, "\n");
        flipso_cat_heading(out, FlipsoIconSave, "Saved card");
        furi_string_cat_printf(out, "Name: %s\n", saved_name);
        if(read_at) {
            furi_string_cat(out, "Read: ");
            flipso_cat_time(out, read_at);
            furi_string_push_back(out, '\n');
        }
    }
}

void flipso_format_payg(FuriString* out, const FlipsoFormat* f, const ItsoCard* card) {
    uint8_t found = 0;
    for(uint8_t i = 0; i < card->product_count; i++) {
        const ItsoProduct* product = &card->products[i];
        /* What the card holds now; a product it has dropped is the product
         * list's to show. */
        if(!product->on_card || product->typ != ItsoTypStoredTravelRights) continue;
        if(found++) furi_string_cat(out, "\n");
        flipso_cat_heading(out, FlipsoIconPurse, "Pay as you go");
        flipso_cat_product_details(out, f, card, product);
    }
    if(!found) {
        flipso_cat_heading(out, FlipsoIconPurse, "Pay as you go");
        furi_string_cat(out, "No purse on this card.\n");
    }
}

void flipso_format_id(FuriString* out, const FlipsoFormat* f, const ItsoCard* card) {
    uint8_t found = 0;
    for(uint8_t i = 0; i < card->product_count; i++) {
        const ItsoProduct* product = &card->products[i];
        if(!product->on_card || !flipso_product_is_identity(product)) continue;
        if(found++) furi_string_cat(out, "\n");
        flipso_cat_heading(out, FlipsoIconId, flipso_product_title(product));
        flipso_cat_product_details(out, f, card, product);
    }
    if(!found) {
        flipso_cat_heading(out, FlipsoIconId, "ID");
        furi_string_cat(out, "No identity product on this card.\n");
    }
}

/** One Transient Ticket Record. */
static void flipso_cat_tap(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoTap* tap) {
    furi_string_cat_printf(
        out, "%s%s\n", itso_transaction_name(tap->transaction_type),
        tap->latest ? " (latest)" : "");
    /* A tap-out record carries the tap-in it closes, copied forward so the
     * record stands on its own - so a tap out has two times, and says which is
     * which, and how long the journey took between them. Any other record has
     * one, labelled "When" as every other time on these screens is. Short
     * labels, so an indented date and time still fits on one row. */
    flipso_cat_datetime_line(out, "  ", tap->has_entry ? "Out" : "When", tap->dts);
    if(tap->has_entry) {
        flipso_cat_datetime_line(out, "  ", "In", tap->entry_dts);
        uint32_t in = itso_dts_to_unix(tap->entry_dts);
        uint32_t at = itso_dts_to_unix(tap->dts);
        if(at > in && at - in < 86400) {
            uint32_t minutes = (at - in) / 60;
            if(minutes >= 60) {
                furi_string_cat_printf(
                    out, "  Journey time: %lu hr %lu min\n", (unsigned long)(minutes / 60),
                    (unsigned long)(minutes % 60));
            } else {
                furi_string_cat_printf(out, "  Journey time: %lu min\n", (unsigned long)minutes);
            }
        }
    }
    if(tap->has_entry_oid) flipso_cat_operator(out, f, "  ", "Tapped in with", tap->entry_oid);
    if(tap->has_entry_oid && tap->entry_iin_index) {
        furi_string_cat(out, "  Tapped in on: Another network\n");
    }
    if(tap->has_entry) flipso_cat_machine(out, f, "  ", "Tap-in reader", tap->entry_isam);

    flipso_cat_location(out, f, "  ", "From", &tap->origin);
    flipso_cat_location(out, f, "  ", "Via", &tap->route);
    flipso_cat_location(out, f, "  ", "To", &tap->destination);

    if(tap->amount.valid && tap->amount.value) {
        flipso_cat_money(out, "  ", "Fare", &tap->amount);
        if(tap->has_mop) furi_string_cat_printf(out, "  Paid by: %s\n", itso_payment_name(tap->mop));
    }
    if(tap->no_fare_charged) flipso_cat_flag(out, "  ", "Fare collected", false);
    if(tap->return_ticket) flipso_cat_flag(out, "  ", "Return fare", true);
    if(tap->companion) flipso_cat_flag(out, "  ", "With a companion", true);
    if(tap->has_vat) flipso_cat_vat(out, "  ", tap->vat);

    if(tap->has_ipe_pointer) flipso_cat_product_ref(out, card, "  ", "Product", tap->ipe_pointer);

    /* The network the machine that wrote it belongs to, where that is not
     * ITSO's own. */
    if(tap->has_iin && !itso_iin_name(tap->iin)) {
        furi_string_cat_printf(out, "  Network: %06lu\n", (unsigned long)tap->iin);
    }
    /* The record's own InstanceID: whose reader wrote this tap. */
    if(tap->has_writer) flipso_cat_machine(out, f, "  ", "Reader", tap->writer_isam);

    /* Flags an inspector or a gate set against this journey. */
    if(tap->invalid_travel) furi_string_cat(out, "  Invalid travel: Flagged\n");
    if(tap->inspected) flipso_cat_flag(out, "  ", "Ticket inspected", true);

    /* The products the gate weighed up for this journey: useful when the one it
     * picked is not the one you expected. */
    if(tap->has_cipe) {
        for(uint8_t c = 0; c < 4; c++) {
            if(tap->cipe[c]) flipso_cat_product_ref(out, card, "  ", "Considered", tap->cipe[c]);
        }
    }
}

void flipso_format_taps(FuriString* out, const FlipsoFormat* f, const ItsoCard* card) {
    if(card->log_entry_valid) {
        flipso_cat_heading(out, FlipsoIconTaps, "Last tap");

        /* The entry/exit indicator counts closed systems - gated stations - the
         * holder is inside. Zero is outside all of them, which is also what
         * every bus tap leaves, so it is said as where the holder is rather than
         * as "tapped out". */
        flipso_cat_flag(out, "", "Inside ticket gates", card->log_eei != 0);
        if(card->log_eei > 1) furi_string_cat_printf(out, "  Gated areas: %u\n", card->log_eei);
        if(card->log_dts) flipso_cat_datetime_line(out, "", "When", card->log_dts);
        flipso_cat_product_ref(out, card, "", "Product", card->log_ptr);
        if(card->log_passback) {
            furi_string_cat_printf(out, "Re-use wait: %u min\n", card->log_passback);
        }
        /* LPF clear: the machine updated this entry and wrote no journey record. */
        if(!card->log_normal_mode) furi_string_cat(out, "Journey record: Not kept\n");
    }

    /* The card's own log first, then whatever only a saved file remembers. */
    uint8_t on_card = 0, past = 0;
    for(uint8_t i = 0; i < card->tap_count; i++) {
        if(card->taps[i].on_card) {
            on_card++;
        } else {
            past++;
        }
    }

    for(uint8_t section = 0; section < 2; section++) {
        const bool live = section == 0;
        if((live ? on_card : past) == 0) continue;
        if(!furi_string_empty(out)) furi_string_cat(out, "\n");
        flipso_cat_heading(
            out, live ? FlipsoIconTaps : FlipsoIconPast, live ? "Journey log" : "Off card");
        bool first = true;
        for(uint8_t i = 0; i < card->tap_count; i++) {
            const ItsoTap* tap = &card->taps[i];
            if(tap->on_card != live) continue;
            if(!first) furi_string_push_back(out, '\n');
            first = false;
            flipso_cat_tap(out, f, card, tap);
        }
    }

    if(card->tap_count == 0) {
        if(!card->log_entry_valid) {
            flipso_cat_heading(out, FlipsoIconTaps, "Journeys");
            furi_string_cat(out, "No journey log on this card.\n");
        } else {
            furi_string_cat(out, "\n");
            flipso_cat_heading(out, FlipsoIconTaps, "Journey log");
            furi_string_cat(out, "No journeys stored.\n");
        }
    }
}

void flipso_format_product(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product) {
    flipso_cat_heading(
        out, product->on_card ? flipso_product_icon(product) : FlipsoIconPast,
        flipso_product_title(product));

    /* Before anything the product says about itself, because everything below
     * is written in the present tense and for this one it is not true any more:
     * the card listed it when the record was saved and does not list it now. */
    if(!product->on_card) {
        furi_string_cat(out, "On card: No longer\n");
        if(product->last_seen) {
            furi_string_cat(out, "  Last seen: ");
            flipso_cat_time(out, product->last_seen);
            furi_string_push_back(out, '\n');
        }
    }

    flipso_cat_product_details(out, f, card, product);

    furi_string_cat(out, "\n");
    flipso_cat_heading(out, FlipsoIconNone, "Technical");
    furi_string_cat_printf(out, "Type code: %u.%u\n", product->typ, product->ptyp);
    furi_string_cat_printf(out, "Operator number: %u\n", product->oid);
    if(product->oid_extended) {
        furi_string_cat_printf(
            out, "  Extended range: Yes (%u)\n", (unsigned)(product->oid & 0x1FFF));
    }
    /* Which may belong to something else by now: slots are reused. */
    furi_string_cat_printf(
        out, "Directory slot: %u%s\n", product->dir_index, product->on_card ? "" : " (then)");

    if(!product->body_parsed) {
        /* Either the sector read failed or this is a type Flipso reports from
         * the directory entry alone. */
        furi_string_cat(out, "Details: Not decoded\n");
        return;
    }

    furi_string_cat_printf(out, "Layout version: %u\n", product->format_rev);
    /* The bitmap says which optional elements the dataset carries, which is the
     * first thing you need when a field is missing unexpectedly. */
    furi_string_cat_printf(out, "Optional fields: 0x%02X\n", product->bitmap);
    if(product->has_remove_date) {
        /* 255 is the documented "only the product owner may remove this". */
        if(product->remove_date == 255) {
            furi_string_cat(out, "Removable: By the owner only\n");
        } else {
            furi_string_cat_printf(
                out, "Removable: %u days after expiry\n", product->remove_date);
        }
    }
    /* Owner-defined codes: meaningless without the scheme's own tables, but they
     * are what tells two otherwise identical tickets apart. An ID's CPICC is its
     * issuer and is shown with the holder details. */
    if(product->has_cpicc && product->typ != ItsoTypId) {
        furi_string_cat_printf(out, "Issuer code: %u\n", product->cpicc);
    }
    if(product->ticket.validity_code) {
        furi_string_cat_printf(out, "Validity code: %u\n", product->ticket.validity_code);
    }
    if(product->ticket.promotion_code) {
        furi_string_cat_printf(out, "Promotion code: %u\n", product->ticket.promotion_code);
    }
    if(product->has_iin) {
        const char* network = itso_iin_name(product->iin);
        if(network) {
            furi_string_cat_printf(out, "Owner network: %s\n", network);
        } else {
            furi_string_cat_printf(out, "Owner network: %06lu\n", (unsigned long)product->iin);
        }
    }

    /* The instance identity. Nothing else in the shell distinguishes one copy of
     * a product from another, so this is what a scheme would quote back when
     * asked about this particular ticket. */
    if(product->instance_valid) {
        flipso_cat_machine(out, f, "", "Created by", product->isam_id);
        furi_string_cat_printf(out, "  Sequence: %lu\n", (unsigned long)product->isam_seq);
        if(product->iteration) furi_string_cat_printf(out, "Times reinstated: %u\n", product->iteration);
        furi_string_cat_printf(out, "Seal key version: %u\n", product->key_id);
    }

    if(product->value_parsed) {
        furi_string_cat_printf(out, "Times updated: %u\n", product->value_ts);
        flipso_cat_machine(out, f, "", "Last updated by", product->value_isam);
        if(product->value_action_seq) {
            furi_string_cat_printf(out, "Action number: %u\n", product->value_action_seq);
        }
    }
}
