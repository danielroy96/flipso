/**
 * @file flipso_scene_products.c
 * @brief List of every product held in the shell's directory.
 */
#include "../flipso.h"

/* Every directory entry can be a product, so the list has to be able to show
 * as many rows as the decoder will keep. */
_Static_assert(
    FLIPSO_MENU_MAX_ITEMS >= ITSO_MAX_PRODUCTS,
    "the product list must hold every product the decoder can keep");

/* Longest of the status suffixes below, " [expired]", plus its terminator. */
#define FLIPSO_PRODUCT_SUFFIX_MAX 11

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
        if(product->status == ItsoProductStatusBlocked) {
            suffix = " [blocked]";
        } else if(itso_date_expired(product->expiry, now)) {
            suffix = " [expired]";
        } else if(product->status == ItsoProductStatusUnused) {
            suffix = " [unused]";
        }

        snprintf(title, sizeof(title), "%s%s", name, suffix);
        flipso_menu_view_add_item(menu, title, flipso_product_icon(product), i);
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
