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
 *
 * The last row is where the card came from and where it can go: a card just
 * read can be saved, and a card opened from the SD card can be deleted. They
 * are mutually exclusive, so the list never grows by more than one row.
 */
#include "../flipso.h"
#include "flipso_icons.h"

typedef enum {
    FlipsoMenuItemCard,
    FlipsoMenuItemPayg,
    FlipsoMenuItemId,
    FlipsoMenuItemTaps,
    FlipsoMenuItemProducts,
    FlipsoMenuItemSave,
    FlipsoMenuItemRename,
    FlipsoMenuItemDelete,
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
     * ticket sold by one operator sits happily on another's card.
     *
     * A blocked shell displaces the branding entirely. The header is the one
     * line of this screen that is read every time, and someone who never opens
     * Card would otherwise leave without learning the card is dead. */
    const char* brand = flipso_operators_brand(app->operators, app->card.oid);
    flipso_menu_view_set_header(
        menu, app->card.shell_blocked ? "Blocked Card" : (brand ? brand : "ITSO Card"));
    flipso_menu_view_set_header_icon(menu, app->card.shell_blocked ? &I_warning_10px : NULL);

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
        /* Counted apart, because the two numbers answer different questions:
         * how many products are on the card, and how many rows the list has. A
         * saved card can remember products the card has since dropped, and
         * folding those into one figure would overstate the card. */
        uint8_t on_card = 0;
        for(uint8_t i = 0; i < app->card.product_count; i++) {
            if(app->card.products[i].on_card) on_card++;
        }
        uint8_t past = (uint8_t)(app->card.product_count - on_card);

        char label[FLIPSO_MENU_LABEL_LEN];
        if(past) {
            snprintf(label, sizeof(label), "Products (%u, %u past)", on_card, past);
        } else {
            snprintf(label, sizeof(label), "Products (%u)", on_card);
        }
        flipso_menu_view_add_item(menu, label, &I_products_10px, FlipsoMenuItemProducts);
    }

    if(furi_string_empty(app->loaded_path)) {
        /* Nothing to write without the bytes the read produced - a card whose
         * shell read but whose directory did not still has a shell to keep, but
         * a read that never got that far has nothing. */
        if(flipso_capture_valid(app->capture)) {
            flipso_menu_view_add_item(menu, "Save card", &I_save_10px, FlipsoMenuItemSave);
        }
    } else {
        /* The name is the only part of a saved card that is the user's rather
         * than the card's, so it is the only part there is anything to change. */
        flipso_menu_view_add_item(menu, "Rename card", &I_rename_10px, FlipsoMenuItemRename);
        flipso_menu_view_add_item(menu, "Delete card", &I_delete_10px, FlipsoMenuItemDelete);
    }

    /* Restore the highlighted row when coming back from a detail screen. The
     * rows at the bottom swap as the card is saved - Save becomes Rename and
     * Delete - so a selection stored under a row that is no longer there has to
     * be read as its counterpart, or saving a card would drop the highlight
     * back to the top of the list. Rename and Delete both survive as
     * themselves, which is what keeps Cancel on either of them where it was. */
    uint32_t selected = scene_manager_get_scene_state(app->scene_manager, FlipsoSceneMenu);
    bool scanned = furi_string_empty(app->loaded_path);
    if(scanned && (selected == FlipsoMenuItemRename || selected == FlipsoMenuItemDelete)) {
        selected = FlipsoMenuItemSave;
    } else if(!scanned && selected == FlipsoMenuItemSave) {
        selected = FlipsoMenuItemRename;
    }
    flipso_menu_view_set_selected(menu, selected);

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
    case FlipsoMenuItemSave:
        next = FlipsoSceneSave;
        break;
    case FlipsoMenuItemRename:
        next = FlipsoSceneRename;
        break;
    case FlipsoMenuItemDelete:
        next = FlipsoSceneDelete;
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
