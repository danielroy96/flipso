/*
 * Build assets/names.dat from the name tables in itso/names/.
 *
 *     tools/names/build_names.sh [OUT]
 *
 * Those tables are compiled only on the host: the device reads their names from
 * this file instead (lookup/flipso_names.h says why). So the file is never
 * edited by hand. Change the table in C and run this; tools/test/run.sh fails
 * while the packaged file differs from what this writes.
 *
 * The tables' sources are included rather than linked, for the arrays they
 * keep static - the railcards, seat attributes and operators are listed from
 * those, where the byte tables are listed by asking their function about every
 * code from 0 to 255.
 */
#include "../../itso/names/itso_name_tables.c"
#include "../../itso/names/itso_operators.c"
#include "../../lookup/flipso_names_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_STRINGS 512
#define MAX_ENTRIES 64

static const char* strings[MAX_STRINGS];
static uint16_t string_count;

typedef struct {
    uint32_t key;
    uint16_t string;
    uint8_t extra;
} Entry;

typedef struct {
    FlipsoNamesKind kind;
    uint16_t count;
    uint16_t codes[256]; /* a byte table's string ids */
    Entry entries[MAX_ENTRIES]; /* a keyed table's, sorted */
} Table;

static Table tables[FlipsoNamesTableCount];

static void fail(const char* what, const char* detail) {
    fprintf(stderr, "build_names: %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
    exit(1);
}

/* Every distinct name once: the file holds a name the tables share only once. */
static uint16_t intern(const char* name) {
    if(strlen(name) > FLIPSO_NAMES_MAX_LEN)
        fail("a name is too long for the device's buffer", name);
    for(uint16_t i = 0; i < string_count; i++) {
        if(strcmp(strings[i], name) == 0) return i;
    }
    if(string_count == MAX_STRINGS) fail("too many names", NULL);
    strings[string_count] = name;
    return string_count++;
}

static void byte_table(
    FlipsoNamesTable id,
    const char* (*function)(uint8_t),
    bool total,
    const char* label) {
    Table* table = &tables[id];
    table->kind = FlipsoNamesKindByte;
    table->count = 256;
    bool any_null = false;
    for(unsigned code = 0; code < 256; code++) {
        const char* name = function((uint8_t)code);
        any_null |= name == NULL;
        table->codes[code] = name ? intern(name) : FLIPSO_NAMES_NONE;
    }
    /* The device answers FLIPSO_NAMES_UNKNOWN for a total table it cannot read,
     * and NULL for the others, so the flag has to say what the function does. */
    if(total == any_null) fail("FLIPSO_NAMES_BYTE_TABLES has the wrong total for", label);
}

static void keyed(FlipsoNamesTable id, uint32_t key, const char* name, uint8_t extra) {
    Table* table = &tables[id];
    table->kind = FlipsoNamesKindKeyed;
    if(table->count == MAX_ENTRIES) fail("too many entries in a keyed table", name);
    Entry* entry = &table->entries[table->count++];
    entry->key = key;
    entry->string = intern(name);
    entry->extra = extra;
}

static int by_key(const void* a, const void* b) {
    uint32_t x = ((const Entry*)a)->key;
    uint32_t y = ((const Entry*)b)->key;
    return (x > y) - (x < y);
}

static void sort(FlipsoNamesTable id, const char* label) {
    Table* table = &tables[id];
    qsort(table->entries, table->count, sizeof(Entry), by_key);
    for(uint16_t i = 1; i < table->count; i++) {
        if(table->entries[i].key == table->entries[i - 1].key)
            fail("a key is listed twice in", label);
    }
}

static void put(FILE* out, uint32_t value, int bytes) {
    for(int i = 0; i < bytes; i++) {
        fputc((value >> (8 * i)) & 0xFF, out);
    }
}

int main(int argc, char** argv) {
    if(argc != 2) {
        fprintf(stderr, "usage: build_names OUT\n");
        return 2;
    }

#define BYTE_TABLE(table, function, total) byte_table(table, function, total, #function);
    FLIPSO_NAMES_BYTE_TABLES(BYTE_TABLE)
#undef BYTE_TABLE

    for(size_t i = 0; i < sizeof(itso_railcards) / sizeof(itso_railcards[0]); i++) {
        uint32_t key;
        if(!itso_railcard_key((const uint8_t*)itso_railcards[i].code, 3, &key))
            fail("a railcard code is not three characters", itso_railcards[i].code);
        keyed(FlipsoNamesRailcard, key, itso_railcards[i].name, itso_railcards[i].card);
    }
    for(size_t i = 0; i < sizeof(itso_seat_attributes) / sizeof(itso_seat_attributes[0]); i++) {
        uint32_t key;
        if(!itso_seat_attribute_key(itso_seat_attributes[i].code, &key))
            fail("a seat attribute is not four characters", itso_seat_attributes[i].code);
        keyed(FlipsoNamesSeat, key, itso_seat_attributes[i].name, 0);
    }
    for(size_t i = 0; i < sizeof(itso_operator_table) / sizeof(itso_operator_table[0]); i++) {
        const ItsoOperatorEntry* entry = &itso_operator_table[i];
        keyed(FlipsoNamesOperator, entry->oid, entry->name, 0);
        if(entry->brand) keyed(FlipsoNamesBrand, entry->oid, entry->brand, 0);
    }
    tables[FlipsoNamesRailcard].kind = FlipsoNamesKindKeyed;
    tables[FlipsoNamesSeat].kind = FlipsoNamesKindKeyed;
    tables[FlipsoNamesOperator].kind = FlipsoNamesKindKeyed;
    tables[FlipsoNamesBrand].kind = FlipsoNamesKindKeyed;
    sort(FlipsoNamesRailcard, "the railcards");
    sort(FlipsoNamesSeat, "the seat attributes");
    sort(FlipsoNamesOperator, "the operators");
    sort(FlipsoNamesBrand, "the brands");

    /* Where each part goes, in the order flipso_names_file.h lays them out. */
    uint32_t offset = FLIPSO_NAMES_HEADER_LEN + FlipsoNamesTableCount * FLIPSO_NAMES_TABLE_LEN;
    uint32_t table_offset[FlipsoNamesTableCount];
    for(int t = 0; t < FlipsoNamesTableCount; t++) {
        table_offset[t] = offset;
        offset += tables[t].kind == FlipsoNamesKindByte ? 256 * 2 :
                                                          tables[t].count * FLIPSO_NAMES_ENTRY_LEN;
    }
    uint32_t index = offset;
    uint32_t text = index + string_count * FLIPSO_NAMES_INDEX_LEN;

    FILE* out = fopen(argv[1], "wb");
    if(!out) fail("cannot write", argv[1]);

    fwrite(FLIPSO_NAMES_MAGIC, 1, 4, out);
    put(out, FlipsoNamesTableCount, 2);
    put(out, string_count, 2);
    put(out, index, 4);
    put(out, text, 4);
    for(int t = 0; t < FlipsoNamesTableCount; t++) {
        put(out, tables[t].kind, 1);
        put(out, 0, 1);
        put(out, tables[t].count, 2);
        put(out, table_offset[t], 4);
    }
    for(int t = 0; t < FlipsoNamesTableCount; t++) {
        for(uint16_t i = 0; i < tables[t].count; i++) {
            if(tables[t].kind == FlipsoNamesKindByte) {
                put(out, tables[t].codes[i], 2);
            } else {
                put(out, tables[t].entries[i].key, 4);
                put(out, tables[t].entries[i].string, 2);
                put(out, tables[t].entries[i].extra, 1);
                put(out, 0, 1);
            }
        }
    }
    uint32_t at = 0;
    for(uint16_t i = 0; i < string_count; i++) {
        size_t length = strlen(strings[i]);
        if(at > 0xFFFF) fail("the names are too long for a 16-bit offset", NULL);
        put(out, at, 2);
        put(out, (uint32_t)length, 1);
        put(out, 0, 1);
        at += (uint32_t)length;
    }
    for(uint16_t i = 0; i < string_count; i++) {
        fwrite(strings[i], 1, strlen(strings[i]), out);
    }
    if(fclose(out) != 0) fail("cannot write", argv[1]);

    printf("%s: %u names, %lu bytes\n", argv[1], string_count, (unsigned long)(text + at));
    return 0;
}
