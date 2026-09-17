/**
 * @file flipso_scene_menu.c
 * @brief Top-level menu for a card that has been read.
 *
 * Sections that the card does not carry are left out entirely rather than shown
 * as empty rows, so a plain pay-as-you-go card gets a two-item menu. Each row
 * carries an icon, because on a 128x64 screen the icon is what makes a row
 * recognisable before it is read.
 *
 * The header is the card's branding where the shell owner is one we can name a
 * card for, because "Freedom Pass" is what is printed on the card in the user's
 * hand and "ITSO Card" is a fact about the standard behind it.
 */
#include "../flipso.h"
#include "flipso_icons.h"

typedef enum {
    FlipsoMenuItemCard,
    FlipsoMenuItemPayg,
    FlipsoMenuItemId,
    FlipsoMenuItemTaps,
    FlipsoMenuItemProducts,
} FlipsoMenuItem;

static void flipso_scene_menu_callback(void* context, uint32_t index) {
    Flipso* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void flipso_scene_menu_on_enter(void* context) {
    Flipso* app = context;
    FlipsoMenuView* menu = app->menu_view;

    flipso_menu_view_reset(menu);
    flipso_menu_view_set_callback(menu, flipso_scene_menu_callback, app);
    /* The shell owner brands the card. A product owner does not: a rail season
     * ticket sold by one operator sits happily on another's card. */
    const char* brand = flipso_operators_brand(app->operators, app->card.oid);
    flipso_menu_view_set_header(menu, brand ? brand : "ITSO Card");

    flipso_menu_view_add_item(menu, "Card", &I_card_10px, FlipsoMenuItemCard);

    if(flipso_find_product(app, ItsoTypStoredTravelRights)) {
        flipso_menu_view_add_item(menu, "Pay as you go", &I_purse_10px, FlipsoMenuItemPayg);
    }

    if(flipso_find_product(app, ItsoTypId) || flipso_find_product(app, ItsoTypEntitlement)) {
        flipso_menu_view_add_item(menu, "ID & entitlement", &I_id_10px, FlipsoMenuItemId);
    }

    if(app->card.log_entry_valid || app->card.tap_count) {
        flipso_menu_view_add_item(menu, "Last taps", &I_taps_10px, FlipsoMenuItemTaps);
    }

    if(app->card.product_count) {
        char label[FLIPSO_MENU_LABEL_LEN];
        snprintf(label, sizeof(label), "Products (%u)", app->card.product_count);
        flipso_menu_view_add_item(menu, label, &I_products_10px, FlipsoMenuItemProducts);
    }

    /* Restore the highlighted row when coming back from a detail screen. */
    flipso_menu_view_set_selected(
        menu, scene_manager_get_scene_state(app->scene_manager, FlipsoSceneMenu));

    view_dispatcher_switch_to_view(app->view_dispatcher, FlipsoViewMenu);
}

bool flipso_scene_menu_on_event(void* context, SceneManagerEvent event) {
    Flipso* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;

    FlipsoScene next;
    switch(event.event) {
    case FlipsoMenuItemCard:
        next = FlipsoSceneCard;
        break;
    case FlipsoMenuItemPayg:
        next = FlipsoScenePayg;
        break;
    case FlipsoMenuItemId:
        next = FlipsoSceneId;
        break;
    case FlipsoMenuItemTaps:
        next = FlipsoSceneTaps;
        break;
    case FlipsoMenuItemProducts:
        next = FlipsoSceneProducts;
        break;
    default:
        /* Not one of ours - a scan event that arrived after the scene changed,
         * for instance. Saving it as the selection would move the highlight to
         * a row that does not exist. */
        return false;
    }

    scene_manager_set_scene_state(app->scene_manager, FlipsoSceneMenu, event.event);
    scene_manager_next_scene(app->scene_manager, next);
    return true;
}

void flipso_scene_menu_on_exit(void* context) {
    Flipso* app = context;
    flipso_menu_view_reset(app->menu_view);
}
