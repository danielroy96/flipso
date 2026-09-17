/**
 * @file flipso_scene_taps.c
 * @brief Tap in/out state and the cyclic log of recent journeys.
 *
 * Two sources are shown. The Log Directory Entry holds the authoritative
 * in/out state (its entry/exit indicator says whether the holder is currently
 * inside a closed system). The cyclic log holds the individual tap records.
 */
#include "../flipso.h"

/** Name the product a log entry points at, so "E1" becomes something readable. */
static void flipso_cat_product_ref(FuriString* out, const Flipso* app, uint8_t dir_index) {
    if(dir_index == 0) return;

    for(uint8_t i = 0; i < app->card.product_count; i++) {
        const ItsoProduct* product = &app->card.products[i];
        if(product->dir_index != dir_index) continue;
        char title[32];
        flipso_product_title(product, title, sizeof(title));
        furi_string_cat_printf(out, "Product: %s\n", title);
        return;
    }

    furi_string_cat_printf(out, "Product: entry %u\n", dir_index);
}

void flipso_scene_taps_on_enter(void* context) {
    Flipso* app = context;
    const ItsoCard* card = &app->card;

    FuriString* text = furi_string_alloc();

    if(card->log_entry_valid) {
        furi_string_cat(text, "\e#Last tap\n");

        /* Entry/exit indicator: zero means outside a closed system, so the holder
         * has tapped out (or never tapped in). Higher values are nesting levels. */
        furi_string_cat_printf(
            text, "Tapped: %s\n", card->log_eei ? "IN" : "OUT");
        if(card->log_eei > 1) {
            furi_string_cat_printf(text, "Nesting level: %u\n", card->log_eei);
        }

        furi_string_cat(text, "Time: ");
        flipso_cat_datetime(text, card->log_dts);
        furi_string_push_back(text, '\n');

        flipso_cat_product_ref(text, app, card->log_ptr);

        if(card->log_passback) {
            furi_string_cat_printf(text, "Passback: %u min\n", card->log_passback);
        }
        if(!card->log_normal_mode) {
            furi_string_cat(text, "(basic mode: no journey\nrecord was written)\n");
        }
    }

    if(card->tap_count) {
        furi_string_cat(text, "\n\e#Journey log\n");

        for(uint8_t i = 0; i < card->tap_count; i++) {
            const ItsoTap* tap = &card->taps[i];

            if(i) furi_string_push_back(text, '\n');
            furi_string_cat_printf(
                text, "%s%s\n", itso_transaction_name(tap->transaction_type),
                tap->latest ? " (latest)" : "");

            furi_string_cat(text, "  ");
            flipso_cat_datetime(text, tap->dts);
            furi_string_push_back(text, '\n');

            /* A check-out record carries the check-in it closes, so show where
             * the journey began before where this tap happened. */
            if(tap->has_entry) {
                furi_string_cat(text, "  In at ");
                flipso_cat_datetime(text, tap->entry_dts);
                furi_string_push_back(text, '\n');
            }
            if(tap->has_entry_oid) {
                flipso_cat_operator(text, app, "  Entry op", tap->entry_oid);
            }

            flipso_cat_location(text, app, "  From", &tap->origin);
            flipso_cat_location(text, app, "  Via", &tap->route);
            flipso_cat_location(text, app, "  To", &tap->destination);

            if(tap->amount.valid && tap->amount.value) {
                char money[24];
                itso_format_money(&tap->amount, money, sizeof(money));
                furi_string_cat_printf(text, "  Fare: %s\n", money);
                if(tap->has_mop) {
                    furi_string_cat_printf(
                        text, "  Paid by: %s\n", itso_payment_name(tap->mop));
                }
            }
            if(tap->no_fare_charged) {
                furi_string_cat(text, "  Fare not collected\n");
            }
            if(tap->has_vat) {
                /* VAT is a percentage in 0.01 steps. */
                furi_string_cat_printf(
                    text, "  VAT: %u.%02u%%\n", tap->vat / 100, tap->vat % 100);
            }

            if(tap->has_ipe_pointer) {
                furi_string_cat(text, "  ");
                flipso_cat_product_ref(text, app, tap->ipe_pointer);
            }

            /* Flags an inspector or a gate set against this journey. */
            if(tap->invalid_travel) {
                furi_string_cat(text, "  Invalid travel flagged\n");
            }
            if(tap->inspected) {
                furi_string_cat(text, "  Inspected\n");
            }

            /* The products the gate weighed up for this journey: useful when the
             * one it picked is not the one you expected. */
            if(tap->has_cipe) {
                for(uint8_t c = 0; c < 4; c++) {
                    if(!tap->cipe[c]) continue;
                    furi_string_cat(text, "  Considered: ");
                    flipso_cat_product_ref(text, app, tap->cipe[c]);
                }
            }
        }
    } else if(!card->log_entry_valid) {
        furi_string_cat(text, "\e#Last taps\nNo journey log on this\ncard.\n");
    } else {
        furi_string_cat(text, "\n\e#Journey log\nNo journey records\nstored.\n");
    }

    flipso_text_view_set_text(app->text_view, furi_string_get_cstr(text));
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipsoViewText);

    furi_string_free(text);
}

bool flipso_scene_taps_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void flipso_scene_taps_on_exit(void* context) {
    Flipso* app = context;
    flipso_text_view_set_text(app->text_view, "");
}
