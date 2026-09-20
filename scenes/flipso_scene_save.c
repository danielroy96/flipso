/**
 * @file flipso_scene_save.c
 * @brief Name a card that has just been read, and write it to the SD card.
 *
 * The name is offered rather than demanded: two cards from the same scheme are
 * told apart by the last four digits of the number printed on them, so that is
 * what the suggestion is built from, and a user who wants "Mum's bus pass"
 * types over it.
 */
#include "../flipso.h"

#include <gui/modules/validators.h>

/* Distinct from the app-wide events so a stray one cannot be mistaken for it. */
#define FlipsoSaveEventCommit 300

/* FAT will not take these, and the Flipper's keyboard offers some of them. */
#define FLIPSO_SAVE_FORBIDDEN "\\/:*?\"<>|"

/**
 * Reject a name that cannot be a file, then fall through to the firmware's own
 * check for one that is already taken.
 *
 * Both have to be one callback because the text input holds only one, and
 * catching the bad character here rather than at the write is the difference
 * between saying what is wrong and reporting a failure the user cannot explain.
 */
static bool flipso_scene_save_validator(const char* text, FuriString* error, void* context) {
    for(const char* c = text; *c; c++) {
        if(strchr(FLIPSO_SAVE_FORBIDDEN, *c)) {
            furi_string_printf(error, "Name cannot\ncontain %c", *c);
            return false;
        }
    }
    return validator_is_file_callback(text, error, context);
}

static void flipso_scene_save_input_callback(void* context) {
    Flipso* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, FlipsoSaveEventCommit);
}

void flipso_scene_save_on_enter(void* context) {
    Flipso* app = context;

    flipso_saved_suggest_name(
        app->save_name, sizeof(app->save_name), &app->card,
        flipso_operators_brand(app->operators, app->card.oid));

    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Name this card");
    text_input_set_result_callback(
        app->text_input, flipso_scene_save_input_callback, app, app->save_name,
        sizeof(app->save_name), true);
    text_input_set_minimum_length(app->text_input, 1);

    /* Warns before overwriting a card already saved under this name, and keeps
     * out the characters a file name cannot carry. */
    ValidatorIsFile* validator =
        validator_is_file_alloc_init(FLIPSO_SAVED_FOLDER, FLIPSO_SAVED_EXTENSION, "");
    text_input_set_validator(app->text_input, flipso_scene_save_validator, validator);

    view_dispatcher_switch_to_view(app->view_dispatcher, FlipsoViewTextInput);
}

bool flipso_scene_save_on_event(void* context, SceneManagerEvent event) {
    Flipso* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event != FlipsoSaveEventCommit) return false;

    FuriString* path = furi_string_alloc();
    flipso_saved_path(path, app->save_name);

    if(flipso_saved_write(app->capture, furi_string_get_cstr(path))) {
        /* The card on screen is now that saved card, so the menu offers to
         * delete it rather than to save it a second time. */
        furi_string_set(app->loaded_path, path);
        notification_message(app->notifications, &sequence_success);
        scene_manager_previous_scene(app->scene_manager);
    } else {
        notification_message(app->notifications, &sequence_error);
        flipso_saved_alert(
            "Cannot save card", "Check the SD card is in\nand has room on it.");
    }

    furi_string_free(path);
    return true;
}

void flipso_scene_save_on_exit(void* context) {
    Flipso* app = context;

    /* The validator is allocated per visit, and the text input does not own it. */
    void* validator = text_input_get_validator_callback_context(app->text_input);
    text_input_set_validator(app->text_input, NULL, NULL);
    if(validator) validator_is_file_free(validator);

    text_input_reset(app->text_input);
}
