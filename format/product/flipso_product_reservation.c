/**
 * @file flipso_product_reservation.c
 * @brief A TYP 24 reserved journey: its portions, restrictions, route and codes.
 */
#include "flipso_product_i.h"

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
void flipso_cat_portion(FuriString* out, const char* label, uint32_t from_dts, uint16_t days) {
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
void flipso_cat_sold_as(FuriString* out, const ItsoTicketTerms* t) {
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
bool flipso_decode_reservation(
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product,
    ItsoReservation* res) {
    const ItsoTicketTerms* ticket = itso_product_ticket(product);
    memset(res, 0, sizeof(*res));
    if(product->typ != ItsoTypReservationTicket || !ticket->valid) return false;
    if(!product->on_card || !f->capture) return false;
    size_t len = 0;
    const uint8_t* group = flipso_capture_product_group(f->capture, product->dir_index, &len);
    if(!group) return false;
    return itso_parse_reservation(group, len, card->sector_size, ticket->reservations, res);
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
void flipso_cat_reservation_id(
    FuriString* out,
    const ItsoProduct* product,
    const ItsoReservation* res) {
    const uint32_t id = flipso_reservation_id(res);
    if(id == 0) return;
    const ItsoTicketTerms* t = itso_product_ticket(product);
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

/**
 * A TYP24Flags flag (table 138) the ticket's pages show only when set, and
 * the page that says it: how it was issued is part of the purchase, a seat
 * it needs is a restriction, and the rest are details of the ticket.
 */
typedef struct {
    uint16_t bit;
    const char* label;
    FlipsoSlot slot;
} FlipsoT24Flag;

/* TestOrLive leads the screen, PassengerDetails is said by the passenger, and
 * AutoRenew is with the other products' renewal line. */
static const FlipsoT24Flag flipso_t24_flags[] = {
    {ITSO_T24_DUPLICATE, "Duplicate", FlipsoSlotPurchase},
    {ITSO_T24_REPLACEMENT, "Replacement", FlipsoSlotPurchase},
    {ITSO_T24_FOLLOW_ON, "Follow-on renewal", FlipsoSlotPurchase},
    {ITSO_T24_WARRANT, "Unfulfilled warrant", FlipsoSlotPurchase},
    {ITSO_T24_CARNET, "Carnet", FlipsoSlotDetails},
    {ITSO_T24_SEAT_REQUIRED, "Seat reservation required", FlipsoSlotRules},
    {ITSO_T24_COMPANION, "Companion allowed", FlipsoSlotDetails},
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
 * days, times and trains it may be used on (its Restrictions page), the route
 * it is held to and the changes and breaks it allows (Route), and the
 * passenger and the products it was sold with (Details), from TS 1000-5
 * tables 136 and 138. The codes behind it are under Technical.
 *
 * @param res the rest of the dataset; NULL, or not valid, where it would not
 *            decode, which leaves what ItsoProduct holds.
 */
void flipso_cat_reservation(
    FlipsoPages* p,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product,
    const ItsoReservation* res) {
    const ItsoTicketTerms* t = itso_product_ticket(product);
    if(product->typ != ItsoTypReservationTicket || !t->valid) return;
    FuriString* rules = flipso_pages_at(p, FlipsoSlotRules);
    FuriString* route = flipso_pages_at(p, FlipsoSlotRoute);
    FuriString* details = flipso_pages_at(p, FlipsoSlotDetails);

    /* Only the flags that are set: a ticket that is none of these is the
     * ordinary case, and Technical lists the rest. */
    for(size_t i = 0; i < COUNT_OF(flipso_t24_flags); i++) {
        if(t->flags & flipso_t24_flags[i].bit)
            flipso_cat_flag(
                flipso_pages_at(p, flipso_t24_flags[i].slot), "", flipso_t24_flags[i].label, true);
    }
    if(product->value_parsed && t->part_used) {
        flipso_cat_flag(details, "", "Part-way through a leg", true);
    }

    if(res && res->valid) {
        if(product->auto_renew) {
            furi_string_cat_printf(
                details,
                "Renews until: %u day%s after expiry\n",
                res->renew_days,
                res->renew_days == 1 ? "" : "s");
        }

        if(res->has_passenger) {
            furi_string_cat_printf(
                details, "Passenger: %s\n", res->passenger[0] ? res->passenger : "Not stored");
            /* RSPS3002 3.8.3: 00 not specified, 01 male, 10 female, 11 not used. */
            const char* gender = res->gender == 1 ? "Male" : res->gender == 2 ? "Female" : NULL;
            furi_string_cat_printf(details, "  Gender: %s\n", gender ? gender : "Not specified");
        }

        /* The other products on the card the ticket was sold with: on rail, the
         * railcard it was priced against (RSPS3002 3.8.3). */
        for(uint8_t i = 0; i < res->associated_count; i++) {
            flipso_cat_product_ref(details, card, "", "Part of this ticket", res->associated[i]);
        }

        /* The times first: on an off-peak ticket they decide which train the
         * holder may catch. */
        for(uint8_t i = 0; i < res->time_band_count; i++) {
            const ItsoTimeBand* b = &res->time_bands[i];
            /* TimeBandIncludeExcludeFlag: valid within the band, or only outside it. */
            furi_string_cat(rules, b->include ? "Valid times: " : "Valid times: Outside ");
            flipso_cat_minutes(rules, b->start);
            furi_string_push_back(rules, '-');
            flipso_cat_minutes(rules, b->end);
            furi_string_push_back(rules, '\n');
            flipso_cat_band_applies(rules, b);
            if(!itso_is_blank(b->operator_code, sizeof(b->operator_code))) {
                flipso_cat_ud_line(
                    rules, "  ", "Operator", b->operator_code, sizeof(b->operator_code));
            }
            flipso_cat_location(rules, f, "  ", "At", &b->location);
        }

        char days[40];
        itso_format_days(res->valid_days, days, sizeof(days));
        furi_string_cat_printf(rules, "Valid days: %s\n", days);
        flipso_cat_flag(rules, "  ", "Public holidays", res->valid_days & ITSO_DOW_SPECIAL);
        itso_format_days(res->restricted_days, days, sizeof(days));
        /* DaysRestrictionApplies: the days the RestrictionCode holds on, not
         * days the ticket is not valid. */
        furi_string_cat_printf(rules, "Restrictions apply: %s\n", days);
        if(res->restricted_days & ITSO_DOW_SPECIAL) {
            flipso_cat_flag(rules, "  ", "Public holidays", true);
        }

        if(itso_is_blank(res->operator_code, sizeof(res->operator_code))) {
            furi_string_cat(rules, "Operators: Any\n");
        } else {
            flipso_cat_ud_line(
                rules, "", "Only on operator", res->operator_code, sizeof(res->operator_code));
        }

        for(uint8_t i = 0; i < res->service_count; i++) {
            const ItsoServiceRule* sr = &res->services[i];
            /* RestrictionOrEasementFlag: a train it may not be used on, or one it
             * may be though its other terms would rule it out. */
            flipso_cat_ud_line(
                rules,
                "",
                sr->restriction ? "Not valid on train" : "Also valid on train",
                sr->service,
                sizeof(sr->service));
            flipso_cat_location(rules, f, "  ", "From", &sr->departs);
            furi_string_cat(rules, "  Departs: ");
            flipso_cat_minutes(rules, sr->time);
            furi_string_push_back(rules, '\n');
        }

        for(uint8_t i = 0; i < res->route_count; i++) {
            const ItsoRoutePoint* r = &res->routes[i];
            if(r->via == 0 || r->via == 1) {
                flipso_cat_location(route, f, "", r->via ? "Via" : "Not via", &r->location);
            } else {
                flipso_cat_location(route, f, "", "Routing point", &r->location);
                furi_string_cat_printf(route, "  Via code: %u\n", r->via);
            }
        }

        flipso_cat_location(route, f, "", "Or from", &res->alt_from);
        flipso_cat_location(route, f, "", "Or to", &res->alt_to);

        /* An out-of-station interchange: off at one station and on again at
         * another nearby, as across London (RSPS3002 4.4.2.2). A time of zero is
         * rail's way of leaving it to the gates (3.8.3). */
        for(uint8_t i = 0; i < res->interchange_count; i++) {
            const ItsoInterchange* x = &res->interchanges[i];
            flipso_cat_location(route, f, "", "Change stations at", &x->exit);
            flipso_cat_location(route, f, "  ", "Continue from", &x->entry);
            if(x->minutes) {
                furi_string_cat_printf(route, "  Time allowed: %u min\n", x->minutes);
            } else {
                furi_string_cat(route, "  Time allowed: Set by the operator\n");
            }
        }

        for(uint8_t i = 0; i < res->transfer_count; i++) {
            const ItsoTransfer* x = &res->transfers[i];
            if(x->type == ITSO_TRANSFER_BREAK_OF_JOURNEY) {
                furi_string_cat(route, "Break of journey: Allowed\n");
                if(x->count < ITSO_TRANSFERS_UNLIMITED) {
                    furi_string_cat_printf(route, "  Breaks allowed: %u\n", x->count);
                }
            } else {
                furi_string_cat_printf(
                    route,
                    "Transfer type %u: %u transfer%s\n",
                    x->type,
                    x->count,
                    x->count == 1 ? "" : "s");
            }
            if(x->hours) {
                furi_string_cat_printf(
                    route, "  Extra time: %u hour%s\n", x->hours, x->hours == 1 ? "" : "s");
            }
        }
    }

    if(product->value_parsed && t->transfers_left) {
        /* Table 139 allows three transfer types of up to 511 each, but keeps
         * one 11-bit count: what is left is the total across them. Rail sets
         * 511 for a break of journey, which is as many as the holder likes. */
        if(t->transfers_left >= ITSO_TRANSFERS_UNLIMITED) {
            furi_string_cat(route, "Transfers left: Unlimited\n");
        } else {
            furi_string_cat_printf(route, "Transfers left: %u in total\n", t->transfers_left);
        }
    }
}

/** A reserved journey's VendorLoc, unless it is the station the retailer already named. */
void flipso_cat_sold_at(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoProduct* product,
    const ItsoReservation* res) {
    if(!res || !res->valid) return;
    ItsoLocation sold_by;
    if(!itso_product_sold_at(product, &sold_by) || strcmp(sold_by.code, res->vendor.code) != 0 ||
       sold_by.code_kind != res->vendor.code_kind) {
        flipso_cat_location(out, f, "", "Sold at", &res->vendor);
    }
}

/** The codes and unset flags behind a reserved journey, for its Technical section. */
void flipso_cat_reservation_codes(
    FuriString* out,
    const ItsoProduct* product,
    const ItsoReservation* res) {
    const ItsoTicketTerms* t = itso_product_ticket(product);
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
