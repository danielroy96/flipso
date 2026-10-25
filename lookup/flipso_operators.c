/**
 * @file flipso_operators.c
 * @brief Loads user-supplied operator names and card branding from the SD card.
 *
 * ITSO does not publish its OID register outside its membership, so the built-in
 * table can only ever be partial. Rather than force a rebuild to name a local bus
 * company, users can drop a text file on the card and have it picked up here.
 *
 * The file is searched where it lies rather than loaded: it can name hundreds
 * of operators, and every one held in RAM would come out of the heap a card
 * screen needs. A lookup reads it from the top until the OID turns up, and the
 * last few answers are kept - a "not in the file" as much as a name - because a
 * screen asks after the same handful of operators line after line.
 */
#include "flipso_operators.h"
#include "../itso/itso_operators.h"

#include <furi.h>
#include <storage/storage.h>
#include <toolbox/stream/file_stream.h>

#define TAG "Flipso"

#define FLIPSO_OPERATORS_PATH     APP_DATA_PATH("operators.txt")
/* Lines a lookup reads before it gives up, so a runaway file costs time, never
 * a hang. */
#define FLIPSO_OPERATORS_LINES    1024
#define FLIPSO_OPERATOR_NAME_LEN  48
/* How much of a brand fits is a question about pixels in the bold header font,
 * and the menu view already answers that by eliding what will not fit. */
#define FLIPSO_OPERATOR_BRAND_LEN 28
/* Answers kept between lookups: more than the distinct operators one card has. */
#define FLIPSO_OPERATORS_CACHE    8

typedef struct {
    uint16_t oid;
    char name[FLIPSO_OPERATOR_NAME_LEN];
    char brand[FLIPSO_OPERATOR_BRAND_LEN]; /**< Empty when the line gave none. */
} FlipsoOperatorEntry;

typedef struct {
    FlipsoOperatorEntry entry;
    bool used;
    bool found; /**< False when the file does not mention entry.oid. */
} FlipsoOperatorSlot;

typedef struct {
    FlipsoOperatorSlot slots[FLIPSO_OPERATORS_CACHE];
    uint8_t next; /**< The slot the next miss replaces. */
} FlipsoOperatorCache;

struct FlipsoOperators {
    Storage* storage;
    uint16_t count; /**< Lines naming an operator, counted when the app opens. */
    /* Its own allocation, so a lookup can fill it through the const handle the
     * screens are given. NULL when there is no file to search. */
    FlipsoOperatorCache* cache;
};

/** Copy one comma-terminated field, trimming the padding around it.
 *
 * Advances @p line past the field and its separator, and returns the number of
 * characters kept, so a caller can tell an empty field from a filled one.
 */
static size_t flipso_operators_field(const char** line, char* out, size_t out_len) {
    const char* p = *line;
    while(*p == ' ' || *p == '\t') {
        p++;
    }

    size_t i = 0;
    while(*p && *p != ',' && *p != '\r' && *p != '\n') {
        char c = *p++;
        if(i + 1 < out_len) out[i++] = c;
    }
    /* Trim trailing spaces so a stray column of padding does not shift the text. */
    while(i > 0 && out[i - 1] == ' ') {
        i--;
    }
    out[i] = '\0';

    /* Leave the caller on the next field, or on the terminator when this was
     * the last one, so a line with no brand column is not an error. */
    *line = (*p == ',') ? p + 1 : p;
    return i;
}

/**
 * Parse one `<oid>,<name>[,<brand>]` line.
 * Returns false for blanks, comments and junk.
 */
static bool flipso_operators_parse_line(const char* line, FlipsoOperatorEntry* entry) {
    while(*line == ' ' || *line == '\t') {
        line++;
    }
    if(*line == '\0' || *line == '#') return false;

    uint32_t oid = 0;
    if(*line < '0' || *line > '9') return false;
    while(*line >= '0' && *line <= '9') {
        oid = oid * 10 + (uint32_t)(*line++ - '0');
        if(oid > 0xFFFF) return false;
    }

    /* Tolerate padding before the separator as the fields themselves do: with
     * three columns to line up, a file written as a table is likely. */
    while(*line == ' ' || *line == '\t') {
        line++;
    }
    if(*line != ',') return false;
    line++;

    entry->oid = (uint16_t)oid;
    if(flipso_operators_field(&line, entry->name, FLIPSO_OPERATOR_NAME_LEN) == 0) return false;
    /* The brand column is optional: most operators do not brand a card of their
     * own, and the ones that do are the minority worth naming. */
    flipso_operators_field(&line, entry->brand, FLIPSO_OPERATOR_BRAND_LEN);
    return true;
}

/**
 * Read the file from the top. With @p oid NULL it counts the lines that name an
 * operator; otherwise it stops at the first naming @p oid, copies it into
 * @p match and returns 1. The first line for an OID wins, so a correction goes
 * above the line it corrects.
 */
static uint16_t
    flipso_operators_read(Storage* storage, const uint16_t* oid, FlipsoOperatorEntry* match) {
    uint16_t count = 0;
    Stream* stream = file_stream_alloc(storage);

    if(file_stream_open(stream, FLIPSO_OPERATORS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        FuriString* line = furi_string_alloc();
        for(uint16_t read = 0; read < FLIPSO_OPERATORS_LINES && stream_read_line(stream, line);
            read++) {
            FlipsoOperatorEntry entry = {0};
            if(!flipso_operators_parse_line(furi_string_get_cstr(line), &entry)) continue;
            if(!oid) {
                count++;
            } else if(entry.oid == *oid) {
                *match = entry;
                count = 1;
                break;
            }
        }
        furi_string_free(line);
    }

    /* Closed on the failed open too: the storage service registered the path
     * before trying it, and only a close takes it off again. */
    file_stream_close(stream);
    stream_free(stream);
    return count;
}

FlipsoOperators* flipso_operators_alloc(void) {
    FlipsoOperators* instance = malloc(sizeof(FlipsoOperators));
    memset(instance, 0, sizeof(FlipsoOperators));
    instance->storage = furi_record_open(RECORD_STORAGE);

    instance->count = flipso_operators_read(instance->storage, NULL, NULL);
    if(instance->count) {
        instance->cache = malloc(sizeof(FlipsoOperatorCache));
        memset(instance->cache, 0, sizeof(FlipsoOperatorCache));
        FURI_LOG_I(
            TAG, "Found %u operator entry(s) in %s", instance->count, FLIPSO_OPERATORS_PATH);
    }

    return instance;
}

void flipso_operators_free(FlipsoOperators* instance) {
    furi_assert(instance);
    if(instance->cache) free(instance->cache);
    furi_record_close(RECORD_STORAGE);
    free(instance);
}

uint16_t flipso_operators_user_count(const FlipsoOperators* instance) {
    return instance ? instance->count : 0;
}

/** The user's entry for this OID, or NULL when they did not name it. Valid until
 * the lookup that reuses its slot. */
static const FlipsoOperatorEntry*
    flipso_operators_entry(const FlipsoOperators* instance, uint16_t oid) {
    if(!instance || !instance->cache) return NULL;

    FlipsoOperatorCache* cache = instance->cache;
    for(uint8_t i = 0; i < FLIPSO_OPERATORS_CACHE; i++) {
        const FlipsoOperatorSlot* slot = &cache->slots[i];
        if(slot->used && slot->entry.oid == oid) return slot->found ? &slot->entry : NULL;
    }

    FlipsoOperatorSlot* slot = &cache->slots[cache->next];
    cache->next = (uint8_t)((cache->next + 1) % FLIPSO_OPERATORS_CACHE);
    memset(slot, 0, sizeof(*slot));
    slot->used = true;
    slot->found = flipso_operators_read(instance->storage, &oid, &slot->entry) > 0;
    slot->entry.oid = oid;
    return slot->found ? &slot->entry : NULL;
}

const char* flipso_operators_name(const FlipsoOperators* instance, uint16_t oid) {
    /* The user's file wins, so a local correction beats a stale built-in entry. */
    const FlipsoOperatorEntry* entry = flipso_operators_entry(instance, oid);
    if(entry) return entry->name;
    return itso_operator_name(oid);
}

const char* flipso_operators_brand(const FlipsoOperators* instance, uint16_t oid) {
    /* A line that names the operator but omits the brand overrides the name
     * only: the built-in brand is the more specific fact and there is no reason
     * to lose it because the user corrected the column next to it. */
    const FlipsoOperatorEntry* entry = flipso_operators_entry(instance, oid);
    if(entry && entry->brand[0]) return entry->brand;
    return itso_operator_brand(oid);
}
