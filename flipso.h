/**
 * @file flipso.h
 * @brief Application state shared by the Flipso scenes.
 */
#pragma once

#include "reader/flipso_reader.h"
#include "cards/flipso_capture.h"
#include "cards/flipso_saved.h"
#include "lookup/flipso_operators.h"
#include "lookup/flipso_stations.h"
#include "lookup/flipso_naptan.h"
#include "format/flipso_format.h"
#include "itso/itso.h"
#include "views/flipso_menu_view.h"
#include "views/flipso_scan_view.h"
#include "views/flipso_text_view.h"
#include "scenes/flipso_scene.h"

#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/widget.h>
#include <gui/modules/text_input.h>
#include <gui/icon.h>
#include <notification/notification_messages.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FlipsoViewScan,
    FlipsoViewMenu,
    FlipsoViewText,
    FlipsoViewWidget,
    FlipsoViewTextInput,
} FlipsoView;

/**
 * Every custom event the view dispatcher carries, in one place.
 *
 * The list scenes post a row's id as the event itself - a menu row, or the
 * index of a product - so 0 to 99 belong to them, and everything else is kept
 * above that. Each scene's own events have a block of their own, so an event
 * that arrives after its scene has gone cannot be mistaken for another's.
 */
typedef enum {
    /** Row ids a list scene posts: menu rows, product indices. */
    FlipsoCustomEventListRowLast = 99,

    /* The app: scanning and the saved-card browser. */
    /** Posted from the scan view when the user presses OK on the idle prompt. */
    FlipsoCustomEventStartScan = 100,
    /** Posted from the NFC worker thread each time a transport has reported. */
    FlipsoCustomEventReaderDone,
    /** Posted from the scan view when the user asks for the saved cards. */
    FlipsoCustomEventOpenSaved,
    /** Posted from the scan view when the user asks for the About menu. */
    FlipsoCustomEventOpenAbout,
    /** Posted by the saved-card scene once the file browser has closed. */
    FlipsoCustomEventSavedPicked,
    FlipsoCustomEventSavedCancelled,

    /* The error screen's centre button, whichever action it carries. */
    FlipsoCustomEventErrorRetry = 200,
    FlipsoCustomEventErrorDetails,

    /* The save screen: its name screen, and its "update the record?" screen. */
    FlipsoCustomEventSaveCommit = 300,
    FlipsoCustomEventSaveReplace,
    FlipsoCustomEventSaveCancel,

    /* The delete confirmation. */
    FlipsoCustomEventDeleteConfirm = 310,
    FlipsoCustomEventDeleteCancel,

    /* The rename screen. */
    FlipsoCustomEventRenameCommit = 320,
} FlipsoCustomEvent;

/** Which screen the text scene shows: its scene state. */
typedef enum {
    FlipsoTextSummary,
    FlipsoTextCard,
    FlipsoTextPayg,
    FlipsoTextId,
    FlipsoTextJourneys,
    FlipsoTextProduct, /**< app->selected_product. */
    FlipsoTextMedia, /**< What a card Flipso cannot decode said about itself. */
    FlipsoTextAbout,
} FlipsoTextScreen;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    FlipsoMenuView* menu_view;
    Widget* widget;
    FlipsoTextView* text_view;
    FlipsoScanView* scan_view;
    TextInput* text_input;

    FlipsoReader* reader;
    FlipsoReaderStatus status;

    /** Operator names: the user's file layered over the built-in table. */
    FlipsoOperators* operators;

    /** Rail station names, read on demand from the SD card. */
    FlipsoStations* stations;

    /** Bus stop names, likewise, when the user has built the table. */
    FlipsoNaptan* naptan;

    ItsoCard card;

    /**
     * The raw blocks the card gave up, whether it was tapped or loaded. This is
     * what gets written when the user saves, and what a saved card is decoded
     * from, so a card on screen behaves the same whichever way it arrived.
     */
    FlipsoCapture* capture;

    /** File the card on screen came from; empty when it was just scanned. */
    FuriString* loaded_path;

    /** Detail text built by the error scene, alive only while that scene is. */
    FuriString* error_detail;

    /** Where the save screen is about to write: a new file, or one being replaced. */
    FuriString* save_path;

    /** Name being edited on the save screen. */
    char save_name[FLIPSO_SAVED_NAME_LEN];

    /**
     * What the record being replaced knew that this read does not, and the
     * other way round. Filled in when the save screen finds a record to update,
     * because that is the one moment both are in hand.
     */
    FlipsoCaptureDiff save_diff;

    /** What a card that is not an ITSO one said about itself. */
    FlipsoMedia media;

    /** The demo cards the demo list offers, alive only while that list is. */
    FlipsoDemos* demos;

    /** Index into card.products chosen on the product list scene. */
    uint8_t selected_product;
} Flipso;

/**
 * The chirps for the two things that happen to a saved card.
 *
 * Both are distinct from the firmware's sequence_success, which is what a
 * completed scan plays: saving happens straight after a read, so the two are
 * heard seconds apart and have to be told apart without looking. These are two
 * slurred notes over about a sixth of a second against that one's four staccato
 * ones over four times as long, so they read as the shorter, smaller events
 * they are - and they are each other's mirror, rising to file a card away and
 * falling to throw one out, which is the distinction that matters once the
 * pair is familiar. The colours follow: green is a card read, blue is a card
 * kept, magenta is a card gone. Red stays with sequence_error, because none of
 * these is a failure.
 */
extern const NotificationSequence flipso_sequence_saved;
extern const NotificationSequence flipso_sequence_deleted;

/**
 * Text input validator for the name of a saved card, used by both the screen
 * that names one and the screen that renames one.
 *
 * Rejects the characters a file name cannot carry, then defers to the
 * firmware's own check for a name already taken. The two have to be one
 * callback because a text input holds only one, and catching a bad character
 * here rather than at the write is the difference between saying what is wrong
 * and reporting a failure the user cannot explain.
 */
typedef struct FlipsoNameValidator FlipsoNameValidator;

/**
 * @param current_name the card's name when renaming it, or "" when naming a new
 *                     one. Keeping that name is allowed, and so is changing
 *                     only its case, which the SD card's file system would
 *                     otherwise report as a clash with the card itself.
 */
FlipsoNameValidator* flipso_name_validator_alloc(const char* current_name);
void flipso_name_validator_free(FlipsoNameValidator* validator);

/** The TextInputValidatorCallback; @p context is a FlipsoNameValidator. */
bool flipso_name_validator(const char* text, FuriString* error, void* context);

/** Current time as a Unix timestamp, from the Flipper's RTC. */
uint32_t flipso_now(void);

/** The lookup tables and the time, for the screen builders in flipso_format.h. */
FlipsoFormat flipso_format_context(const Flipso* app);

/** The image for one of flipso_format.h's icon numbers; NULL for none. */
const Icon* flipso_icon(FlipsoIcon icon);

/**
 * Forget where the card's menus were left, for a card that is not the one they
 * were left on: a new card starts on its Summary, not on whichever row the last
 * one was closed from.
 */
void flipso_reset_card_menus(Flipso* app);

/** Put a screen built by flipso_format.h on the text view and show it. */
void flipso_show_text(Flipso* app, const FuriString* text);

/** Open one of the scrolling text screens on top of the current scene. */
void flipso_open_text(Flipso* app, FlipsoTextScreen screen);

/**
 * Make the card saved at @p path the card on screen, as a tap would have: a
 * saved card or a demo card, which are the same kind of file.
 *
 * @return false, with no card held, when the file will not open or decode.
 */
bool flipso_load_card(Flipso* app, const char* path);

#ifdef __cplusplus
}
#endif
