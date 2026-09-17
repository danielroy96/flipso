/**
 * @file flipso_scene_media.c
 * @brief What the card said about itself when it would not say anything else.
 *
 * Reached from the error screen for a card Flipso recognises but cannot decode.
 * It is deliberately a plain scrolling dump rather than a summary: the point of
 * the screen is that nothing is being interpreted away.
 */
#include "../flipso.h"

void flipso_scene_media_on_enter(void* context) {
    Flipso* app = context;

    FuriString* text = furi_string_alloc();
    flipso_media_cat(text, &app->media);

    flipso_text_view_set_text(app->text_view, furi_string_get_cstr(text));
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipsoViewText);

    furi_string_free(text);
}

bool flipso_scene_media_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void flipso_scene_media_on_exit(void* context) {
    Flipso* app = context;
    flipso_text_view_set_text(app->text_view, "");
}
