/**
 * @file flipso_ticket_types.c
 * @brief Binary search over the packed ticket type table held on the SD card.
 *
 * The station table's design, searched by the reader they share
 * (flipso_table.c): a sorted index of fixed-width entries followed by a blob
 * of names, so a lookup is a dozen short reads without loading any of it.
 * The layout is documented in
 * tools/ticket_types/FORMAT.md, and the file is a build artefact of
 * tools/ticket_types/build_ticket_types.py.
 *
 * Unlike the station table the file is not held open: it is opened for each
 * lookup and closed after it, because only a reserved journey ever asks.
 */
#include "flipso_ticket_types.h"
#include "flipso_table.h"

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
    FlipsoTable table;
    uint32_t count; /**< Index entries, ascending by code. */
} FlipsoTicketTable;

/** Validate the header and record what the search needs from it. */
static bool flipso_ticket_types_check(FlipsoTable* table, void* context) {
    FlipsoTicketTable* tickets = context;
    uint8_t header[FLIPSO_TICKET_TYPES_HEADER];
    if(!flipso_table_read_at(table, 0, header, sizeof(header))) return false;

    if(memcmp(header, FLIPSO_TICKET_TYPES_MAGIC, 4) != 0) return false;
    if(header[4] != FLIPSO_TICKET_TYPES_VERSION) return false;
    if(header[5] > FLIPSO_TICKET_TYPE_NAME_MAX) return false;

    tickets->count = flipso_table_le(header + 8, 4);
    table->names = flipso_table_le(header + 12, 4);

    if(tickets->count == 0) return false;
    if(tickets->count > (UINT32_MAX - FLIPSO_TICKET_TYPES_HEADER) / FLIPSO_TICKET_TYPES_ENTRY)
        return false;
    if(table->names != FLIPSO_TICKET_TYPES_HEADER + tickets->count * FLIPSO_TICKET_TYPES_ENTRY)
        return false;
    return table->names <= table->size;
}

/** Open whichever table there is: the user's, else the packaged one. */
static bool flipso_ticket_types_open(FlipsoTicketTypes* instance, FlipsoTicketTable* tickets) {
    memset(tickets, 0, sizeof(*tickets));
    tickets->table.file = storage_file_alloc(instance->storage);
    if(flipso_table_try(
           &tickets->table, FLIPSO_TICKET_TYPES_USER_PATH, flipso_ticket_types_check, tickets) ==
           FlipsoTableReady ||
       flipso_table_try(
           &tickets->table, FLIPSO_TICKET_TYPES_ASSET_PATH, flipso_ticket_types_check, tickets) ==
           FlipsoTableReady) {
        return true;
    }
    storage_file_free(tickets->table.file);
    tickets->table.file = NULL;
    return false;
}

static void flipso_ticket_types_close(FlipsoTicketTable* tickets) {
    storage_file_close(tickets->table.file);
    storage_file_free(tickets->table.file);
    tickets->table.file = NULL;
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
    FlipsoTicketTable tickets;
    if(!flipso_ticket_types_open(instance, &tickets)) return 0;
    uint32_t count = tickets.count;
    flipso_ticket_types_close(&tickets);
    return count;
}

/** The index is keyed on the code's three ASCII bytes, in memcmp order. */
static int flipso_ticket_types_order(const uint8_t* entry, const void* key) {
    return memcmp(entry, key, FLIPSO_TICKET_TYPE_CODE_LEN);
}

const char* flipso_ticket_types_name(
    FlipsoTicketTypes* instance,
    const uint8_t code[FLIPSO_TICKET_TYPE_CODE_LEN]) {
    if(!instance || !code) return NULL;
    /* Codes are printable ASCII; a blank or binary field is in no table. */
    for(size_t i = 0; i < FLIPSO_TICKET_TYPE_CODE_LEN; i++) {
        if(code[i] <= ' ' || code[i] > '~') return NULL;
    }

    FlipsoTicketTable tickets;
    if(!flipso_ticket_types_open(instance, &tickets)) return NULL;

    const char* found = NULL;
    uint8_t entry[FLIPSO_TICKET_TYPES_ENTRY];
    if(flipso_table_find(
           &tickets.table,
           FLIPSO_TICKET_TYPES_HEADER,
           tickets.count,
           FLIPSO_TICKET_TYPES_ENTRY,
           flipso_ticket_types_order,
           code,
           entry)) {
        found = flipso_table_name(
            &tickets.table,
            entry,
            FLIPSO_TICKET_TYPE_CODE_LEN,
            instance->name,
            FLIPSO_TICKET_TYPE_NAME_MAX);
    }

    flipso_ticket_types_close(&tickets);
    return found;
}
