/**
 * @file flipso_product_space_saving.c
 * @brief A Space Saving paper ticket (TYP 27, 28 and 29): its area, conditions and use.
 */
#include "flipso_product_i.h"

#include <ctype.h>

/**
 * Where a Space Saving IPE is good (TS 1000-5 tables 50, 53 and 57). A reference
 * fare code is the owner's own, so it says the operator decides rather than
 * what the operator decided - even code 0, which SPT's whole-network Subway
 * tickets carry but another scheme may give its innermost zone.
 */
void flipso_cat_space_area(FuriString* out, const FlipsoFormat* f, const ItsoCard* card) {
    const ItsoSpaceSaving* ss = card->space;
    if(!ss) return;
    switch(ss->area_kind) {
    case ItsoAreaFareCode:
        /* The code itself is under Technical: the owner's own number, which
         * nothing here can name. */
        furi_string_cat(out, "Area: Set by the operator\n");
        break;
    case ItsoAreaFareValue: {
        furi_string_cat(out, "Area: Set by fare value\n");
        ItsoMoney fare = {
            .value = (int32_t)ss->area_value, .currency = ss->euro ? 1 : 0, .valid = true};
        flipso_cat_money(out, "  ", "Fare value", &fare);
        break;
    }
    case ItsoAreaLocation:
        /* A LOC3 or LOC4 names a journey's two ends and perhaps a via; a zone
         * map or a single place fills only the first, and then it is the area
         * the ticket is good in rather than where a journey starts. */
        if(!ss->area[1].valid && !ss->area[2].valid) {
            if(ss->area[0].valid) {
                flipso_cat_location(out, f, "", "Area", &ss->area[0]);
            } else {
                furi_string_cat(out, "Area: Not recorded\n");
            }
        } else {
            flipso_cat_location(out, f, "", "From", &ss->area[0]);
            flipso_cat_location(out, f, "", "To", &ss->area[1]);
            flipso_cat_location(out, f, "", "Via", &ss->area[2]);
        }
        break;
    }
}

/**
 * The codes behind a Space Saving IPE's area, where it has one, for the
 * Technical section: an owner's fare code, or the location type of an area the
 * ticket does not fill in.
 */
void flipso_cat_space_codes(FuriString* out, const ItsoCard* card) {
    const ItsoSpaceSaving* ss = card->space;
    if(!ss) return;
    if(ss->area_kind == ItsoAreaFareCode) {
        furi_string_cat_printf(out, "Fare code: %lu\n", (unsigned long)ss->area_value);
    } else if(
        ss->area_kind == ItsoAreaLocation && !ss->area[0].valid && !ss->area[1].valid &&
        !ss->area[2].valid) {
        furi_string_cat_printf(out, "Location type: %lu\n", (unsigned long)ss->area_value);
    }
}

/**
 * What TYP 29's one-time-programmable backup says is left, and whether it
 * agrees with the count on the screen above (TS 1000-5 table 58b). Each bit set
 * is m used, so the count it gives is a ceiling m wide: the true count agrees
 * when it falls within that band. A mismatch is a torn write or a misread.
 */
void flipso_cat_space_backup(FuriString* out, const ItsoCard* card, const ItsoProduct* product) {
    const ItsoSpaceSaving* ss = card->space;
    const char* count = itso_count_name(product->count_kind);
    if(!product->space_saving || !ss || !ss->has_backup || !count) return;
    if(ss->backup_step == 1) {
        furi_string_cat_printf(out, "Backup count: %u\n", ss->backup_count);
    } else {
        /* Each bit stands for a step's worth used, so the backup pins the
         * count to a band a step wide that ends at the count it gives. */
        const unsigned low =
            ss->backup_count >= ss->backup_step ? ss->backup_count - ss->backup_step + 1 : 0;
        furi_string_cat_printf(out, "Backup count: %u to %u\n", low, ss->backup_count);
    }
    const bool agrees = product->count <= ss->backup_count &&
                        product->count + ss->backup_step > ss->backup_count;
    /* "Rides left" as the middle of a label: "Agrees with rides left". */
    furi_string_cat_printf(
        out,
        "  Agrees with %c%s: %s\n",
        tolower((unsigned char)count[0]),
        count + 1,
        agrees ? "Yes" : "No");
}

void flipso_cat_last_use(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product,
    const char* place_label) {
    const ItsoSpaceSaving* ss = card->space;
    if(!product->space_saving || !ss) return;

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
 * shared with a full ticket: its restrictions, on its Conditions page, and the
 * day passes or journeys it keeps count of, on its Use page. Its price, issue
 * date, class and travellers are in @c ticket and shown by
 * flipso_cat_ticket_terms() and flipso_cat_ticket_price(), its area by
 * flipso_cat_space_area(), its rides or passes left by the product's counter,
 * and where or when it was last used by flipso_cat_last_use(), all on the
 * pages those belong to.
 *
 * Every element the dataset carries is shown, default or not: a paper ticket
 * holds little enough that "Off-peak only: No" is information, not clutter.
 */
void flipso_cat_space_saving(FlipsoPages* p, const ItsoCard* card, const ItsoProduct* product) {
    const ItsoTicketTerms* ticket = itso_product_ticket(product);
    const ItsoSpaceSaving* ss = card->space;
    if(!product->space_saving || !ss) return;
    FuriString* rules = flipso_pages_at(p, FlipsoSlotRules);
    FuriString* left = flipso_pages_at(p, FlipsoSlotLeft);

    flipso_cat_flag(rules, "", "Off-peak only", ss->flags & ITSO_SS_OFF_PEAK);
    flipso_cat_flag(rules, "", "Weekdays only", ss->flags & ITSO_SS_WEEKDAY);
    /* ExpiryTimeFlag (tables 49, 52 and 56): 23:59, or a time the owner sets in
     * its readers - end of service, which can fall after midnight. */
    if(ss->flags & ITSO_SS_EXPIRY_TIME) {
        furi_string_cat(rules, "Ends at: Set by the operator\n");
    } else {
        furi_string_cat(rules, "Ends at: 23:59 on the expiry date\n");
    }
    if(product->typ == ItsoTypPeriodCompact && !ticket->photocard) {
        furi_string_cat(flipso_pages_at(p, FlipsoSlotWho), "Photocard number: None\n");
    }

    /* TYP 28: the day passes spent so far, each the day it was used. A tick of
     * zero is a pass still to use and 31 one never sold (clause 2.15.2).
     *
     * The two flags are passes too, not validity: the carnet is good from its
     * issue date to its expiry date inclusive either way. NDoIE says a pass
     * was spent on the day it was bought, and NDoEE that one was set aside at
     * issue for its last day, so neither needs a tick - which is why they sit
     * with the days used rather than among the conditions. */
    if(product->typ == ItsoTypCarnet) {
        flipso_cat_flag(left, "", "Used on day of issue", ss->carnet_issue_day);
        flipso_cat_flag(left, "", "Pass kept for last day", ss->carnet_expiry_day);
        for(size_t i = 0; i < COUNT_OF(ss->carnet_ticks); i++) {
            uint8_t tick = ss->carnet_ticks[i];
            if(tick == 0 || tick == 31 || tick > product->expiry) continue;
            flipso_cat_date_line(left, "", "Day used", (ItsoDate)(product->expiry - tick));
        }
    }

    /* TYP 29 revision 2, multi-leg journeys. The daily count is the count for
     * the day the latest journey began - which is only today if that was today,
     * so it hangs off that date rather than claiming to be today's. */
    if(product->typ == ItsoTypMultiUse && product->format_rev == 2) {
        furi_string_cat_printf(rules, "Daily journey limit: %u\n", ss->max_daily_journeys);
        furi_string_cat_printf(rules, "Changes allowed: %u\n", ticket->max_transfers);
        if(ss->journey_start_dts) {
            flipso_cat_datetime_line(left, "", "Journey began", ss->journey_start_dts);
            furi_string_cat_printf(left, "  Changes made: %u\n", ss->transfers);
            furi_string_cat_printf(left, "  Journeys that day: %u\n", ss->daily_journeys);
        } else {
            furi_string_cat(left, "Journey began: Not yet\n");
        }
    }
}
