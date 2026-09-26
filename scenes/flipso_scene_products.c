/**
 * @file flipso_scene_products.c
 * @brief List of every product on the card, and of any the card has dropped.
 *
 * A saved card can know about products the card itself has forgotten - an
 * expired ticket whose directory entry has since been freed - so the list has
 * two kinds of row in it, and the distinction is the first thing it has to
 * make: everything else on these screens is about a card the user is holding.
 */
#include "../flipso.h"
#include "flipso_icons.h"

/* Every directory entry can be a product, and a saved card adds the ones the
 * card has dropped since, so the list has to show as many rows as the decoder
 * will keep. */
_Static_assert(
    FLIPSO_MENU_MAX_ITEMS >= ITSO_MAX_CARD_PRODUCTS,
    "the product list must hold every product the decoder can keep");

/* Longest of the status suffixes below, " [off card]", plus its terminator. */
#define FLIPSO_PRODUCT_SUFFIX_MAX 12

static void flipso_scene_products_callback(void* context, uint32_t index) {
    Flipso* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void flipso_scene_products_on_enter(void* context) {
    Flipso* app = context;
    FlipsoMenuView* menu = app->menu_view;
    uint32_t now = flipso_now();

    flipso_menu_view_reset(menu);
    flipso_menu_view_set_callback(menu, flipso_scene_products_callback, app);
    flipso_menu_view_set_header(menu, "Products");

    for(uint8_t i = 0; i < app->card.product_count; i++) {
        const ItsoProduct* product = &app->card.products[i];

        /* Flag expired and blocked products in the list so the user does not
         * have to open each one to find the live ticket. */
        char title[FLIPSO_MENU_LABEL_LEN];
        /* Sized so the name plus the longest suffix still fits the row label;
         * no product name is anywhere near this long. */
        char name[FLIPSO_MENU_LABEL_LEN - FLIPSO_PRODUCT_SUFFIX_MAX];
        flipso_product_title(product, name, sizeof(name));

        const char* suffix = "";
        if(!product->on_card) {
            /* Said before anything else about it, because it is the one thing
             * that is not true of the card in front of you: this product was on
             * it when the record was written and is not on it now. Whether it
             * was blocked or expired when it left is the detail screen's to
             * tell - it is history either way. */
            suffix = " [off card]";
        } else if(product->status == ItsoProductStatusBlocked) {
            suffix = " [blocked]";
        } else if(itso_date_expired(product->expiry, now)) {
            suffix = " [expired]";
        } else if(product->status == ItsoProductStatusUnused) {
            suffix = " [unused]";
        }

        snprintf(title, sizeof(title), "%s%s", name, suffix);
        /* A clock rather than the product's own icon: the label already names
         * the type, so the icon is what makes the two groups tell apart at a
         * glance down the list. */
        const Icon* icon = product->on_card ? flipso_product_icon(product) : &I_past_10px;
        flipso_menu_view_add_item(menu, title, icon, i);
    }

    flipso_menu_view_set_selected(
        menu, scene_manager_get_scene_state(app->scene_manager, FlipsoSceneProducts));

    view_dispatcher_switch_to_view(app->view_dispatcher, FlipsoViewMenu);
}

bool flipso_scene_products_on_event(void* context, SceneManagerEvent event) {
    Flipso* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event >= app->card.product_count) return false;

    app->selected_product = (uint8_t)event.event;
    scene_manager_set_scene_state(app->scene_manager, FlipsoSceneProducts, event.event);
    scene_manager_next_scene(app->scene_manager, FlipsoSceneProduct);
    return true;
}

void flipso_scene_products_on_exit(void* context) {
    Flipso* app = context;
    flipso_menu_view_reset(app->menu_view);
}
