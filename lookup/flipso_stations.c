/**
 * @file flipso_stations.c
 * @brief Binary search over the packed station table held on the SD card.
 *
 * The table is a sorted index of fixed-width entries followed by a blob of
 * names, which is what makes the search possible without reading any of it
 * into memory: entry i starts at a known offset, so a lookup is a dozen short
 * reads regardless of how many stations the table holds. The search and the
 * checks every table needs are flipso_table.c's; the layout is documented in
 * tools/stations/FORMAT.md.
 *
 * The file is a build artefact of tools/stations/build_stations.py. The copy
 * that ships with the app is bundled into the .fap as a file asset and
 * unpacked to /ext/apps_assets/flipso/ by the firmware; a newer or wider table
 * can be dropped at /ext/apps_data/flipso/stations.dat to take over.
 */
#include "flipso_stations.h"
#include "flipso_table.h"

#include <furi.h>
#include <storage/storage.h>
#include <string.h>

#define TAG "Flipso"

/* Checked first, so a build of your own wins over the packaged table. */
#define FLIPSO_STATIONS_USER_PATH  APP_DATA_PATH("stations.dat")
#define FLIPSO_STATIONS_ASSET_PATH APP_ASSETS_PATH("stations.dat")

#define FLIPSO_STATIONS_MAGIC   "FSTN"
#define FLIPSO_STATIONS_VERSION 1
#define FLIPSO_STATIONS_HEADER  16
#define FLIPSO_STATIONS_ENTRY   6

struct FlipsoStations {
    Storage* storage;
    FlipsoTable table;
    bool open;
    uint32_t count; /**< Index entries, all four-digit NLCs in ascending order. */
    /* Scratch for the entry being examined, and the name handed back. */
    uint8_t entry[FLIPSO_STATIONS_ENTRY];
    char name[FLIPSO_STATION_NAME_MAX + 1];
};

/** Validate the header and record what the search needs from it. */
static bool flipso_stations_check(FlipsoTable* table, void* context) {
    FlipsoStations* instance = context;
    uint8_t header[FLIPSO_STATIONS_HEADER];
    if(!flipso_table_read_at(table, 0, header, sizeof(header))) return false;

    if(memcmp(header, FLIPSO_STATIONS_MAGIC, 4) != 0) return false;
    if(header[4] != FLIPSO_STATIONS_VERSION) return false;
    if(header[5] > FLIPSO_STATION_NAME_MAX) return false;

    instance->count = flipso_table_le(header + 8, 4);
    table->names = flipso_table_le(header + 12, 4);

    if(instance->count == 0) return false;
    if(instance->count > (UINT32_MAX - FLIPSO_STATIONS_HEADER) / FLIPSO_STATIONS_ENTRY)
        return false;
    if(table->names != FLIPSO_STATIONS_HEADER + instance->count * FLIPSO_STATIONS_ENTRY)
        return false;
    return table->names <= table->size;
}

/** Open @p path if it holds a table we understand. */
static bool flipso_stations_try(FlipsoStations* instance, const char* path) {
    switch(flipso_table_try(&instance->table, path, flipso_stations_check, instance)) {
    case FlipsoTableReady:
        FURI_LOG_I(TAG, "Station table: %lu entries from %s", instance->count, path);
        return true;
    case FlipsoTableUnusable:
        FURI_LOG_W(TAG, "Unusable station table at %s", path);
        return false;
    default:
        return false;
    }
}

FlipsoStations* flipso_stations_alloc(void) {
    FlipsoStations* instance = malloc(sizeof(FlipsoStations));
    memset(instance, 0, sizeof(FlipsoStations));

    instance->storage = furi_record_open(RECORD_STORAGE);
    instance->table.file = storage_file_alloc(instance->storage);

    instance->open = flipso_stations_try(instance, FLIPSO_STATIONS_USER_PATH) ||
                     flipso_stations_try(instance, FLIPSO_STATIONS_ASSET_PATH);

    return instance;
}

void flipso_stations_free(FlipsoStations* instance) {
    furi_assert(instance);
    storage_file_close(instance->table.file);
    storage_file_free(instance->table.file);
    furi_record_close(RECORD_STORAGE);
    free(instance);
}

bool flipso_stations_available(const FlipsoStations* instance) {
    return instance && instance->open;
}

uint32_t flipso_stations_count(const FlipsoStations* instance) {
    return flipso_stations_available(instance) ? instance->count : 0;
}

/** Parse a four-character NLC into the number the index is keyed on. */
static bool flipso_stations_key(const char* nlc, uint16_t* out) {
    uint16_t value = 0;
    for(int i = 0; i < 4; i++) {
        if(nlc[i] < '0' || nlc[i] > '9') return false;
        value = (uint16_t)(value * 10 + (nlc[i] - '0'));
    }
    if(nlc[4] != '\0') return false;
    *out = value;
    return true;
}

/** The index is keyed on the NLC as a little-endian number, in numeric order. */
static int flipso_stations_order(const uint8_t* entry, const void* key) {
    const uint16_t code = (uint16_t)flipso_table_le(entry, 2);
    const uint16_t wanted = *(const uint16_t*)key;
    return code < wanted ? -1 : code > wanted ? 1 : 0;
}

const char* flipso_stations_name(FlipsoStations* instance, const char* nlc) {
    if(!flipso_stations_available(instance) || nlc == NULL) return NULL;

    uint16_t wanted;
    /* Published NLCs are four digits; anything else is not in the table. */
    if(!flipso_stations_key(nlc, &wanted)) return NULL;

    if(!flipso_table_find(
           &instance->table,
           FLIPSO_STATIONS_HEADER,
           instance->count,
           FLIPSO_STATIONS_ENTRY,
           flipso_stations_order,
           &wanted,
           instance->entry)) {
        return NULL;
    }
    return flipso_table_name(
        &instance->table, instance->entry, 2, instance->name, FLIPSO_STATION_NAME_MAX);
}
