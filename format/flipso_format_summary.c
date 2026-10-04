/**
 * @file flipso_format_summary.c
 * @brief The Summary screen: the card's state, what it holds, and where it was last used.
 */
#include "flipso_format_i.h"

/**
 * True when a product's summary line leads with its state - blocked, used
 * up, or expired with the date - as flipso_summary_product() writes it.
 */
static bool flipso_summary_states(const ItsoProduct* product, ItsoUnixTime now) {
    const ItsoPurseTerms* purse = itso_product_purse(product);
    const ItsoIdTerms* id = itso_product_id(product);
    if(product->status == ItsoProductStatusBlocked) return true;
    if(purse->balance.valid || id->has_entitlement) return false;
    if(flipso_product_used_up(product, now)) return true;
    return !itso_date_open(product->expiry) && itso_date_expired(product->expiry, now);
}

/** One product as a summary line: "Period ticket: Until 31/03/2027". */
static void flipso_summary_product(
    FuriString* out,
    const ItsoCard* card,
    const ItsoProduct* product,
    ItsoUnixTime now) {
    const ItsoPurseTerms* purse = itso_product_purse(product);
    const ItsoIdTerms* id = itso_product_id(product);
    const ItsoTicketTerms* ticket = itso_product_ticket(product);
    const char* title = flipso_product_title(product);

    if(product->status == ItsoProductStatusBlocked) {
        furi_string_cat_printf(out, "%s: Blocked\n", title);
        return;
    }
    if(purse->balance.valid) {
        char money[FLIPSO_MONEY_LEN];
        itso_format_money(&purse->balance, money, sizeof(money));
        furi_string_cat_printf(
            out, "%s: %s%s\n", title, money, purse->balance_is_spend ? " spent" : "");
    } else if(id->has_entitlement) {
        /* An ITSO ID is summed up by who the holder is to the scheme - a
         * pensioner, a student - where it says; "ITSO ID: Capped fare" reads as
         * though the ID were a fare. An entitlement is its entitlement. */
        const char* what = product->typ == ItsoTypId && id->concession_class ?
                               itso_profile_name(id->concession_class) :
                               itso_entitlement_name(id->entitlement_code);
        furi_string_cat_printf(out, "%s: %s\n", title, what);
    } else {
        furi_string_cat_printf(out, "%s: ", title);
        /* A date it is good until reads as a ticket still good, and an
         * expired one is the more telling of the two. */
        if(flipso_product_used_up(product, now) &&
           (itso_date_open(product->expiry) || !itso_date_expired(product->expiry, now))) {
            furi_string_cat(out, "Used up");
        } else if(itso_date_open(product->expiry)) {
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
    /* What it is not valid without, which no other line here would say. */
    flipso_cat_valid_only_with(out, card, product, "  ");
    if(product->typ == ItsoTypReservationTicket && (ticket->flags & ITSO_T24_TEST)) {
        flipso_cat_flag(out, "  ", "Test ticket", true);
    }
    if(purse->balance.valid || id->has_entitlement) {
        if(product->expiry && itso_date_expired(product->expiry, now)) {
            flipso_cat_date_line(out, "  ", "Expired", product->expiry);
        }
    }
}

void flipso_format_summary(FuriString* out, const FlipsoFormat* f, const ItsoCard* card) {
    flipso_cat_page(out, FlipsoIconInfo, "Summary");

    /* The card's own state first: a blocked or expired card is the headline,
     * whatever its products say. */
    const bool expired = !itso_date_open(card->expiry) && itso_date_expired(card->expiry, f->now);
    if(card->shell_compact) {
        /* A paper ticket, not a card: its state is its one product's. Where the
         * product's own line below leads with it - "Blocked", "Expired
         * 21/09/2026" - saying it here too put the same words on two lines in
         * a row; the line earns its place for what that one cannot say. */
        if(!card->product_count || !flipso_summary_states(&card->products[0], f->now)) {
            flipso_cat_ticket_state(out, "Ticket", card, f->now);
        }
    } else if(card->shell_blocked) {
        furi_string_cat(out, "Card: Blocked by its issuer\n");
    } else if(itso_card_retired(card)) {
        furi_string_cat(out, "Card: Retired\n");
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
        const ItsoIdTerms* id = itso_product_id(product);
        if(product->on_card && id->has_name) {
            furi_string_cat_printf(out, "Holder: %s\n", id->name);
            break;
        }
    }

    /* The first page is the card and the holder: their money and who they are
     * to the scheme, whatever state those are in. The tickets follow on pages
     * of their own, the ones that can be used today ahead of the ones that
     * cannot, so a glance finds what will get the holder through the gate. A
     * paper ticket is its one product, and keeps it on the first page. */
    FuriString* tickets = furi_string_alloc();
    FuriString* lapsed = furi_string_alloc();
    uint8_t shown = 0, past = 0;
    for(uint8_t i = 0; i < card->product_count; i++) {
        const ItsoProduct* product = &card->products[i];
        if(!product->on_card) {
            past++;
            continue;
        }
        FuriString* to = out;
        if(!card->shell_compact && product->typ != ItsoTypStoredTravelRights &&
           product->typ != ItsoTypId && product->typ != ItsoTypEntitlement) {
            to = flipso_summary_states(product, f->now) ? lapsed : tickets;
        }
        flipso_summary_product(to, card, product, f->now);
        shown++;
    }
    if(card->dir_valid && !shown) furi_string_cat(out, "Products: None\n");
    if(!card->dir_valid) furi_string_cat(out, "Products: Could not be read\n");

    /* A paper ticket keeps no log, so what a card's last tap says is in its one
     * product instead - and a holder checks a ticket against what they paid. */
    if(card->shell_compact && card->product_count) {
        const ItsoProduct* ticket = &card->products[0];
        flipso_cat_last_use(out, f, card, ticket, "Last used");
        flipso_cat_money(out, "", "Price paid", &itso_product_ticket(ticket)->amount_paid);
    }

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

    /* What the card has dropped is no use today either, so it is counted with
     * the tickets that are blocked or out of date. */
    if(past) {
        furi_string_cat_printf(lapsed, "Products off card: %u\n", past);
    }
    flipso_cat_page_from(out, FlipsoIconProducts, "Tickets", tickets);
    flipso_cat_page_from(out, FlipsoIconInvalid, "Not valid", lapsed);
    furi_string_free(tickets);
    furi_string_free(lapsed);
}
