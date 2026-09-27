/**
 * @file flipso.c
 * @brief Application lifecycle, and the few helpers that need the app itself.
 *
 * What the screens say is built in flipso_format.c, where it can be tested on
 * the host.
 */
#include "flipso.h"

#include "flipso_icons.h"

#include <gui/modules/validators.h>
#include <furi_hal_rtc.h>
#include <datetime/datetime.h>
#include <string.h>
#include <strings.h>

/* G5 into C6, run together rather than separated by a rest, then out. The
 * firmware's own note messages are the whole vocabulary the speaker has here:
 * a message sets a frequency and it sounds until the next one changes it. */
const NotificationSequence flipso_sequence_saved = {
    &message_display_backlight_on,
    &message_blue_255,
    &message_note_g5,
    &message_delay_50,
    &message_note_c6,
    &message_delay_100,
    &message_sound_off,
    NULL,
};

/* The same two notes the other way up. Red and blue together make the magenta
 * that separates this from the blue of a card kept. */
const NotificationSequence flipso_sequence_deleted = {
    &message_display_backlight_on,
    &message_red_255,
    &message_blue_255,
    &message_note_c6,
    &message_delay_50,
    &message_note_g5,
    &message_delay_100,
    &message_sound_off,
    NULL,
};

/* FAT will not take these, and the Flipper's keyboard offers some of them. */
#define FLIPSO_NAME_FORBIDDEN "\\/:*?\"<>|"

struct FlipsoNameValidator {
    ValidatorIsFile* is_file;
    char current_name[FLIPSO_SAVED_NAME_LEN];
};

FlipsoNameValidator* flipso_name_validator_alloc(const char* current_name) {
    FlipsoNameValidator* validator = malloc(sizeof(FlipsoNameValidator));
    snprintf(validator->current_name, sizeof(validator->current_name), "%s", current_name);
    validator->is_file =
        validator_is_file_alloc_init(FLIPSO_SAVED_FOLDER, FLIPSO_SAVED_EXTENSION, current_name);
    return validator;
}

void flipso_name_validator_free(FlipsoNameValidator* validator) {
    if(!validator) return;
    validator_is_file_free(validator->is_file);
    free(validator);
}

bool flipso_name_validator(const char* text, FuriString* error, void* context) {
    FlipsoNameValidator* validator = context;
    for(const char* c = text; *c; c++) {
        if(strchr(FLIPSO_NAME_FORBIDDEN, *c)) {
            furi_string_printf(error, "Name cannot\ncontain %c", *c);
            return false;
        }
    }
    /* A name that is only spaces is a file FAT will not make. */
    bool blank = true;
    for(const char* c = text; *c; c++) {
        if(*c != ' ') blank = false;
    }
    if(blank) {
        furi_string_set(error, "Name cannot\nbe blank");
        return false;
    }
    /* The card's own name in different case: the SD card compares names
     * without case, so the firmware's check would find the card itself and
     * call it taken. */
    if(validator->current_name[0] && strcasecmp(text, validator->current_name) == 0) return true;
    return validator_is_file_callback(text, error, validator->is_file);
}

uint32_t flipso_now(void) {
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    return datetime_datetime_to_timestamp(&now);
}

/* Indexed by FlipsoIcon less one, which is also how the text view numbers a
 * heading's icon. */
static const Icon* const flipso_icons[FlipsoIconCount - 1] = {
    [FlipsoIconCard - 1] = &I_card_10px,
    [FlipsoIconPurse - 1] = &I_purse_10px,
    [FlipsoIconId - 1] = &I_id_10px,
    [FlipsoIconTaps - 1] = &I_taps_10px,
    [FlipsoIconProducts - 1] = &I_products_10px,
    [FlipsoIconPass - 1] = &I_pass_10px,
    [FlipsoIconTicket - 1] = &I_ticket_10px,
    [FlipsoIconStar - 1] = &I_star_10px,
    [FlipsoIconTag - 1] = &I_tag_10px,
    [FlipsoIconPast - 1] = &I_past_10px,
    [FlipsoIconWarning - 1] = &I_warning_10px,
    [FlipsoIconSave - 1] = &I_save_10px,
    [FlipsoIconAccount - 1] = &I_account_10px,
    [FlipsoIconInfo - 1] = &I_info_10px,
};
_Static_assert(
    FLIPSO_MENU_MAX_ITEMS <= FlipsoCustomEventListRowLast + 1,
    "a list row's id must not collide with the app's own events");
_Static_assert(
    COUNT_OF(flipso_icons) <= FLIPSO_TEXT_MAX_ICONS,
    "the text view cannot number this many heading icons");

const Icon* flipso_icon(FlipsoIcon icon) {
    if(icon <= FlipsoIconNone || icon >= FlipsoIconCount) return NULL;
    return flipso_icons[icon - 1];
}

FlipsoFormat flipso_format_context(const Flipso* app) {
    FlipsoFormat f = {
        .operators = app->operators,
        .stations = app->stations,
        .naptan = app->naptan,
        .capture = app->capture,
        .media = &app->media,
        .now = flipso_now(),
    };
    return f;
}

void flipso_reset_card_menus(Flipso* app) {
    scene_manager_set_scene_state(app->scene_manager, FlipsoSceneMenu, 0);
    scene_manager_set_scene_state(app->scene_manager, FlipsoSceneProducts, 0);
    app->selected_product = 0;
}

void flipso_show_text(Flipso* app, const FuriString* text) {
    flipso_text_view_set_text(app->text_view, furi_string_get_cstr(text));
    view_dispatcher_switch_to_view(app->view_dispatcher, FlipsoViewText);
}

void flipso_open_text(Flipso* app, FlipsoTextScreen screen) {
    scene_manager_set_scene_state(app->scene_manager, FlipsoSceneText, screen);
    scene_manager_next_scene(app->scene_manager, FlipsoSceneText);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static bool flipso_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    Flipso* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool flipso_back_event_callback(void* context) {
    furi_assert(context);
    Flipso* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static Flipso* flipso_alloc(void) {
    Flipso* app = malloc(sizeof(Flipso));
    memset(app, 0, sizeof(Flipso));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&flipso_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, flipso_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, flipso_back_event_callback);

    app->menu_view = flipso_menu_view_alloc();
    app->widget = widget_alloc();
    app->text_view = flipso_text_view_alloc();
    flipso_text_view_set_icons(app->text_view, flipso_icons, COUNT_OF(flipso_icons));
    app->scan_view = flipso_scan_view_alloc();
    app->text_input = text_input_alloc();

    view_dispatcher_add_view(
        app->view_dispatcher, FlipsoViewScan, flipso_scan_view_get_view(app->scan_view));
    view_dispatcher_add_view(
        app->view_dispatcher, FlipsoViewMenu, flipso_menu_view_get_view(app->menu_view));
    view_dispatcher_add_view(
        app->view_dispatcher, FlipsoViewText, flipso_text_view_get_view(app->text_view));
    view_dispatcher_add_view(app->view_dispatcher, FlipsoViewWidget, widget_get_view(app->widget));
    view_dispatcher_add_view(
        app->view_dispatcher, FlipsoViewTextInput, text_input_get_view(app->text_input));

    /* Before anything lists the saved cards: a save that a power cut stopped
     * half way is finished or undone, so it never shows as a missing card. */
    flipso_saved_recover();

    app->reader = flipso_reader_alloc();
    app->capture = flipso_capture_alloc();
    app->loaded_path = furi_string_alloc();
    app->save_path = furi_string_alloc();
    app->operators = flipso_operators_alloc();
    app->stations = flipso_stations_alloc();
    app->naptan = flipso_naptan_alloc();

    return app;
}

static void flipso_free(Flipso* app) {
    furi_assert(app);

    /* Exiting the app does not unwind the scene stack by itself, so run the
     * current scene's on_exit to stop polling and release the LED and backlight. */
    scene_manager_stop(app->scene_manager);

    flipso_reader_free(app->reader);
    flipso_capture_free(app->capture);
    furi_string_free(app->loaded_path);
    furi_string_free(app->save_path);
    flipso_operators_free(app->operators);
    flipso_stations_free(app->stations);
    flipso_naptan_free(app->naptan);

    view_dispatcher_remove_view(app->view_dispatcher, FlipsoViewScan);
    view_dispatcher_remove_view(app->view_dispatcher, FlipsoViewMenu);
    view_dispatcher_remove_view(app->view_dispatcher, FlipsoViewText);
    view_dispatcher_remove_view(app->view_dispatcher, FlipsoViewWidget);
    view_dispatcher_remove_view(app->view_dispatcher, FlipsoViewTextInput);

    text_input_free(app->text_input);
    flipso_scan_view_free(app->scan_view);
    flipso_menu_view_free(app->menu_view);
    flipso_text_view_free(app->text_view);
    widget_free(app->widget);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t flipso_app(void* p) {
    UNUSED(p);

    Flipso* app = flipso_alloc();

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, FlipsoSceneScan);
    /* tools/flipper/flipctl waits for this line after a launch. The loader calls
     * an app running as soon as its thread exists, which is also true of one
     * stuck in startup behind the desktop; this line is only reached once the
     * first scene is up and Back and `loader close` will be heard. */
    FURI_LOG_I("Flipso", "UI ready");
    view_dispatcher_run(app->view_dispatcher);

    flipso_free(app);
    return 0;
}
