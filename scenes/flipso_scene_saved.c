/**
 * @file flipso_scene_saved.c
 * @brief Open a card saved earlier, and hand it to the same screens a tap does.
 *
 * The scene has no view of its own: it runs the firmware's file browser, which
 * is modal and returns once the user has chosen or backed out. That is why the
 * work happens in on_enter and the navigation in on_event - moving to another
 * scene from inside on_enter would re-enter the scene manager while it is still
 * entering this one.
 *
 * Coming back here from the card is deliberate: on_enter runs again, so Back
 * from a saved card returns to the list of them rather than to the scanner.
 */
#include "../flipso.h"

void flipso_scene_saved_on_enter(void* context) {
    Flipso* app = context;

    FuriString* path = furi_string_alloc();
    /* A card still named here is the one the user has just backed out of. */
    FuriString* select = furi_string_alloc_set(app->loaded_path);
    bool loaded = false;

    /* Back into the list after a file that will not open, with the cursor on
     * it: the user is choosing a card, and the one they chose not working is
     * no reason to throw them out of the list. */
    while(!loaded && flipso_saved_pick(path, select)) {
        /* Decoding here rather than at save time is the whole point of keeping
         * the raw blocks: the card is parsed by the build that is running, so a
         * decoder fix reaches the cards already on the SD card. */
        loaded = flipso_saved_read(app->capture, furi_string_get_cstr(path)) &&
                 flipso_capture_decode(app->capture, &app->card);

        if(loaded) {
            furi_string_set(app->loaded_path, path);
            flipso_reset_card_menus(app);
            /* What the chip said when it was read, for the Card screen; a card
             * saved before the file kept it simply has none. */
            flipso_media_reset(&app->media);
            size_t chip_len = 0;
            const uint8_t* chip = flipso_capture_chip(app->capture, &chip_len);
            if(chip) flipso_media_parse_chip(&app->media, chip, chip_len);
            /* The detail scenes read this to decide a card was read at all. */
            app->status = FlipsoReaderStatusSuccess;
        } else {
            itso_card_reset(&app->card);
            flipso_capture_reset(app->capture);
            flipso_saved_alert(
                "Cannot open card", "The file is damaged, or was\nsaved by a newer Flipso.");
            furi_string_set(select, path);
        }
    }

    furi_string_free(select);
    furi_string_free(path);

    view_dispatcher_send_custom_event(
        app->view_dispatcher,
        loaded ? FlipsoCustomEventSavedPicked : FlipsoCustomEventSavedCancelled);
}

bool flipso_scene_saved_on_event(void* context, SceneManagerEvent event) {
    Flipso* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == FlipsoCustomEventSavedPicked) {
        scene_manager_next_scene(app->scene_manager, FlipsoSceneMenu);
        return true;
    }

    if(event.event == FlipsoCustomEventSavedCancelled) {
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }

    return false;
}

void flipso_scene_saved_on_exit(void* context) {
    UNUSED(context);
}
