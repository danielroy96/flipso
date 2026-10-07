/**
 * @file flipso_scene_scan.c
 * @brief Idle until the user asks to scan, then read a card.
 *
 * The reader is not started on entry: the NFC field draws power and the LED
 * is lit, so it only runs while the user is actually presenting a card.
 */
#include "../flipso.h"

/* A run up two octaves with the motor under it, so a good read is felt as well
 * as heard. The green outlasts the sound by a second, so the result is still
 * showing when the user looks down from the reader. */
static const NotificationSequence flipso_sequence_scan_success = {
    &message_display_backlight_on,
    &message_green_255,
    &message_note_c5,
    &message_vibro_on,
    &message_delay_50,
    &message_note_g5,
    &message_delay_50,
    &message_note_c6,
    &message_delay_50,
    &message_note_g6,
    &message_delay_50,
    &message_note_c7,
    &message_delay_50,
    &message_sound_off,
    &message_vibro_off,
    &message_delay_1000,
    &message_green_0,
    NULL,
};

/* Three long buzzes on one low note, in red: nothing like the rising run of a
 * good read, so the two cannot be confused without looking. */
static const NotificationSequence flipso_sequence_scan_fail = {
    &message_display_backlight_on,
    &message_red_255,
    &message_vibro_on,
    &message_note_c5,
    &message_delay_250,
    &message_red_0,
    &message_vibro_off,
    &message_sound_off,
    &message_delay_50,
    &message_red_255,
    &message_vibro_on,
    &message_note_c5,
    &message_delay_250,
    &message_red_0,
    &message_vibro_off,
    &message_sound_off,
    &message_delay_50,
    &message_red_255,
    &message_vibro_on,
    &message_note_c5,
    &message_delay_250,
    &message_red_0,
    &message_vibro_off,
    &message_sound_off,
    &message_delay_50,
    NULL,
};

/** Scene state: whether the reader is currently running. */
typedef enum {
    FlipsoScanStateIdle,
    FlipsoScanStateScanning,
} FlipsoScanState;

/* Runs on the NFC worker thread: wake the UI, which asks the reader what next. */
static void flipso_scene_scan_reader_callback(void* context) {
    Flipso* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, FlipsoCustomEventReaderDone);
}

/* Runs on the UI thread from the view's OK handler. */
static void flipso_scene_scan_ok_callback(void* context) {
    Flipso* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, FlipsoCustomEventStartScan);
}

/* Likewise for Left, which opens the cards already on the SD card. */
static void flipso_scene_scan_saved_callback(void* context) {
    Flipso* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, FlipsoCustomEventOpenSaved);
}

/* And Right, for the About menu, which is also where the demo cards are. */
static void flipso_scene_scan_about_callback(void* context) {
    Flipso* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, FlipsoCustomEventOpenAbout);
}

/* Begin a scan, from the reader's detect stage. */
static void flipso_scene_scan_start_reader(Flipso* app) {
    flipso_reader_start(
        app->reader, &app->card, &app->media, app->capture, flipso_scene_scan_reader_callback, app);
}

static void flipso_scene_scan_stop(Flipso* app) {
    flipso_reader_stop(app->reader);
    /* The firmware's own, not one of ours: this is queued on every exit, and the
     * notification thread reads a sequence after the call returns - one of ours
     * would be read out of the .fap after the loader has freed it. A blink stop
     * also clears red, green and blue, whatever set them. */
    notification_message(app->notifications, &sequence_blink_stop);
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
    flipso_scan_view_set_scanning(app->scan_view, false);
    scene_manager_set_scene_state(app->scene_manager, FlipsoSceneScan, FlipsoScanStateIdle);
}

void flipso_scene_scan_on_enter(void* context) {
    Flipso* app = context;

    itso_card_reset(&app->card);
    flipso_media_reset(&app->media);
    flipso_capture_reset(app->capture);
    /* Whatever was on screen is gone, so the card is nobody's saved card now. */
    furi_string_reset(app->loaded_path);
    app->status = FlipsoReaderStatusIdle;
    flipso_reset_card_menus(app);

    scene_manager_set_scene_state(app->scene_manager, FlipsoSceneScan, FlipsoScanStateIdle);
    flipso_scan_view_set_scanning(app->scan_view, false);
    /* Asked on every entry rather than once: the user may have just deleted the
     * last saved card, or saved the first one, and come straight back here. */
    flipso_scan_view_set_has_saved(app->scan_view, flipso_saved_any());
    flipso_scan_view_set_callback(
        app->scan_view,
        flipso_scene_scan_ok_callback,
        flipso_scene_scan_saved_callback,
        flipso_scene_scan_about_callback,
        app);

    view_dispatcher_switch_to_view(app->view_dispatcher, FlipsoViewScan);
}

bool flipso_scene_scan_on_event(void* context, SceneManagerEvent event) {
    Flipso* app = context;
    FlipsoScanState state = scene_manager_get_scene_state(app->scene_manager, FlipsoSceneScan);

    if(event.type == SceneManagerEventTypeBack) {
        /* Back stops an in-progress scan rather than leaving the app, so the
         * user can change their mind without dropping out to the launcher. */
        if(state == FlipsoScanStateScanning) {
            flipso_scene_scan_stop(app);
            return true;
        }
        return false; /* Idle: let the app exit. */
    }

    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == FlipsoCustomEventOpenSaved) {
        if(state == FlipsoScanStateScanning) return true;
        scene_manager_next_scene(app->scene_manager, FlipsoSceneSaved);
        return true;
    }

    if(event.event == FlipsoCustomEventOpenAbout) {
        if(state == FlipsoScanStateScanning) return true;
        scene_manager_next_scene(app->scene_manager, FlipsoSceneAbout);
        return true;
    }

    if(event.event == FlipsoCustomEventStartScan) {
        if(state == FlipsoScanStateScanning) return true;

        scene_manager_set_scene_state(
            app->scene_manager, FlipsoSceneScan, FlipsoScanStateScanning);
        flipso_scan_view_set_scanning(app->scan_view, true);

        /* Reading takes a second or two of holding still; keep the screen and
         * the LED lit so it is obvious the app is waiting on the user. */
        notification_message(app->notifications, &sequence_display_backlight_enforce_on);
        notification_message(app->notifications, &sequence_solid_yellow);

        flipso_scene_scan_start_reader(app);
        return true;
    }

    if(event.event == FlipsoCustomEventReaderDone) {
        /* The worker posts this before it can be stopped, so pressing Back can
         * leave one in the queue after the scan has already been cancelled.
         * Acting on it would restart the reader behind an idle screen. */
        if(state != FlipsoScanStateScanning) return true;

        /* Another transport, or the same one again, with the card still on
         * the reader: as far as the user is concerned this is all one scan.
         * Which, and why, is flipso_scan_session.c's. */
        FlipsoReaderStatus status;
        if(flipso_reader_advance(app->reader, &status)) return true;
        app->status = status;

        flipso_scene_scan_stop(app);

        if(app->status == FlipsoReaderStatusSuccess) {
            /* A blocked shell read perfectly and is still useless, so it gets
             * the same tone as a card we could not read at all - and so does a
             * retired CMD9. The beep is the whole result for anyone not looking
             * at the screen, and a chirp saying "fine" over a dead card is worse
             * than no sound. */
            const bool dead = app->card.shell_blocked || itso_card_retired(&app->card);
            if(dead) {
                notification_message(app->notifications, &flipso_sequence_scan_fail);
            } else {
                notification_message(app->notifications, &flipso_sequence_scan_success);
            }
            /* Stamped here rather than on the worker thread: it is the time the
             * card was read, and the RTC is the UI thread's to ask. */
            flipso_capture_set_time(app->capture, flipso_now());
            scene_manager_next_scene(app->scene_manager, FlipsoSceneMenu);
        } else {
            notification_message(app->notifications, &flipso_sequence_scan_fail);
            scene_manager_next_scene(app->scene_manager, FlipsoSceneError);
        }
        return true;
    }

    return false;
}

void flipso_scene_scan_on_exit(void* context) {
    Flipso* app = context;
    flipso_scene_scan_stop(app);
    flipso_scan_view_set_callback(app->scan_view, NULL, NULL, NULL, NULL);
}
