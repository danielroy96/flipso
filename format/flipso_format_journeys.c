/**
 * @file flipso_format_journeys.c
 * @brief The journeys screen: where the card stands, then a page per journey.
 */
#include "flipso_format_i.h"

const ItsoTap* flipso_latest_tap(const ItsoCard* card) {
    /* Newest first, so the first on-card record is the newest. */
    for(uint8_t i = 0; i < card->tap_count; i++) {
        if(card->taps[i].on_card) return &card->taps[i];
    }
    return NULL;
}

/** What a record was, "Tap out", which titles its page and its Technical entry. */
static const char* flipso_tap_title(const ItsoTap* tap) {
    return itso_transaction_name(tap->transaction_type);
}

/**
 * One Transient Ticket Record as a page, less its reader numbers: see
 * flipso_cat_tap_technical(). When first, because the date is how a holder
 * recognises a trip; then where it went, what it cost and what paid for it.
 */
static void flipso_cat_tap(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoTap* tap) {
    /* The card's own log in the taps icon, and what only a saved file
     * remembers in the clock the product list gives a dropped product. */
    flipso_cat_page(out, tap->on_card ? FlipsoIconTaps : FlipsoIconPast, flipso_tap_title(tap));
    /* Said in words as well as by the icon, as a dropped product's first page
     * says it: everything below reads as a journey on the card in front of
     * the holder, and this one is only in the saved file. */
    if(!tap->on_card) furi_string_cat(out, "On card: No longer\n");

    /* A tap-out record carries the tap-in it closes, copied forward so the
     * record stands on its own - so a tap out has two times, and says which is
     * which, in the order they happened, and how long the journey took between
     * them. Any other record has one, labelled "When" as every other time on
     * these screens is. */
    if(tap->has_entry) flipso_cat_datetime_line(out, "", "In", tap->entry_dts);
    flipso_cat_datetime_line(out, "", tap->has_entry ? "Out" : "When", tap->dts);
    if(tap->has_entry) {
        uint32_t in = itso_dts_to_unix(tap->entry_dts);
        uint32_t at = itso_dts_to_unix(tap->dts);
        if(at > in && at - in < 86400) {
            uint32_t minutes = (at - in) / 60;
            if(minutes >= 60) {
                furi_string_cat_printf(
                    out,
                    "Journey time: %lu hr %lu min\n",
                    (unsigned long)(minutes / 60),
                    (unsigned long)(minutes % 60));
            } else {
                furi_string_cat_printf(out, "Journey time: %lu min\n", (unsigned long)minutes);
            }
        }
    }

    flipso_cat_location(out, f, "", "From", &tap->origin);
    flipso_cat_location(out, f, "", "Via", &tap->route);
    flipso_cat_location(out, f, "", "To", &tap->destination);

    if(tap->amount.valid && tap->amount.value) {
        flipso_cat_money(out, "", "Fare", &tap->amount);
        if(tap->has_mop)
            furi_string_cat_printf(out, "  Paid by: %s\n", itso_payment_name(tap->mop));
    }
    if(tap->no_fare_charged) flipso_cat_flag(out, "", "Fare collected", false);
    if(tap->return_ticket) flipso_cat_flag(out, "", "Return fare", true);
    if(tap->companion) flipso_cat_flag(out, "", "With a companion", true);
    if(tap->has_vat) flipso_cat_vat(out, "", tap->vat);

    if(tap->has_ipe_pointer) flipso_cat_product_ref(out, card, "", "Product", tap->ipe_pointer);
    if(tap->has_entry_oid) flipso_cat_operator(out, f, "", "Tapped in with", tap->entry_oid);

    /* Flags an inspector or a gate set against this journey. */
    if(tap->invalid_travel) furi_string_cat(out, "Invalid travel: Flagged\n");
    if(tap->inspected) flipso_cat_flag(out, "", "Ticket inspected", true);

    /* The products the gate weighed up for this journey: useful when the one it
     * picked is not the one you expected. */
    if(tap->has_cipe) {
        const char* sep = "Products checked: ";
        for(uint8_t c = 0; c < 4; c++) {
            if(!tap->cipe[c]) continue;
            furi_string_cat(out, sep);
            flipso_cat_product_name(out, card, tap->cipe[c]);
            sep = ", ";
        }
        if(sep[0] == ',') furi_string_push_back(out, '\n');
    }
}

/** True when a tap has anything for the Technical section. */
static bool flipso_tap_has_technical(const ItsoTap* tap) {
    return (tap->has_entry_oid && tap->entry_iin_index) || (tap->has_entry && tap->entry_isam) ||
           (tap->has_iin && !itso_iin_name(tap->iin)) || tap->has_writer;
}

/**
 * The numbers behind one tap: the machines that wrote it and the networks they
 * belong to. Nothing names a machine, so these are for whoever is working out
 * which gate did what, and would crowd a page the holder reads for where and
 * when. Headed by the record's name and time, as its own page has them, so
 * each can be matched to its journey.
 */
static void flipso_cat_tap_technical(FuriString* out, const FlipsoFormat* f, const ItsoTap* tap) {
    furi_string_cat_printf(out, "%s\n", flipso_tap_title(tap));
    flipso_cat_datetime_line(out, "  ", "When", tap->dts);
    /* The entry operator's own network, where it is not the card's. */
    if(tap->has_entry_oid && tap->entry_iin_index) {
        furi_string_cat(out, "  Tapped in on: Another network\n");
    }
    if(tap->has_entry) flipso_cat_machine(out, f, "  ", "Tap-in reader", tap->entry_isam);
    /* The record's own InstanceID: whose reader wrote this tap. */
    if(tap->has_writer) flipso_cat_machine(out, f, "  ", "Reader", tap->writer_isam);
    /* The network the machine that wrote it belongs to, where that is not
     * ITSO's own. */
    if(tap->has_iin && !itso_iin_name(tap->iin)) {
        furi_string_cat_printf(
            out, "  Reader network: Outside ITSO (%06lu)\n", (unsigned long)tap->iin);
    }
}

void flipso_format_taps(FuriString* out, const FlipsoFormat* f, const ItsoCard* card) {
    /* The first question on a bus or a train is where the holder stands now -
     * inside the gates, with a tap out still owed - so the log entry has the
     * first page, and each journey a page of its own after it, newest first. */
    if(card->log_entry_valid) {
        flipso_cat_page(out, FlipsoIconTaps, "Last tap");

        /* The entry/exit indicator counts closed systems - gated stations - the
         * holder is inside. Zero is outside all of them, which is also what
         * every bus tap leaves, so it is said as where the holder is rather than
         * as "tapped out". */
        flipso_cat_flag(out, "", "Inside ticket gates", card->log_eei != 0);
        if(card->log_eei > 1) furi_string_cat_printf(out, "  Gated areas: %u\n", card->log_eei);
        if(card->log_dts) flipso_cat_datetime_line(out, "", "When", card->log_dts);
        /* Where, from the newest journey record, as the summary says it: where
         * it ended if it was a tap out, where it began otherwise - but only
         * when that record is this entry's. An entry updated without a record
         * (LPF clear) has a newer time than the newest record, and that
         * record's place would be somewhere the holder was not at that time. */
        const ItsoTap* tap = flipso_latest_tap(card);
        if(tap && card->log_normal_mode && tap->dts == card->log_dts) {
            flipso_cat_location(
                out, f, "", "At", tap->destination.valid ? &tap->destination : &tap->origin);
        }
        flipso_cat_product_ref(out, card, "", "Product", card->log_ptr);
        /* LPF clear: the machine updated this entry and wrote no journey record. */
        if(!card->log_normal_mode) furi_string_cat(out, "Journey details: Not recorded\n");
        if(card->log_passback) {
            furi_string_cat_printf(out, "Passback timeout: %u min\n", card->log_passback);
        }
        if(card->tap_count == 0) furi_string_cat(out, "No journeys stored.\n");
    }

    /* The card's own log first, then whatever only a saved file remembers. */
    for(uint8_t section = 0; section < 2; section++) {
        for(uint8_t i = 0; i < card->tap_count; i++) {
            const ItsoTap* tap = &card->taps[i];
            if(tap->on_card == (section == 0)) flipso_cat_tap(out, f, card, tap);
        }
    }

    /* In the order the pages above have them: the card's own, then the file's. */
    bool headed = false;
    for(uint8_t section = 0; section < 2; section++) {
        for(uint8_t i = 0; i < card->tap_count; i++) {
            const ItsoTap* tap = &card->taps[i];
            if(tap->on_card != (section == 0) || !flipso_tap_has_technical(tap)) continue;
            if(!headed) {
                flipso_cat_page(out, FlipsoIconCode, "Technical");
                headed = true;
            } else {
                furi_string_push_back(out, '\n');
            }
            flipso_cat_tap_technical(out, f, tap);
        }
    }

    if(!card->log_entry_valid && card->tap_count == 0) {
        flipso_cat_page(out, FlipsoIconTaps, "Journeys");
        furi_string_cat(out, "No journey log on this card.\n");
    }
}
