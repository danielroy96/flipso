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

static void flipso_scene_save_input_callback(void* context) {
    Flipso* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, FlipsoCustomEventSaveCommit);
}

static void
    flipso_scene_save_button_callback(GuiButtonType result, InputType type, void* context) {
    Flipso* app = context;
    if(type != InputTypeShort) return;
    if(result == GuiButtonTypeRight) {
        view_dispatcher_send_custom_event(app->view_dispatcher, FlipsoCustomEventSaveReplace);
    } else if(result == GuiButtonTypeLeft) {
        view_dispatcher_send_custom_event(app->view_dispatcher, FlipsoCustomEventSaveCancel);
    }
}

/** "2 new journeys", or nothing at all when a count is zero. */
static void flipso_scene_save_cat_count(
    FuriString* out,
    uint8_t count,
    const char* singular,
    const char* plural) {
    if(!count) return;
    furi_string_cat_printf(out, "\n%u new %s", count, count == 1 ? singular : plural);
}

/** The "you have this card already" screen. */
static void flipso_scene_save_ask_update(Flipso* app, uint32_t read_at) {
    FuriString* name = furi_string_alloc();
    flipso_saved_name(name, furi_string_get_cstr(app->save_path));

    /* The name, and how old the record is: the date is the half that answers
     * the question being asked, which is what is about to be replaced. */
    FuriString* text = furi_string_alloc_set(name);
    if(read_at) {
        furi_string_cat(text, "\nRead ");
        flipso_cat_time(text, read_at);
    }

    /* Then what the update is worth, which is the other half of the same
     * question. The record is not being thrown away - the journeys and
     * transactions that have rolled off the card since it was written are kept
     * - so what changes is that this read's are added to them. */
    const FlipsoCaptureDiff* diff = &app->save_diff;
    const bool ticket = app->card.shell_compact;
    if(diff->new_taps || diff->new_values || diff->new_products || diff->changed_products) {
        flipso_scene_save_cat_count(text, diff->new_taps, "journey", "journeys");
        flipso_scene_save_cat_count(text, diff->new_values, "transaction", "transactions");
        flipso_scene_save_cat_count(text, diff->new_products, "product", "products");
        /* A paper ticket is its one product, and changing is all it can do: it
         * keeps no journeys or transactions to count. */
        if(diff->changed_products && ticket) {
            furi_string_cat(text, "\nThe ticket has changed");
        } else if(diff->changed_products) {
            furi_string_cat_printf(
                text,
                "\n%u product%s changed",
                diff->changed_products,
                diff->changed_products == 1 ? "" : "s");
        }
    } else {
        furi_string_cat(
            text, ticket ? "\nNothing new on the ticket" : "\nNothing new on the card");
    }

    uint16_t kept = (uint16_t)(diff->kept_taps + diff->kept_values);
    if(kept) {
        furi_string_cat_printf(text, "\nKeeping %u older record%s", kept, kept == 1 ? "" : "s");
    }

    /* Said separately from the records, because it is a bigger thing to have
     * happened: a whole product has left the card since, and the record is the
     * only place it still exists. */
    if(diff->kept_products) {
        furi_string_cat_printf(
            text,
            "\n%s %u product%s now\noff the card",
            kept ? "and" : "Keeping",
            diff->kept_products,
            diff->kept_products == 1 ? "" : "s");
    }

    widget_reset(app->widget);
    /* The same header line as the error and delete screens. */
    widget_add_icon_element(app->widget, 4, 3, &I_save_10px);
    widget_add_string_element(
        app->widget, 70, 4, AlignCenter, AlignTop, FontPrimary, "Update saved card?");
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
    /* Named from the same identity the save matches on, so that two paper
     * tickets - whose card numbers are identical - are offered different names. */
    char number[ITSO_ISRN_DIGITS + 1] = "";
    if(!flipso_capture_card_number(app->capture, number)) {
        snprintf(number, sizeof(number), "%s", app->card.isrn);
    }
    flipso_saved_suggest_name(
        app->save_name,
        sizeof(app->save_name),
        number,
        flipso_operators_brand(app->operators, itso_card_issuer_oid(&app->card)));

    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Name this card");
    text_input_set_result_callback(
        app->text_input,
        flipso_scene_save_input_callback,
        app,
        app->save_name,
        sizeof(app->save_name),
        true);
    text_input_set_minimum_length(app->text_input, 1);

    /* Warns before overwriting a card already saved under this name, which by
     * now can only be a different card someone gave the same name to. */
    text_input_set_validator(
        app->text_input, flipso_name_validator, flipso_name_validator_alloc(""));

    view_dispatcher_switch_to_view(app->view_dispatcher, FlipsoViewTextInput);
}

void flipso_scene_save_on_enter(void* context) {
    Flipso* app = context;
    memset(&app->save_diff, 0, sizeof(app->save_diff));

    uint32_t read_at = 0;
    if(flipso_saved_find(app->capture, app->save_path, &read_at)) {
        /* Take what that record knows before offering to replace it. A card
         * keeps only its last four journeys and its last couple of
         * transactions, so everything older than that exists solely in the
         * file, and writing this read out on its own would lose it.
         *
         * Done here rather than after the user agrees so that the screen can
         * say what the update is worth - and it costs nothing if they decline,
         * because the merge only adds records to a capture that is thrown away
         * when the scan screen comes back. */
        FlipsoCapture* previous = flipso_capture_alloc();
        if(flipso_saved_read(previous, furi_string_get_cstr(app->save_path))) {
            flipso_capture_merge_history(app->capture, previous, &app->save_diff);
        }
        flipso_capture_free(previous);

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
        /* And it now knows more than the card in the reader does, so decode it
         * again: the history that came out of the file belongs on the screens
         * without waiting for the card to be opened afresh. Only when there was
         * something to merge, so a first save leaves the card exactly as the
         * read left it. */
        if(app->save_diff.kept_taps || app->save_diff.kept_values ||
           app->save_diff.kept_products) {
            flipso_capture_decode(app->capture, &app->card);
        }
        notification_message(app->notifications, &flipso_sequence_saved);
        scene_manager_previous_scene(app->scene_manager);
    } else {
        notification_message(app->notifications, &sequence_error);
        flipso_saved_alert("Cannot save card", "Check the SD card is in\nand has room on it.");
    }
}

bool flipso_scene_save_on_event(void* context, SceneManagerEvent event) {
    Flipso* app = context;

    /* Which of the two screens is up does not need recording anywhere: only
     * that screen's own buttons can post, so the event says which path this is. */
    if(event.type != SceneManagerEventTypeCustom) return false;

    switch(event.event) {
    case FlipsoCustomEventSaveCommit:
        /* The name screen wrote into app->save_name; turn it into a path. */
        flipso_saved_path(app->save_path, app->save_name);
        flipso_scene_save_commit(app);
        return true;

    case FlipsoCustomEventSaveReplace:
        /* app->save_path is already the record that was found. */
        flipso_scene_save_commit(app);
        return true;

    case FlipsoCustomEventSaveCancel:
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
    FlipsoNameValidator* validator = text_input_get_validator_callback_context(app->text_input);
    text_input_set_validator(app->text_input, NULL, NULL);
    flipso_name_validator_free(validator);

    text_input_reset(app->text_input);
    widget_reset(app->widget);
}
