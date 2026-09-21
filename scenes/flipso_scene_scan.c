/**
 * @file flipso_scene_scan.c
 * @brief Idle until the user asks to scan, then read a card.
 *
 * The reader is not started on entry: the NFC field draws power and the LED
 * blinks, so it only runs while the user is actually presenting a card.
 */
#include "../flipso.h"

/** Scene state: whether the reader is currently running. */
typedef enum {
    FlipsoScanStateIdle,
    FlipsoScanStateScanning,
} FlipsoScanState;

/*
 * Reads to give up on before deciding a transport has nothing to say.
 *
 * A card that drops out of the field mid-read is nearly always a fumbled tap,
 * not a card we cannot read, so the same transport is simply tried again. It
 * costs nothing while no card is present: the poller sits waiting rather than
 * failing, so the budget is only spent on cards that are there and dropping out.
 */
#define FLIPSO_CARD_ERROR_RETRIES 2

/* Runs on the NFC worker thread: record the outcome and wake the UI. */
static void flipso_scene_scan_reader_callback(FlipsoReaderStatus status, void* context) {
    Flipso* app = context;
    app->status = status;
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

/* Begin a read with whichever transport the reader is currently on. */
static void flipso_scene_scan_start_reader(Flipso* app) {
    flipso_reader_start(
        app->reader, &app->card, &app->media, app->capture, flipso_scene_scan_reader_callback,
        app);
}

static void flipso_scene_scan_stop(Flipso* app) {
    flipso_reader_stop(app->reader);
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
    app->selected_product = 0;
    app->card_error_retries = 0;
    app->card_dropped = false;
    flipso_reader_reset_transport(app->reader);

    scene_manager_set_scene_state(app->scene_manager, FlipsoSceneScan, FlipsoScanStateIdle);
    flipso_scan_view_set_scanning(app->scan_view, false);
    /* Asked on every entry rather than once: the user may have just deleted the
     * last saved card, or saved the first one, and come straight back here. */
    flipso_scan_view_set_has_saved(app->scan_view, flipso_saved_any());
    flipso_scan_view_set_callback(
        app->scan_view, flipso_scene_scan_ok_callback, flipso_scene_scan_saved_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, FlipsoViewScan);
}

bool flipso_scene_scan_on_event(void* context, SceneManagerEvent event) {
    Flipso* app = context;
    FlipsoScanState state = scene_manager_get_scene_state(app->scene_manager, FlipsoSceneScan);

    if(event.type == SceneManagerEventTypeTick) {
        if(state == FlipsoScanStateScanning) flipso_scan_view_tick(app->scan_view);
        return true;
    }

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

    if(event.event == FlipsoCustomEventStartScan) {
        if(state == FlipsoScanStateScanning) return true;

        scene_manager_set_scene_state(
            app->scene_manager, FlipsoSceneScan, FlipsoScanStateScanning);
        flipso_scan_view_set_scanning(app->scan_view, true);
        app->card_error_retries = 0;
        app->card_dropped = false;

        /* Reading takes a second or two of holding still; keep the screen lit and
         * the LED pulsing so it is obvious the app is waiting on the user. */
        notification_message(app->notifications, &sequence_display_backlight_enforce_on);
        notification_message(app->notifications, &sequence_blink_start_cyan);

        flipso_scene_scan_start_reader(app);
        return true;
    }

    if(event.event == FlipsoCustomEventReaderDone) {
        /* The worker posts this before it can be stopped, so pressing Back can
         * leave one in the queue after the scan has already been cancelled.
         * Acting on it would restart the reader behind an idle screen. */
        if(state != FlipsoScanStateScanning) return true;

        /* Stop polling from the UI thread: the poller cannot stop itself. */
        flipso_reader_stop(app->reader);

        if(app->status == FlipsoReaderStatusCardError ||
           app->status == FlipsoReaderStatusCardLost) {
            app->card_dropped = true;
        }

        /* The card left the field part way through. Try the same transport
         * again rather than concluding anything from it: a half-finished read
         * says nothing about what the card is. */
        if((app->status == FlipsoReaderStatusCardError ||
            app->status == FlipsoReaderStatusCardLost) &&
           app->card_error_retries < FLIPSO_CARD_ERROR_RETRIES) {
            app->card_error_retries++;
            flipso_scene_scan_start_reader(app);
            return true;
        }

        /* A card with no ITSO application in this command set may still be an
         * ITSO card in another one, and it is still sitting on the reader. Move
         * to the next transport without telling the user anything: as far as
         * they are concerned this is all one scan.
         *
         * A card that kept dropping out gets the same treatment, because some
         * cards answer a command set they do not implement with silence rather
         * than with an error - and a CMD2 card that did that would never be
         * read if a timeout ended the scan here.
         *
         * FlipsoReaderStatusCardLost is deliberately not in that list. It means
         * the ITSO application had already selected when the card went, so this
         * transport is the right one and the next could only report that it
         * found no ITSO application - which is how a card that had just named
         * its operator came to be called "Not an ITSO card". */
        if((app->status == FlipsoReaderStatusNotItso ||
            app->status == FlipsoReaderStatusCardError) &&
           flipso_reader_next_transport(app->reader)) {
            app->card_error_retries = 0;
            flipso_scene_scan_start_reader(app);
            return true;
        }

        /* Every transport has had its turn and the last one found no ITSO
         * application. That is only worth saying of a card we managed to ask
         * properly: if the card was dropping out of the field along the way,
         * the more likely story is that it left before it could answer, and
         * "Not an ITSO card" is a verdict on a card nobody ever read. Say the
         * read failed, which is both true and the one the user can act on. */
        if(app->status == FlipsoReaderStatusNotItso && app->card_dropped) {
            app->status = FlipsoReaderStatusCardError;
        }

        flipso_scene_scan_stop(app);

        if(app->status == FlipsoReaderStatusSuccess) {
            /* A blocked shell read perfectly and is still useless, so it gets
             * the same tone as a card we could not read at all. The beep is the
             * whole result for anyone not looking at the screen, and a chirp
             * saying "fine" over a dead card is worse than no sound. */
            notification_message(
                app->notifications,
                app->card.shell_blocked ? &sequence_error : &sequence_success);
            /* Stamped here rather than on the worker thread: it is the time the
             * card was read, and the RTC is the UI thread's to ask. */
            flipso_capture_set_time(app->capture, flipso_now());
            scene_manager_next_scene(app->scene_manager, FlipsoSceneMenu);
        } else {
            notification_message(app->notifications, &sequence_error);
            scene_manager_next_scene(app->scene_manager, FlipsoSceneError);
        }
        return true;
    }

    return false;
}

void flipso_scene_scan_on_exit(void* context) {
    Flipso* app = context;
    flipso_scene_scan_stop(app);
    flipso_scan_view_set_callback(app->scan_view, NULL, NULL, NULL);
}
