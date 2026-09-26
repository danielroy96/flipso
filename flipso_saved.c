/**
 * @file flipso_saved.c
 * @brief Reading and writing the saved-card files. See flipso_saved.h.
 */
#include "flipso_saved.h"

#include "flipso_icons.h"

#include <dialogs/dialogs.h>
#include <toolbox/path.h>
#include <toolbox/stream/file_stream.h>

#include <strings.h>

#define TAG "Flipso"

/* Beside the record it will replace, so the rename that puts it there is a
 * rename within one folder. Not a card extension, so the browser never lists
 * one left behind by a power cut. */
#define FLIPSO_SAVED_TEMP_SUFFIX ".tmp"

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

/** Write every line of @p capture to @p path, replacing anything there. */
static bool flipso_saved_write_file(Storage* storage, const FlipsoCapture* capture, const char* path) {
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
    return ok;
}

bool flipso_saved_write(const FlipsoCapture* capture, const char* path) {
    furi_assert(capture);
    furi_assert(path);

    flipso_saved_mkdir();

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FuriString* temp = furi_string_alloc();
    furi_string_printf(temp, "%s%s", path, FLIPSO_SAVED_TEMP_SUFFIX);
    const char* temp_path = furi_string_get_cstr(temp);

    /* The whole file goes to one side first. Updating a card writes over the
     * only copy of the journeys that have rolled off it since, so a write that
     * fails part way - a full SD card - must fail before the old record is
     * touched, not after it has been truncated. */
    bool ok = flipso_saved_write_file(storage, capture, temp_path);
    /* The rename replaces the old record in one step: the SDK documents that
     * it overwrites its destination. A rename that fails leaves the old record
     * alone, and nothing here removes it to try again - if that retry failed
     * too, both copies would be gone. */
    if(ok) ok = storage_common_rename(storage, temp_path, path) == FSE_OK;

    if(!ok) {
        /* Everything downstream treats a file that is there as a card that can
         * be loaded, so a partial write must not leave one behind. The record
         * being replaced, if there was one, is still as it was. */
        FURI_LOG_E(TAG, "Failed to write %s", path);
        storage_simply_remove(storage, temp_path);
    }

    furi_string_free(temp);
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

/*
 * Lines to read before giving up on finding a candidate's shell.
 *
 * We write it fourth. The cap is not about our own files but about anything
 * else that has ended up in the folder with the right extension: without it a
 * stray megabyte would be read a line at a time, once per save.
 */
#define FLIPSO_SAVED_PEEK_LINES 64

/**
 * Read a candidate's header far enough to learn which card it holds.
 * @return false for a file that is not ours, or that has no shell near its top.
 */
static bool flipso_saved_peek(Stream* stream, FlipsoCapture* scratch, char* isrn) {
    flipso_capture_reset(scratch);

    FuriString* line = furi_string_alloc();
    bool ours = true;
    /* Stops at the shell rather than reading on: everything after it is the
     * part of the card that changes, and none of it says which card this is. */
    for(size_t i = 0; i < FLIPSO_SAVED_PEEK_LINES; i++) {
        if(flipso_capture_valid(scratch)) break;
        if(!stream_read_line(stream, line)) break;
        if(!flipso_capture_parse_line(scratch, furi_string_get_cstr(line))) {
            ours = false;
            break;
        }
    }
    furi_string_free(line);

    return ours && flipso_capture_card_number(scratch, isrn);
}

bool flipso_saved_find(const FlipsoCapture* capture, FuriString* path, uint32_t* read_at) {
    furi_assert(capture);
    furi_assert(path);

    char wanted[ITSO_ISRN_DIGITS + 1];
    if(!flipso_capture_card_number(capture, wanted)) return false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* dir = storage_file_alloc(storage);
    Stream* stream = file_stream_alloc(storage);
    FlipsoCapture* scratch = flipso_capture_alloc();
    FuriString* candidate = furi_string_alloc();
    bool found = false;

    if(storage_dir_open(dir, FLIPSO_SAVED_FOLDER)) {
        FileInfo info;
        char name[128];
        while(!found && storage_dir_read(dir, &info, name, sizeof(name))) {
            if(info.flags & FSF_DIRECTORY) continue;
            const char* ext = strrchr(name, '.');
            if(!ext || strcmp(ext, FLIPSO_SAVED_EXTENSION) != 0) continue;

            furi_string_printf(candidate, "%s/%s", FLIPSO_SAVED_FOLDER, name);
            if(!file_stream_open(
                   stream, furi_string_get_cstr(candidate), FSAM_READ, FSOM_OPEN_EXISTING)) {
                /* Closed even so, or the path stays registered as open and the
                 * next open of it waits for ever (see flipso_stations_try()). */
                file_stream_close(stream);
                continue;
            }

            char isrn[ITSO_ISRN_DIGITS + 1];
            if(flipso_saved_peek(stream, scratch, isrn) && strcmp(isrn, wanted) == 0) {
                furi_string_set(path, candidate);
                if(read_at) *read_at = flipso_capture_time(scratch);
                found = true;
            }
            file_stream_close(stream);
        }
    }

    storage_dir_close(dir);
    storage_file_free(dir);
    stream_free(stream);
    flipso_capture_free(scratch);
    furi_string_free(candidate);
    furi_record_close(RECORD_STORAGE);

    if(found) FURI_LOG_I(TAG, "Card %s is already saved", wanted);
    return found;
}

bool flipso_saved_pick(FuriString* path, const FuriString* select) {
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

    /* The browser opens on the file its start path names, so coming back from
     * a card lands on that card rather than on the top of the list. */
    FuriString* start = furi_string_alloc_set(FLIPSO_SAVED_FOLDER);
    if(select && !furi_string_empty(select)) furi_string_set_str(start, furi_string_get_cstr(select));
    bool picked = dialog_file_browser_show(dialogs, path, start, &options);
    furi_string_free(start);

    furi_record_close(RECORD_DIALOGS);
    return picked;
}

bool flipso_saved_rename(const char* from, const char* to) {
    furi_assert(from);
    furi_assert(to);

    /* Renaming to the name it already has: the text input allows it, because
     * the current name is the one the "already taken" check has to let through,
     * and the file system need not be asked to move a file onto itself. */
    if(strcmp(from, to) == 0) return true;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FS_Error error;

    if(strcasecmp(from, to) == 0) {
        /* Only the case changes. FAT names ignore case, so to the file system
         * the destination is the card itself, and a rename that clears its
         * destination first would delete it. Going by way of a name that is
         * certainly free keeps each step an ordinary move. */
        FuriString* step = furi_string_alloc();
        furi_string_printf(step, "%s%s", from, FLIPSO_SAVED_TEMP_SUFFIX);
        error = storage_common_rename(storage, from, furi_string_get_cstr(step));
        if(error == FSE_OK) {
            error = storage_common_rename(storage, furi_string_get_cstr(step), to);
            if(error != FSE_OK) storage_common_rename(storage, furi_string_get_cstr(step), from);
        }
        furi_string_free(step);
    } else if(storage_common_exists(storage, to)) {
        /* The firmware overwrites a destination that exists, and that would be
         * somebody else's card. The name screen refuses a taken name already;
         * this is the check that does not depend on it. */
        error = FSE_EXIST;
    } else {
        error = storage_common_rename(storage, from, to);
    }

    furi_record_close(RECORD_STORAGE);

    if(error != FSE_OK) FURI_LOG_E(TAG, "Could not rename %s to %s", from, to);
    return error == FSE_OK;
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
