/**
 * @file flipso_scene_taps.c
 * @brief Where the card stands in a journey, and the journey log.
 */
#include "../flipso.h"

void flipso_scene_taps_on_enter(void* context) {
    Flipso* app = context;
    FlipsoFormat f = flipso_format_context(app);

    FuriString* text = furi_string_alloc();
    flipso_format_taps(text, &f, &app->card);

    flipso_show_text(app, text);
    furi_string_free(text);
}

bool flipso_scene_taps_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void flipso_scene_taps_on_exit(void* context) {
    Flipso* app = context;
    flipso_text_view_set_text(app->text_view, "");
}
