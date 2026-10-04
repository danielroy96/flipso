/**
 * @file flipso_scene_demos.c
 * @brief The demo cards packaged with the app, and opening one.
 *
 * A demo card is a saved-card file, so opening one is exactly opening a saved
 * card: the decoder that is running reads its blocks, and what is on screen is
 * that decoder's reading of them rather than a mock-up of the screens.
 *
 * Back from a demo card comes back here, with the cursor on it, so that going
 * through them one after another is a Back and a Down each.
 */
#include "../flipso.h"
#include "flipso_icons.h"

static void flipso_scene_demos_callback(void* context, uint32_t index) {
    Flipso* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

/* "Demo 01 The Key Kent" is "The Key Kent" in a list headed "Demo cards": the
 * prefix is there to mark the file out among saved cards, and the number only
 * to put the files in order, which the list already is. */
static const char* flipso_scene_demos_label(const char* name) {
    const char* prefix = "Demo ";
    if(strncmp(name, prefix, strlen(prefix)) != 0) return name;
    const char* label = name + strlen(prefix);
    while(*label >= '0' && *label <= '9') {
        label++;
    }
    while(*label == ' ') {
        label++;
    }
    return *label ? label : name;
}

void flipso_scene_demos_on_enter(void* context) {
    Flipso* app = context;
    FlipsoMenuView* menu = app->menu_view;

    /* On the heap and only while the list is up: twenty names is more than
     * the 4 KB stack should carry, and nothing needs them once a card is open.
     * The list's rows are labelled with them in place. */
    if(!app->demos) app->demos = malloc(sizeof(FlipsoDemos));
    flipso_saved_demos(app->demos);

    flipso_menu_view_reset(menu);
    flipso_menu_view_set_callback(menu, flipso_scene_demos_callback, app);
    flipso_menu_view_set_header(menu, "Demo cards");
    flipso_menu_view_set_header_icon(menu, &I_card_10px);
    for(uint8_t i = 0; i < app->demos->count; i++) {
        flipso_menu_view_add_item(
            menu, flipso_scene_demos_label(app->demos->names[i]), &I_card_10px, i);
    }
    flipso_menu_view_set_selected(
        menu, scene_manager_get_scene_state(app->scene_manager, FlipsoSceneDemos));

    view_dispatcher_switch_to_view(app->view_dispatcher, FlipsoViewMenu);
}

bool flipso_scene_demos_on_event(void* context, SceneManagerEvent event) {
    Flipso* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;
    if(!app->demos || event.event >= app->demos->count) return false;

    scene_manager_set_scene_state(app->scene_manager, FlipsoSceneDemos, event.event);

    FuriString* path = furi_string_alloc();
    flipso_saved_demo_path(path, app->demos->names[event.event]);
    bool loaded = flipso_load_card(app, furi_string_get_cstr(path));
    furi_string_free(path);

    if(loaded) {
        scene_manager_next_scene(app->scene_manager, FlipsoSceneMenu);
    } else {
        /* A demo card is written by the build it ships with, so one that will
         * not open has been damaged on the SD card since. */
        flipso_saved_alert("Cannot open demo", "Reinstall Flipso to\nrestore the demo cards.");
    }
    return true;
}

void flipso_scene_demos_on_exit(void* context) {
    Flipso* app = context;
    /* The rows point into the names rather than copying them, so the list is
     * emptied before the names go. */
    flipso_menu_view_reset(app->menu_view);
    free(app->demos);
    app->demos = NULL;
}
