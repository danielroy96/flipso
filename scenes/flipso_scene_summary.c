/**
 * @file flipso_scene_summary.c
 * @brief The card at a glance: its state, what it holds, and its last tap.
 *
 * The first row of a card's menu, because it answers the question most people
 * tap a card to ask - is it working, and what is on it - without opening each
 * of the screens below it.
 */
#include "../flipso.h"

void flipso_scene_summary_on_enter(void* context) {
    Flipso* app = context;
    FlipsoFormat f = flipso_format_context(app);

    FuriString* text = furi_string_alloc();
    flipso_format_summary(text, &f, &app->card);

    flipso_show_text(app, text);
    furi_string_free(text);
}

bool flipso_scene_summary_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void flipso_scene_summary_on_exit(void* context) {
    Flipso* app = context;
    flipso_text_view_set_text(app->text_view, "");
}
