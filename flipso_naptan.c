/**
 * @file flipso_naptan.c
 * @brief Binary search over the packed NaPTAN stop table held on the SD card.
 *
 * The same seek-and-compare design as flipso_stations.c, and for the same
 * reason: the table stays on the card and is searched in place, so a lookup
 * costs a handful of short reads whatever the table holds. That matters more
 * here than it does for stations, because this table is two orders of
 * magnitude larger - NaPTAN lists around 390,000 active stops.
 *
 * It carries two indexes over one blob of names, because a card can name a stop
 * two ways. A NaptanCode arrives folded onto a telephone keypad and packed into
 * BCD (TS 1000-1 table 28), so that index is keyed on the folded number; an
 * AtcoCode arrives whole, so that one is keyed on the twelve characters. Both
 * point into the same names, since the two codes describe the same stop.
 *
 * The file is a build artefact of tools/naptan/build_naptan.py, shipped in the
 * project's data/ directory and copied to the SD card rather than packaged into
 * the .fap; data/README.md says why. The layout is documented in
 * tools/naptan/FORMAT.md.
 */
#include "flipso_naptan.h"

#include <furi.h>
#include <storage/storage.h>
#include <string.h>

#define TAG "Flipso"

/* Where the table actually lives. The asset path is checked second so that a
 * table small enough to package would still be found. */
#define FLIPSO_NAPTAN_USER_PATH  APP_DATA_PATH("naptan.dat")
#define FLIPSO_NAPTAN_ASSET_PATH APP_ASSETS_PATH("naptan.dat")

#define FLIPSO_NAPTAN_MAGIC       "FNPT"
#define FLIPSO_NAPTAN_VERSION     1
#define FLIPSO_NAPTAN_HEADER      24
#define FLIPSO_NAPTAN_STOP_ENTRY  8
#define FLIPSO_NAPTAN_ATCO_ENTRY  16

struct FlipsoNaptan {
    Storage* storage;
    File* file;
    bool open;
    uint32_t stops; /**< NaptanCode index entries, ascending by folded code. */
    uint32_t atcos; /**< AtcoCode index entries, ascending by memcmp order. */
    uint32_t atco_index; /**< Absolute offset of the AtcoCode index. */
    uint32_t names; /**< Absolute offset of the name blob. */
    uint32_t size; /**< File size, so a corrupt offset cannot read past the end. */
    /* Scratch for the entry being examined, and the name handed back. */
    uint8_t entry[FLIPSO_NAPTAN_ATCO_ENTRY];
    char name[FLIPSO_NAPTAN_NAME_MAX + 1];
};

static uint32_t flipso_naptan_le(const uint8_t* bytes, size_t n) {
    uint32_t value = 0;
    for(size_t i = 0; i < n; i++) {
        value |= (uint32_t)bytes[i] << (8 * i);
    }
    return value;
}

static bool flipso_naptan_read_at(FlipsoNaptan* instance, uint32_t offset, void* into,
                                  uint16_t length) {
    if(!storage_file_seek(instance->file, offset, true)) return false;
    return storage_file_read(instance->file, into, length) == length;
}

/**
 * Validate the header and record what the searches need from it.
 *
 * Both index offsets are recomputed from the counts rather than trusted, so a
 * file that disagrees with itself is refused here instead of sending a search
 * off the end of it. The table lives on a removable card.
 */
static bool flipso_naptan_read_header(FlipsoNaptan* instance) {
    uint8_t header[FLIPSO_NAPTAN_HEADER];
    if(!flipso_naptan_read_at(instance, 0, header, sizeof(header))) return false;

    if(memcmp(header, FLIPSO_NAPTAN_MAGIC, 4) != 0) return false;
    if(header[4] != FLIPSO_NAPTAN_VERSION) return false;
    if(header[5] > FLIPSO_NAPTAN_NAME_MAX) return false;

    instance->stops = flipso_naptan_le(header + 8, 4);
    instance->atcos = flipso_naptan_le(header + 12, 4);
    instance->atco_index = flipso_naptan_le(header + 16, 4);
    instance->names = flipso_naptan_le(header + 20, 4);

    if(instance->stops == 0 && instance->atcos == 0) return false;
    /* Guard the multiplications below before performing them. */
    if(instance->stops > UINT32_MAX / FLIPSO_NAPTAN_STOP_ENTRY) return false;
    if(instance->atcos > UINT32_MAX / FLIPSO_NAPTAN_ATCO_ENTRY) return false;

    uint32_t stop_bytes = instance->stops * FLIPSO_NAPTAN_STOP_ENTRY;
    uint32_t atco_bytes = instance->atcos * FLIPSO_NAPTAN_ATCO_ENTRY;
    if(stop_bytes > UINT32_MAX - FLIPSO_NAPTAN_HEADER) return false;
    if(instance->atco_index != FLIPSO_NAPTAN_HEADER + stop_bytes) return false;
    if(atco_bytes > UINT32_MAX - instance->atco_index) return false;
    if(instance->names != instance->atco_index + atco_bytes) return false;
    return instance->names <= instance->size;
}

/** Open @p path if it holds a table we understand. */
static bool flipso_naptan_try(FlipsoNaptan* instance, const char* path) {
    if(!storage_file_open(instance->file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        /* A failed open still has to be closed; see flipso_stations_try(). */
        storage_file_close(instance->file);
        FURI_LOG_D(TAG, "No stop table at %s", path);
        return false;
    }

    instance->size = (uint32_t)storage_file_size(instance->file);
    if(flipso_naptan_read_header(instance)) {
        FURI_LOG_I(
            TAG, "Stop table: %lu NaptanCodes, %lu AtcoCodes from %s",
            instance->stops, instance->atcos, path);
        return true;
    }

    FURI_LOG_W(TAG, "Unusable stop table at %s", path);
    storage_file_close(instance->file);
    return false;
}

FlipsoNaptan* flipso_naptan_alloc(void) {
    FlipsoNaptan* instance = malloc(sizeof(FlipsoNaptan));
    memset(instance, 0, sizeof(FlipsoNaptan));

    instance->storage = furi_record_open(RECORD_STORAGE);
    instance->file = storage_file_alloc(instance->storage);

    instance->open = flipso_naptan_try(instance, FLIPSO_NAPTAN_USER_PATH) ||
                     flipso_naptan_try(instance, FLIPSO_NAPTAN_ASSET_PATH);

    return instance;
}

void flipso_naptan_free(FlipsoNaptan* instance) {
    furi_assert(instance);
    storage_file_close(instance->file);
    storage_file_free(instance->file);
    furi_record_close(RECORD_STORAGE);
    free(instance);
}

bool flipso_naptan_available(const FlipsoNaptan* instance) {
    return instance && instance->open;
}

uint32_t flipso_naptan_count(const FlipsoNaptan* instance) {
    return flipso_naptan_available(instance) ? instance->stops : 0;
}

/**
 * Copy the name an index entry points at into the instance buffer.
 * @param at  offset of the name pointer within the entry.
 */
static const char* flipso_naptan_fetch(FlipsoNaptan* instance, const uint8_t* entry, size_t at) {
    uint32_t offset = flipso_naptan_le(entry + at, 3);
    uint8_t length = entry[at + 3];

    if(length == 0 || length > FLIPSO_NAPTAN_NAME_MAX) return NULL;
    /* Written as subtractions rather than as names + offset + length: the
     * offset is 24 bits of whatever the file happened to contain, and the sum
     * of three of these can wrap a uint32 and pass a check it should fail.
     * The header check has already established names <= size. */
    if(offset > instance->size - instance->names) return NULL;
    if(length > instance->size - instance->names - offset) return NULL;
    if(!flipso_naptan_read_at(instance, instance->names + offset, instance->name, length))
        return NULL;

    instance->name[length] = '\0';
    return instance->name;
}

/**
 * Parse the digits an ITSO card stores into the number the stop index is keyed
 * on. Eight is the most four bytes of BCD can hold (TS 1000-1 clause 4.2.4.3.4)
 * and is also what keeps the value inside a uint32, so anything longer is not a
 * code that could have come off a card.
 */
static bool flipso_naptan_key(const char* digits, uint32_t* out) {
    uint32_t value = 0;
    size_t i = 0;
    for(; digits[i] != '\0'; i++) {
        if(i >= 8) return false;
        if(digits[i] < '0' || digits[i] > '9') return false;
        value = value * 10 + (uint32_t)(digits[i] - '0');
    }
    if(i == 0) return false;
    *out = value;
    return true;
}

const char* flipso_naptan_stop(FlipsoNaptan* instance, const char* digits) {
    if(!flipso_naptan_available(instance) || digits == NULL) return NULL;
    if(instance->stops == 0) return NULL;

    uint32_t wanted;
    if(!flipso_naptan_key(digits, &wanted)) return NULL;

    uint32_t low = 0;
    uint32_t high = instance->stops;

    while(low < high) {
        uint32_t mid = low + (high - low) / 2;
        uint32_t at = FLIPSO_NAPTAN_HEADER + mid * FLIPSO_NAPTAN_STOP_ENTRY;
        if(!flipso_naptan_read_at(instance, at, instance->entry, FLIPSO_NAPTAN_STOP_ENTRY))
            return NULL;

        uint32_t code = flipso_naptan_le(instance->entry, 4);
        if(code == wanted) {
            return flipso_naptan_fetch(instance, instance->entry, 4);
        } else if(code < wanted) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    return NULL;
}

/**
 * Normalise an AtcoCode into the fixed-width, zero-padded, upper case form the
 * index is sorted in, so the search is a plain memcmp of equal-length keys.
 */
static bool flipso_naptan_atco_key(const char* atco, uint8_t* out) {
    memset(out, 0, FLIPSO_NAPTAN_ATCO_MAX);
    size_t i = 0;
    for(; atco[i] != '\0'; i++) {
        if(i >= FLIPSO_NAPTAN_ATCO_MAX) return false;
        char c = atco[i];
        if(c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        /* The register uses digits and upper case letters only; anything else
         * is not a code it holds, so there is nothing to find. */
        if(!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'))) return false;
        out[i] = (uint8_t)c;
    }
    return i > 0;
}

const char* flipso_naptan_atco(FlipsoNaptan* instance, const char* atco) {
    if(!flipso_naptan_available(instance) || atco == NULL) return NULL;
    if(instance->atcos == 0) return NULL;

    uint8_t wanted[FLIPSO_NAPTAN_ATCO_MAX];
    if(!flipso_naptan_atco_key(atco, wanted)) return NULL;

    uint32_t low = 0;
    uint32_t high = instance->atcos;

    while(low < high) {
        uint32_t mid = low + (high - low) / 2;
        uint32_t at = instance->atco_index + mid * FLIPSO_NAPTAN_ATCO_ENTRY;
        if(!flipso_naptan_read_at(instance, at, instance->entry, FLIPSO_NAPTAN_ATCO_ENTRY))
            return NULL;

        int order = memcmp(instance->entry, wanted, FLIPSO_NAPTAN_ATCO_MAX);
        if(order == 0) {
            return flipso_naptan_fetch(instance, instance->entry, FLIPSO_NAPTAN_ATCO_MAX);
        } else if(order < 0) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    return NULL;
}
