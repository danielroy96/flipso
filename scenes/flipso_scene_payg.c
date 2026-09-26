/**
 * @file flipso_scene_payg.c
 * @brief Stored travel rights: the pay-as-you-go purse or purses on the card.
 */
#include "../flipso.h"

void flipso_scene_payg_on_enter(void* context) {
    Flipso* app = context;
    FlipsoFormat f = flipso_format_context(app);

    FuriString* text = furi_string_alloc();
    flipso_format_payg(text, &f, &app->card);

    flipso_show_text(app, text);
    furi_string_free(text);
}

bool flipso_scene_payg_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void flipso_scene_payg_on_exit(void* context) {
    Flipso* app = context;
    flipso_text_view_set_text(app->text_view, "");
}
