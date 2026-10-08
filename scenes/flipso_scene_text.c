/**
 * @file flipso_scene_text.c
 * @brief Every scrolling text screen: a card's details, and About Flipso.
 *
 * They differ only in which builder in flipso_format.h writes the text, so they
 * are one scene, and the scene state says which screen it is. Open one with
 * flipso_open_text(). Back returns to whatever opened it.
 */
#include "../flipso.h"

/** The build's version, which the build system passes in; NULL without one. */
static const char* flipso_version(void) {
#ifdef FAP_VERSION
    return FAP_VERSION;
#else
    return NULL;
#endif
}

void flipso_scene_text_on_enter(void* context) {
    Flipso* app = context;
    FlipsoFormat f = flipso_format_context(app);
    FuriString* text = furi_string_alloc();

    switch((FlipsoTextScreen)scene_manager_get_scene_state(app->scene_manager, FlipsoSceneText)) {
    case FlipsoTextSummary:
        flipso_format_summary(text, &f, &app->card);
        break;

    case FlipsoTextCard: {
        /* Where this came from, for a card opened off the SD card. */
        FuriString* name = NULL;
        if(!furi_string_empty(app->loaded_path)) {
            name = furi_string_alloc();
            flipso_saved_name(name, furi_string_get_cstr(app->loaded_path));
        }
        flipso_format_card(
            text,
            &f,
            &app->card,
            name ? furi_string_get_cstr(name) : NULL,
            flipso_saved_is_demo(furi_string_get_cstr(app->loaded_path)),
            flipso_capture_time(app->capture));
        if(name) furi_string_free(name);
        break;
    }

    case FlipsoTextPayg:
        flipso_format_payg(text, &f, &app->card);
        break;

    case FlipsoTextId:
        flipso_format_id(text, &f, &app->card);
        break;

    case FlipsoTextJourneys:
        flipso_format_taps(text, &f, &app->card);
        break;

    case FlipsoTextProduct:
        /* Only ever set from a row the product list or the menu offered. */
        furi_check(app->selected_product < app->card.product_count);
        flipso_format_product(text, &f, &app->card, &app->card.products[app->selected_product]);
        break;

    case FlipsoTextMedia:
        flipso_format_media(text, &app->media);
        break;

    case FlipsoTextAbout:
        flipso_format_about(
            text,
            flipso_version(),
            flipso_stations_count(app->stations),
            flipso_naptan_count(app->naptan),
            flipso_ticket_types_count(app->ticket_types),
            flipso_operators_user_count(app->operators));
        break;
    }

    flipso_show_text(app, text);
    furi_string_free(text);
}

bool flipso_scene_text_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void flipso_scene_text_on_exit(void* context) {
    Flipso* app = context;
    flipso_text_view_set_text(app->text_view, "");
}
