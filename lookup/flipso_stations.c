/**
 * @file flipso_stations.c
 * @brief Binary search over the packed station table held on the SD card.
 *
 * The table is a sorted index of fixed-width entries followed by a blob of
 * names, which is what makes the search possible without reading any of it
 * into memory: entry i starts at a known offset, so a lookup is a dozen short
 * reads regardless of how many stations the table holds. The layout is
 * documented in tools/stations/FORMAT.md.
 *
 * The file is a build artefact of tools/stations/build_stations.py. The copy
 * that ships with the app is bundled into the .fap as a file asset and
 * unpacked to /ext/apps_assets/flipso/ by the firmware; a newer or wider table
 * can be dropped at /ext/apps_data/flipso/stations.dat to take over.
 */
#include "flipso_stations.h"

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
    File* file;
    bool open;
    uint32_t count; /**< Index entries, all four-digit NLCs in ascending order. */
    uint32_t names; /**< Absolute offset of the name blob. */
    uint32_t size; /**< File size, so a corrupt offset cannot read past the end. */
    /* Scratch for the entry being examined, and the name handed back. */
    uint8_t entry[FLIPSO_STATIONS_ENTRY];
    char name[FLIPSO_STATION_NAME_MAX + 1];
};

static uint32_t flipso_stations_le(const uint8_t* bytes, size_t n) {
    uint32_t value = 0;
    for(size_t i = 0; i < n; i++) {
        value |= (uint32_t)bytes[i] << (8 * i);
    }
    return value;
}

static bool flipso_stations_read_at(
    FlipsoStations* instance,
    uint32_t offset,
    void* into,
    uint16_t length) {
    if(!storage_file_seek(instance->file, offset, true)) return false;
    return storage_file_read(instance->file, into, length) == length;
}

/**
 * Validate the header and record what the search needs from it.
 *
 * Every field is range checked against the file size: the table is a file on a
 * removable card, so a truncated or unrelated file has to fail cleanly rather
 * than send the search off the end.
 */
static bool flipso_stations_read_header(FlipsoStations* instance) {
    uint8_t header[FLIPSO_STATIONS_HEADER];
    if(!flipso_stations_read_at(instance, 0, header, sizeof(header))) return false;

    if(memcmp(header, FLIPSO_STATIONS_MAGIC, 4) != 0) return false;
    if(header[4] != FLIPSO_STATIONS_VERSION) return false;
    if(header[5] > FLIPSO_STATION_NAME_MAX) return false;

    instance->count = flipso_stations_le(header + 8, 4);
    instance->names = flipso_stations_le(header + 12, 4);

    if(instance->count == 0) return false;
    if(instance->count > (UINT32_MAX - FLIPSO_STATIONS_HEADER) / FLIPSO_STATIONS_ENTRY)
        return false;
    if(instance->names != FLIPSO_STATIONS_HEADER + instance->count * FLIPSO_STATIONS_ENTRY)
        return false;
    return instance->names <= instance->size;
}

/** Open @p path if it holds a table we understand. */
static bool flipso_stations_try(FlipsoStations* instance, const char* path) {
    if(!storage_file_open(instance->file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        /* Close even though the open failed: the storage service registers the
         * path before it tries the file, and only a close takes it back off.
         * Left there, the next launch's open of the same missing path is told
         * it is already open and waits for ever for a close that never comes -
         * the app hangs before its first frame, behind the desktop, and the
         * loader cannot close it. The SDK says so on storage_file_open(). */
        storage_file_close(instance->file);
        FURI_LOG_D(TAG, "No station table at %s", path);
        return false;
    }

    instance->size = (uint32_t)storage_file_size(instance->file);
    if(flipso_stations_read_header(instance)) {
        FURI_LOG_I(TAG, "Station table: %lu entries from %s", instance->count, path);
        return true;
    }

    FURI_LOG_W(TAG, "Unusable station table at %s", path);
    storage_file_close(instance->file);
    return false;
}

FlipsoStations* flipso_stations_alloc(void) {
    FlipsoStations* instance = malloc(sizeof(FlipsoStations));
    memset(instance, 0, sizeof(FlipsoStations));

    instance->storage = furi_record_open(RECORD_STORAGE);
    instance->file = storage_file_alloc(instance->storage);

    instance->open = flipso_stations_try(instance, FLIPSO_STATIONS_USER_PATH) ||
                     flipso_stations_try(instance, FLIPSO_STATIONS_ASSET_PATH);

    return instance;
}

void flipso_stations_free(FlipsoStations* instance) {
    furi_assert(instance);
    storage_file_close(instance->file);
    storage_file_free(instance->file);
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

/** Copy the name an index entry points at into the instance buffer. */
static const char* flipso_stations_fetch(FlipsoStations* instance, const uint8_t* entry) {
    uint32_t offset = flipso_stations_le(entry + 2, 3);
    uint8_t length = entry[5];

    if(length == 0 || length > FLIPSO_STATION_NAME_MAX) return NULL;
    /* Written as subtractions rather than as names + offset + length: the
     * offset is 24 bits of whatever the file happened to contain, and the sum
     * of three of these can wrap a uint32 and pass a check it should fail.
     * The header check has already established names <= size. */
    if(offset > instance->size - instance->names) return NULL;
    if(length > instance->size - instance->names - offset) return NULL;
    if(!flipso_stations_read_at(instance, instance->names + offset, instance->name, length))
        return NULL;

    instance->name[length] = '\0';
    return instance->name;
}

const char* flipso_stations_name(FlipsoStations* instance, const char* nlc) {
    if(!flipso_stations_available(instance) || nlc == NULL) return NULL;

    uint16_t wanted;
    /* Published NLCs are four digits; anything else is not in the table. */
    if(!flipso_stations_key(nlc, &wanted)) return NULL;

    uint32_t low = 0;
    uint32_t high = instance->count;

    while(low < high) {
        uint32_t mid = low + (high - low) / 2;
        uint32_t at = FLIPSO_STATIONS_HEADER + mid * FLIPSO_STATIONS_ENTRY;
        if(!flipso_stations_read_at(instance, at, instance->entry, FLIPSO_STATIONS_ENTRY))
            return NULL;

        uint16_t code = (uint16_t)flipso_stations_le(instance->entry, 2);
        if(code == wanted) {
            return flipso_stations_fetch(instance, instance->entry);
        } else if(code < wanted) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    return NULL;
}
