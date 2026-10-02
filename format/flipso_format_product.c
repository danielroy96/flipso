/**
 * @file flipso_format_product.c
 * @brief What a screen says about one product: the product screen, the purse and the ID.
 */
#include "flipso_format_i.h"

#include <ctype.h>

static bool flipso_product_is_identity(const ItsoProduct* product) {
    return product->typ == ItsoTypId || product->typ == ItsoTypEntitlement;
}

/**
 * True when two dates say the same thing on screen: the same day, or both one
 * of the two encodings of "no expiry" (itso_date_open()). A second date is only
 * worth its line when it differs from the expiry.
 */
static bool flipso_same_date(uint16_t a, uint16_t b) {
    return a == b || (itso_date_open(a) && itso_date_open(b));
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
 * Owner-defined bytes: as text when they are printable ASCII, which a rail
 * RouteCode is, else as hex. Zero padding after text is the spec's, and all
 * zeros is its "not used" (TS 1000-5 tables 27a and 31a).
 */
static void flipso_cat_code_bytes(FuriString* out, const uint8_t* data, size_t len) {
    size_t text = 0;
    while(text < len && data[text] >= 0x20 && data[text] <= 0x7E) {
        text++;
    }
    size_t end = text;
    while(end < len && data[end] == 0) {
        end++;
    }
    if(text == 0 && end == len) {
        furi_string_cat(out, "None");
    } else if(text > 0 && end == len) {
        for(size_t i = 0; i < text; i++) {
            furi_string_push_back(out, (char)data[i]);
        }
    } else {
        for(size_t i = 0; i < len; i++) {
            furi_string_cat_printf(out, "%02X", data[i]);
        }
    }
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
            out, "Ticket use: %s\n", t->mode < defined ? modes[t->mode] : "Other");
        if(t->mode == ItsoJourneyModeStoredJourneys ||
           (t->mode == ItsoJourneyModeReturn && defined == 4)) {
            /* TimeLimit counts 30 second steps between the start of one leg
             * and the next. */
            furi_string_cat_printf(out, "  Changes allowed: %u\n", t->max_transfers);
            furi_string_cat_printf(
                out,
                "  Time between legs: %u min%s\n",
                t->time_limit / 2,
                (t->time_limit & 1) ? " 30 s" : "");
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
    /* Revision 3's TreatmentOfExpiredSP: what a top-up does with passes whose
     * stock has expired (rule 8 of TS 1000-5 clause 2.9.3.4). */
    if(product->typ == ItsoTypPeriodTicket && product->format_rev >= 3) {
        furi_string_cat_printf(
            out,
            "Expired passes at top-up: %s\n",
            (t->flags & ITSO_T22_KEEP_EXPIRED) ? "Kept" : "Written off");
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
 * A user-defined element (TS 1000-1's UD) as it stands: text when it is
 * printable, less trailing spaces and zero padding; otherwise a number when it
 * is short enough to be one, which a ticket number is; otherwise hex. All
 * zeros is "None".
 */
static void flipso_cat_ud(FuriString* out, const uint8_t* data, size_t len) {
    size_t text = 0;
    while(text < len && data[text] >= 0x20 && data[text] <= 0x7E) {
        text++;
    }
    size_t end = text;
    while(end < len && data[end] == 0) {
        end++;
    }
    if(text > 0 && end == len) {
        while(text > 0 && data[text - 1] == ' ') {
            text--;
        }
        if(text > 0) {
            furi_string_cat_printf(out, "%.*s", (int)text, (const char*)data);
            return;
        }
    }
    if(len <= 4) {
        uint32_t number = 0;
        for(size_t i = 0; i < len; i++) {
            number = (number << 8) | data[i];
        }
        if(number == 0) {
            furi_string_cat(out, "None");
        } else {
            furi_string_cat_printf(out, "%lu", (unsigned long)number);
        }
        return;
    }
    flipso_cat_code_bytes(out, data, len);
}

/** "Label: <UD element>". */
static void flipso_cat_ud_line(
    FuriString* out,
    const char* indent,
    const char* label,
    const uint8_t* data,
    size_t len) {
    furi_string_cat_printf(out, "%s%s: ", indent, label);
    flipso_cat_ud(out, data, len);
    furi_string_push_back(out, '\n');
}

/** "hh:mm" from minutes past midnight, the TIME data type. */
static void flipso_cat_minutes(FuriString* out, uint16_t minutes) {
    furi_string_cat_printf(out, "%02u:%02u", (minutes / 60) % 24, minutes % 60);
}

/**
 * One portion of a reserved journey: "Outward: 01/10/2026 to 31/10/2026". Its
 * period counts days on from the start (table 136), so a period of 0 is a
 * portion good on its first day alone: "Outward: 13/03/2026 only". A start and
 * period both zero is a portion the ticket gives no validity of its own.
 */
static void
    flipso_cat_portion(FuriString* out, const char* label, uint32_t from_dts, uint16_t days) {
    if(from_dts == 0 && days == 0) {
        furi_string_cat_printf(out, "%s: No validity of its own\n", label);
        return;
    }
    const uint32_t from = itso_dts_to_unix(from_dts);
    furi_string_cat_printf(out, "%s: ", label);
    flipso_cat_timestamp(out, from, false);
    if(days) {
        furi_string_cat(out, " to ");
        flipso_cat_timestamp(out, from + (uint32_t)days * 86400u, false);
    } else {
        furi_string_cat(out, " only");
    }
    furi_string_push_back(out, '\n');
    /* A portion that opens at a time of day says so. Rail starts every one at
     * 00:01 (RSPS3002 3.8.3), which is the start of the day, not a time. */
    const uint32_t minute = (from % 86400u) / 60;
    if(minute > 1) {
        furi_string_cat(out, "  Starts at: ");
        flipso_cat_minutes(out, (uint16_t)minute);
        furi_string_push_back(out, '\n');
    }
}

/** What NumberOfJourneysSold buys, given ProductTypeEncoding (table 136). */
static void flipso_cat_sold_as(FuriString* out, const ItsoTicketTerms* t) {
    const unsigned n = t->journeys_sold;
    furi_string_cat(out, "Sold as: ");
    switch(t->sold_as) {
    case ItsoSoldOneWay:
        if(n == 1) {
            furi_string_cat(out, "Single");
        } else {
            furi_string_cat_printf(out, "%u singles", n);
        }
        break;
    case ItsoSoldReturns:
        if(n == 2) {
            furi_string_cat(out, "Return");
        } else {
            furi_string_cat_printf(out, "%u journeys, in return pairs", n);
        }
        break;
    case ItsoSoldEitherWay:
        furi_string_cat_printf(out, "%u journey%s, either way", n, n == 1 ? "" : "s");
        break;
    default:
        furi_string_cat_printf(out, "Type %u", t->sold_as);
        break;
    }
    furi_string_push_back(out, '\n');
    furi_string_cat_printf(out, "  Journeys sold: %u\n", n);
}

/**
 * Decode a reserved journey's dataset and reservations into @p res, from the
 * capture, as the capping extension is.
 * @return false when the product is not one, or it did not decode.
 */
static bool flipso_decode_reservation(
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product,
    ItsoReservation* res) {
    memset(res, 0, sizeof(*res));
    if(product->typ != ItsoTypReservationTicket || !product->ticket.valid) return false;
    if(!product->on_card || !f->capture) return false;
    size_t len = 0;
    const uint8_t* group = flipso_capture_product_group(f->capture, product->dir_index, &len);
    if(!group) return false;
    return itso_parse_reservation(
        group, len, card->sector_size, product->ticket.reservations, res);
}

/* IdDocumentReference on rail: a five-digit number, the first digit the kind
 * of ID and the other four the last four digits of its number (RSPS3002
 * 3.8.3). Anything else is not rail's, and is a number of its own. */
#define FLIPSO_RAIL_ID_MIN 10000u
#define FLIPSO_RAIL_ID_MAX 99999u

static uint32_t flipso_reservation_id(const ItsoReservation* res) {
    return ((uint32_t)res->id_doc[0] << 24) | ((uint32_t)res->id_doc[1] << 16) |
           ((uint32_t)res->id_doc[2] << 8) | res->id_doc[3];
}

/**
 * The railcard or ID a reserved journey is held to, under the line that names
 * it: the number the ticket carries for it. On rail that is only its last
 * four digits. A discount that is not a card to carry - a GroupSave, or a code
 * the table does not know - is not a railcard to number, so the line stands on
 * its own.
 */
static void flipso_cat_reservation_id(
    FuriString* out,
    const ItsoProduct* product,
    const ItsoReservation* res) {
    const uint32_t id = flipso_reservation_id(res);
    if(id == 0) return;
    const ItsoTicketTerms* t = &product->ticket;
    bool railcard = false;
    if(t->has_discount) {
        bool is_card = false;
        const bool named = itso_railcard_name(t->discount, sizeof(t->discount), &is_card);
        railcard = itso_discount_from_card(t->discount, sizeof(t->discount)) || (named && is_card);
    }
    if(id >= FLIPSO_RAIL_ID_MIN && id <= FLIPSO_RAIL_ID_MAX) {
        furi_string_cat_printf(
            out,
            "%s: Ends %04lu\n",
            railcard ? "  Railcard number" : "Railcard or photocard number",
            (unsigned long)(id % 10000));
    } else {
        furi_string_cat_printf(
            out,
            "%s: %lu\n",
            railcard ? "  Railcard number" : "ID document number",
            (unsigned long)id);
    }
}

/** A TYP24Flags flag (table 138) the main section shows only when set. */
typedef struct {
    uint16_t bit;
    const char* label;
} FlipsoT24Flag;

/* TestOrLive leads the screen, PassengerDetails is said by the passenger, and
 * AutoRenew is with the other products' renewal line. */
static const FlipsoT24Flag flipso_t24_flags[] = {
    {ITSO_T24_DUPLICATE, "Duplicate"},
    {ITSO_T24_REPLACEMENT, "Replacement"},
    {ITSO_T24_FOLLOW_ON, "Follow-on renewal"},
    {ITSO_T24_WARRANT, "Unfulfilled warrant"},
    {ITSO_T24_CARNET, "Carnet"},
    {ITSO_T24_SEAT_REQUIRED, "Seat reservation required"},
    {ITSO_T24_COMPANION, "Companion allowed"},
};

/** "Outward departures": which journeys a time band applies to, and how. */
static void flipso_cat_band_applies(FuriString* out, const ItsoTimeBand* b) {
    const char* times = b->arrival ? "arrivals" : "departures";
    switch(b->portion & 0x03) {
    case ITSO_T24_BAND_OUTWARD:
        furi_string_cat_printf(out, "  Applies to: Outward %s\n", times);
        break;
    case ITSO_T24_BAND_RETURN:
        furi_string_cat_printf(out, "  Applies to: Return %s\n", times);
        break;
    default:
        /* Both bits, or neither, which no journey could otherwise mean. */
        furi_string_cat_printf(
            out, "  Applies to: %s both ways\n", b->arrival ? "Arrivals" : "Departures");
        break;
    }
}

/**
 * The terms of a TYP 24 reserved journey beyond what every ticket shows: the
 * flags that are set, the passenger, the days and times it may be used, the
 * trains and routes it is held to, and the changes and breaks it allows
 * (TS 1000-5 tables 136 and 138). The codes behind it are under Technical.
 *
 * @param res the rest of the dataset; NULL, or not valid, where it would not
 *            decode, which leaves what ItsoProduct holds.
 */
static void flipso_cat_reservation(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product,
    const ItsoReservation* res) {
    const ItsoTicketTerms* t = &product->ticket;
    if(product->typ != ItsoTypReservationTicket || !t->valid) return;

    /* Only the flags that are set: a ticket that is none of these is the
     * ordinary case, and Technical lists the rest. */
    for(size_t i = 0; i < COUNT_OF(flipso_t24_flags); i++) {
        if(t->flags & flipso_t24_flags[i].bit)
            flipso_cat_flag(out, "", flipso_t24_flags[i].label, true);
    }
    if(product->value_parsed && t->part_used) {
        flipso_cat_flag(out, "", "Part-way through a leg", true);
    }
    if(product->value_parsed && t->transfers_left) {
        /* Table 139 allows three transfer types of up to 511 each, but keeps
         * one 11-bit count: what is left is the total across them. Rail sets
         * 511 for a break of journey, which is as many as the holder likes. */
        if(t->transfers_left >= ITSO_TRANSFERS_UNLIMITED) {
            furi_string_cat(out, "Transfers left: Unlimited\n");
        } else {
            furi_string_cat_printf(out, "Transfers left: %u in total\n", t->transfers_left);
        }
    }

    if(!res || !res->valid) return;

    if(product->auto_renew) {
        furi_string_cat_printf(
            out,
            "Renews until: %u day%s after expiry\n",
            res->renew_days,
            res->renew_days == 1 ? "" : "s");
    }

    if(res->has_passenger) {
        furi_string_cat_printf(
            out, "Passenger: %s\n", res->passenger[0] ? res->passenger : "Not stored");
        /* RSPS3002 3.8.3: 00 not specified, 01 male, 10 female, 11 not used. */
        const char* gender = res->gender == 1 ? "Male" : res->gender == 2 ? "Female" : NULL;
        furi_string_cat_printf(out, "  Gender: %s\n", gender ? gender : "Not specified");
    }

    flipso_cat_location(out, f, "", "Or from", &res->alt_from);
    flipso_cat_location(out, f, "", "Or to", &res->alt_to);

    char days[40];
    itso_format_days(res->valid_days, days, sizeof(days));
    furi_string_cat_printf(out, "Valid days: %s\n", days);
    flipso_cat_flag(out, "  ", "Public holidays", res->valid_days & ITSO_DOW_SPECIAL);
    itso_format_days(res->restricted_days, days, sizeof(days));
    /* DaysRestrictionApplies: the days the RestrictionCode holds on, not
     * days the ticket is not valid. */
    furi_string_cat_printf(out, "Restrictions apply: %s\n", days);
    if(res->restricted_days & ITSO_DOW_SPECIAL) {
        flipso_cat_flag(out, "  ", "Public holidays", true);
    }

    if(itso_is_blank(res->operator_code, sizeof(res->operator_code))) {
        furi_string_cat(out, "Operators: Any\n");
    } else {
        flipso_cat_ud_line(
            out, "", "Only on operator", res->operator_code, sizeof(res->operator_code));
    }

    /* The other products on the card the ticket was sold with: on rail, the
     * railcard it was priced against (RSPS3002 3.8.3). */
    for(uint8_t i = 0; i < res->associated_count; i++) {
        flipso_cat_product_ref(out, card, "", "Part of this ticket", res->associated[i]);
    }

    for(uint8_t i = 0; i < res->route_count; i++) {
        const ItsoRoutePoint* r = &res->routes[i];
        if(r->via == 0 || r->via == 1) {
            flipso_cat_location(out, f, "", r->via ? "Via" : "Not via", &r->location);
        } else {
            flipso_cat_location(out, f, "", "Routing point", &r->location);
            furi_string_cat_printf(out, "  Via code: %u\n", r->via);
        }
    }

    /* An out-of-station interchange: off at one station and on again at
     * another nearby, as across London (RSPS3002 4.4.2.2). A time of zero is
     * rail's way of leaving it to the gates (3.8.3). */
    for(uint8_t i = 0; i < res->interchange_count; i++) {
        const ItsoInterchange* x = &res->interchanges[i];
        flipso_cat_location(out, f, "", "Change stations at", &x->exit);
        flipso_cat_location(out, f, "  ", "Continue from", &x->entry);
        if(x->minutes) {
            furi_string_cat_printf(out, "  Time allowed: %u min\n", x->minutes);
        } else {
            furi_string_cat(out, "  Time allowed: Set by the operator\n");
        }
    }

    for(uint8_t i = 0; i < res->transfer_count; i++) {
        const ItsoTransfer* x = &res->transfers[i];
        if(x->type == ITSO_TRANSFER_BREAK_OF_JOURNEY) {
            furi_string_cat(out, "Break of journey: Allowed\n");
            if(x->count < ITSO_TRANSFERS_UNLIMITED) {
                furi_string_cat_printf(out, "  Breaks allowed: %u\n", x->count);
            }
        } else {
            furi_string_cat_printf(
                out,
                "Transfer type %u: %u transfer%s\n",
                x->type,
                x->count,
                x->count == 1 ? "" : "s");
        }
        if(x->hours) {
            furi_string_cat_printf(
                out, "  Extra time: %u hour%s\n", x->hours, x->hours == 1 ? "" : "s");
        }
    }

    for(uint8_t i = 0; i < res->time_band_count; i++) {
        const ItsoTimeBand* b = &res->time_bands[i];
        /* TimeBandIncludeExcludeFlag: valid within the band, or only outside it. */
        furi_string_cat(out, b->include ? "Valid times: " : "Valid times: Outside ");
        flipso_cat_minutes(out, b->start);
        furi_string_push_back(out, '-');
        flipso_cat_minutes(out, b->end);
        furi_string_push_back(out, '\n');
        flipso_cat_band_applies(out, b);
        if(!itso_is_blank(b->operator_code, sizeof(b->operator_code))) {
            flipso_cat_ud_line(out, "  ", "Operator", b->operator_code, sizeof(b->operator_code));
        }
        flipso_cat_location(out, f, "  ", "At", &b->location);
    }

    for(uint8_t i = 0; i < res->service_count; i++) {
        const ItsoServiceRule* s = &res->services[i];
        /* RestrictionOrEasementFlag: a train it may not be used on, or one it
         * may be though its other terms would rule it out. */
        flipso_cat_ud_line(
            out,
            "",
            s->restriction ? "Not valid on train" : "Also valid on train",
            s->service,
            sizeof(s->service));
        flipso_cat_location(out, f, "  ", "From", &s->departs);
        furi_string_cat(out, "  Departs: ");
        flipso_cat_minutes(out, s->time);
        furi_string_push_back(out, '\n');
    }

    /* VendorLoc, unless it is the station the retailer already named. */
    ItsoLocation sold_by;
    if(!itso_product_sold_at(product, &sold_by) || strcmp(sold_by.code, res->vendor.code) != 0 ||
       sold_by.code_kind != res->vendor.code_kind) {
        flipso_cat_location(out, f, "", "Sold at", &res->vendor);
    }
}

/**
 * The seats, berths and spaces a reserved journey holds, from its VGXRef 3
 * extension (TS 1000-5 table AD3), under a heading of their own. The kinds of
 * place and seat direction are as RSPS3002 3.8.6 defines them for rail.
 */
static void flipso_cat_reserved_legs(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoProduct* product,
    const ItsoReservation* res) {
    if(product->typ != ItsoTypReservationTicket || !product->ticket.valid) return;
    const uint8_t expected = product->ticket.reservations;
    if(!res->has_extension && !expected) return;

    furi_string_cat(out, "\n");
    flipso_cat_heading(out, FlipsoIconNone, "Reservations");
    if(!res->has_extension) {
        furi_string_cat(out, "Reservations: Could not be read\n");
        return;
    }
    flipso_cat_ud_line(
        out, "", "Booking reference", (const uint8_t*)res->booking, strlen(res->booking));
    if(res->last_validation) {
        flipso_cat_datetime_line(out, "", "Last validated", res->last_validation);
        flipso_cat_location(out, f, "  ", "At", &res->last_validation_at);
    } else {
        furi_string_cat(out, "Last validated: Never\n");
    }
    if(!res->leg_count && !expected) furi_string_cat(out, "Reserved legs: None\n");

    static const char* const places[] = {
        "Seat", "Sleeper berth", "Bicycle space", "No specific place", "Wheelchair space"};
    static const char* const directions[] = {NULL, "Forwards", "Backwards", "Airline style"};
    for(uint8_t i = 0; i < res->leg_count; i++) {
        const ItsoReservedLeg* leg = &res->legs[i];
        char label[12];
        snprintf(label, sizeof(label), "Leg %u", i + 1);
        flipso_cat_datetime_line(out, "", label, leg->departs);
        if(leg->service[0]) furi_string_cat_printf(out, "  Train: %s\n", leg->service);
        flipso_cat_location(out, f, "  ", "From", &leg->from);
        flipso_cat_location(out, f, "  ", "To", &leg->to);
        if(leg->type < COUNT_OF(places)) {
            furi_string_cat_printf(out, "  Reserved: %s\n", places[leg->type]);
        } else {
            furi_string_cat_printf(out, "  Reserved: Other (%u)\n", leg->type);
        }
        if(leg->coach[0]) furi_string_cat_printf(out, "  Coach: %s\n", leg->coach);
        if(leg->seat[0]) {
            furi_string_cat_printf(
                out, "  %s: %s\n", leg->type == ItsoPlaceBerth ? "Berth" : "Seat", leg->seat);
        }
        if(directions[leg->direction & 0x03]) {
            furi_string_cat_printf(out, "  Facing: %s\n", directions[leg->direction & 0x03]);
        }
        if(leg->attribute[0]) {
            const char* feature = itso_seat_attribute_name(leg->attribute);
            furi_string_cat_printf(out, "  Feature: %s\n", feature ? feature : leg->attribute);
        }
        if(leg->berth == 1 || leg->berth == 2) {
            furi_string_cat_printf(out, "  Bunk: %s\n", leg->berth == 1 ? "Lower" : "Upper");
            flipso_cat_flag(out, "  ", "Cabin shared", leg->together);
        }
    }
    if(expected > res->leg_count) {
        furi_string_cat_printf(out, "Not read: %u of %u\n", expected - res->leg_count, expected);
    }
}

/** The codes and unset flags behind a reserved journey, for its Technical section. */
static void flipso_cat_reservation_codes(
    FuriString* out,
    const ItsoProduct* product,
    const ItsoReservation* res) {
    const ItsoTicketTerms* t = &product->ticket;
    if(product->typ != ItsoTypReservationTicket || !t->valid) return;

    /* The flags the main section leaves out because they are clear. */
    if(!(t->flags & ITSO_T24_TEST)) flipso_cat_flag(out, "", "Test ticket", false);
    for(size_t i = 0; i < COUNT_OF(flipso_t24_flags); i++) {
        if(!(t->flags & flipso_t24_flags[i].bit))
            flipso_cat_flag(out, "", flipso_t24_flags[i].label, false);
    }
    if(product->value_parsed) {
        if(!t->part_used) flipso_cat_flag(out, "", "Part-way through a leg", false);
        if(!t->transfers_left) furi_string_cat(out, "Transfers left: 0\n");
    }

    if(!res->valid) return;
    flipso_cat_ud_line(out, "", "Ticket number", res->ticket_number, sizeof(res->ticket_number));
    flipso_cat_ud_line(out, "", "Fare type", res->ftot, sizeof(res->ftot));
    flipso_cat_ud_line(
        out, "", "Restriction code", res->restriction_code, sizeof(res->restriction_code));
    const uint32_t id = flipso_reservation_id(res);
    if(id >= FLIPSO_RAIL_ID_MIN && id <= FLIPSO_RAIL_ID_MAX) {
        /* RSPS3008 numbers the kinds of ID, and is not published. */
        furi_string_cat_printf(out, "ID type: %lu\n", (unsigned long)(id / 10000));
    }
    static const char* const code_types[] = {
        NULL, "Status code", "Discount code", "From the card"};
    for(uint8_t i = 0; i < res->discount_count; i++) {
        const ItsoDiscount* d = &res->discounts[i];
        flipso_cat_ud_line(out, "", "Discount code", d->code, sizeof(d->code));
        /* One or the other: each is zero when the other is used. */
        const uint16_t tenths = itso_discount_tenths(d);
        if(tenths) {
            furi_string_cat_printf(out, "  Percentage: %u", tenths / 10);
            if(tenths % 10) furi_string_cat_printf(out, ".%u", tenths % 10);
            furi_string_cat(out, "%\n");
        } else {
            flipso_cat_money(out, "  ", "Amount", &d->amount);
        }
        if(itso_discount_is_rail(d)) {
            furi_string_cat_printf(out, "  Code type: %s\n", code_types[d->type]);
        } else {
            furi_string_cat_printf(out, "  Code type: %u\n", d->type);
        }
    }
    for(uint8_t i = 0; i < res->supplement_count; i++) {
        furi_string_cat_printf(
            out, "Supplement: %s\n", res->supplements[i][0] ? res->supplements[i] : "None");
    }
    if(res->overrun) furi_string_cat(out, "Optional details: Run past the dataset\n");
}

/**
 * Where a Space Saving IPE is good (TS 1000-5 tables 50, 53 and 57). A reference
 * fare code is the owner's own, so it says the operator decides rather than
 * what the operator decided - even code 0, which SPT's whole-network Subway
 * tickets carry but another scheme may give its innermost zone.
 */
static void flipso_cat_space_area(FuriString* out, const FlipsoFormat* f, const ItsoCard* card) {
    const ItsoSpaceSaving* ss = &card->space;
    switch((ItsoAreaKind)ss->area_kind) {
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
static void flipso_cat_space_codes(FuriString* out, const ItsoCard* card) {
    const ItsoSpaceSaving* ss = &card->space;
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
static void
    flipso_cat_space_backup(FuriString* out, const ItsoCard* card, const ItsoProduct* product) {
    const ItsoSpaceSaving* ss = &card->space;
    const char* count = itso_count_name(product->count_kind);
    if(!product->space_saving || !ss->has_backup || !count) return;
    if(ss->backup_step == 1) {
        furi_string_cat_printf(out, "Backup count: %u\n", ss->backup_count);
    } else {
        furi_string_cat_printf(out, "Backup count: Up to %u\n", ss->backup_count);
        furi_string_cat_printf(out, "  Step: %u\n", ss->backup_step);
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
 * The parts of an ITSO ID or entitlement beyond name and entitlement: language,
 * valid periods, fare rounding and deposits. An entitlement (TYP 14) carries
 * all of these but the language and the card deposit. Its issuer and holder
 * numbers are under Technical, with the other numbers nothing names.
 */
static void flipso_cat_id_details(FuriString* out, const ItsoProduct* product) {
    if(!flipso_product_is_identity(product) || !product->body_parsed) return;

    char code[3];
    if(itso_language_code(product->language, code)) {
        const char* name = itso_language_name(product->language);
        if(name) {
            furi_string_cat_printf(out, "Language: %s\n", name);
        } else {
            /* ISO 639-1, upper cased so it reads as a code rather than a word. */
            furi_string_cat_printf(out, "Language: %c%c\n", code[0] - 32, code[1] - 32);
        }
        /* IDFlags bit 3 points a POST at another application on the card, and
         * then Language "shall be ignored" (TS 1000-5 table 22). */
        if(product->id_flags & 0x08) furi_string_cat(out, "  In use: No\n");
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

    /* IDFlags bits 3, 6 and 7 (TS 1000-5 table 24); bit 5 is shown with the
     * other print flags, under Technical. */
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
    /* The whole ValueCurrencyCode, scaling bits and all, as the balance has. */
    uint8_t valc = product->value_parsed ? product->value_valc : 0;
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
 *
 * @param res a reserved journey's dataset and reservations, from
 *            flipso_decode_reservation(); NULL for any other product.
 */
static void flipso_cat_product_details(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product,
    const ItsoReservation* res) {
    const uint32_t now = f->now;
    const bool identity = flipso_product_is_identity(product);

    /* A test ticket is not valid for travel, which outranks everything else
     * the screen says about it (TS 1000-5 table 138, TestOrLive). Clear, it is
     * the ordinary case, and Technical says so. */
    if(product->typ == ItsoTypReservationTicket && (product->ticket.flags & ITSO_T24_TEST)) {
        flipso_cat_flag(out, "", "Test ticket", true);
    }

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

    /* --- What it is not valid without: a railcard, or an ID. Up here, with
     * what it is worth, because a ticket without it is worth nothing. --- */
    flipso_cat_valid_only_with(out, card, product, "");
    if(res && res->valid) flipso_cat_reservation_id(out, product, res);

    /* --- Whose it is. --- */
    flipso_cat_operator(out, f, "", "Operator", product->oid);
    /* The retailer is only worth a row when it differs from the owner; on most
     * products the operator sells its own product and the two are the same.
     * A rail ticket's may be the station that sold it instead. */
    ItsoLocation sold_at;
    if(itso_product_sold_at(product, &sold_at)) {
        flipso_cat_location(out, f, "", "Sold by", &sold_at);
    } else if(product->has_retailer && product->retailer != product->oid) {
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
        (product->count_kind == ItsoCountRides || product->count_kind == ItsoCountCoupons ||
         product->count_kind == ItsoCountJourneys) &&
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
    } else if(product->typ == ItsoTypReservationTicket) {
        /* A reserved journey is good for two portions, each from a start for
         * a number of days, rather than from one date. */
        if(product->ticket.valid) {
            const ItsoTicketTerms* t = &product->ticket;
            flipso_cat_sold_as(out, t);
            flipso_cat_portion(out, "Outward", t->valid_from_dts, t->outward_days);
            flipso_cat_portion(out, "Return", t->return_from_dts, t->return_days);
        }
    } else if(product->ticket.valid_from_dts) {
        /* Revisions 1 and 2 of a period ticket hold a DTS here, not a DATE. */
        flipso_cat_datetime_line(out, "", "Valid from", product->ticket.valid_from_dts);
    }
    if(product->has_end_date && !flipso_same_date(product->end_date, product->expiry)) {
        flipso_cat_expiry(out, "", "Valid to", "Ended", product->end_date, now);
    }
    if(product->has_sub_expiry && !flipso_same_date(product->sub_expiry, product->expiry)) {
        flipso_cat_expiry(
            out, "", "Entitlement until", "Entitlement ended", product->sub_expiry, now);
    }
    /* The pass in use and the stock of unused passes expire separately, so a
     * season ticket can be live while the passes behind it have lapsed. */
    if(product->has_current_expiry) {
        flipso_cat_expiry(
            out, "", "Current pass until", "Current pass ended", product->current_expiry, now);
    }
    if(product->has_stored_expiry && !flipso_same_date(product->stored_expiry, product->expiry)) {
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
    /* A ticket's zone map (LocDefType 204, "valid within zone", TS 1000-1
     * table 6) is an area too, when it stands alone: "From: Zones 1,2,3" reads
     * as the start of a journey that has no end. */
    const bool zones = product->from.valid && !product->to.valid && product->from.def_type == 204;
    if(product->space_saving) {
        flipso_cat_space_area(out, f, card);
    } else {
        flipso_cat_location(out, f, "", identity || zones ? "Valid in" : "From", &product->from);
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
    flipso_cat_reservation(out, f, card, product, res);
    flipso_cat_space_saving(out, f, card, product);
    flipso_cat_id_details(out, product);
    flipso_cat_last_transaction(out, product);
    flipso_cat_purse_terms(out, product);
    flipso_cat_capping(out, f, card, product);
    if(res) flipso_cat_reserved_legs(out, f, product, res);

    if(product->value_group && !product->value_parsed) {
        furi_string_cat(out, "Transaction history: Could not be read\n");
    }

    /* Last, because the terms are what the product is and the history is what
     * has happened to it: a card read more than once can carry several
     * screenfuls of the latter. */
    flipso_cat_value_history(out, product);
}

/**
 * The Technical section under a product: the codes and machine numbers behind
 * it, which mean nothing without the scheme's own tables but are what tells two
 * otherwise identical products apart.
 *
 * @param res as flipso_cat_product_details() takes it.
 */
static void flipso_cat_product_technical(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product,
    const ItsoReservation* res) {
    furi_string_cat(out, "\n");
    flipso_cat_heading(out, FlipsoIconNone, "Technical");
    furi_string_cat_printf(out, "Type code: %u.%u\n", product->typ, product->ptyp);
    furi_string_cat_printf(out, "Operator number: %u\n", product->oid);
    if(product->oid_extended) {
        furi_string_cat_printf(
            out, "  Extended range: Yes (%u)\n", (unsigned)(product->oid & 0x1FFF));
    }
    /* IINL: the operator belongs to the network the product's own IIN names
     * (Owner network, below) rather than the card's (TS 1000-2 clause 6.1.7). */
    if(product->foreign_iin) furi_string_cat(out, "  Network: Not the card's own\n");
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
        /* Any machine may delete the product this many days after it expires,
         * but 255 means only the product owner may (TS 1000-5, RemoveDate in
         * every IPE). The owner is the operator named above - "owner" alone
         * reads as the holder, who is the one person it does not mean. */
        if(product->remove_date == 255) {
            furi_string_cat(out, "Removable: Only by the operator\n");
        } else if(product->remove_date == 0) {
            furi_string_cat(out, "Removable: Once expired\n");
        } else {
            furi_string_cat_printf(
                out,
                "Removable: %u day%s after expiry\n",
                product->remove_date,
                product->remove_date == 1 ? "" : "s");
        }
    }
    /* Owner-defined codes: meaningless without the scheme's own tables, but they
     * are what tells two otherwise identical tickets apart. On an English,
     * Scottish or Welsh concessionary pass an ID's CPICC is the pass issuer -
     * the council - by the schemes' own numbering, which is not published;
     * elsewhere it is whatever the owner uses it for. */
    if(product->has_cpicc) {
        furi_string_cat_printf(
            out,
            "%s: %u\n",
            flipso_product_is_identity(product) ? "Pass issuer code" : "Issuer code",
            product->cpicc);
    }
    if(product->has_holder_id) {
        furi_string_cat_printf(out, "Holder number: %lu\n", (unsigned long)product->holder_id);
    }
    if(product->has_secondary_holder && product->secondary_holder_id) {
        furi_string_cat_printf(
            out, "Second holder number: %lu\n", (unsigned long)product->secondary_holder_id);
    }
    if(product->ticket.validity_code) {
        furi_string_cat_printf(out, "Validity code: %u\n", product->ticket.validity_code);
    }
    if(product->ticket.promotion_code) {
        furi_string_cat_printf(out, "Promotion code: %u\n", product->ticket.promotion_code);
    }
    /* IdentityDocumentID's coding, when it is one table 3.27 leaves RFU: the
     * line above has shown its bytes. */
    const ItsoTicketTerms* terms = &product->ticket;
    if(terms->has_id_doc &&
       (terms->id_doc_type < ItsoIdDocHex || terms->id_doc_type > ItsoIdDocEntry)) {
        furi_string_cat_printf(out, "ID document coding: Type %u\n", terms->id_doc_type);
    }
    if(product->ticket.has_route_code) {
        furi_string_cat(out, "Route code: ");
        flipso_cat_code_bytes(out, product->ticket.route_code, sizeof(product->ticket.route_code));
        furi_string_push_back(out, '\n');
    }
    /* TYP 3's UserDefined: the loyalty scheme's own two bytes. */
    if(product->has_owner_data) {
        furi_string_cat_printf(out, "Owner data: %u\n", product->owner_data);
    }
    /* Instructions to the machine rather than facts about the product, so
     * shown here, and only those the type defines. */
    if(product->print_defined & ITSO_PRINT_TICKET) {
        flipso_cat_flag(out, "", "Print ticket", product->print_flags & ITSO_PRINT_TICKET);
    }
    if(product->print_defined & ITSO_PRINT_RECEIPT) {
        flipso_cat_flag(out, "", "Print receipt", product->print_flags & ITSO_PRINT_RECEIPT);
    }
    /* PassbackTime: how long a gate refuses the same pass after it has been
     * used, so it cannot be handed back through for a second person - an
     * instruction to the gate like the two above. Zero is not "no wait" but
     * "the reader's own rule" (TS 1000-5, every IPE that carries it), so it is
     * shown as that rather than left out. */
    if(product->has_passback) {
        if(product->passback) {
            furi_string_cat_printf(out, "Passback timeout: %u min\n", product->passback);
        } else {
            furi_string_cat(out, "Passback timeout: Set by the operator\n");
        }
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
    if(product->space_saving) {
        flipso_cat_space_codes(out, card);
        flipso_cat_space_backup(out, card, product);
    }
    if(product->space_saving && card->space.has_events) {
        furi_string_cat_printf(out, "Event 1: %s\n", itso_transaction_name(card->space.event1));
        furi_string_cat_printf(out, "Event 2: %s\n", itso_transaction_name(card->space.event2));
    }

    if(res) flipso_cat_reservation_codes(out, product, res);

    /* The capping strategy is the scheme's own number for its rule set, and
     * means nothing without the scheme's tables. */
    ItsoCapping* cap = malloc(sizeof(ItsoCapping));
    if(flipso_decode_capping(f, card, product, cap) && cap->strategy) {
        furi_string_cat_printf(out, "Capping rules: %u\n", cap->strategy);
    }
    free(cap);
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
        flipso_cat_product_details(out, f, card, product, NULL);
        /* The product list leaves these out, so this is the only screen with
         * room for the rest of what the card says about them. */
        flipso_cat_product_technical(out, f, card, product, NULL);
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
        flipso_cat_product_details(out, f, card, product, NULL);
        flipso_cat_product_technical(out, f, card, product, NULL);
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

    /* A reserved journey's dataset and reservations, decoded once for both
     * sections and released before the text is shown. */
    ItsoReservation* res = NULL;
    if(product->typ == ItsoTypReservationTicket && product->ticket.valid) {
        res = malloc(sizeof(ItsoReservation));
        flipso_decode_reservation(f, card, product, res);
    }
    flipso_cat_product_details(out, f, card, product, res);
    flipso_cat_product_technical(out, f, card, product, res);
    if(res) {
        itso_reservation_free(res);
        free(res);
    }
}
