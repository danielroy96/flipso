/**
 * @file flipso_ticket_types.c
 * @brief Binary search over the packed ticket type table held on the SD card.
 *
 * The station table's design (flipso_stations.c): a sorted index of
 * fixed-width entries followed by a blob of names, so a lookup is a dozen
 * short reads without loading any of it. The layout is documented in
 * tools/ticket_types/FORMAT.md, and the file is a build artefact of
 * tools/ticket_types/build_ticket_types.py.
 *
 * Unlike the station table the file is not held open: it is opened for each
 * lookup and closed after it, because only a reserved journey ever asks.
 */
#include "flipso_ticket_types.h"

#include <furi.h>
#include <storage/storage.h>
#include <string.h>

/* Checked first, so a build of your own wins over the packaged table. */
#define FLIPSO_TICKET_TYPES_USER_PATH  APP_DATA_PATH("ticket_types.dat")
#define FLIPSO_TICKET_TYPES_ASSET_PATH APP_ASSETS_PATH("ticket_types.dat")

#define FLIPSO_TICKET_TYPES_MAGIC   "FTKT"
#define FLIPSO_TICKET_TYPES_VERSION 1
#define FLIPSO_TICKET_TYPES_HEADER  16
#define FLIPSO_TICKET_TYPES_ENTRY   7

struct FlipsoTicketTypes {
    Storage* storage;
    /** The name handed back, valid until the next lookup. */
    char name[FLIPSO_TICKET_TYPE_NAME_MAX + 1];
};

/** One table, open for one lookup. */
typedef struct {
    File* file;
    uint32_t count; /**< Index entries, ascending by code. */
    uint32_t names; /**< Absolute offset of the name blob. */
    uint32_t size; /**< File size, so a corrupt offset cannot read past the end. */
} FlipsoTicketTable;

static uint32_t flipso_ticket_types_le(const uint8_t* bytes, size_t n) {
    uint32_t value = 0;
    for(size_t i = 0; i < n; i++) {
        value |= (uint32_t)bytes[i] << (8 * i);
    }
    return value;
}

static bool flipso_ticket_types_read_at(
    FlipsoTicketTable* table,
    uint32_t offset,
    void* into,
    uint16_t length) {
    if(!storage_file_seek(table->file, offset, true)) return false;
    return storage_file_read(table->file, into, length) == length;
}

/**
 * Validate the header and record what the search needs from it, every field
 * range checked against the file size: a truncated or unrelated file on a
 * removable card has to fail cleanly rather than send the search off the end.
 */
static bool flipso_ticket_types_read_header(FlipsoTicketTable* table) {
    uint8_t header[FLIPSO_TICKET_TYPES_HEADER];
    if(!flipso_ticket_types_read_at(table, 0, header, sizeof(header))) return false;

    if(memcmp(header, FLIPSO_TICKET_TYPES_MAGIC, 4) != 0) return false;
    if(header[4] != FLIPSO_TICKET_TYPES_VERSION) return false;
    if(header[5] > FLIPSO_TICKET_TYPE_NAME_MAX) return false;

    table->count = flipso_ticket_types_le(header + 8, 4);
    table->names = flipso_ticket_types_le(header + 12, 4);

    if(table->count == 0) return false;
    if(table->count > (UINT32_MAX - FLIPSO_TICKET_TYPES_HEADER) / FLIPSO_TICKET_TYPES_ENTRY)
        return false;
    if(table->names != FLIPSO_TICKET_TYPES_HEADER + table->count * FLIPSO_TICKET_TYPES_ENTRY)
        return false;
    return table->names <= table->size;
}

/** Open @p path into @p table if it holds a table we understand. */
static bool flipso_ticket_types_try(FlipsoTicketTable* table, const char* path) {
    if(!storage_file_open(table->file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        /* Close even though the open failed: the storage service registers the
         * path before it tries the file, and only a close takes it back off.
         * Left there, the next open of the same missing path - the next
         * lookup, or the next launch - waits for ever. See flipso_stations.c. */
        storage_file_close(table->file);
        return false;
    }
    table->size = (uint32_t)storage_file_size(table->file);
    if(flipso_ticket_types_read_header(table)) return true;
    storage_file_close(table->file);
    return false;
}

/** Open whichever table there is: the user's, else the packaged one. */
static bool flipso_ticket_types_open(FlipsoTicketTypes* instance, FlipsoTicketTable* table) {
    memset(table, 0, sizeof(*table));
    table->file = storage_file_alloc(instance->storage);
    if(flipso_ticket_types_try(table, FLIPSO_TICKET_TYPES_USER_PATH) ||
       flipso_ticket_types_try(table, FLIPSO_TICKET_TYPES_ASSET_PATH)) {
        return true;
    }
    storage_file_free(table->file);
    table->file = NULL;
    return false;
}

static void flipso_ticket_types_close(FlipsoTicketTable* table) {
    storage_file_close(table->file);
    storage_file_free(table->file);
    table->file = NULL;
}

FlipsoTicketTypes* flipso_ticket_types_alloc(void) {
    FlipsoTicketTypes* instance = malloc(sizeof(FlipsoTicketTypes));
    memset(instance, 0, sizeof(FlipsoTicketTypes));
    instance->storage = furi_record_open(RECORD_STORAGE);
    return instance;
}

void flipso_ticket_types_free(FlipsoTicketTypes* instance) {
    furi_assert(instance);
    furi_record_close(RECORD_STORAGE);
    free(instance);
}

uint32_t flipso_ticket_types_count(FlipsoTicketTypes* instance) {
    if(!instance) return 0;
    FlipsoTicketTable table;
    if(!flipso_ticket_types_open(instance, &table)) return 0;
    uint32_t count = table.count;
    flipso_ticket_types_close(&table);
    return count;
}

/** Copy the name an index entry points at into the instance buffer. */
static const char* flipso_ticket_types_fetch(
    FlipsoTicketTypes* instance,
    FlipsoTicketTable* table,
    const uint8_t* entry) {
    uint32_t offset = flipso_ticket_types_le(entry + FLIPSO_TICKET_TYPE_CODE_LEN, 3);
    uint8_t length = entry[FLIPSO_TICKET_TYPE_CODE_LEN + 3];

    if(length == 0 || length > FLIPSO_TICKET_TYPE_NAME_MAX) return NULL;
    /* Subtractions rather than names + offset + length, which can wrap a
     * uint32 on a corrupt offset and pass a check it should fail. The header
     * check has already established names <= size. */
    if(offset > table->size - table->names) return NULL;
    if(length > table->size - table->names - offset) return NULL;
    if(!flipso_ticket_types_read_at(table, table->names + offset, instance->name, length))
        return NULL;

    instance->name[length] = '\0';
    return instance->name;
}

const char* flipso_ticket_types_name(
    FlipsoTicketTypes* instance,
    const uint8_t code[FLIPSO_TICKET_TYPE_CODE_LEN]) {
    if(!instance || !code) return NULL;
    /* Codes are printable ASCII; a blank or binary field is in no table. */
    for(size_t i = 0; i < FLIPSO_TICKET_TYPE_CODE_LEN; i++) {
        if(code[i] <= ' ' || code[i] > '~') return NULL;
    }

    FlipsoTicketTable table;
    if(!flipso_ticket_types_open(instance, &table)) return NULL;

    const char* found = NULL;
    uint8_t entry[FLIPSO_TICKET_TYPES_ENTRY];
    uint32_t low = 0;
    uint32_t high = table.count;
    while(low < high) {
        uint32_t mid = low + (high - low) / 2;
        uint32_t at = FLIPSO_TICKET_TYPES_HEADER + mid * FLIPSO_TICKET_TYPES_ENTRY;
        if(!flipso_ticket_types_read_at(&table, at, entry, sizeof(entry))) break;

        int order = memcmp(entry, code, FLIPSO_TICKET_TYPE_CODE_LEN);
        if(order == 0) {
            found = flipso_ticket_types_fetch(instance, &table, entry);
            break;
        } else if(order < 0) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    flipso_ticket_types_close(&table);
    return found;
}
