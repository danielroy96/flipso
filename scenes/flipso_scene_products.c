/**
 * @file flipso_scene_products.c
 * @brief List of the products on the card, and of any the card has dropped.
 *
 * The purse, the ID and the entitlement are left out while the card holds
 * them: each has a row of its own on the card menu, and listing them here too
 * made two ways to one screen (flipso_product_listed()).
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

static void flipso_scene_products_callback(void* context, uint32_t index) {
    Flipso* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

/** True when another row of the list will carry the same name as @p index. */
static bool flipso_scene_products_shares_name(const ItsoCard* card, uint8_t index) {
    const char* title = flipso_product_title(&card->products[index]);
    for(uint8_t i = 0; i < card->product_count; i++) {
        if(i == index || !flipso_product_listed(&card->products[i])) continue;
        if(strcmp(flipso_product_title(&card->products[i]), title) == 0) return true;
    }
    return false;
}

void flipso_scene_products_on_enter(void* context) {
    Flipso* app = context;
    FlipsoMenuView* menu = app->menu_view;
    ItsoUnixTime now = flipso_now();

    flipso_menu_view_reset(menu);
    flipso_menu_view_set_callback(menu, flipso_scene_products_callback, app);
    flipso_menu_view_set_header(menu, "Products");
    flipso_menu_view_set_header_icon(menu, &I_products_10px);

    FuriString* tag = furi_string_alloc();
    for(uint8_t i = 0; i < app->card.product_count; i++) {
        const ItsoProduct* product = &app->card.products[i];
        if(!flipso_product_listed(product)) continue;

        /* Expired, blocked and dropped products are flagged in the list so
         * the user does not have to open each one to find the live ticket.
         * With nothing to flag, two products of one type - two season tickets
         * - are told apart by when they run to. */
        furi_string_reset(tag);
        const char* status = flipso_product_tag(product, now);
        if(status) {
            furi_string_set(tag, status);
        } else if(product->expiry && flipso_scene_products_shares_name(&app->card, i)) {
            flipso_cat_short_date(tag, product->expiry);
        }

        /* A clock rather than the product's own icon for a dropped product:
         * the label already names the type, so the icon is what makes the two
         * groups tell apart at a glance down the list. */
        const Icon* icon = product->on_card ? flipso_icon(flipso_product_icon(product)) :
                                              &I_past_10px;
        flipso_menu_view_add_tagged_item(
            menu, flipso_product_title(product), furi_string_get_cstr(tag), icon, i);
    }
    furi_string_free(tag);

    flipso_menu_view_set_selected(
        menu, scene_manager_get_scene_state(app->scene_manager, FlipsoSceneProducts));

    view_dispatcher_switch_to_view(app->view_dispatcher, FlipsoViewMenu);
}

bool flipso_scene_products_on_event(void* context, SceneManagerEvent event) {
    Flipso* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event >= app->card.product_count) return false;
    if(!flipso_product_listed(&app->card.products[event.event])) return false;

    app->selected_product = (uint8_t)event.event;
    scene_manager_set_scene_state(app->scene_manager, FlipsoSceneProducts, event.event);
    flipso_open_text(app, FlipsoTextProduct);
    return true;
}

void flipso_scene_products_on_exit(void* context) {
    Flipso* app = context;
    flipso_menu_view_reset(app->menu_view);
}
