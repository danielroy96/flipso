/**
 * @file flipso_operators.c
 * @brief Loads user-supplied operator names and card branding from the SD card.
 *
 * ITSO does not publish its OID register outside its membership, so the built-in
 * table can only ever be partial. Rather than force a rebuild to name a local bus
 * company, users can drop a text file on the card and have it picked up here.
 */
#include "flipso_operators.h"
#include "itso/itso_operators.h"

#include <furi.h>
#include <storage/storage.h>
#include <toolbox/stream/file_stream.h>

#define TAG "Flipso"

#define FLIPSO_OPERATORS_PATH    APP_DATA_PATH("operators.txt")
/* Bounded so a malformed or hostile file cannot exhaust the heap. */
#define FLIPSO_OPERATORS_MAX     48
#define FLIPSO_OPERATOR_NAME_LEN 28
/* Same length as a name: how much of a brand actually fits is a question about
 * pixels in the bold header font, and the menu view already answers that by
 * eliding what will not fit. */
#define FLIPSO_OPERATOR_BRAND_LEN FLIPSO_OPERATOR_NAME_LEN

typedef struct {
    uint16_t oid;
    char name[FLIPSO_OPERATOR_NAME_LEN];
    char brand[FLIPSO_OPERATOR_BRAND_LEN]; /**< Empty when the line gave none. */
} FlipsoOperatorEntry;

struct FlipsoOperators {
    FlipsoOperatorEntry* entries;
    uint16_t count;
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

FlipsoOperators* flipso_operators_alloc(void) {
    FlipsoOperators* instance = malloc(sizeof(FlipsoOperators));
    memset(instance, 0, sizeof(FlipsoOperators));

    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = file_stream_alloc(storage);

    if(file_stream_open(stream, FLIPSO_OPERATORS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        instance->entries = malloc(sizeof(FlipsoOperatorEntry) * FLIPSO_OPERATORS_MAX);

        FuriString* line = furi_string_alloc();
        while(instance->count < FLIPSO_OPERATORS_MAX && stream_read_line(stream, line)) {
            FlipsoOperatorEntry entry = {0};
            if(flipso_operators_parse_line(furi_string_get_cstr(line), &entry)) {
                instance->entries[instance->count++] = entry;
            }
        }
        furi_string_free(line);

        FURI_LOG_I(TAG, "Loaded %u operator entry(s) from %s", instance->count,
                   FLIPSO_OPERATORS_PATH);

        if(instance->count == 0) {
            free(instance->entries);
            instance->entries = NULL;
        } else if(instance->count < FLIPSO_OPERATORS_MAX) {
            /* Give back what the file did not fill. A typical file names two or
             * three operators, so the slab above is almost all slack, and this
             * runs once on a device with 256 KB of RAM. */
            FlipsoOperatorEntry* trimmed =
                realloc(instance->entries, sizeof(FlipsoOperatorEntry) * instance->count);
            if(trimmed) instance->entries = trimmed;
        }
    }

    file_stream_close(stream);
    stream_free(stream);
    furi_record_close(RECORD_STORAGE);

    return instance;
}

void flipso_operators_free(FlipsoOperators* instance) {
    furi_assert(instance);
    if(instance->entries) free(instance->entries);
    free(instance);
}

uint16_t flipso_operators_user_count(const FlipsoOperators* instance) {
    return instance ? instance->count : 0;
}

/** The user's entry for this OID, or NULL when they did not name it. */
static const FlipsoOperatorEntry*
    flipso_operators_entry(const FlipsoOperators* instance, uint16_t oid) {
    if(instance && instance->entries) {
        for(uint16_t i = 0; i < instance->count; i++) {
            if(instance->entries[i].oid == oid) return &instance->entries[i];
        }
    }
    return NULL;
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
