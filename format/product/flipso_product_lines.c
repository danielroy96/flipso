/**
 * @file flipso_product_lines.c
 * @brief How a product is named and summed up on screens other than its own.
 *
 * Its title, icon and row tag, a reference to it by directory entry, what a
 * ticket is valid only with, and a paper ticket's state - the lines the summary,
 * the card and journeys screens share with the product screens.
 */
#include "../flipso_format_i.h"

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
    const ItsoTicketTerms* t = itso_product_ticket(product);

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
