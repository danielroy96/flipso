/**
 * @file flipso_format.c
 * @brief The pieces every screen shares, and the Summary and About screens.
 *
 * The other screens are built beside it: flipso_format_product.c (a product,
 * the purse and the ID), flipso_format_card.c (the card, its chip, and what a
 * card Flipso cannot decode says about itself) and flipso_format_journeys.c.
 * See flipso_format.h for the house style they all follow.
 */
#include "flipso_format_i.h"

void flipso_cat_datetime_struct(FuriString* out, const DateTime* dt, bool with_time) {
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
void flipso_cat_timestamp(FuriString* out, uint32_t timestamp, bool with_time) {
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

void flipso_cat_heading(FuriString* out, FlipsoIcon icon, const char* title) {
    if(icon > FlipsoIconNone && icon < FlipsoIconCount) {
        furi_string_cat_printf(out, "\e#%c%s\n", (char)(FLIPSO_TEXT_ICON_BASE + icon), title);
    } else {
        furi_string_cat_printf(out, "\e#%s\n", title);
    }
}

void flipso_cat_page(FuriString* out, FlipsoIcon icon, const char* title) {
    if(!furi_string_empty(out)) furi_string_push_back(out, FLIPSO_TEXT_PAGE);
    flipso_cat_heading(out, icon, title);
}

void flipso_cat_page_from(FuriString* out, FlipsoIcon icon, const char* title, FuriString* body) {
    if(furi_string_empty(body)) return;
    flipso_cat_page(out, icon, title);
    furi_string_cat(out, body);
    furi_string_reset(body);
}

/** "Label: Yes" or "Label: No". */
void flipso_cat_flag(FuriString* out, const char* indent, const char* label, bool value) {
    furi_string_cat_printf(out, "%s%s: %s\n", indent, label, value ? "Yes" : "No");
}

/** "Label: £1.23", or nothing when the amount was not decoded. */
void flipso_cat_money(
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
void flipso_cat_date_line(FuriString* out, const char* indent, const char* label, uint16_t date) {
    furi_string_cat_printf(out, "%s%s: ", indent, label);
    flipso_cat_date(out, date);
    furi_string_push_back(out, '\n');
}

/** "Label: dd/mm/yyyy hh:mm" for a DTS. */
void flipso_cat_datetime_line(FuriString* out, const char* indent, const char* label, uint32_t dts) {
    furi_string_cat_printf(out, "%s%s: ", indent, label);
    flipso_cat_datetime(out, dts);
    furi_string_push_back(out, '\n');
}

/**
 * An expiry date, whose label changes once it has passed.
 *
 * Changing the label rather than appending a marker keeps the line to what fits
 * across the screen. The dates that mean "no expiry" (itso_date_open()) say so
 * rather than printing a day in 2041.
 */
void flipso_cat_expiry(
    FuriString* out,
    const char* indent,
    const char* label,
    const char* past_label,
    uint16_t date,
    uint32_t now) {
    if(itso_date_open(date)) {
        furi_string_cat_printf(out, "%s%s: No expiry\n", indent, label);
        return;
    }
    flipso_cat_date_line(out, indent, itso_date_expired(date, now) ? past_label : label, date);
}

/** "VAT: 20.00%", from a rate in 0.01% steps. Nothing for a rate of zero. */
void flipso_cat_vat(FuriString* out, const char* indent, uint16_t vat) {
    if(vat) furi_string_cat_printf(out, "%sVAT: %u.%02u%%\n", indent, vat / 100, vat % 100);
}

/**
 * "Label: Southeastern", or the operator's number where it has no name.
 *
 * ITSO's operator register is not public, so an unknown number is expected
 * rather than exceptional; the number is what lets the user add it to their
 * operators file.
 */
void flipso_cat_operator(
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
void flipso_cat_machine(
    FuriString* out,
    const FlipsoFormat* f,
    const char* indent,
    const char* label,
    uint32_t isam) {
    if(!isam) return;
    furi_string_cat_printf(out, "%s%s: %08lX\n", indent, label, (unsigned long)isam);
    /* The operator is a detail of the machine, so one step further in. */
    char under[16];
    snprintf(under, sizeof(under), "%s  ", indent);
    flipso_cat_operator(out, f, under, "Operator", itso_isam_oid(isam));
}

/**
 * "Label: place", or nothing when the location is absent.
 *
 * Rail codes are resolved to station names and bus stop codes to stop names,
 * where the tables that hold them are on the SD card; anything else falls back
 * to the text the decoder rendered from the code itself.
 */
void flipso_cat_location(
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

bool flipso_product_listed(const ItsoProduct* product) {
    /* The Pay as you go and ID screens show every one of these the card holds,
     * so a row here would be a second way to the same screen. One the card has
     * dropped is on neither, and the list is the only place it can be seen. */
    if(!product->on_card) return true;
    return product->typ != ItsoTypStoredTravelRights && product->typ != ItsoTypId &&
           product->typ != ItsoTypEntitlement;
}

void flipso_cat_valid_only_with(
    FuriString* out,
    const ItsoCard* card,
    const ItsoProduct* product,
    const char* indent) {
    const ItsoTicketTerms* t = &product->ticket;

    if(product->typ == ItsoTypReservationTicket && t->has_discount) {
        bool is_card = true;
        const char* name = itso_railcard_name(t->discount, sizeof(t->discount), &is_card);
        if(itso_discount_from_card(t->discount, sizeof(t->discount))) {
            /* Priced against an entitlement on this card, which the screen
             * names as part of the ticket. */
            furi_string_cat_printf(out, "%sValid only with: A railcard on this card\n", indent);
        } else if(name && is_card) {
            furi_string_cat_printf(out, "%sValid only with: %s\n", indent, name);
        } else if(name) {
            furi_string_cat_printf(out, "%sDiscount: %s\n", indent, name);
        } else {
            /* A code the table does not know: shown as the card has it. */
            size_t len = sizeof(t->discount);
            while(len > 0 && (t->discount[len - 1] == ' ' || t->discount[len - 1] == 0)) {
                len--;
            }
            furi_string_cat_printf(out, "%sDiscount: ", indent);
            for(size_t i = 0; i < len; i++) {
                const uint8_t c = t->discount[i];
                if(c >= 0x20 && c <= 0x7E) {
                    furi_string_push_back(out, (char)c);
                } else {
                    furi_string_cat_printf(out, "%02X", c);
                }
            }
            if(len == 0) furi_string_cat(out, "None");
            furi_string_push_back(out, '\n');
        }
        return;
    }

    if(product->typ != ItsoTypPeriodTicket || !t->has_id_doc) return;
    const uint8_t kept = t->id_doc_len < ITSO_ID_DOC_LEN ? t->id_doc_len : ITSO_ID_DOC_LEN;
    /* Another product on the card, such as a railcard: named. A pointer to
     * entry 0 names nothing, and falls through to its bytes. */
    if(t->id_doc_type == ItsoIdDocEntry && t->id_doc[0]) {
        flipso_cat_product_ref(out, card, indent, "Valid only with", t->id_doc[0]);
        return;
    }
    furi_string_cat_printf(out, "%sValid only with: ID ", indent);
    if(t->id_doc_type == ItsoIdDocHex && kept <= 4) {
        uint32_t number = 0;
        for(uint8_t i = 0; i < kept; i++) {
            number = (number << 8) | t->id_doc[i];
        }
        furi_string_cat_printf(out, "%lu", (unsigned long)number);
    } else if(t->id_doc_type == ItsoIdDocAscii) {
        for(uint8_t i = 0; i < kept && t->id_doc[i]; i++) {
            const uint8_t c = t->id_doc[i];
            furi_string_push_back(out, c >= 0x20 && c <= 0x7E ? (char)c : '?');
        }
    } else {
        /* A number too long for 32 bits, or a coding that is RFU: its bytes. */
        for(uint8_t i = 0; i < kept; i++) {
            furi_string_cat_printf(out, "%02X", t->id_doc[i]);
        }
    }
    if(t->id_doc_len > kept) {
        furi_string_cat_printf(out, " and %u more bytes", t->id_doc_len - kept);
    }
    furi_string_push_back(out, '\n');
}

/** "Label: Pay as you go", naming the product in directory entry @p dir_index. */
void flipso_cat_product_ref(
    FuriString* out,
    const ItsoCard* card,
    const char* indent,
    const char* label,
    uint8_t dir_index) {
    if(dir_index == 0) return;
    furi_string_cat_printf(out, "%s%s: ", indent, label);
    flipso_cat_product_name(out, card, dir_index);
    furi_string_push_back(out, '\n');
}

void flipso_cat_product_name(FuriString* out, const ItsoCard* card, uint8_t dir_index) {
    /* The card's own products come first in the array, so an entry that has
     * been freed and reused names the product in it now rather than the one a
     * saved card remembers holding it. */
    for(uint8_t i = 0; i < card->product_count; i++) {
        const ItsoProduct* product = &card->products[i];
        if(product->dir_index != dir_index) continue;
        furi_string_cat(out, flipso_product_title(product));
        return;
    }
    furi_string_cat_printf(out, "Directory slot %u", dir_index);
}

const ItsoTap* flipso_latest_tap(const ItsoCard* card) {
    /* Newest first, so the first on-card record is the newest. */
    for(uint8_t i = 0; i < card->tap_count; i++) {
        if(card->taps[i].on_card) return &card->taps[i];
    }
    return NULL;
}

/**
 * "Label: Active" for a compact-shell ticket, from its one product.
 *
 * A paper ticket is its product: its shell is implied by the CMD, never expires
 * and cannot be blocked (TS 1000-10 table 42), so the state a full card takes
 * from its shell would call a spent ticket "Active". This asks the product
 * instead - blocked, out of date, or out of rides - in that order.
 */
void flipso_cat_ticket_state(
    FuriString* out,
    const char* label,
    const ItsoCard* card,
    uint32_t now) {
    if(!card->product_count) {
        furi_string_cat_printf(out, "%s: No ticket on it\n", label);
        return;
    }
    const ItsoProduct* product = &card->products[0];
    if(product->status == ItsoProductStatusBlocked) {
        furi_string_cat_printf(out, "%s: Blocked\n", label);
    } else if(!itso_date_open(product->expiry) && itso_date_expired(product->expiry, now)) {
        furi_string_cat_printf(out, "%s: Expired ", label);
        flipso_cat_date(out, product->expiry);
        furi_string_push_back(out, '\n');
    } else if(itso_count_name(product->count_kind) && product->count == 0) {
        furi_string_cat_printf(out, "%s: Used up\n", label);
    } else {
        furi_string_cat_printf(out, "%s: Active\n", label);
    }
}

/**
 * True when a product's summary line leads with its state - blocked, or
 * expired with the date - as flipso_summary_product() writes it.
 */
static bool flipso_summary_states(const ItsoProduct* product, uint32_t now) {
    if(product->status == ItsoProductStatusBlocked) return true;
    if(product->balance.valid || product->has_entitlement) return false;
    return !itso_date_open(product->expiry) && itso_date_expired(product->expiry, now);
}

/** One product as a summary line: "Period ticket: Until 31/03/2027". */
static void flipso_summary_product(
    FuriString* out,
    const ItsoCard* card,
    const ItsoProduct* product,
    uint32_t now) {
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
        /* An ITSO ID is summed up by who the holder is to the scheme - a
         * pensioner, a student - where it says; "ITSO ID: Capped fare" reads as
         * though the ID were a fare. An entitlement is its entitlement. */
        const char* what = product->typ == ItsoTypId && product->concession_class ?
                               itso_profile_name(product->concession_class) :
                               itso_entitlement_name(product->entitlement_code);
        furi_string_cat_printf(out, "%s: %s\n", title, what);
    } else {
        furi_string_cat_printf(out, "%s: ", title);
        if(itso_date_open(product->expiry)) {
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
    if(product->typ == ItsoTypReservationTicket && (product->ticket.flags & ITSO_T24_TEST)) {
        flipso_cat_flag(out, "  ", "Test ticket", true);
    }
    if(product->balance.valid || product->has_entitlement) {
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
        if(product->on_card && product->has_name) {
            furi_string_cat_printf(out, "Holder: %s\n", product->name);
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
        flipso_cat_money(out, "", "Price paid", &ticket->ticket.amount_paid);
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

void flipso_format_about(
    FuriString* out,
    const char* version,
    uint32_t stations,
    uint32_t stops,
    uint16_t operators) {
    flipso_cat_page(out, FlipsoIconInfo, "Flipso");
    if(version) furi_string_cat_printf(out, "Version: %s\n", version);
    furi_string_cat(
        out,
        "Reads UK ITSO travel smartcards, such as bus passes, rail smartcards "
        "and concessionary passes.\n");

    flipso_cat_page(out, FlipsoIconTrain, "Station names");
    if(stations) {
        furi_string_cat_printf(out, "Installed: %lu stations\n", (unsigned long)stations);
    } else {
        furi_string_cat(out, "Installed: No\nReinstall Flipso to restore them.\n");
    }

    flipso_cat_page(out, FlipsoIconBus, "Bus stop names");
    if(stops) {
        furi_string_cat_printf(out, "Installed: %lu stops\n", (unsigned long)stops);
    } else {
        furi_string_cat(
            out,
            "Installed: No\n"
            "Bus stops show as numbers until naptan.dat is copied to "
            "apps_data/flipso on the SD card. It comes with Flipso's source, in "
            "its data folder.\n");
    }

    flipso_cat_page(out, FlipsoIconOperator, "Operator names");
    if(operators) {
        furi_string_cat_printf(out, "Your operators file: %u names\n", operators);
    } else {
        furi_string_cat(
            out,
            "Your operators file: None\n"
            "Add names to apps_data/flipso/operators.txt on the SD card.\n");
    }

    flipso_cat_page(out, FlipsoIconSave, "Saved cards");
    furi_string_cat(out, "Folder: apps_data/flipso/cards\n");
    furi_string_cat(
        out, "Saved cards can contain personal information. Take care sharing them.\n");
}
