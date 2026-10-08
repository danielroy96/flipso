/**
 * @file flipso_product_details.c
 * @brief Everything a product screen says but its codes, each line on the page it belongs to.
 */
#include "flipso_product_i.h"

/**
 * True when two dates say the same thing on screen: the same day, or both one
 * of the two encodings of "no expiry" (itso_date_open()). A second date is only
 * worth its line when it differs from the expiry.
 */
/* A rail ProductRetailer is the NLC of the office that sold the ticket
 * (RSPS3002 3.6.3) - a station's ticket office, or an operator's web or phone
 * sales - so a code the table cannot name is a retailer, not a station. */
static void
    flipso_cat_rail_retailer(FuriString* out, const FlipsoFormat* f, const ItsoLocation* sold_at) {
    char code[ITSO_LOC_CODE_LEN];
    itso_location_code(sold_at, code, sizeof(code));
    const char* name = flipso_stations_name(f->stations, code);
    furi_string_cat_printf(out, "Sold by: %s%s\n", name ? "" : "Retailer ", name ? name : code);
}

static bool flipso_same_date(ItsoDate a, ItsoDate b) {
    return a == b || (itso_date_open(a) && itso_date_open(b));
}

/**
 * Everything a screen says about one product but its codes, each line on the
 * page it belongs to (see FlipsoSlot). The first page answers what a holder
 * asks first - for where, until when, whether it still is, with what, and
 * whose - and its lines are written in that order; the rest are written in the order they
 * read on their own pages.
 *
 * Shared by the purse, ID and product screens, so a product reads the same
 * wherever it is reached from.
 *
 * @param res a reserved journey's dataset and reservations, from
 *            flipso_decode_reservation(); NULL for any other product.
 */
void flipso_cat_product_details(
    FlipsoPages* p,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product,
    const ItsoReservation* res) {
    const ItsoPurseTerms* purse = itso_product_purse(product);
    const ItsoIdTerms* id = itso_product_id(product);
    const ItsoTicketTerms* ticket = itso_product_ticket(product);
    const ItsoUnixTime now = f->now;
    const bool identity = flipso_product_is_identity(product);
    const FlipsoKind kind = p->kind;
    FuriString* main = flipso_pages_at(p, FlipsoSlotMain);
    FuriString* left = flipso_pages_at(p, FlipsoSlotLeft);
    FuriString* rules = flipso_pages_at(p, FlipsoSlotRules);
    FuriString* details = flipso_pages_at(p, FlipsoSlotDetails);
    FuriString* purchase = flipso_pages_at(p, FlipsoSlotPurchase);

    /* A test ticket is not valid for travel, which outranks everything else
     * the screen says about it (TS 1000-5 table 138, TestOrLive). Clear, it is
     * the ordinary case, and Technical says so. */
    if(product->typ == ItsoTypReservationTicket && (ticket->flags & ITSO_T24_TEST)) {
        flipso_cat_flag(main, "", "Test ticket", true);
    }

    /* Before anything the product says about itself, because everything below
     * is written in the present tense and for this one it is not true any more:
     * the card listed it when the record was saved and does not list it now. */
    if(!product->on_card) {
        furi_string_cat(main, "On card: No longer\n");
        if(product->last_seen) {
            furi_string_cat(main, "  Last seen: ");
            flipso_cat_time(main, product->last_seen);
            furi_string_push_back(main, '\n');
        }
    }

    /* --- Who it belongs to (TYP 14 / TYP 16): the name on the first page,
     * the rest of what it says about the holder on a page of its own. --- */
    if(id->has_name) {
        furi_string_cat_printf(main, "Name: %s\n", id->name);
    } else if(product->typ == ItsoTypId && product->body_parsed) {
        /* The name fields are optional and often left off cards that carry a
         * printed photo ID instead. */
        furi_string_cat(main, "Name: Not stored\n");
    }
    if(id->has_dob) {
        /* A Datef rather than a DATE, so it can predate the DATE epoch, and is
         * formatted from its parts rather than through a timestamp. */
        DateTime dob = {.day = id->dob_day, .month = id->dob_month, .year = id->dob_year};
        furi_string_cat(left, "Born: ");
        flipso_cat_datetime_struct(left, &dob, false);
        furi_string_push_back(left, '\n');
    }
    if(id->has_id_flags) {
        const char* gender = itso_gender_name(id->id_flags);
        if(gender) furi_string_cat_printf(left, "Gender: %s\n", gender);
    }

    /* --- What it is worth. A season ticket's stock of passes is not what
     * the holder asks about first, nor a reserved journey's count of
     * journeys: the dates and the trains are. --- */
    if(purse->balance.valid) {
        flipso_cat_money(
            main, "", purse->balance_is_spend ? "Spent so far" : "Balance", &purse->balance);
    } else if(product->typ == ItsoTypStoredTravelRights) {
        furi_string_cat(main, "Balance: Not readable\n");
    }
    FuriString* count = (kind == FlipsoKindPeriod || kind == FlipsoKindReserved) ? left : main;
    const char* count_label = itso_count_name(product->count_kind);
    if(count_label) {
        furi_string_cat_printf(count, "%s: %lu\n", count_label, (unsigned long)product->count);
    }
    /* TYP 25 MaxValue25: the most the voucher buys, which is what it is worth.
     * A journey ticket's ride value shares the element and is a term of use. */
    if(kind == FlipsoKindVoucher) {
        flipso_cat_money(count, "", "Worth up to", &ticket->unit_value);
    }
    if(product->count_kind == ItsoCountTransactions && purse->has_charge_period) {
        furi_string_cat_printf(
            count,
            "  Allowance: %u every %u week%s\n",
            purse->max_transactions,
            purse->weeks_per_period,
            purse->weeks_per_period == 1 ? "" : "s");
    }
    if(purse->has_last_reset && purse->last_reset) {
        flipso_cat_date_line(count, "  ", "Count last reset", purse->last_reset);
    }

    /* --- Where. An entitlement's two locations are areas it is good in, not
     * the ends of a journey, and come after what it entitles the holder to. --- */
    /* A Space Saving IPE has an area element instead, and keeps the place it
     * was last used in @c from - not the start of a journey, so
     * flipso_cat_last_use() labels it with the other facts of its use. */
    /* A ticket's zone map (LocDefType 204, "valid within zone", TS 1000-1
     * table 6) is an area too, when it stands alone: "From: Zones 1,2,3" reads
     * as the start of a journey that has no end. It is called an area, as a
     * paper ticket's is and as a ticket with no locations says it is. */
    if(!identity) {
        const bool zones = product->from.valid && !product->to.valid &&
                           product->from.def_type == 204;
        if(product->space_saving) {
            flipso_cat_space_area(main, f, card);
        } else {
            flipso_cat_location(main, f, "", zones ? "Area" : "From", &product->from);
            flipso_cat_location(main, f, "", "To", &product->to);
        }
        /* A period ticket may leave both locations out, and then it is good
         * wherever its owner has configured that product type to be accepted -
         * an operator's whole network, typically. The card cannot say more than
         * that, and saying nothing reads as though Flipso had failed to decode
         * them. */
        if(product->typ == ItsoTypPeriodTicket && ticket->valid && !product->from.valid &&
           !product->to.valid) {
            furi_string_cat(main, "Area: Set by the operator\n");
        }
    }

    /* --- What kind of ticket it is, which on rail is what the holder calls
     * it: "Off-Peak Return" says as much about when it is good as the dates
     * below do. --- */
    if(res && res->valid) flipso_cat_ticket_type(main, f, res);

    /* --- Whether it is still good, straight after where: it is what a holder
     * reads the first page for, and below the dates it sat out of sight. --- */
    /* The status comes from where the card keeps the product - in use, blocked,
     * never used - not from whether it is still any good. A ticket still "in
     * use" by that measure can have run out of date or of rides, and saying
     * "Active" beside "Expired" contradicts the line under it, and so does
     * "Active" above nothing left to travel on. */
    if(product->status == ItsoProductStatusActive && !itso_date_open(product->expiry) &&
       itso_date_expired(product->expiry, now)) {
        furi_string_cat(main, "Status: Expired\n");
    } else if(product->status == ItsoProductStatusActive && flipso_product_used_up(product, now)) {
        furi_string_cat(main, "Status: Used up\n");
    } else if(product->status != ItsoProductStatusUnknown) {
        furi_string_cat_printf(main, "Status: %s\n", itso_status_name(product->status));
    }

    /* --- When it is good for. --- */
    /* Time left is counted down only on a ticket that can still be used: on
     * one blocked, used up or gone from the card it reads as time to travel.
     * A reserved journey's expiry is the end of its return portion, which the
     * first page has already said, so it is a detail and not counted down. */
    const bool live = product->on_card && product->status != ItsoProductStatusBlocked &&
                      !flipso_product_used_up(product, now);
    /* The pass in use and the stock of unused passes expire separately, so a
     * season ticket can be live while the passes behind it have lapsed. The
     * pass in use comes first: it is what the ticket is good until today,
     * and the season's own dates are the bounds of the passes behind it. */
    if(ticket->has_current_expiry) {
        flipso_cat_expiry(
            main, "", "Current pass until", "Current pass ended", ticket->current_expiry, now);
        if(live) flipso_cat_time_left(main, "  ", ticket->current_expiry, now);
    }
    /* A season ticket's start is one of the dates it is checked against; a
     * carnet's, an account's or an ID's is one of its terms. */
    FuriString* start =
        (kind == FlipsoKindJourney || kind == FlipsoKindAccount || identity) ? rules : main;
    /* A purse's start date gates auto-top-up, and is shown with it. */
    if(product->has_start && product->typ != ItsoTypStoredTravelRights) {
        furi_string_cat(start, "Valid from: ");
        flipso_cat_date(start, product->start);
        if(ticket->has_start_time && ticket->start_time) {
            furi_string_cat_printf(
                start, " %02u:%02u", ticket->start_time / 60, ticket->start_time % 60);
        }
        furi_string_push_back(start, '\n');
    } else if(product->typ == ItsoTypReservationTicket) {
        /* A reserved journey is good for two portions, each from a start for
         * a number of days, rather than from one date. What it was sold as is
         * a detail; the dates are what it is checked against. */
        if(ticket->valid) {
            flipso_cat_sold_as(details, ticket);
            flipso_cat_portion(main, "Outward", ticket->valid_from_dts, ticket->outward_days);
            flipso_cat_portion(main, "Return", ticket->return_from_dts, ticket->return_days);
        }
    } else if(ticket->valid_from_dts) {
        /* Revisions 1 and 2 of a period ticket hold a DTS here, not a DATE. */
        flipso_cat_datetime_line(start, "", "Valid from", ticket->valid_from_dts);
    }
    if(kind == FlipsoKindReserved) {
        flipso_cat_expiry(details, "", "Expires", "Expired", product->expiry, now);
    } else {
        flipso_cat_expiry(main, "", "Expires", "Expired", product->expiry, now);
        if(live) flipso_cat_time_left(main, "  ", product->expiry, now);
    }
    /* A charge-to-account's EndDate is when the account stops paying for
     * travel, which may come before the product's own expiry (TS 1000-5
     * tables 13 and 17): two bare dates, "Expires" and "Valid to", left the
     * holder to guess which one counts. */
    if(purse->has_end_date && !flipso_same_date(purse->end_date, product->expiry)) {
        flipso_cat_expiry(main, "", "Account ends", "Account ended", purse->end_date, now);
    }
    if(id->has_sub_expiry && !flipso_same_date(id->sub_expiry, product->expiry)) {
        flipso_cat_expiry(
            identity ? rules : main, "", "Benefit until", "Benefit ended", id->sub_expiry, now);
    }
    if(ticket->has_stored_expiry && !flipso_same_date(ticket->stored_expiry, product->expiry)) {
        const bool rides = product->typ == ItsoTypJourneyTicket;
        flipso_cat_expiry(
            kind == FlipsoKindPeriod ? left : main,
            "",
            rides ? "Unused rides until" : "Unused passes until",
            rides ? "Unused rides expired" : "Unused passes expired",
            ticket->stored_expiry,
            now);
    }

    /* --- What it is not valid without: a railcard, or an ID. On the first
     * page, because a ticket without it is worth nothing. --- */
    flipso_cat_valid_only_with(main, card, product, "");
    if(res && res->valid) flipso_cat_reservation_id(main, product, res);

    /* A journey in progress: legs taken so far and the fare accumulated across
     * them, which is what a capped or multi-leg discount is computed from. */
    if(purse->has_journey && (purse->journey_legs || purse->cumulative_fare.value)) {
        furi_string_cat_printf(
            main,
            "Current journey: %u leg%s\n",
            purse->journey_legs,
            purse->journey_legs == 1 ? "" : "s");
        flipso_cat_money(main, "  ", "Fare so far", &purse->cumulative_fare);
    }

    /* --- What it entitles the holder to. EntitlementCode (TS 1000-5 annex
     * A.8) is the kind of fare deal - capped, free, a loyalty tier - and is
     * called the holder's benefit: "Entitlement" is also the name of a
     * product, whose page said "Entitlement: Proportional fare" under a
     * title of "Entitlement". --- */
    if(id->has_entitlement) {
        furi_string_cat_printf(main, "Benefit: %s\n", itso_entitlement_name(id->entitlement_code));
        /* Profile code zero is "unspecified", which tells the holder nothing. */
        if(id->concession_class) {
            furi_string_cat_printf(
                main, "Concession: %s\n", itso_profile_name(id->concession_class));
        }
    }
    if(id->has_id_flags) {
        /* CompanionAllowed: a companion travels at the holder's own rate with
         * no entitlement of their own (TS 1000-5 table 24). Not "free": that
         * is only what it means where the holder's rate is. */
        if(itso_id_companion(id->id_flags)) {
            furi_string_cat(main, "Companion: Travels at the same rate\n");
        }
        if(identity)
            flipso_cat_flag(left, "", "Photo on card", itso_id_personalised(id->id_flags));
    }
    if(identity) {
        flipso_cat_location(main, f, "", "Valid in", &product->from);
        flipso_cat_location(main, f, "", "Also valid in", &product->to);
    }

    /* A paper ticket keeps no journey log, so when or where it was last used
     * is what its first page has in place of a last tap. */
    flipso_cat_last_use(main, f, card, product, NULL);
    /* A voucher's AutoRenewQuantity2 counts the uses each renewal adds, and a
     * toll pass's AutoRenewQuantity3 the crossings (TS 1000-5 tables 36 and
     * 40): each is a detail of the renewal. */
    if(product->auto_renew) {
        furi_string_cat(kind == FlipsoKindOther ? main : left, "Auto-renew: On\n");
        if((kind == FlipsoKindVoucher || kind == FlipsoKindToll) && ticket->renew_quantity) {
            const bool one = ticket->renew_quantity == 1;
            furi_string_cat_printf(
                left,
                "  Renewal adds: %u %s\n",
                ticket->renew_quantity,
                kind == FlipsoKindVoucher ? (one ? "use" : "uses") :
                                            (one ? "crossing" : "crossings"));
        }
    }

    /* --- Whose it is, last on the first page. --- */
    flipso_cat_operator(main, f, "", "Operator", product->oid);

    /* --- What it cost. The retailer is only worth a row when it differs
     * from the owner; on most products the operator sells its own product and
     * the two are the same. A rail ticket's may be the station that sold it
     * instead. --- */
    flipso_cat_booking(purchase, product, res);
    ItsoLocation sold_at;
    if(itso_product_sold_at(product, &sold_at)) {
        flipso_cat_rail_retailer(purchase, f, &sold_at);
    } else if(product->has_retailer && product->retailer != product->oid) {
        flipso_cat_operator(purchase, f, "", "Sold by", product->retailer);
    }
    flipso_cat_sold_at(purchase, f, product, res);
    flipso_cat_ticket_price(purchase, product);

    /* --- Its state. --- */
    if(ticket->ticket_used) flipso_cat_flag(left, "", "Used", true);
    if(purse->priority_override) flipso_cat_flag(left, "", "Used first", true);
    if(ticket->has_transfers && ticket->transfers) {
        furi_string_cat_printf(left, "Changes made: %u\n", ticket->transfers);
    }

    /* --- The terms behind it, then what has happened to it. --- */
    flipso_cat_ticket_terms(p, product);
    flipso_cat_reservation(p, f, card, product, res);
    flipso_cat_reservation_record(p, f, product, res);
    flipso_cat_space_saving(p, card, product);
    flipso_cat_id_details(p, product);
    flipso_cat_id_deposits(purchase, product);
    flipso_cat_purse_terms(left, product);

    FuriString* history = flipso_pages_at(p, FlipsoSlotHistory);
    flipso_cat_last_transaction(history, product);
    if(product->value_group && !product->value_parsed) {
        furi_string_cat(history, "Transaction history: Could not be read\n");
    }
    flipso_cat_value_history(p, product);
}
