/**
 * @file flipso_scene_rename.c
 * @brief Give a saved card a different name.
 *
 * The name is the only part of a saved card the user owns - everything else in
 * the file is what the card said - so it is the one part worth being able to
 * change without re-reading anything. Offered from the card's own menu, for the
 * same reason deleting is: by the time a card is open, it is obvious which one
 * is being renamed.
 */
#include "../flipso.h"


/* Distinct from the app-wide events so a stray one cannot be mistaken for it. */
#define FlipsoRenameEventCommit 320

static void flipso_scene_rename_input_callback(void* context) {
    Flipso* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, FlipsoRenameEventCommit);
}

void flipso_scene_rename_on_enter(void* context) {
    Flipso* app = context;

    /* Start from the name it has rather than from a fresh suggestion: renaming
     * is usually adjusting a name, not thinking of a new one. */
    FuriString* name = furi_string_alloc();
    flipso_saved_name(name, furi_string_get_cstr(app->loaded_path));
    snprintf(app->save_name, sizeof(app->save_name), "%s", furi_string_get_cstr(name));

    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Rename this card");
    /* The existing text is left in place for editing rather than cleared on the
     * first key, which is the opposite of the save screen: there the text is a
     * suggestion to type over, here it is the answer to adjust. */
    text_input_set_result_callback(
        app->text_input, flipso_scene_rename_input_callback, app, app->save_name,
        sizeof(app->save_name), false);
    text_input_set_minimum_length(app->text_input, 1);

    /* Its own name is passed as the current one, so keeping it - or changing
     * only its case - is allowed while another card's name is still refused. */
    text_input_set_validator(
        app->text_input, flipso_name_validator,
        flipso_name_validator_alloc(furi_string_get_cstr(name)));

    furi_string_free(name);

    view_dispatcher_switch_to_view(app->view_dispatcher, FlipsoViewTextInput);
}

bool flipso_scene_rename_on_event(void* context, SceneManagerEvent event) {
    Flipso* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event != FlipsoRenameEventCommit) return false;

    flipso_saved_path(app->save_path, app->save_name);

    if(flipso_saved_rename(
           furi_string_get_cstr(app->loaded_path), furi_string_get_cstr(app->save_path))) {
        /* The card on screen is the same card; only where it lives has moved,
         * so the Card screen and the delete row follow it. */
        furi_string_set(app->loaded_path, app->save_path);
        notification_message(app->notifications, &flipso_sequence_saved);
        scene_manager_previous_scene(app->scene_manager);
    } else {
        notification_message(app->notifications, &sequence_error);
        flipso_saved_alert("Cannot rename card", "The file could not be\nmoved.");
    }

    return true;
}

void flipso_scene_rename_on_exit(void* context) {
    Flipso* app = context;

    /* The validator is allocated per visit, and the text input does not own it. */
    FlipsoNameValidator* validator = text_input_get_validator_callback_context(app->text_input);
    text_input_set_validator(app->text_input, NULL, NULL);
    flipso_name_validator_free(validator);

    text_input_reset(app->text_input);
}
