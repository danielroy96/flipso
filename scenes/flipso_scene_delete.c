/**
 * @file flipso_scene_delete.c
 * @brief Confirm, then remove a saved card from the SD card.
 *
 * Deleting is offered from the card's own menu rather than from the list of
 * saved cards, because the file browser gives no way to ask "which of these is
 * this?" - by the time the card is open, the user knows what they are deleting.
 */
#include "../flipso.h"
#include "flipso_icons.h"

/* Distinct from the app-wide events so a stray one cannot be mistaken for it. */
#define FlipsoDeleteEventConfirm 310
#define FlipsoDeleteEventCancel  311

static void
    flipso_scene_delete_button_callback(GuiButtonType result, InputType type, void* context) {
    Flipso* app = context;
    if(type != InputTypeShort) return;
    if(result == GuiButtonTypeRight) {
        view_dispatcher_send_custom_event(app->view_dispatcher, FlipsoDeleteEventConfirm);
    } else if(result == GuiButtonTypeLeft) {
        view_dispatcher_send_custom_event(app->view_dispatcher, FlipsoDeleteEventCancel);
    }
}

void flipso_scene_delete_on_enter(void* context) {
    Flipso* app = context;

    FuriString* name = furi_string_alloc();
    flipso_saved_name(name, furi_string_get_cstr(app->loaded_path));

    /* Three lines, broken by hand: the scroll element wraps, but the band
     * between the header and the buttons is exactly three lines tall, so a
     * sentence left to wrap would put its tail under the buttons. */
    FuriString* text = furi_string_alloc();
    furi_string_printf(
        text, "%s\nThe card itself is not\ntouched, only this copy.",
        furi_string_get_cstr(name));

    widget_reset(app->widget);
    /* The same header line as the error and save screens: title at row 4, and
     * the icon centred on it. */
    widget_add_icon_element(app->widget, 4, 3, &I_warning_10px);
    widget_add_string_element(
        app->widget, 70, 4, AlignCenter, AlignTop, FontPrimary, "Delete saved card?");
    widget_add_text_scroll_element(
        app->widget, 0, 17, 128, 33, furi_string_get_cstr(text));
    widget_add_button_element(
        app->widget, GuiButtonTypeLeft, "Cancel", flipso_scene_delete_button_callback, app);
    widget_add_button_element(
        app->widget, GuiButtonTypeRight, "Delete", flipso_scene_delete_button_callback, app);

    furi_string_free(text);
    furi_string_free(name);

    view_dispatcher_switch_to_view(app->view_dispatcher, FlipsoViewWidget);
}

bool flipso_scene_delete_on_event(void* context, SceneManagerEvent event) {
    Flipso* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == FlipsoDeleteEventCancel) {
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }

    if(event.event != FlipsoDeleteEventConfirm) return false;

    if(flipso_saved_delete(furi_string_get_cstr(app->loaded_path))) {
        notification_message(app->notifications, &flipso_sequence_deleted);
        /* The file the card came from has gone, so its menu has nothing left to
         * show. Back to the list it was picked from, which is where a user
         * clearing out old cards wants to be next - opened at the top, since
         * the card it would have put the cursor on is the one just deleted. A
         * card saved from a scan has no list behind it, and goes back to the
         * scan screen, which resets everything on entry. */
        furi_string_reset(app->loaded_path);
        if(scene_manager_has_previous_scene(app->scene_manager, FlipsoSceneSaved)) {
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, FlipsoSceneSaved);
        } else {
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, FlipsoSceneScan);
        }
    } else {
        notification_message(app->notifications, &sequence_error);
        flipso_saved_alert("Cannot delete card", "The file could not be\nremoved.");
        scene_manager_previous_scene(app->scene_manager);
    }
    return true;
}

void flipso_scene_delete_on_exit(void* context) {
    Flipso* app = context;
    widget_reset(app->widget);
}
