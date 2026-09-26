/**
 * @file flipso_scene_payg.c
 * @brief Stored travel rights: the pay-as-you-go purse or purses on the card.
 */
#include "../flipso.h"

void flipso_scene_payg_on_enter(void* context) {
    Flipso* app = context;
    const ItsoCard* card = &app->card;
    uint32_t now = flipso_now();

    FuriString* text = furi_string_alloc();
    uint8_t found = 0;

    for(uint8_t i = 0; i < card->product_count; i++) {
        const ItsoProduct* product = &card->products[i];
        /* What the card holds now, which is what this screen is about; a
         * product it has dropped is the product list's to show. */
        if(!product->on_card) continue;
        if(product->typ != ItsoTypStoredTravelRights) continue;

        if(found) furi_string_cat(text, "\n");
        furi_string_cat(text, "\e#Pay as you go\n");
        found++;

        char money[24];
        if(product->balance.valid) {
            itso_format_money(&product->balance, money, sizeof(money));
            furi_string_cat_printf(text, "Balance: %s\n", money);
        } else {
            furi_string_cat(text, "Balance: not readable\n");
        }

        flipso_cat_operator(text, app, "Operator", product->oid);
        if(product->has_retailer && product->retailer != product->oid) {
            flipso_cat_operator(text, app, "Sold by", product->retailer);
        }

        flipso_cat_last_transaction(text, product);

        /* A journey in progress: what has been spent so far across its legs,
         * which is what a daily cap or multi-leg discount is measured against. */
        if(product->has_journey && (product->journey_legs || product->cumulative_fare.value)) {
            furi_string_cat_printf(text, "Journey legs: %u\n", product->journey_legs);
            flipso_cat_money(text, "Fare so far", &product->cumulative_fare);
        }

        flipso_cat_expiry(text, "Expires", "Expired", product->expiry, now);

        furi_string_cat_printf(text, "Status: %s\n", itso_status_name(product->status));

        flipso_cat_purse_terms(text, product);
        /* How far this purse is towards its fare caps, where the operator caps. */
        flipso_cat_capping(text, app, product);
        /* Last, because the terms are what the purse is and the history is what
         * has happened to it: a card that has been read more than once can
         * carry several screenfuls of the latter. */
        flipso_cat_value_history(text, product);
    }

    if(!found) {
        furi_string_cat(text, "\e#Pay as you go\nNo purse on this card.\n");
    }

    flipso_text_view_set_text(app->text_view, furi_string_get_cstr(text));
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipsoViewText);

    furi_string_free(text);
}

bool flipso_scene_payg_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void flipso_scene_payg_on_exit(void* context) {
    Flipso* app = context;
    flipso_text_view_set_text(app->text_view, "");
}
