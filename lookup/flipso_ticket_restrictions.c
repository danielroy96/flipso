/**
 * @file flipso_ticket_restrictions.c
 * @brief Binary search over the packed ticket restriction table on the SD card.
 *
 * The ticket type table's design, through the reader the packed tables share
 * (flipso_table.c), with three texts to an entry rather than one: a name, and
 * the restriction's outward and return wording. The layout is documented in
 * tools/ticket_restrictions/FORMAT.md, and the file is a build artefact of
 * tools/ticket_restrictions/build_ticket_restrictions.py. Like the ticket type
 * table it is opened for each lookup and closed after it.
 */
#include "flipso_ticket_restrictions.h"
#include "flipso_table.h"

#include <furi.h>
#include <storage/storage.h>
#include <string.h>

/* Checked first, so a build of your own wins over the packaged table. */
#define FLIPSO_TICKET_RESTRICTIONS_USER_PATH  APP_DATA_PATH("ticket_restrictions.dat")
#define FLIPSO_TICKET_RESTRICTIONS_ASSET_PATH APP_ASSETS_PATH("ticket_restrictions.dat")

#define FLIPSO_TICKET_RESTRICTIONS_MAGIC   "FRST"
#define FLIPSO_TICKET_RESTRICTIONS_VERSION 1
/* The code, then three texts: a 3-byte offset and a length byte each. */
#define FLIPSO_TICKET_RESTRICTIONS_ENTRY   (FLIPSO_TICKET_RESTRICTION_CODE_LEN + 3 * 4)

struct FlipsoTicketRestrictions {
    Storage* storage;
};

/** One table, open for one lookup. */
typedef struct {
    FlipsoTable table;
    uint32_t count; /**< Index entries, ascending by code. */
} FlipsoRestrictionTable;

/** Validate the header and record what the search needs from it. */
static bool flipso_ticket_restrictions_check(FlipsoTable* table, void* context) {
    FlipsoRestrictionTable* restrictions = context;
    return flipso_table_check_header(
        table,
        FLIPSO_TICKET_RESTRICTIONS_MAGIC,
        FLIPSO_TICKET_RESTRICTIONS_VERSION,
        FLIPSO_TICKET_RESTRICTION_TEXT_MAX,
        FLIPSO_TICKET_RESTRICTIONS_ENTRY,
        &restrictions->count);
}

/** Open whichever table there is: the user's, else the packaged one. */
static bool flipso_ticket_restrictions_open(
    FlipsoTicketRestrictions* instance,
    FlipsoRestrictionTable* restrictions) {
    memset(restrictions, 0, sizeof(*restrictions));
    return flipso_table_open(
        &restrictions->table,
        instance->storage,
        FLIPSO_TICKET_RESTRICTIONS_USER_PATH,
        FLIPSO_TICKET_RESTRICTIONS_ASSET_PATH,
        flipso_ticket_restrictions_check,
        restrictions);
}

FlipsoTicketRestrictions* flipso_ticket_restrictions_alloc(void) {
    FlipsoTicketRestrictions* instance = malloc(sizeof(FlipsoTicketRestrictions));
    memset(instance, 0, sizeof(FlipsoTicketRestrictions));
    instance->storage = furi_record_open(RECORD_STORAGE);
    return instance;
}

void flipso_ticket_restrictions_free(FlipsoTicketRestrictions* instance) {
    furi_assert(instance);
    furi_record_close(RECORD_STORAGE);
    free(instance);
}

uint32_t flipso_ticket_restrictions_count(FlipsoTicketRestrictions* instance) {
    if(!instance) return 0;
    FlipsoRestrictionTable restrictions;
    if(!flipso_ticket_restrictions_open(instance, &restrictions)) return 0;
    uint32_t count = restrictions.count;
    flipso_table_close(&restrictions.table);
    return count;
}

/** The index is keyed on the code's two ASCII bytes, in memcmp order. */
static int flipso_ticket_restrictions_order(const uint8_t* entry, const void* key) {
    return memcmp(entry, key, FLIPSO_TICKET_RESTRICTION_CODE_LEN);
}

/** One of an entry's three texts into @p out; empty where it has none. */
static void flipso_ticket_restrictions_text(
    FlipsoTable* table,
    const uint8_t* entry,
    uint8_t field,
    FuriString* out) {
    const size_t at = FLIPSO_TICKET_RESTRICTION_CODE_LEN + (size_t)field * 4;
    /* A length of 0 is a text the feed did not have, which flipso_table_cat()
     * refuses: it is left empty, not an error. */
    flipso_table_cat(table, entry, at, out, FLIPSO_TICKET_RESTRICTION_TEXT_MAX);
}

bool flipso_ticket_restrictions_find(
    FlipsoTicketRestrictions* instance,
    const uint8_t code[FLIPSO_TICKET_RESTRICTION_CODE_LEN],
    FuriString* name,
    FuriString* outward,
    FuriString* back) {
    if(!name || !outward || !back) return false;
    furi_string_reset(name);
    furi_string_reset(outward);
    furi_string_reset(back);
    if(!instance || !code) return false;
    /* Codes are printable ASCII; a blank or binary field is in no table. */
    for(size_t i = 0; i < FLIPSO_TICKET_RESTRICTION_CODE_LEN; i++) {
        if(code[i] <= ' ' || code[i] > '~') return false;
    }

    FlipsoRestrictionTable restrictions;
    if(!flipso_ticket_restrictions_open(instance, &restrictions)) return false;

    uint8_t entry[FLIPSO_TICKET_RESTRICTIONS_ENTRY];
    const bool found = flipso_table_find(
        &restrictions.table,
        FLIPSO_TABLE_HEADER,
        restrictions.count,
        FLIPSO_TICKET_RESTRICTIONS_ENTRY,
        flipso_ticket_restrictions_order,
        code,
        entry);
    if(found) {
        flipso_ticket_restrictions_text(&restrictions.table, entry, 0, name);
        flipso_ticket_restrictions_text(&restrictions.table, entry, 1, outward);
        flipso_ticket_restrictions_text(&restrictions.table, entry, 2, back);
    }

    flipso_table_close(&restrictions.table);
    return found;
}
