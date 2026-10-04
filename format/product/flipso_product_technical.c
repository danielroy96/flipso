/**
 * @file flipso_product_technical.c
 * @brief A product's Technical page: the codes and machine numbers behind it.
 */
#include "flipso_product_i.h"

/**
 * What a product's Technical page says: the codes and machine numbers behind
 * it, which mean nothing without the scheme's own tables but are what tells two
 * otherwise identical products apart. The caller gives it its title.
 *
 * @param res as flipso_cat_product_details() takes it.
 */
void flipso_cat_product_technical(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product,
    const ItsoReservation* res) {
    const ItsoIdTerms* id = itso_product_id(product);
    const ItsoTicketTerms* ticket = itso_product_ticket(product);
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
    if(id->has_holder_id) {
        furi_string_cat_printf(out, "Holder number: %lu\n", (unsigned long)id->holder_id);
    }
    if(id->has_secondary_holder && id->secondary_holder_id) {
        furi_string_cat_printf(
            out, "Second holder number: %lu\n", (unsigned long)id->secondary_holder_id);
    }
    if(ticket->validity_code) {
        furi_string_cat_printf(out, "Validity code: %u\n", ticket->validity_code);
    }
    if(ticket->promotion_code) {
        furi_string_cat_printf(out, "Promotion code: %u\n", ticket->promotion_code);
    }
    /* IdentityDocumentID's coding, when it is one table 3.27 leaves RFU: the
     * line above has shown its bytes. */
    if(ticket->has_id_doc &&
       (ticket->id_doc_type < ItsoIdDocHex || ticket->id_doc_type > ItsoIdDocEntry)) {
        furi_string_cat_printf(out, "ID document coding: Type %u\n", ticket->id_doc_type);
    }
    if(ticket->has_route_code) {
        furi_string_cat(out, "Route code: ");
        flipso_cat_code_bytes(out, ticket->route_code, sizeof(ticket->route_code));
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
    if(product->space_saving && card->space && card->space->has_events) {
        furi_string_cat_printf(out, "Event 1: %s\n", itso_transaction_name(card->space->event1));
        furi_string_cat_printf(out, "Event 2: %s\n", itso_transaction_name(card->space->event2));
    }

    if(res) flipso_cat_reservation_codes(out, product, res);

    /* Which of the two capping records the card keeps (VGXRef, TS 1000-5
     * clauses 4.1.1 and 4.1.2): the reduced one has one place for all four
     * caps and no last fare, which is why those lines can be missing from the
     * capping page. The strategy is the scheme's own number for its rule set,
     * and means nothing without the scheme's tables. */
    ItsoCapping* cap = malloc(sizeof(ItsoCapping));
    if(flipso_decode_capping(f, card, product, cap)) {
        furi_string_cat_printf(
            out, "Capping record: %s\n", cap->ref == 1 ? "Reduced (type 1)" : "Full (type 2)");
        if(cap->strategy) furi_string_cat_printf(out, "Capping rules: %u\n", cap->strategy);
    }
    free(cap);
}
