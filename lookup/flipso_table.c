/**
 * @file flipso_table.c
 * @brief The seek-and-compare reader the packed lookup tables share.
 */
#include "flipso_table.h"

#include <string.h>

uint32_t flipso_table_le(const uint8_t* bytes, size_t n) {
    uint32_t value = 0;
    for(size_t i = 0; i < n; i++) {
        value |= (uint32_t)bytes[i] << (8 * i);
    }
    return value;
}

bool flipso_table_read_at(FlipsoTable* table, uint32_t offset, void* into, uint16_t length) {
    if(!storage_file_seek(table->file, offset, true)) return false;
    return storage_file_read(table->file, into, length) == length;
}

FlipsoTableState flipso_table_try(
    FlipsoTable* table,
    const char* path,
    FlipsoTableHeaderCheck check,
    void* context) {
    if(!storage_file_open(table->file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        /* Close even though the open failed: the storage service registers the
         * path before it tries the file, and only a close takes it back off.
         * Left there, the next open of the same missing path - the next
         * launch's, for a table opened at startup - is told it is already
         * open and waits for ever for a close that never comes: the app hangs
         * before its first frame, behind the desktop, and the loader cannot
         * close it. The SDK says so on storage_file_open(). */
        storage_file_close(table->file);
        return FlipsoTableMissing;
    }
    table->size = (uint32_t)storage_file_size(table->file);
    if(check(table, context)) return FlipsoTableReady;
    storage_file_close(table->file);
    return FlipsoTableUnusable;
}

bool flipso_table_find(
    FlipsoTable* table,
    uint32_t index,
    uint32_t count,
    uint16_t entry_len,
    FlipsoTableOrder order,
    const void* key,
    uint8_t* entry) {
    uint32_t low = 0;
    uint32_t high = count;
    while(low < high) {
        uint32_t mid = low + (high - low) / 2;
        if(!flipso_table_read_at(table, index + mid * entry_len, entry, entry_len)) return false;
        int side = order(entry, key);
        if(side == 0) {
            return true;
        } else if(side < 0) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return false;
}

/** Where an entry's text is, checked against the file; false when it is not in it. */
static bool flipso_table_locate(
    FlipsoTable* table,
    const uint8_t* entry,
    size_t at,
    uint8_t max,
    uint32_t* offset,
    uint8_t* length) {
    *offset = flipso_table_le(entry + at, 3);
    *length = entry[at + 3];
    if(*length == 0 || *length > max) return false;
    /* Written as subtractions rather than as names + offset + length: the
     * offset is 24 bits of whatever the file happened to contain, and the sum
     * of three of these can wrap a uint32 and pass a check it should fail.
     * Every header check has already established names <= size. */
    if(*offset > table->size - table->names) return false;
    return *length <= table->size - table->names - *offset;
}

bool flipso_table_cat(
    FlipsoTable* table,
    const uint8_t* entry,
    size_t at,
    FuriString* out,
    uint8_t max) {
    uint32_t offset;
    uint8_t length;
    if(!flipso_table_locate(table, entry, at, max, &offset, &length)) return false;
    if(!storage_file_seek(table->file, table->names + offset, true)) return false;
    const size_t before = furi_string_size(out);
    /* Room for all of it at once: one allocation, rather than one each time
     * a chunk outgrows the string, on a heap the screen has already split. */
    furi_string_reserve(out, before + length);
    char chunk[33];
    while(length) {
        const uint16_t take = length < 32 ? length : 32;
        if(storage_file_read(table->file, chunk, take) != take) {
            furi_string_left(out, before);
            return false;
        }
        chunk[take] = '\0';
        furi_string_cat(out, chunk);
        length = (uint8_t)(length - take);
    }
    return true;
}

bool flipso_table_check_header(
    FlipsoTable* table,
    const char* magic,
    uint8_t version,
    uint8_t max,
    uint16_t entry_len,
    uint32_t* count) {
    uint8_t header[FLIPSO_TABLE_HEADER];
    if(!flipso_table_read_at(table, 0, header, sizeof(header))) return false;

    if(memcmp(header, magic, 4) != 0) return false;
    if(header[4] != version) return false;
    if(header[5] > max) return false;

    *count = flipso_table_le(header + 8, 4);
    table->names = flipso_table_le(header + 12, 4);

    if(*count == 0) return false;
    if(*count > (UINT32_MAX - FLIPSO_TABLE_HEADER) / entry_len) return false;
    if(table->names != FLIPSO_TABLE_HEADER + *count * entry_len) return false;
    return table->names <= table->size;
}

bool flipso_table_open(
    FlipsoTable* table,
    Storage* storage,
    const char* user_path,
    const char* asset_path,
    FlipsoTableHeaderCheck check,
    void* context) {
    table->file = storage_file_alloc(storage);
    if(flipso_table_try(table, user_path, check, context) == FlipsoTableReady ||
       flipso_table_try(table, asset_path, check, context) == FlipsoTableReady) {
        return true;
    }
    storage_file_free(table->file);
    table->file = NULL;
    return false;
}

void flipso_table_close(FlipsoTable* table) {
    if(!table->file) return;
    storage_file_close(table->file);
    storage_file_free(table->file);
    table->file = NULL;
}

const char*
    flipso_table_name(FlipsoTable* table, const uint8_t* entry, size_t at, char* out, uint8_t max) {
    uint32_t offset;
    uint8_t length;
    if(!flipso_table_locate(table, entry, at, max, &offset, &length)) return NULL;
    if(!flipso_table_read_at(table, table->names + offset, out, length)) return NULL;

    out[length] = '\0';
    return out;
}
