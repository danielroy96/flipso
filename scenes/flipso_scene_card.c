/**
 * @file flipso_scene_card.c
 * @brief Card number, expiry and issuer, plus the chip and layout underneath.
 */
#include "../flipso.h"

void flipso_scene_card_on_enter(void* context) {
    Flipso* app = context;
    FlipsoFormat f = flipso_format_context(app);

    FuriString* text = furi_string_alloc();
    /* Where this came from, for a card opened off the SD card. */
    FuriString* name = NULL;
    if(!furi_string_empty(app->loaded_path)) {
        name = furi_string_alloc();
        flipso_saved_name(name, furi_string_get_cstr(app->loaded_path));
    }
    flipso_format_card(
        text, &f, &app->card, name ? furi_string_get_cstr(name) : NULL,
        flipso_capture_time(app->capture));
    if(name) furi_string_free(name);

    flipso_show_text(app, text);
    furi_string_free(text);
}

bool flipso_scene_card_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void flipso_scene_card_on_exit(void* context) {
    Flipso* app = context;
    flipso_text_view_set_text(app->text_view, "");
}
