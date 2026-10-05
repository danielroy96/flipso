/**
 * @file flipso_names.c
 * @brief Lookups in names.dat, the long name tables the .fap does not carry.
 *
 * The layout is in flipso_names_file.h, and the file is a build artefact of
 * tools/names/build_names.c, bundled as an asset like the station table. All of
 * the reading state - the open file, the directory and the buffers the names
 * are handed back in - lives in a session that the first lookup allocates and
 * flipso_names_release() frees, so between screens the tables cost nothing.
 */
#include "flipso_names.h"

#include <furi.h>
#include <storage/storage.h>
#include <string.h>

#define FLIPSO_NAMES_PATH APP_ASSETS_PATH("names.dat")

typedef struct {
    Storage* storage;
    File* file;
    /** The header and directory read and checked: lookups can go ahead. */
    bool ok;
    uint32_t size;
    uint16_t string_count;
    uint32_t index;
    uint32_t strings;
    uint8_t directory[FlipsoNamesTableCount][FLIPSO_NAMES_TABLE_LEN];
    char slots[FLIPSO_NAMES_SLOTS][FLIPSO_NAMES_MAX_LEN + 1];
    uint8_t next;
} FlipsoNamesSession;

struct FlipsoNames {
    FlipsoNamesSession* session;
};

/* The itso_ name functions have no context to carry one in. */
static FlipsoNames* flipso_names_instance;

static uint32_t flipso_names_le(const uint8_t* bytes, size_t n) {
    uint32_t value = 0;
    for(size_t i = 0; i < n; i++) {
        value |= (uint32_t)bytes[i] << (8 * i);
    }
    return value;
}

static bool
    flipso_names_read(FlipsoNamesSession* session, uint32_t offset, void* into, uint16_t length) {
    if(offset > session->size || length > session->size - offset) return false;
    if(!storage_file_seek(session->file, offset, true)) return false;
    return storage_file_read(session->file, into, length) == length;
}

/**
 * Check the header and directory against this build and the file's size: the
 * file is on a removable card, so a truncated or stale one has to fail cleanly
 * rather than send a lookup off the end.
 */
static bool flipso_names_read_header(FlipsoNamesSession* session) {
    uint8_t header[FLIPSO_NAMES_HEADER_LEN];
    if(!flipso_names_read(session, 0, header, sizeof(header))) return false;
    if(memcmp(header, FLIPSO_NAMES_MAGIC, 4) != 0) return false;
    if(flipso_names_le(header + 4, 2) != FlipsoNamesTableCount) return false;

    session->string_count = (uint16_t)flipso_names_le(header + 6, 2);
    session->index = flipso_names_le(header + 8, 4);
    session->strings = flipso_names_le(header + 12, 4);
    if(session->index > session->size) return false;
    if(session->string_count > (session->size - session->index) / FLIPSO_NAMES_INDEX_LEN)
        return false;
    if(session->strings > session->size) return false;

    return flipso_names_read(
        session, FLIPSO_NAMES_HEADER_LEN, session->directory, sizeof(session->directory));
}

/** The session the lookups read through, opening the file for the first of them. */
static FlipsoNamesSession* flipso_names_session(void) {
    FlipsoNames* instance = flipso_names_instance;
    if(!instance) return NULL;
    if(instance->session) return instance->session;

    FlipsoNamesSession* session = malloc(sizeof(FlipsoNamesSession));
    memset(session, 0, sizeof(FlipsoNamesSession));
    session->storage = furi_record_open(RECORD_STORAGE);
    session->file = storage_file_alloc(session->storage);
    if(storage_file_open(session->file, FLIPSO_NAMES_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        session->size = (uint32_t)storage_file_size(session->file);
        session->ok = flipso_names_read_header(session);
    }
    if(!session->ok) {
        /* Closed even after a failed open, which still registers the path:
         * see flipso_stations_try(). Lookups then answer NULL until the
         * release, rather than trying the file again for every name. */
        storage_file_close(session->file);
    }
    instance->session = session;
    return session;
}

FlipsoNames* flipso_names_alloc(void) {
    FlipsoNames* instance = malloc(sizeof(FlipsoNames));
    memset(instance, 0, sizeof(FlipsoNames));
    flipso_names_instance = instance;
    return instance;
}

void flipso_names_free(FlipsoNames* instance) {
    furi_assert(instance);
    flipso_names_release();
    if(flipso_names_instance == instance) flipso_names_instance = NULL;
    free(instance);
}

void flipso_names_release(void) {
    FlipsoNames* instance = flipso_names_instance;
    if(!instance || !instance->session) return;

    FlipsoNamesSession* session = instance->session;
    if(session->ok) storage_file_close(session->file);
    storage_file_free(session->file);
    furi_record_close(RECORD_STORAGE);
    free(session);
    instance->session = NULL;
}

/** String @p id, copied into the next buffer. */
static const char* flipso_names_string(FlipsoNamesSession* session, uint16_t id) {
    if(id >= session->string_count) return NULL;
    uint8_t entry[FLIPSO_NAMES_INDEX_LEN];
    if(!flipso_names_read(
           session, session->index + (uint32_t)id * FLIPSO_NAMES_INDEX_LEN, entry, sizeof(entry)))
        return NULL;
    uint32_t offset = flipso_names_le(entry, 2);
    uint8_t length = entry[2];
    if(length > FLIPSO_NAMES_MAX_LEN) return NULL;

    char* slot = session->slots[session->next];
    if(!flipso_names_read(session, session->strings + offset, slot, length)) return NULL;
    slot[length] = '\0';
    session->next = (session->next + 1) % FLIPSO_NAMES_SLOTS;
    return slot;
}

/** The directory entry for @p table, if the session can read it as @p kind. */
static const uint8_t*
    flipso_names_table(FlipsoNamesSession* session, FlipsoNamesTable table, FlipsoNamesKind kind) {
    if(!session || !session->ok || table >= FlipsoNamesTableCount) return NULL;
    const uint8_t* entry = session->directory[table];
    return entry[0] == kind ? entry : NULL;
}

const char* flipso_names_byte(FlipsoNamesTable table, uint8_t code) {
    FlipsoNamesSession* session = flipso_names_session();
    const uint8_t* entry = flipso_names_table(session, table, FlipsoNamesKindByte);
    if(!entry) return NULL;

    uint8_t id[2];
    if(!flipso_names_read(session, flipso_names_le(entry + 4, 4) + code * 2u, id, sizeof(id)))
        return NULL;
    uint16_t string = (uint16_t)flipso_names_le(id, 2);
    return string == FLIPSO_NAMES_NONE ? NULL : flipso_names_string(session, string);
}

const char* flipso_names_keyed(FlipsoNamesTable table, uint32_t key, uint8_t* extra) {
    FlipsoNamesSession* session = flipso_names_session();
    const uint8_t* entry = flipso_names_table(session, table, FlipsoNamesKindKeyed);
    if(!entry) return NULL;

    uint32_t base = flipso_names_le(entry + 4, 4);
    uint32_t low = 0;
    uint32_t high = flipso_names_le(entry + 2, 2);
    while(low < high) {
        uint32_t mid = low + (high - low) / 2;
        uint8_t candidate[FLIPSO_NAMES_ENTRY_LEN];
        if(!flipso_names_read(
               session, base + mid * FLIPSO_NAMES_ENTRY_LEN, candidate, sizeof(candidate)))
            return NULL;
        uint32_t found = flipso_names_le(candidate, 4);
        if(found == key) {
            if(extra) *extra = candidate[6];
            return flipso_names_string(session, (uint16_t)flipso_names_le(candidate + 4, 2));
        }
        if(found < key) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return NULL;
}
