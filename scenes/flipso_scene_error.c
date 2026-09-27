/**
 * @file flipso_scene_error.c
 * @brief Explains why a card could not be read, and offers somewhere to go next.
 *
 * Every route off this screen used to be Back, which on a failed read is the
 * one thing the user does not want: the card is still in their hand. OK now
 * returns to the scan screen and starts a scan straight away.
 *
 * A card Flipso will not decode but that described itself - an Oyster, or any
 * other DESFire - is the exception. Scanning it again would say the same
 * thing, so OK there opens what the card did tell us instead.
 */
#include "../flipso.h"
#include "flipso_icons.h"

/* Distinct from the app-wide events so a stray one cannot be mistaken for it. */
#define FlipsoErrorEventRetry   200
#define FlipsoErrorEventDetails 201

static void
    flipso_scene_error_button_callback(GuiButtonType result, InputType type, void* context) {
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
    /* The cases that build their text need it to outlive this function: the
     * widget element is handed a const char*, and nothing in its contract
     * promises a copy. Freed in on_exit alongside the widget. */
    FuriString* built = NULL;
    const Icon* icon;
    const char* button = "Scan again";
    uint32_t action = FlipsoErrorEventRetry;

    switch(app->status) {
    case FlipsoReaderStatusOyster:
        icon = &I_oyster_14px;
        title = "TfL Oyster Card";
        /* Lines are kept to what fits across 128px, as the other details are:
         * the scroll element wraps, but wrapping mid-sentence reads badly. */
        detail = "Oyster uses Transport for\n"
                 "London's own encrypted\n"
                 "system, not ITSO, so its\n"
                 "balance and journeys\n"
                 "cannot be read.\n\n"
                 "Card details shows what\n"
                 "the chip says about itself.\n";
        /* Rescanning would reach the same conclusion; the card details are the
         * only thing left worth pressing a button for. */
        button = "Card details";
        action = FlipsoErrorEventDetails;
        break;
    case FlipsoReaderStatusNotItso:
        icon = &I_not_itso_14px;
        title = "Not an ITSO card";
        detail = "This card has no ITSO\n"
                 "travel data on it.\n\n"
                 "ITSO smartcards are used\n"
                 "for bus and rail travel\n"
                 "across the UK.";
        /* A DESFire still says what it is, and that is worth offering rather
         * than a scan that will say the same thing again. */
        if(app->media.valid) {
            button = "Card details";
            action = FlipsoErrorEventDetails;
        }
        break;
    case FlipsoReaderStatusUnsupported:
        icon = &I_not_itso_14px;
        title = "Unsupported card";
        if(app->card.shell_valid) {
            /* Only the Type 2 transport decodes a shell and still calls the card
             * unsupported: an ITSO card on NTAG or Ultralight EV1 media (CMD9,
             * CMD10), whose full directory Flipso does not walk. Saying it is
             * ITSO is the useful part - it tells the holder the card is not
             * faulty, and whoever files the bug which media to add. */
            built = furi_string_alloc_printf(
                "This is an ITSO card, on\n"
                "a kind of NFC tag (CMD%u)\n"
                "that Flipso cannot read\n"
                "yet.\n\n"
                "Flipso reads DESFire and\n"
                "ISO 7816 ITSO cards, and\n"
                "ITSO paper tickets.",
                app->card.fvc);
            detail = furi_string_get_cstr(built);
        } else {
            detail = "This card cannot be read\n"
                     "by Flipso. It may be a\n"
                     "MIFARE Classic, a hotel\n"
                     "or building key, or an\n"
                     "older kind of ITSO card.\n\n"
                     "Flipso reads DESFire and\n"
                     "ISO 7816 ITSO cards, and\n"
                     "ITSO paper tickets.";
        }
        break;
    case FlipsoReaderStatusBadShell:
        icon = &I_bad_shell_14px;
        title = "Card not readable";
        /* Built rather than fixed, because the two causes want opposite advice:
         * bytes that fail their own checksum mean the read was at fault and
         * tapping again is worth doing, while bytes that verify mean the card
         * is laid out in a way Flipso does not understand and tapping again
         * will say exactly the same thing. */
        built = furi_string_alloc_set("This is an ITSO card, but\n"
                                      "its main record could not\n"
                                      "be decoded.\n\n");
        if(app->card.secrc_checked && !app->card.secrc_valid) {
            furi_string_cat(
                built,
                "The record failed its own\n"
                "checksum, so the read did\n"
                "not come through cleanly.\n"
                "Hold the card still and\n"
                "try again.\n\n");
        } else if(app->card.secrc_checked) {
            furi_string_cat(
                built,
                "The record passed its own\n"
                "checksum, so the card may\n"
                "be laid out in a way\n"
                "Flipso does not know\n"
                "about yet.\n\n");
        } else {
            furi_string_cat(
                built,
                "The card may be laid out\n"
                "in a way Flipso does not\n"
                "know about yet.\n\n");
        }
        /* The reason itself goes last: it is for a bug report, not for the user. */
        furi_string_cat_printf(
            built, "Reason: %s\n", itso_shell_reject_name(app->card.shell_reject));
        detail = furi_string_get_cstr(built);
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

    app->error_detail = built;
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
    if(app->error_detail) {
        furi_string_free(app->error_detail);
        app->error_detail = NULL;
    }
}
