/**
 * @file flipso_scene_error.c
 * @brief Explains why a card could not be read, and offers somewhere to go next.
 *
 * Every route off this screen used to be Back, which on a failed read is the
 * one thing the user does not want: the card is still in their hand. OK now
 * returns to the scan screen and starts a scan straight away.
 *
 * A recognised card that Flipso will not decode - an Oyster - is the exception.
 * Scanning it again would say the same thing, so OK there opens what the card
 * did tell us instead.
 */
#include "../flipso.h"
#include "flipso_icons.h"

/* Distinct from the app-wide events so a stray one cannot be mistaken for it. */
#define FlipsoErrorEventRetry   200
#define FlipsoErrorEventDetails 201

static void flipso_scene_error_button_callback(GuiButtonType result, InputType type, void* context) {
    Flipso* app = context;
    if(type != InputTypeShort || result != GuiButtonTypeCenter) return;
    /* Which action the centre button carries is decided in on_enter and stored
     * as the scene state, so this callback does not have to know the status. */
    view_dispatcher_send_custom_event(
        app->view_dispatcher, scene_manager_get_scene_state(app->scene_manager, FlipsoSceneError));
}

void flipso_scene_error_on_enter(void* context) {
    Flipso* app = context;

    const char* title;
    const char* detail;
    const Icon* icon;
    const char* button = "Scan again";
    uint32_t action = FlipsoErrorEventRetry;

    switch(app->status) {
    case FlipsoReaderStatusOyster:
        icon = &I_oyster_14px;
        title = "TfL Oyster Card";
        /* Lines are kept to what fits across 128px, as the other details are:
         * the scroll element wraps, but wrapping mid-sentence reads badly. */
        detail = "TfL Oyster is an encrypted\n"
                 "MIFARE DESFire card\n"
                 "running a proprietary\n"
                 "Oyster function.\n\n"
                 "It is not compatible with\n"
                 "ITSO or Flipso :(\n";
        /* Rescanning would reach the same conclusion; the card details are the
         * only thing left worth pressing a button for. */
        button = "Card details";
        action = FlipsoErrorEventDetails;
        break;
    case FlipsoReaderStatusNotItso:
        icon = &I_not_itso_14px;
        title = "Not an ITSO card";
        detail = "This card has no ITSO\n"
                 "application on it.\n\n"
                 "ITSO smartcards are used\n"
                 "for bus and rail travel\n"
                 "across the UK.";
        break;
    case FlipsoReaderStatusBadShell:
        icon = &I_bad_shell_14px;
        title = "Unreadable shell";
        detail = "The ITSO application is\n"
                 "present but its shell\n"
                 "could not be decoded.\n\n"
                 "The card may use a media\n"
                 "definition Flipso does\n"
                 "not know about.";
        break;
    default:
        icon = &I_read_failed_14px;
        title = "Read failed";
        detail = "The card moved away\n"
                 "before the read finished.\n\n"
                 "Hold it flat against the\n"
                 "back of the Flipper and\n"
                 "keep it still.";
        break;
    }

    widget_reset(app->widget);
    /* Icon, then title, on one header band; the detail scrolls under it and the
     * bottom twelve rows belong to the button. */
    widget_add_icon_element(app->widget, 2, 1, icon);
    widget_add_string_element(app->widget, 70, 4, AlignCenter, AlignTop, FontPrimary, title);
    widget_add_text_scroll_element(app->widget, 0, 17, 128, 33, detail);
    widget_add_button_element(
        app->widget, GuiButtonTypeCenter, button, flipso_scene_error_button_callback, app);

    scene_manager_set_scene_state(app->scene_manager, FlipsoSceneError, action);
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipsoViewWidget);
}

bool flipso_scene_error_on_event(void* context, SceneManagerEvent event) {
    Flipso* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == FlipsoErrorEventDetails) {
        scene_manager_next_scene(app->scene_manager, FlipsoSceneMedia);
        return true;
    }

    if(event.event != FlipsoErrorEventRetry) return false;

    /* Back to the scan screen, which resets the card and the transport, then
     * ask it to start polling without waiting for a second press. */
    scene_manager_previous_scene(app->scene_manager);
    view_dispatcher_send_custom_event(app->view_dispatcher, FlipsoCustomEventStartScan);
    return true;
}

void flipso_scene_error_on_exit(void* context) {
    Flipso* app = context;
    widget_reset(app->widget);
}
