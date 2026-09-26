/**
 * @file flipso_scene_product.c
 * @brief Everything Flipso decoded about one product.
 */
#include "../flipso.h"

void flipso_scene_product_on_enter(void* context) {
    Flipso* app = context;

    if(app->selected_product >= app->card.product_count) {
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    FlipsoFormat f = flipso_format_context(app);
    FuriString* text = furi_string_alloc();
    flipso_format_product(text, &f, &app->card, &app->card.products[app->selected_product]);
    flipso_show_text(app, text);
    furi_string_free(text);
}

bool flipso_scene_product_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void flipso_scene_product_on_exit(void* context) {
    Flipso* app = context;
    flipso_text_view_set_text(app->text_view, "");
}
