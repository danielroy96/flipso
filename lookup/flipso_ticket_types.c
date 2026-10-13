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
    return flipso_table_check_header(
        table,
        FLIPSO_TICKET_TYPES_MAGIC,
        FLIPSO_TICKET_TYPES_VERSION,
        FLIPSO_TICKET_TYPE_NAME_MAX,
        FLIPSO_TICKET_TYPES_ENTRY,
        &tickets->count);
}

/** Open whichever table there is: the user's, else the packaged one. */
static bool flipso_ticket_types_open(FlipsoTicketTypes* instance, FlipsoTicketTable* tickets) {
    memset(tickets, 0, sizeof(*tickets));
    return flipso_table_open(
        &tickets->table,
        instance->storage,
        FLIPSO_TICKET_TYPES_USER_PATH,
        FLIPSO_TICKET_TYPES_ASSET_PATH,
        flipso_ticket_types_check,
        tickets);
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
    flipso_table_close(&tickets.table);
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
           FLIPSO_TABLE_HEADER,
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

    flipso_table_close(&tickets.table);
    return found;
}
