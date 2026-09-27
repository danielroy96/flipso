/**
 * @file flipso_format_product.c
 * @brief What a screen says about one product: the product screen, the purse and the ID.
 */
#include "flipso_format_i.h"

static bool flipso_product_is_identity(const ItsoProduct* product) {
    return product->typ == ItsoTypId || product->typ == ItsoTypEntitlement;
}

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
     * journey (TS 1000-5 table 35a) - one ride used per leg, one per journey
     * however many changes it takes within the limits below, or none at all
     * for an ordinary single ticket. */
    if(t->has_mode_group) {
        static const char* const modes[] = {
            "One ride per leg", "One ride per journey, changes included", "As a single ticket"};
        furi_string_cat_printf(
            out, "Ticket use: %s\n", t->mode < COUNT_OF(modes) ? modes[t->mode] : "Other");
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
            out,
            "Ends at: %02u:%02u %s\n",
            minutes / 60,
            minutes % 60,
            next_day ? "the day after expiry" : "on the expiry date");
    }

    if(t->has_pass_duration && t->pass_duration) {
        static const char* const units[] = {"day", "month", "quarter", "year"};
        const char* unit = t->duration_unit < COUNT_OF(units) ? units[t->duration_unit] : "unit";
        furi_string_cat_printf(
            out,
            "Pass length: %u %s%s\n",
            t->pass_duration,
            unit,
            t->pass_duration == 1 ? "" : "s");
    }

    /* AutoRenewQuantity1 counts passes in stored-pass mode and days otherwise
     * (rules 5 and 6 of TS 1000-5 clause 2.9.1.4). */
    if(product->auto_renew && t->renew_quantity) {
        const bool one = t->renew_quantity == 1;
        furi_string_cat_printf(
            out,
            "Renewal adds: %u %s\n",
            t->renew_quantity,
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
        if(t->paid_mop)
            furi_string_cat_printf(out, "  Paid by: %s\n", itso_payment_name(t->paid_mop));
        flipso_cat_vat(out, "  ", t->vat);
    }
}

/**
 * Where a Space Saving IPE is good (TS 1000-5 tables 50, 53 and 57). A reference
 * fare code is the owner's own, so it says the operator decides rather than
 * what the operator decided - even code 0, which SPT's whole-network Subway
 * tickets carry but another scheme may give its innermost zone.
 */
static void flipso_cat_space_area(FuriString* out, const ItsoCard* card) {
    const ItsoSpaceSaving* ss = &card->space;
    switch((ItsoAreaKind)ss->area_kind) {
    case ItsoAreaFareCode:
        furi_string_cat(out, "Area: Set by the operator\n");
        furi_string_cat_printf(out, "  Fare code: %lu\n", (unsigned long)ss->area_value);
        break;
    case ItsoAreaFareValue: {
        furi_string_cat(out, "Area: Set by fare value\n");
        ItsoMoney fare = {
            .value = (int32_t)ss->area_value, .currency = ss->euro ? 1 : 0, .valid = true};
        flipso_cat_money(out, "  ", "Fare value", &fare);
        break;
    }
    case ItsoAreaLocation:
        furi_string_cat(out, "Area: A location, not decoded\n");
        furi_string_cat_printf(out, "  Location type: %lu\n", (unsigned long)ss->area_value);
        break;
    }
}

void flipso_cat_last_use(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product,
    const char* place_label) {
    if(!product->space_saving) return;
    const ItsoSpaceSaving* ss = &card->space;

    /* TYP 29 revision 1 records one place and no time: where the holder last
     * got on, or last got off. On an SPT Subway ticket that is the station the
     * gate is in. An unwritten one means the ticket has not been through a gate. */
    if(product->typ == ItsoTypMultiUse && product->format_rev == 1) {
        if(!product->from.valid) {
            furi_string_cat(out, "Last used: Never\n");
        } else if(place_label) {
            flipso_cat_location(out, f, "", place_label, &product->from);
        } else {
            flipso_cat_location(
                out, f, "", ss->usage_alighted ? "Last got off" : "Last got on", &product->from);
        }
        return;
    }

    /* The other types record a time and no place. A LastUseDTS of zero is a
     * ticket not yet used, not the DTS epoch. */
    if(ss->has_last_use) {
        if(ss->last_use_dts) {
            flipso_cat_datetime_line(out, "", "Last used", ss->last_use_dts);
        } else {
            furi_string_cat(out, "Last used: Never\n");
        }
    }
}

/**
 * The parts of a Space Saving IPE (TYP 27, 28 or 29) that are its own rather than
 * shared with a full ticket: its restrictions, where and when it was last used,
 * and the day passes or journeys it keeps count of. Its price, issue date, class
 * and travellers are in @c ticket and shown by flipso_cat_ticket_terms(), its
 * area by flipso_cat_space_area(), and its rides or passes left by the
 * product's counter.
 *
 * Every element the dataset carries is shown, default or not: a paper ticket
 * holds little enough that "Off-peak only: No" is information, not clutter.
 */
static void flipso_cat_space_saving(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product) {
    if(!product->space_saving) return;
    const ItsoSpaceSaving* ss = &card->space;

    flipso_cat_flag(out, "", "Off-peak only", ss->flags & ITSO_SS_OFF_PEAK);
    flipso_cat_flag(out, "", "Weekdays only", ss->flags & ITSO_SS_WEEKDAY);
    /* ExpiryTimeFlag (tables 49, 52 and 56): 23:59, or a time the owner sets in
     * its readers - end of service, which can fall after midnight. */
    if(ss->flags & ITSO_SS_EXPIRY_TIME) {
        furi_string_cat(out, "Ends at: Set by the operator\n");
    } else {
        furi_string_cat(out, "Ends at: 23:59 on the expiry date\n");
    }
    if(product->typ == ItsoTypPeriodCompact && !product->ticket.photocard) {
        furi_string_cat(out, "Photocard number: None\n");
    }

    flipso_cat_last_use(out, f, card, product, NULL);

    /* TYP 28: the day passes spent so far, each the day it was used. A tick of
     * zero is a pass still to use and 31 one never sold (clause 2.15.2). */
    if(product->typ == ItsoTypCarnet) {
        flipso_cat_flag(out, "", "Valid on day of issue", ss->carnet_issue_day);
        flipso_cat_flag(out, "", "Valid on day of expiry", ss->carnet_expiry_day);
        for(size_t i = 0; i < COUNT_OF(ss->carnet_ticks); i++) {
            uint8_t tick = ss->carnet_ticks[i];
            if(tick == 0 || tick == 31 || tick > product->expiry) continue;
            flipso_cat_date_line(out, "", "Day used", (uint16_t)(product->expiry - tick));
        }
    }

    /* TYP 29 revision 2, multi-leg journeys. The daily count is the count for
     * the day the latest journey began - which is only today if that was today,
     * so it hangs off that date rather than claiming to be today's. */
    if(product->typ == ItsoTypMultiUse && product->format_rev == 2) {
        furi_string_cat_printf(out, "Daily journey limit: %u\n", ss->max_daily_journeys);
        furi_string_cat_printf(out, "Changes allowed: %u\n", product->ticket.max_transfers);
        if(ss->journey_start_dts) {
            flipso_cat_datetime_line(out, "", "Journey began", ss->journey_start_dts);
            furi_string_cat_printf(out, "  Changes made: %u\n", ss->transfers);
            furi_string_cat_printf(out, "  Journeys that day: %u\n", ss->daily_journeys);
        } else {
            furi_string_cat(out, "Journey began: Not yet\n");
        }
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
            out,
            "Fare rounding: %s to %s\n",
            (product->rounding & ITSO_ROUNDING_FLAG) ? "Up" : "Down",
            (product->rounding & ITSO_ROUNDING_VALUE) ? "5p" : "1p");
    }

    /* IDFlags bits 3, 6 and 7 (TS 1000-5 table 24). */
    if(product->has_id_flags && (product->id_flags & 0x08)) {
        furi_string_cat(out, "More details: In another app on the card\n");
    }
    if(product->has_deposit) {
        flipso_cat_deposit(
            out,
            "Deposit",
            &product->deposit,
            product->deposit_mop,
            product->deposit_vat,
            (product->id_flags & 0x40) != 0);
    }
    if(product->has_shell_deposit) {
        flipso_cat_deposit(
            out,
            "Card deposit",
            &product->shell_deposit,
            product->shell_deposit_mop,
            product->shell_deposit_vat,
            (product->id_flags & 0x80) != 0);
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
            out,
            "",
            product->typ == ItsoTypStoredTravelRights ? "Balance limit" : "Spending limit",
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
            flipso_cat_date_line(out, "  ", "Starts", product->start);
        }
    }

    if(product->has_deposit) {
        flipso_cat_money(out, "", "Deposit", &product->deposit);
        if(product->deposit_mop) {
            furi_string_cat_printf(
                out, "  Paid by: %s\n", itso_payment_name(product->deposit_mop));
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
/**
 * Decode a product's capping extension into @p cap.
 * @return false when the product has none, or it did not decode.
 */
static bool flipso_decode_capping(
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product,
    ItsoCapping* cap) {
    if(product->vgx_ref != 1 && product->vgx_ref != 2) return false;
    if(!product->on_card || !f->capture) return false;
    size_t len = 0;
    const uint8_t* group = flipso_capture_product_group(f->capture, product->dir_index, &len);
    if(!group) return false;
    uint8_t valc = product->balance.valid ? product->balance.currency : 0;
    return itso_parse_capping(group, len, card->sector_size, valc, cap);
}

static void flipso_cat_capping(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product) {
    ItsoCapping* cap = malloc(sizeof(ItsoCapping));
    if(flipso_decode_capping(f, card, product, cap)) {
        furi_string_cat(out, "\n");
        flipso_cat_heading(out, FlipsoIconNone, "Fare capping");
        bool any = false;
        static const char* const rules[] = {NULL, "Daily", "Short period", "Long period"};
        for(uint8_t a = 0; a < ITSO_CAP_ACCUMULATORS; a++) {
            const ItsoCapAccumulator* acc = &cap->acc[a];
            if(acc->rule == ItsoCapRuleNone) continue;
            any = true;
            furi_string_cat_printf(
                out,
                "Cap %u: %s\n",
                a + 1,
                acc->rule < COUNT_OF(rules) ? rules[acc->rule] : "Other");
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
                flipso_cat_heading(out, FlipsoIconPast, on_card ? "Earlier on card" : "Off card");
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
            out,
            "  Allowance: %u every %u week%s\n",
            product->max_transactions,
            product->weeks_per_period,
            product->weeks_per_period == 1 ? "" : "s");
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
    /* The status comes from where the card keeps the product - in use, blocked,
     * never used - not from whether it is still any good. A ticket still "in
     * use" by that measure can have run out of date or of rides, and saying
     * "Active" beside "Expired" contradicts the line under it. Only the rides
     * kinds count as used up at zero: a period ticket with no passes left in
     * stock can still be in its current pass. */
    if(product->status == ItsoProductStatusActive && !itso_date_open(product->expiry) &&
       itso_date_expired(product->expiry, now)) {
        furi_string_cat(out, "Status: Expired\n");
    } else if(
        product->status == ItsoProductStatusActive &&
        (product->count_kind == ItsoCountRides || product->count_kind == ItsoCountCoupons) &&
        product->count == 0) {
        furi_string_cat(out, "Status: Used up\n");
    } else if(product->status != ItsoProductStatusUnknown) {
        furi_string_cat_printf(out, "Status: %s\n", itso_status_name(product->status));
    }
    flipso_cat_expiry(out, "", "Expires", "Expired", product->expiry, now);

    /* A purse's start date gates auto-top-up, and is shown with it. */
    if(product->has_start && product->typ != ItsoTypStoredTravelRights) {
        furi_string_cat(out, "Valid from: ");
        flipso_cat_date(out, product->start);
        if(product->ticket.has_start_time && product->ticket.start_time) {
            furi_string_cat_printf(
                out,
                " %02u:%02u",
                product->ticket.start_time / 60,
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
            out,
            "",
            rides ? "Unused rides until" : "Unused passes until",
            rides ? "Unused rides expired" : "Unused passes expired",
            product->stored_expiry,
            now);
    }

    /* --- Where. An entitlement's two locations are areas it is good in, not
     * the ends of a journey. --- */
    /* A Space Saving IPE has an area element instead, and keeps the place it
     * was last used in @c from - not the start of a journey, so
     * flipso_cat_space_saving() labels it with the other facts of its use. */
    if(product->space_saving) {
        flipso_cat_space_area(out, card);
    } else {
        flipso_cat_location(out, f, "", identity ? "Valid in" : "From", &product->from);
        flipso_cat_location(out, f, "", identity ? "Also valid in" : "To", &product->to);
    }
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
        if(identity)
            flipso_cat_flag(out, "", "Photo on card", itso_id_personalised(product->id_flags));
    }
    /* PassbackTime: how long a gate refuses the same pass after it has been
     * used, so it cannot be handed back through for a second person. Zero is
     * not "no wait" but "the reader's own rule" (TS 1000-5, every IPE that
     * carries it), so it is shown as that rather than left out. */
    if(product->has_passback) {
        if(product->passback) {
            furi_string_cat_printf(out, "Passback timeout: %u min\n", product->passback);
        } else {
            furi_string_cat(out, "Passback timeout: Set by the operator\n");
        }
    }

    /* --- Its state. --- */
    if(product->ticket_used) flipso_cat_flag(out, "", "Used", true);
    if(product->auto_renew) furi_string_cat(out, "Auto-renew: On\n");
    if(product->priority_override) flipso_cat_flag(out, "", "Used first", true);
    /* A journey in progress: legs taken so far and the fare accumulated across
     * them, which is what a capped or multi-leg discount is computed from. */
    if(product->has_journey && (product->journey_legs || product->cumulative_fare.value)) {
        furi_string_cat_printf(
            out,
            "Current journey: %u leg%s\n",
            product->journey_legs,
            product->journey_legs == 1 ? "" : "s");
        flipso_cat_money(out, "  ", "Fare so far", &product->cumulative_fare);
    }
    if(product->has_transfers && product->transfers) {
        furi_string_cat_printf(out, "Changes made: %u\n", product->transfers);
    }

    /* --- The terms behind it, then what has happened to it. --- */
    flipso_cat_ticket_terms(out, product);
    flipso_cat_space_saving(out, f, card, product);
    flipso_cat_id_details(out, product);
    flipso_cat_last_transaction(out, product);
    flipso_cat_purse_terms(out, product);
    flipso_cat_capping(out, f, card, product);

    if(product->value_group && !product->value_parsed) {
        furi_string_cat(out, "Transaction history: Could not be read\n");
    }
    if(product->foreign_iin) flipso_cat_flag(out, "", "Issued outside ITSO", true);

    /* Last, because the terms are what the product is and the history is what
     * has happened to it: a card read more than once can carry several
     * screenfuls of the latter. */
    flipso_cat_value_history(out, product);
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

void flipso_format_product(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product) {
    flipso_cat_heading(
        out,
        product->on_card ? flipso_product_icon(product) : FlipsoIconPast,
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
            furi_string_cat_printf(out, "Removable: %u days after expiry\n", product->remove_date);
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
        flipso_cat_machine(out, f, "", "Created by machine", product->isam_id);
        furi_string_cat_printf(out, "  Sequence: %lu\n", (unsigned long)product->isam_seq);
        if(product->iteration)
            furi_string_cat_printf(out, "Times reinstated: %u\n", product->iteration);
        furi_string_cat_printf(out, "Seal key version: %u\n", product->key_id);
    }

    if(product->value_parsed) {
        furi_string_cat_printf(out, "Times updated: %u\n", product->value_ts);
        flipso_cat_machine(out, f, "", "Last updated by machine", product->value_isam);
        if(product->value_action_seq) {
            furi_string_cat_printf(out, "Action number: %u\n", product->value_action_seq);
        }
    }

    /* A paper period ticket's two EventTypeCodes (TYP 27). The spec neither
     * orders nor explains them, so they are shown as numbered on the card - and
     * here rather than above, as what they are: raw codes. */
    if(product->space_saving && card->space.has_events) {
        furi_string_cat_printf(out, "Event 1: %s\n", itso_transaction_name(card->space.event1));
        furi_string_cat_printf(out, "Event 2: %s\n", itso_transaction_name(card->space.event2));
    }

    /* The capping strategy is the scheme's own number for its rule set, and
     * means nothing without the scheme's tables. */
    ItsoCapping* cap = malloc(sizeof(ItsoCapping));
    if(flipso_decode_capping(f, card, product, cap) && cap->strategy) {
        furi_string_cat_printf(out, "Capping rules: %u\n", cap->strategy);
    }
    free(cap);
}
