/**
 * @file flipso_scene_save.c
 * @brief Write the card on screen to the SD card, as a new file or over its own.
 *
 * A card read a second time is nearly always the same card with different
 * products on it - a season ticket renewed, a purse topped up - so saving it
 * looks first for the record it already has and offers to bring that up to
 * date. Cards are matched by number rather than by name, because the user named
 * the file and the card did not.
 *
 * A card with no record yet gets the name screen instead. The name is offered
 * rather than demanded: two cards from the same scheme are told apart by the
 * last four digits of the number printed on them, so that is what the
 * suggestion is built from, and a user who wants "Mum's bus pass" types over it.
 */
#include "../flipso.h"
#include "flipso_icons.h"

#include <gui/modules/validators.h>

/* Distinct from the app-wide events so a stray one cannot be mistaken for it.
 * Which of the two screens is up does not need recording anywhere: only that
 * screen's own buttons can post, so the event says which path this is. */
#define FlipsoSaveEventCommit  300
#define FlipsoSaveEventReplace 301
#define FlipsoSaveEventCancel  302

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

static void
    flipso_scene_save_button_callback(GuiButtonType result, InputType type, void* context) {
    Flipso* app = context;
    if(type != InputTypeShort) return;
    if(result == GuiButtonTypeRight) {
        view_dispatcher_send_custom_event(app->view_dispatcher, FlipsoSaveEventReplace);
    } else if(result == GuiButtonTypeLeft) {
        view_dispatcher_send_custom_event(app->view_dispatcher, FlipsoSaveEventCancel);
    }
}

/** The "you have this card already" screen. */
static void flipso_scene_save_ask_update(Flipso* app, uint32_t read_at) {
    FuriString* name = furi_string_alloc();
    flipso_saved_name(name, furi_string_get_cstr(app->save_path));

    /* The name, and how old the record is. Two lines, which is what fits above
     * the buttons without scrolling, and the date is the half that answers the
     * question being asked: what is about to be thrown away. */
    FuriString* text = furi_string_alloc_set(name);
    if(read_at) {
        furi_string_cat(text, "\nRead ");
        flipso_cat_time(text, read_at);
    }

    widget_reset(app->widget);
    widget_add_icon_element(app->widget, 2, 1, &I_save_10px);
    widget_add_string_element(
        app->widget, 70, 2, AlignCenter, AlignTop, FontPrimary, "Update saved card?");
    widget_add_text_scroll_element(app->widget, 0, 17, 128, 33, furi_string_get_cstr(text));
    widget_add_button_element(
        app->widget, GuiButtonTypeLeft, "Cancel", flipso_scene_save_button_callback, app);
    widget_add_button_element(
        app->widget, GuiButtonTypeRight, "Update", flipso_scene_save_button_callback, app);

    furi_string_free(text);
    furi_string_free(name);

    view_dispatcher_switch_to_view(app->view_dispatcher, FlipsoViewWidget);
}

/** The name screen, for a card with no record yet. */
static void flipso_scene_save_ask_name(Flipso* app) {
    flipso_saved_suggest_name(
        app->save_name, sizeof(app->save_name), &app->card,
        flipso_operators_brand(app->operators, app->card.oid));

    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Name this card");
    text_input_set_result_callback(
        app->text_input, flipso_scene_save_input_callback, app, app->save_name,
        sizeof(app->save_name), true);
    text_input_set_minimum_length(app->text_input, 1);

    /* Warns before overwriting a card already saved under this name, which by
     * now can only be a different card someone gave the same name to. */
    ValidatorIsFile* validator =
        validator_is_file_alloc_init(FLIPSO_SAVED_FOLDER, FLIPSO_SAVED_EXTENSION, "");
    text_input_set_validator(app->text_input, flipso_scene_save_validator, validator);

    view_dispatcher_switch_to_view(app->view_dispatcher, FlipsoViewTextInput);
}

void flipso_scene_save_on_enter(void* context) {
    Flipso* app = context;

    uint32_t read_at = 0;
    if(flipso_saved_find(app->capture, app->save_path, &read_at)) {
        flipso_scene_save_ask_update(app, read_at);
    } else {
        flipso_scene_save_ask_name(app);
    }
}

/** Write app->save_path and leave, or explain why it could not be written. */
static void flipso_scene_save_commit(Flipso* app) {
    if(flipso_saved_write(app->capture, furi_string_get_cstr(app->save_path))) {
        /* The card on screen is now that saved card, so the menu offers to
         * delete it rather than to save it again. */
        furi_string_set(app->loaded_path, app->save_path);
        notification_message(app->notifications, &flipso_sequence_saved);
        scene_manager_previous_scene(app->scene_manager);
    } else {
        notification_message(app->notifications, &sequence_error);
        flipso_saved_alert("Cannot save card", "Check the SD card is in\nand has room on it.");
    }
}

bool flipso_scene_save_on_event(void* context, SceneManagerEvent event) {
    Flipso* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;

    switch(event.event) {
    case FlipsoSaveEventCommit:
        /* The name screen wrote into app->save_name; turn it into a path. */
        flipso_saved_path(app->save_path, app->save_name);
        flipso_scene_save_commit(app);
        return true;

    case FlipsoSaveEventReplace:
        /* app->save_path is already the record that was found. */
        flipso_scene_save_commit(app);
        return true;

    case FlipsoSaveEventCancel:
        scene_manager_previous_scene(app->scene_manager);
        return true;

    default:
        return false;
    }
}

void flipso_scene_save_on_exit(void* context) {
    Flipso* app = context;

    /* Only one of the two screens was built, but tearing down both is cheaper
     * than remembering which, and neither minds being reset unused. */
    void* validator = text_input_get_validator_callback_context(app->text_input);
    text_input_set_validator(app->text_input, NULL, NULL);
    if(validator) validator_is_file_free(validator);

    text_input_reset(app->text_input);
    widget_reset(app->widget);
}
