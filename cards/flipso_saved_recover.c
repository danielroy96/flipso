/**
 * @file flipso_saved_recover.c
 * @brief Finishing, or undoing, whatever a power cut left half done in the
 * saved-cards folder.
 */
#include "flipso_saved_i.h"

#include <toolbox/path.h>
#include <toolbox/stream/file_stream.h>

#include <string.h>
#include <strings.h>

/* Leftovers dealt with per launch. More than this means something other than a
 * power cut put them there, and the rest wait for the next launch. */
#define FLIPSO_SAVED_RECOVER_MAX 8
#define FLIPSO_SAVED_NAME_MAX    128

/** True when @p name is a card's name with @p suffix after it. */
static bool flipso_saved_is_leftover(const char* name, const char* suffix) {
    size_t len = strlen(name);
    size_t suffix_len = strlen(suffix);
    size_t ext_len = strlen(FLIPSO_SAVED_EXTENSION);
    if(len <= ext_len + suffix_len) return false;
    return strcmp(name + len - suffix_len, suffix) == 0 &&
           strncmp(name + len - suffix_len - ext_len, FLIPSO_SAVED_EXTENSION, ext_len) == 0;
}

/**
 * Collect the leftovers carrying @p suffix into @p names, which holds @p max
 * names of FLIPSO_SAVED_NAME_MAX bytes. Collected before anything is moved, so
 * the folder is not renamed under its own directory walk.
 */
static uint8_t
    flipso_saved_collect(Storage* storage, const char* suffix, char* names, uint8_t max) {
    File* dir = storage_file_alloc(storage);
    uint8_t count = 0;
    if(storage_dir_open(dir, FLIPSO_SAVED_FOLDER)) {
        FileInfo info;
        char name[FLIPSO_SAVED_NAME_MAX];
        while(count < max && storage_dir_read(dir, &info, name, sizeof(name))) {
            if(info.flags & FSF_DIRECTORY) continue;
            if(!flipso_saved_is_leftover(name, suffix)) continue;
            memcpy(names + (size_t)count * FLIPSO_SAVED_NAME_MAX, name, sizeof(name));
            count++;
        }
    }
    storage_dir_close(dir);
    storage_file_free(dir);
    return count;
}

void flipso_saved_recover(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    char* names = malloc((size_t)FLIPSO_SAVED_RECOVER_MAX * FLIPSO_SAVED_NAME_MAX);
    FuriString* leftover = furi_string_alloc();
    FuriString* card = furi_string_alloc();
    FuriString* temp = furi_string_alloc();

    /* A record set aside by a save: the save stopped somewhere between moving
     * it out of the way and removing it. If the card's name is free, the new
     * copy never arrived there - but a .tmp beside it is whole, because a copy
     * is only ever renamed once it is closed, so it goes in; failing that, the
     * old record goes back. If the name is taken the save finished, and the old
     * record is only the step it did not get to. */
    uint8_t count =
        flipso_saved_collect(storage, FLIPSO_SAVED_OLD_SUFFIX, names, FLIPSO_SAVED_RECOVER_MAX);
    for(uint8_t i = 0; i < count; i++) {
        const char* name = names + (size_t)i * FLIPSO_SAVED_NAME_MAX;
        furi_string_printf(leftover, "%s/%s", FLIPSO_SAVED_FOLDER, name);
        furi_string_printf(
            card,
            "%s/%.*s",
            FLIPSO_SAVED_FOLDER,
            (int)(strlen(name) - strlen(FLIPSO_SAVED_OLD_SUFFIX)),
            name);
        furi_string_printf(temp, "%s%s", furi_string_get_cstr(card), FLIPSO_SAVED_TEMP_SUFFIX);

        if(storage_common_exists(storage, furi_string_get_cstr(card))) {
            storage_simply_remove(storage, furi_string_get_cstr(leftover));
        } else if(
            storage_common_exists(storage, furi_string_get_cstr(temp)) &&
            storage_common_rename(
                storage, furi_string_get_cstr(temp), furi_string_get_cstr(card)) == FSE_OK) {
            storage_simply_remove(storage, furi_string_get_cstr(leftover));
        } else {
            storage_common_rename(
                storage, furi_string_get_cstr(leftover), furi_string_get_cstr(card));
        }
        FURI_LOG_W(TAG, "Recovered %s", furi_string_get_cstr(card));
    }

    /* A card part way through a change of case: it is whole, so it goes back
     * under the name it had. Beside a card of that name it is left alone -
     * that is not a state a rename leaves, and not one to guess about. */
    count =
        flipso_saved_collect(storage, FLIPSO_SAVED_MOVE_SUFFIX, names, FLIPSO_SAVED_RECOVER_MAX);
    for(uint8_t i = 0; i < count; i++) {
        const char* name = names + (size_t)i * FLIPSO_SAVED_NAME_MAX;
        furi_string_printf(leftover, "%s/%s", FLIPSO_SAVED_FOLDER, name);
        furi_string_printf(
            card,
            "%s/%.*s",
            FLIPSO_SAVED_FOLDER,
            (int)(strlen(name) - strlen(FLIPSO_SAVED_MOVE_SUFFIX)),
            name);
        if(storage_common_exists(storage, furi_string_get_cstr(card))) continue;
        storage_common_rename(storage, furi_string_get_cstr(leftover), furi_string_get_cstr(card));
        FURI_LOG_W(TAG, "Recovered %s", furi_string_get_cstr(card));
    }

    /* Any copy still being written is not known to be whole - it may be the
     * front of a card that a power cut cut short - and every whole one has been
     * put in place above. */
    count =
        flipso_saved_collect(storage, FLIPSO_SAVED_TEMP_SUFFIX, names, FLIPSO_SAVED_RECOVER_MAX);
    for(uint8_t i = 0; i < count; i++) {
        furi_string_printf(
            leftover, "%s/%s", FLIPSO_SAVED_FOLDER, names + (size_t)i * FLIPSO_SAVED_NAME_MAX);
        storage_simply_remove(storage, furi_string_get_cstr(leftover));
    }

    furi_string_free(temp);
    furi_string_free(card);
    furi_string_free(leftover);
    free(names);
    furi_record_close(RECORD_STORAGE);
}
