/**
 * @file flipso_format_journeys.c
 * @brief The journeys screen: where the card stands, and the journey log.
 */
#include "flipso_format_i.h"

/** "Tap out (latest)": what a record was, which heads its lines. */
static void flipso_cat_tap_title(FuriString* out, const ItsoTap* tap) {
    furi_string_cat_printf(
        out,
        "%s%s\n",
        itso_transaction_name(tap->transaction_type),
        tap->latest ? " (latest)" : "");
}

/** One Transient Ticket Record, less its reader numbers: see flipso_cat_tap_technical(). */
static void flipso_cat_tap(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoTap* tap) {
    flipso_cat_tap_title(out, tap);
    /* A tap-out record carries the tap-in it closes, copied forward so the
     * record stands on its own - so a tap out has two times, and says which is
     * which, and how long the journey took between them. Any other record has
     * one, labelled "When" as every other time on these screens is. Short
     * labels, so an indented date and time still fits on one row. */
    flipso_cat_datetime_line(out, "  ", tap->has_entry ? "Out" : "When", tap->dts);
    if(tap->has_entry) {
        flipso_cat_datetime_line(out, "  ", "In", tap->entry_dts);
        uint32_t in = itso_dts_to_unix(tap->entry_dts);
        uint32_t at = itso_dts_to_unix(tap->dts);
        if(at > in && at - in < 86400) {
            uint32_t minutes = (at - in) / 60;
            if(minutes >= 60) {
                furi_string_cat_printf(
                    out,
                    "  Journey time: %lu hr %lu min\n",
                    (unsigned long)(minutes / 60),
                    (unsigned long)(minutes % 60));
            } else {
                furi_string_cat_printf(out, "  Journey time: %lu min\n", (unsigned long)minutes);
            }
        }
    }
    if(tap->has_entry_oid) flipso_cat_operator(out, f, "  ", "Tapped in with", tap->entry_oid);

    flipso_cat_location(out, f, "  ", "From", &tap->origin);
    flipso_cat_location(out, f, "  ", "Via", &tap->route);
    flipso_cat_location(out, f, "  ", "To", &tap->destination);

    if(tap->amount.valid && tap->amount.value) {
        flipso_cat_money(out, "  ", "Fare", &tap->amount);
        if(tap->has_mop)
            furi_string_cat_printf(out, "  Paid by: %s\n", itso_payment_name(tap->mop));
    }
    if(tap->no_fare_charged) flipso_cat_flag(out, "  ", "Fare collected", false);
    if(tap->return_ticket) flipso_cat_flag(out, "  ", "Return fare", true);
    if(tap->companion) flipso_cat_flag(out, "  ", "With a companion", true);
    if(tap->has_vat) flipso_cat_vat(out, "  ", tap->vat);

    if(tap->has_ipe_pointer) flipso_cat_product_ref(out, card, "  ", "Product", tap->ipe_pointer);

    /* Flags an inspector or a gate set against this journey. */
    if(tap->invalid_travel) furi_string_cat(out, "  Invalid travel: Flagged\n");
    if(tap->inspected) flipso_cat_flag(out, "  ", "Ticket inspected", true);

    /* The products the gate weighed up for this journey: useful when the one it
     * picked is not the one you expected. */
    if(tap->has_cipe) {
        const char* sep = "  Products checked: ";
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
 * which gate did what, and would crowd a log the holder reads for where and
 * when. Headed by the record's name and time, as the log above has it, so each
 * can be matched to its journey.
 */
static void flipso_cat_tap_technical(FuriString* out, const FlipsoFormat* f, const ItsoTap* tap) {
    flipso_cat_tap_title(out, tap);
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
    if(card->log_entry_valid) {
        flipso_cat_heading(out, FlipsoIconTaps, "Last tap");

        /* The entry/exit indicator counts closed systems - gated stations - the
         * holder is inside. Zero is outside all of them, which is also what
         * every bus tap leaves, so it is said as where the holder is rather than
         * as "tapped out". */
        flipso_cat_flag(out, "", "Inside ticket gates", card->log_eei != 0);
        if(card->log_eei > 1) furi_string_cat_printf(out, "  Gated areas: %u\n", card->log_eei);
        if(card->log_dts) flipso_cat_datetime_line(out, "", "When", card->log_dts);
        flipso_cat_product_ref(out, card, "", "Product", card->log_ptr);
        if(card->log_passback) {
            furi_string_cat_printf(out, "Passback timeout: %u min\n", card->log_passback);
        }
        /* LPF clear: the machine updated this entry and wrote no journey record. */
        if(!card->log_normal_mode) furi_string_cat(out, "Journey details: Not recorded\n");
    }

    /* The card's own log first, then whatever only a saved file remembers. */
    uint8_t on_card = 0, past = 0;
    for(uint8_t i = 0; i < card->tap_count; i++) {
        if(card->taps[i].on_card) {
            on_card++;
        } else {
            past++;
        }
    }

    for(uint8_t section = 0; section < 2; section++) {
        const bool live = section == 0;
        if((live ? on_card : past) == 0) continue;
        if(!furi_string_empty(out)) furi_string_cat(out, "\n");
        flipso_cat_heading(
            out, live ? FlipsoIconTaps : FlipsoIconPast, live ? "Journey log" : "Off card");
        bool first = true;
        for(uint8_t i = 0; i < card->tap_count; i++) {
            const ItsoTap* tap = &card->taps[i];
            if(tap->on_card != live) continue;
            if(!first) furi_string_push_back(out, '\n');
            first = false;
            flipso_cat_tap(out, f, card, tap);
        }
    }

    /* In the order the log above lists them: the card's own, then the file's. */
    bool headed = false;
    for(uint8_t section = 0; section < 2; section++) {
        for(uint8_t i = 0; i < card->tap_count; i++) {
            const ItsoTap* tap = &card->taps[i];
            if(tap->on_card != (section == 0) || !flipso_tap_has_technical(tap)) continue;
            furi_string_push_back(out, '\n');
            if(!headed) {
                flipso_cat_heading(out, FlipsoIconNone, "Technical");
                headed = true;
            }
            flipso_cat_tap_technical(out, f, tap);
        }
    }

    if(card->tap_count == 0) {
        if(!card->log_entry_valid) {
            flipso_cat_heading(out, FlipsoIconTaps, "Journeys");
            furi_string_cat(out, "No journey log on this card.\n");
        } else {
            furi_string_cat(out, "\n");
            flipso_cat_heading(out, FlipsoIconTaps, "Journey log");
            furi_string_cat(out, "No journeys stored.\n");
        }
    }
}
