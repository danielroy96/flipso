/**
 * @file flipso_saved.c
 * @brief Reading and writing the saved-card files. See flipso_saved.h.
 */
#include "flipso_saved.h"

#include "flipso_icons.h"

#include <dialogs/dialogs.h>
#include <toolbox/path.h>
#include <toolbox/stream/file_stream.h>

#define TAG "Flipso"

void flipso_saved_mkdir(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, FLIPSO_SAVED_FOLDER);
    furi_record_close(RECORD_STORAGE);
}

bool flipso_saved_any(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* dir = storage_file_alloc(storage);
    bool found = false;

    if(storage_dir_open(dir, FLIPSO_SAVED_FOLDER)) {
        FileInfo info;
        /* Comfortably past anything the save screen will write: a name too long
         * for the buffer ends the walk, which would hide the cards after it. */
        char name[128];
        while(!found && storage_dir_read(dir, &info, name, sizeof(name))) {
            if(info.flags & FSF_DIRECTORY) continue;
            const char* ext = strrchr(name, '.');
            found = ext && strcmp(ext, FLIPSO_SAVED_EXTENSION) == 0;
        }
    }

    storage_dir_close(dir);
    storage_file_free(dir);
    furi_record_close(RECORD_STORAGE);
    return found;
}

void flipso_saved_path(FuriString* path, const char* name) {
    furi_string_printf(path, "%s/%s%s", FLIPSO_SAVED_FOLDER, name, FLIPSO_SAVED_EXTENSION);
}

void flipso_saved_name(FuriString* name, const char* path) {
    path_extract_filename_no_ext(path, name);
}

void flipso_saved_suggest_name(
    char* out,
    size_t out_len,
    const ItsoCard* card,
    const char* brand) {
    furi_assert(out);
    furi_assert(out_len);

    /* The last four digits of the card number go on the end: that is how two
     * cards from the same scheme are told apart, and it is printed on the card
     * itself. Room for them is reserved before the brand is copied, because the
     * brand is the half a user can recognise without them. */
    size_t digits = 0;
    while(digits < ITSO_ISRN_DIGITS && card->isrn[digits]) {
        digits++;
    }
    bool numbered = digits >= 4 && out_len > 6;
    size_t room = numbered ? out_len - 5 : out_len;

    /* Anything outside this set is dropped: the name goes straight into a file
     * name, and a suggestion the user has to correct before they can save is
     * worse than one that is a character or two shorter. */
    size_t pos = 0;
    for(const char* c = brand ? brand : "ITSO Card"; *c && pos + 1 < room; c++) {
        bool safe = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
                    (*c >= '0' && *c <= '9') || *c == ' ' || *c == '-' || *c == '_';
        if(safe) out[pos++] = *c;
    }
    /* A brand that filled the field leaves a trailing space before the digits. */
    while(pos && out[pos - 1] == ' ') {
        pos--;
    }

    if(numbered) {
        if(pos) out[pos++] = ' ';
        memcpy(out + pos, card->isrn + digits - 4, 4);
        pos += 4;
    }

    out[pos] = '\0';
}

bool flipso_saved_write(const FlipsoCapture* capture, const char* path) {
    furi_assert(capture);
    furi_assert(path);

    flipso_saved_mkdir();

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    char* line = malloc(FLIPSO_CAPTURE_LINE_MAX);
    bool ok = storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS);

    for(size_t i = 0; ok && i < flipso_capture_lines(capture); i++) {
        if(!flipso_capture_line(capture, i, line, FLIPSO_CAPTURE_LINE_MAX)) {
            ok = false;
            break;
        }
        size_t len = strlen(line);
        line[len++] = '\n';
        ok = storage_file_write(file, line, len) == len;
    }

    storage_file_close(file);
    storage_file_free(file);
    free(line);

    if(!ok) {
        /* Everything downstream treats a file that is there as a card that can
         * be loaded, so a partial write must not leave one behind. */
        FURI_LOG_E(TAG, "Failed to write %s", path);
        storage_simply_remove(storage, path);
    }

    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool flipso_saved_read(FlipsoCapture* capture, const char* path) {
    furi_assert(capture);
    furi_assert(path);

    flipso_capture_reset(capture);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = file_stream_alloc(storage);
    bool ok = file_stream_open(stream, path, FSAM_READ, FSOM_OPEN_EXISTING);

    if(ok) {
        FuriString* line = furi_string_alloc();
        while(stream_read_line(stream, line)) {
            if(!flipso_capture_parse_line(capture, furi_string_get_cstr(line))) {
                /* Not one of our files, or one from a later version. Either way
                 * what has been read so far describes something else. */
                FURI_LOG_W(TAG, "%s is not a card this build can read", path);
                ok = false;
                break;
            }
        }
        furi_string_free(line);
    } else {
        FURI_LOG_E(TAG, "Could not open %s", path);
    }

    file_stream_close(stream);
    stream_free(stream);
    furi_record_close(RECORD_STORAGE);

    /* A file that parsed but carries no shell is as unusable as one that did
     * not parse: there is no card in it. */
    if(ok && !flipso_capture_valid(capture)) {
        FURI_LOG_W(TAG, "%s has no shell in it", path);
        ok = false;
    }
    if(!ok) flipso_capture_reset(capture);
    return ok;
}

bool flipso_saved_pick(FuriString* path) {
    furi_assert(path);

    /* The browser opens on the folder, so it has to exist even before the first
     * card is saved - otherwise the first press lands the user in /ext. */
    flipso_saved_mkdir();

    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);

    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, FLIPSO_SAVED_EXTENSION, &I_card_10px);
    options.base_path = FLIPSO_SAVED_FOLDER;
    /* The extension is the same on every row, so showing it only costs the
     * characters that tell two cards apart. */
    options.hide_ext = true;

    FuriString* start = furi_string_alloc_set(FLIPSO_SAVED_FOLDER);
    bool picked = dialog_file_browser_show(dialogs, path, start, &options);
    furi_string_free(start);

    furi_record_close(RECORD_DIALOGS);
    return picked;
}

void flipso_saved_alert(const char* header, const char* text) {
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, header, 64, 6, AlignCenter, AlignTop);
    dialog_message_set_text(message, text, 64, 24, AlignCenter, AlignTop);
    dialog_message_set_buttons(message, NULL, "OK", NULL);
    dialog_message_show(dialogs, message);
    dialog_message_free(message);
    furi_record_close(RECORD_DIALOGS);
}

bool flipso_saved_delete(const char* path) {
    furi_assert(path);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool ok = storage_simply_remove(storage, path);
    furi_record_close(RECORD_STORAGE);
    return ok;
}
