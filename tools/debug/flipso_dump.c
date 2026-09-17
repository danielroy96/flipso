/**
 * @file flipso_dump.c
 * @brief Opt-in diagnostic: append the raw bytes read off a card to the SD card.
 *
 * See flipso_dump.h for how to wire this in and take it back out.
 */
#include "flipso_dump.h"

#include <furi.h>
#include <storage/storage.h>

#define FLIPSO_DUMP_PATH APP_DATA_PATH("dump.txt")

static void flipso_dump_write(const char* text, bool truncate) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);

    if(storage_file_open(
           file, FLIPSO_DUMP_PATH, FSAM_WRITE, truncate ? FSOM_CREATE_ALWAYS : FSOM_OPEN_APPEND)) {
        storage_file_write(file, text, strlen(text));
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

void flipso_dump_begin(void) {
    flipso_dump_write("--- scan ---\n", true);
}

void flipso_dump_block(const char* label, const uint8_t* data, size_t len) {
    /* One block per line keeps the file trivially parseable by tools/test/replay.py,
     * and the length is written out because a truncated read is exactly the kind
     * of bug this is here to find. */
    FuriString* text = furi_string_alloc();
    furi_string_cat_printf(text, "%s %u\n", label, (unsigned)len);
    for(size_t i = 0; i < len; i++) {
        furi_string_cat_printf(text, "%02X", data[i]);
    }
    furi_string_cat(text, "\n");

    flipso_dump_write(furi_string_get_cstr(text), false);
    furi_string_free(text);
}
