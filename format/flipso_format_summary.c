/**
 * @file flipso_format_summary.c
 * @brief The Summary screen: the card's state, what it holds, and where it was last used.
 */
#include "flipso_format_i.h"

/**
 * The last day a product is good for: its expiry, or a charge-to-account's
 * EndDate where that comes first - the day the account stops paying, which
 * TS 1000-5 tables 13 and 17 let fall before the product leaves the card.
 */
static ItsoDate flipso_summary_until(const ItsoProduct* product) {
    const ItsoPurseTerms* purse = itso_product_purse(product);
    if(purse->has_end_date && !itso_date_open(purse->end_date) &&
       (itso_date_open(product->expiry) || purse->end_date < product->expiry)) {
        return purse->end_date;
    }
    return product->expiry;
}

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
    const ItsoDate until = flipso_summary_until(product);
    return !itso_date_open(until) && itso_date_expired(until, now);
}

/** One product as a summary line: "Period ticket: Until 20/10/2026". */
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
         * pensioner, a student - and labelled as that, not as the ID: "ITSO
         * ID: Adult" read as the ID's name, even on a card that stores none.
         * Without a concession it is the holder's benefit, as its own page
         * calls it. An entitlement is its benefit, under its own name. */
        if(product->typ == ItsoTypId && id->concession_class) {
            furi_string_cat_printf(
                out, "Concession: %s\n", itso_profile_name(id->concession_class));
        } else if(product->typ == ItsoTypId) {
            furi_string_cat_printf(
                out, "Benefit: %s\n", itso_entitlement_name(id->entitlement_code));
        } else {
            furi_string_cat_printf(
                out, "%s: %s\n", title, itso_entitlement_name(id->entitlement_code));
        }
    } else {
        furi_string_cat_printf(out, "%s: ", title);
        /* A period ticket's own expiry is when its unused passes lapse; day
         * to day it is good until the pass in use ends. Once that has ended
         * the next pass starts on the next tap, and the product's expiry is
         * all there is to go by. */
        const bool pass = ticket->has_current_expiry && !itso_date_open(ticket->current_expiry) &&
                          !itso_date_expired(ticket->current_expiry, now);
        const ItsoDate until = flipso_summary_until(product);
        const bool lapsed = !itso_date_open(until) && itso_date_expired(until, now);
        /* A date it is good until reads as a ticket still good, and an
         * expired one is the more telling of the two. */
        if(flipso_product_used_up(product, now) && !lapsed) {
            furi_string_cat(out, "Used up");
        } else if(lapsed) {
            furi_string_cat(out, until == product->expiry ? "Expired " : "Ended ");
            flipso_cat_date(out, until);
        } else if(pass) {
            furi_string_cat(out, "Until ");
            flipso_cat_date(out, ticket->current_expiry);
        } else if(itso_date_open(until)) {
            furi_string_cat(out, "No expiry");
        } else {
            furi_string_cat(out, "Until ");
            flipso_cat_date(out, until);
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

    /* A card that cannot be used is the headline, whatever its products say:
     * a blocked, retired or expired card is the one line that answers for all
     * of them. A card that is fine says so last, under what it holds. */
    const bool expired = !itso_date_open(card->expiry) && itso_date_expired(card->expiry, f->now);
    if(card->shell_compact) {
        /* A paper ticket, not a card: its state is its one product's, which
         * that product's own line below already says - "Until", "Expired",
         * "Used up" - so only a ticket with nothing on it needs a line here. */
        if(!card->product_count) flipso_cat_ticket_state(out, "Ticket", card, f->now);
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
    }

    /* Then what will get the holder through the gate today - the tickets that
     * can be used, the purse - ahead of who they are and of the card itself.
     * The tickets that cannot be used have a page of their own after this
     * one. A paper ticket is its one product, whatever state it is in. */
    FuriString* tickets = furi_string_alloc();
    FuriString* money = furi_string_alloc();
    FuriString* who = furi_string_alloc();
    FuriString* lapsed = furi_string_alloc();
    uint8_t shown = 0, past = 0;
    for(uint8_t i = 0; i < card->product_count; i++) {
        const ItsoProduct* product = &card->products[i];
        if(!product->on_card) {
            past++;
            continue;
        }
        FuriString* to = tickets;
        if(card->shell_compact) {
            /* Its one product, in whatever state: there is nothing else. */
        } else if(product->typ == ItsoTypStoredTravelRights) {
            to = money;
        } else if(product->typ == ItsoTypId || product->typ == ItsoTypEntitlement) {
            to = who;
        } else if(flipso_summary_states(product, f->now)) {
            to = lapsed;
        }
        flipso_summary_product(to, card, product, f->now);
        shown++;
    }
    furi_string_cat(out, furi_string_get_cstr(tickets));
    furi_string_cat(out, furi_string_get_cstr(money));
    if(card->dir_valid && !shown) furi_string_cat(out, "Products: None\n");
    if(!card->dir_valid) furi_string_cat(out, "Products: Could not be read\n");

    for(uint8_t i = 0; i < card->product_count; i++) {
        const ItsoProduct* product = &card->products[i];
        const ItsoIdTerms* id = itso_product_id(product);
        if(product->on_card && id->has_name) {
            furi_string_cat_printf(out, "Holder: %s\n", id->name);
            break;
        }
    }
    furi_string_cat(out, furi_string_get_cstr(who));

    if(!card->shell_compact && !card->shell_blocked && !itso_card_retired(card) && !expired) {
        if(card->dir_valid) furi_string_cat(out, "Card: Active\n");
        flipso_cat_expiry(out, "", "Card expires", "Card expired", card->expiry, f->now);
    }

    /* A paper ticket keeps no log, so what a card's last tap says is in its one
     * product instead - and a holder checks a ticket against what they paid. */
    if(card->shell_compact && card->product_count) {
        const ItsoProduct* ticket = &card->products[0];
        /* "Last used at": the one revision that keeps a place rather than a
         * time, so the label says which it is. */
        flipso_cat_last_use(out, f, card, ticket, "Last used at");
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
    flipso_cat_page_from(out, FlipsoIconInvalid, "Not valid", lapsed);
    furi_string_free(tickets);
    furi_string_free(money);
    furi_string_free(who);
    furi_string_free(lapsed);
}
