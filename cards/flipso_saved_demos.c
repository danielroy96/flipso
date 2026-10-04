/**
 * @file flipso_saved_demos.c
 * @brief The demo cards the About menu opens: saved cards of the app's own,
 * packaged with it rather than written by the user.
 */
#include "flipso_saved_i.h"

#include <toolbox/path.h>
#include <toolbox/stream/file_stream.h>

#include <string.h>
#include <strings.h>

uint8_t flipso_saved_demos(FlipsoDemos* demos) {
    uint8_t count = 0;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* dir = storage_file_alloc(storage);

    if(storage_dir_open(dir, FLIPSO_DEMO_FOLDER)) {
        FileInfo info;
        char name[128];
        while(storage_dir_read(dir, &info, name, sizeof(name))) {
            if(info.flags & FSF_DIRECTORY) continue;
            char* ext = strrchr(name, '.');
            if(!ext || strcmp(ext, FLIPSO_SAVED_EXTENSION) != 0) continue;
            *ext = '\0';
            /* Cut short, the name would no longer be the file's. */
            const size_t len = strlen(name);
            if(len >= FLIPSO_SAVED_NAME_LEN) continue;

            if(!demos) {
                if(count < FLIPSO_DEMO_MAX) count++;
                continue;
            }
            /* Inserted in order as they come, because the directory's own order
             * is whatever the SD card's allocation left it. With the list full,
             * the last in order is the one that gives way, so which cards are
             * listed never depends on that order either. */
            uint8_t at = count;
            while(at && strcmp(name, demos->names[at - 1]) < 0) {
                at--;
            }
            if(at == FLIPSO_DEMO_MAX) continue;
            uint8_t last = count < FLIPSO_DEMO_MAX ? count : FLIPSO_DEMO_MAX - 1;
            memmove(demos->names[at + 1], demos->names[at], (last - at) * FLIPSO_SAVED_NAME_LEN);
            memcpy(demos->names[at], name, len + 1);
            if(count < FLIPSO_DEMO_MAX) count++;
        }
    }

    storage_dir_close(dir);
    storage_file_free(dir);
    furi_record_close(RECORD_STORAGE);

    if(demos) demos->count = count;
    return count;
}

void flipso_saved_demo_path(FuriString* path, const char* name) {
    furi_string_printf(path, "%s/%s%s", FLIPSO_DEMO_FOLDER, name, FLIPSO_SAVED_EXTENSION);
}

bool flipso_saved_is_demo(const char* path) {
    furi_assert(path);
    const char* folder = FLIPSO_DEMO_FOLDER "/";
    return strncmp(path, folder, strlen(folder)) == 0;
}
