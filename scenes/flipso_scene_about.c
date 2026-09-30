/**
 * @file flipso_scene_about.c
 * @brief The About menu: what the app knows, and the demo cards packaged with it.
 *
 * The demo cards are here rather than beside Saved cards on the scan screen
 * because they are not the user's: they show what Flipso can read to someone
 * who has no card to hand, which is a question about the app.
 */
#include "../flipso.h"
#include "flipso_icons.h"

typedef enum {
    FlipsoAboutItemInfo,
    FlipsoAboutItemDemos,
} FlipsoAboutItem;

static void flipso_scene_about_callback(void* context, uint32_t index) {
    Flipso* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void flipso_scene_about_on_enter(void* context) {
    Flipso* app = context;
    FlipsoMenuView* menu = app->menu_view;

    flipso_menu_view_reset(menu);
    flipso_menu_view_set_callback(menu, flipso_scene_about_callback, app);
    flipso_menu_view_set_header(menu, "About");
    flipso_menu_view_set_header_icon(menu, &I_info_10px);

    flipso_menu_view_add_item(menu, "About Flipso", &I_info_10px, FlipsoAboutItemInfo);

    /* The demo cards are unpacked from the .fap on install, so the only way to
     * have none is to have deleted them, and a row leading to an empty list
     * would say less than no row. */
    uint8_t demos = flipso_saved_demos(NULL);
    if(demos) {
        char count[FLIPSO_MENU_TAG_LEN];
        snprintf(count, sizeof(count), "%u", demos);
        flipso_menu_view_add_tagged_item(
            menu, "Demo cards", count, &I_card_10px, FlipsoAboutItemDemos);
    }

    flipso_menu_view_set_selected(
        menu, scene_manager_get_scene_state(app->scene_manager, FlipsoSceneAbout));

    view_dispatcher_switch_to_view(app->view_dispatcher, FlipsoViewMenu);
}

bool flipso_scene_about_on_event(void* context, SceneManagerEvent event) {
    Flipso* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;

    switch(event.event) {
    case FlipsoAboutItemInfo:
        scene_manager_set_scene_state(app->scene_manager, FlipsoSceneAbout, event.event);
        flipso_open_text(app, FlipsoTextAbout);
        return true;
    case FlipsoAboutItemDemos:
        scene_manager_set_scene_state(app->scene_manager, FlipsoSceneAbout, event.event);
        scene_manager_next_scene(app->scene_manager, FlipsoSceneDemos);
        return true;
    default:
        return false;
    }
}

void flipso_scene_about_on_exit(void* context) {
    Flipso* app = context;
    flipso_menu_view_reset(app->menu_view);
}
