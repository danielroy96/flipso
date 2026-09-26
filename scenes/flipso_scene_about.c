/**
 * @file flipso_scene_about.c
 * @brief What Flipso is, which build this is, and what it has to work with.
 *
 * The lookup tables are optional and live on the SD card, and nothing else in
 * the app says whether they are there: without the stop table a bus journey
 * reads "Stop 28632832", which looks like a fault rather than a missing file.
 * This is where that is said, along with what to do about it.
 */
#include "../flipso.h"

void flipso_scene_about_on_enter(void* context) {
    Flipso* app = context;
    FuriString* text = furi_string_alloc();

    flipso_cat_heading(text, FlipsoIconInfo, "Flipso");
#ifdef FAP_VERSION
    furi_string_cat_printf(text, "Version: %s\n", FAP_VERSION);
#endif
    furi_string_cat(
        text,
        "Reads UK ITSO travel smartcards, such as bus passes, rail smartcards "
        "and concessionary passes.\n");

    furi_string_cat(text, "\n");
    flipso_cat_heading(text, FlipsoIconNone, "Station names");
    uint32_t stations = flipso_stations_count(app->stations);
    if(stations) {
        furi_string_cat_printf(text, "Installed: %lu codes\n", (unsigned long)stations);
    } else {
        furi_string_cat(text, "Installed: No\nReinstall Flipso to restore them.\n");
    }

    furi_string_cat(text, "\n");
    flipso_cat_heading(text, FlipsoIconNone, "Bus stop names");
    uint32_t stops = flipso_naptan_count(app->naptan);
    if(stops) {
        furi_string_cat_printf(text, "Installed: %lu stops\n", (unsigned long)stops);
    } else {
        furi_string_cat(
            text,
            "Installed: No\n"
            "Bus stops show as numbers until naptan.dat is copied to "
            "apps_data/flipso on the SD card. See the README.\n");
    }

    furi_string_cat(text, "\n");
    flipso_cat_heading(text, FlipsoIconNone, "Operator names");
    uint16_t operators = flipso_operators_user_count(app->operators);
    if(operators) {
        furi_string_cat_printf(text, "Your operators file: %u names\n", operators);
    } else {
        furi_string_cat(
            text,
            "Your operators file: None\n"
            "Add names to apps_data/flipso/operators.txt on the SD card.\n");
    }

    furi_string_cat(text, "\n");
    flipso_cat_heading(text, FlipsoIconSave, "Saved cards");
    furi_string_cat(text, "Folder: apps_data/flipso/cards\n");
    furi_string_cat(
        text, "Saved cards hold the card number and any name on the card. Take care sharing them.\n");

    flipso_show_text(app, text);
    furi_string_free(text);
}

bool flipso_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void flipso_scene_about_on_exit(void* context) {
    Flipso* app = context;
    flipso_text_view_set_text(app->text_view, "");
}
