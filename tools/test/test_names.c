/*
 * Host-side test for names.dat and the device's reader of it.
 *
 * The device answers the long name tables from assets/names.dat, which
 * tools/names/ builds from the C tables in itso/names/ that every other host
 * test uses. This holds the reader to those tables for every code, so a screen
 * the host tests pass reads the same on the Flipper; and it checks the reader
 * opens the file only while names are wanted, closes it on every path, and
 * turns a damaged file away rather than reading off its end.
 *
 *     ./test_names assets/names.dat
 */
#include "test.h"
#include "lookup/flipso_names.h"

/* For the arrays they keep static, which list the keyed tables' entries. */
#include "../../itso/names/itso_name_tables.c"
#include "../../itso/names/itso_operators.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSET "stub_assets_names.dat"

/* ---- storage stubs ------------------------------------------------------ */

struct File {
    FILE* handle;
};

static int opens, closes;

void* furi_record_open(const char* name) {
    (void)name;
    return NULL;
}

void furi_record_close(const char* name) {
    (void)name;
}

typedef struct Storage Storage;

struct File* storage_file_alloc(Storage* storage) {
    (void)storage;
    return calloc(1, sizeof(struct File));
}

void storage_file_free(struct File* file) {
    free(file);
}

bool storage_file_open(struct File* file, const char* path, int access, int mode) {
    (void)access;
    (void)mode;
    opens++;
    file->handle = fopen(path, "rb");
    return file->handle != NULL;
}

void storage_file_close(struct File* file) {
    closes++;
    if(file->handle) fclose(file->handle);
    file->handle = NULL;
}

uint64_t storage_file_size(struct File* file) {
    long here = ftell(file->handle);
    fseek(file->handle, 0, SEEK_END);
    long size = ftell(file->handle);
    fseek(file->handle, here, SEEK_SET);
    return (uint64_t)size;
}

bool storage_file_seek(struct File* file, uint32_t offset, bool from_start) {
    (void)from_start;
    return fseek(file->handle, (long)offset, SEEK_SET) == 0;
}

uint16_t storage_file_read(struct File* file, void* buffer, uint16_t size) {
    return (uint16_t)fread(buffer, 1, size, file->handle);
}

/* ---- test --------------------------------------------------------------- */

static uint8_t* asset;
static size_t asset_size;

static void install(const uint8_t* data, size_t size) {
    FILE* out = fopen(ASSET, "wb");
    if(!out || fwrite(data, 1, size, out) != size || fclose(out) != 0) {
        perror(ASSET);
        exit(1);
    }
}

static bool same(const char* a, const char* b) {
    return (a == NULL && b == NULL) || (a && b && strcmp(a, b) == 0);
}

static bool byte_table_agrees(FlipsoNamesTable table, const char* (*function)(uint8_t)) {
    for(unsigned code = 0; code < 256; code++) {
        const char* want = function((uint8_t)code);
        const char* got = flipso_names_byte(table, (uint8_t)code);
        if(!same(want, got)) {
            printf(
                "      code %u: got %s, wanted %s\n",
                code,
                got ? got : "NULL",
                want ? want : "NULL");
            return false;
        }
    }
    return true;
}

static void test_tables(void) {
    printf("\nEvery name the C tables give\n");
#define AGREES(table, function, total) \
    check(#function " agrees for every code", byte_table_agrees(table, function));
    FLIPSO_NAMES_BYTE_TABLES(AGREES)
#undef AGREES

    bool railcards = true;
    for(size_t i = 0; i < sizeof(itso_railcards) / sizeof(itso_railcards[0]); i++) {
        uint32_t key;
        uint8_t card = 0xFF;
        itso_railcard_key((const uint8_t*)itso_railcards[i].code, 3, &key);
        const char* name = flipso_names_keyed(FlipsoNamesRailcard, key, &card);
        railcards &= same(name, itso_railcards[i].name) && card == itso_railcards[i].card;
    }
    check("every railcard, with its card flag", railcards);
    /* A code that is no railcard, between two that are. */
    uint32_t key;
    itso_railcard_key((const uint8_t*)"DIA", 3, &key);
    check(
        "a code the table lacks has no name", !flipso_names_keyed(FlipsoNamesRailcard, key, NULL));

    bool seats = true;
    for(size_t i = 0; i < sizeof(itso_seat_attributes) / sizeof(itso_seat_attributes[0]); i++) {
        itso_seat_attribute_key(itso_seat_attributes[i].code, &key);
        seats &=
            same(flipso_names_keyed(FlipsoNamesSeat, key, NULL), itso_seat_attributes[i].name);
    }
    check("every seat attribute", seats);

    bool operators = true;
    for(size_t i = 0; i < sizeof(itso_operator_table) / sizeof(itso_operator_table[0]); i++) {
        /* Each OID, and the numbers either side, which are mostly not operators. */
        for(int d = -1; d <= 1; d++) {
            uint16_t oid = (uint16_t)(itso_operator_table[i].oid + d);
            operators &=
                same(flipso_names_keyed(FlipsoNamesOperator, oid, NULL), itso_operator_name(oid));
            operators &=
                same(flipso_names_keyed(FlipsoNamesBrand, oid, NULL), itso_operator_brand(oid));
        }
    }
    check("every operator and brand, and the OIDs around them", operators);
    flipso_names_release();
}

static void test_lifetime(void) {
    printf("\nOpen only while wanted\n");
    opens = closes = 0;
    const char* names[FLIPSO_NAMES_SLOTS];
    for(int i = 0; i < FLIPSO_NAMES_SLOTS; i++) {
        names[i] = flipso_names_byte(FlipsoNamesProfile, (uint8_t)(i + 1));
    }
    bool intact = true;
    for(int i = 0; i < FLIPSO_NAMES_SLOTS; i++) {
        intact &= same(names[i], itso_profile_name((uint8_t)(i + 1)));
    }
    check("FLIPSO_NAMES_SLOTS names are good at once", intact);
    check("the file is opened once for many names", opens == 1 && closes == 0);
    flipso_names_release();
    check("a release closes it", closes == 1);
    flipso_names_release();
    check("a second release does nothing", closes == 1);
    check(
        "a lookup after a release opens it again",
        flipso_names_byte(FlipsoNamesPayment, 1) && opens == 2);
    flipso_names_release();
}

static void test_damage(void) {
    printf("\nA missing or damaged file\n");
    remove(ASSET);
    opens = closes = 0;
    check("a missing file gives no names", !flipso_names_byte(FlipsoNamesPayment, 1));
    check(
        "and is not tried again for the next",
        !flipso_names_byte(FlipsoNamesPayment, 2) && opens == 1);
    check("its failed open is closed", closes == 1);
    flipso_names_release();
    check("and closed only once", closes == 1);

    uint8_t* copy = malloc(asset_size);
    memcpy(copy, asset, asset_size);
    copy[0] = 'X';
    install(copy, asset_size);
    check(
        "a file that is not names.dat gives no names", !flipso_names_byte(FlipsoNamesPayment, 1));
    flipso_names_release();

    memcpy(copy, asset, asset_size);
    copy[4]++; /* another build's table count */
    install(copy, asset_size);
    check("another build's file gives no names", !flipso_names_byte(FlipsoNamesPayment, 1));
    flipso_names_release();

    /* Cut short at every length up to the end of the directory and a little
     * past: under ASan, a read past the end is a failure here. */
    bool refused = true;
    for(size_t cut = 0;
        cut < FLIPSO_NAMES_HEADER_LEN + FlipsoNamesTableCount * FLIPSO_NAMES_TABLE_LEN + 8;
        cut++) {
        install(asset, cut);
        refused &= !flipso_names_byte(FlipsoNamesPayment, 1);
        flipso_names_release();
    }
    check("a truncated file gives no names", refused);
    /* One byte short cuts the last name in the file. Every other answer is
     * still right, and that one is missing rather than misread. */
    install(asset, asset_size - 1);
    unsigned lost = 0, wrong = 0;
    for(size_t i = 0; i < sizeof(itso_operator_table) / sizeof(itso_operator_table[0]); i++) {
        uint16_t oid = itso_operator_table[i].oid;
        const char* pairs[2][2] = {
            {flipso_names_keyed(FlipsoNamesOperator, oid, NULL), itso_operator_name(oid)},
            {flipso_names_keyed(FlipsoNamesBrand, oid, NULL), itso_operator_brand(oid)},
        };
        for(int p = 0; p < 2; p++) {
            if(!pairs[p][0] && pairs[p][1]) {
                lost++;
            } else if(!same(pairs[p][0], pairs[p][1])) {
                wrong++;
            }
        }
        flipso_names_release();
    }
    for(unsigned code = 0; code < 256; code++) {
#define CUT(table, function, total)                                \
    {                                                              \
        const char* got = flipso_names_byte(table, (uint8_t)code); \
        const char* want = function((uint8_t)code);                \
        if(!got && want) {                                         \
            lost++;                                                \
        } else if(!same(got, want)) {                              \
            wrong++;                                               \
        }                                                          \
        flipso_names_release();                                    \
    }
        FLIPSO_NAMES_BYTE_TABLES(CUT)
#undef CUT
    }
    check("a file cut short loses its last name", lost > 0);
    check("and reads none of the others wrong", wrong == 0);
    free(copy);
    install(asset, asset_size);
}

int main(int argc, char** argv) {
    if(argc != 2) {
        fprintf(stderr, "usage: test_names assets/names.dat\n");
        return 2;
    }
    FILE* in = fopen(argv[1], "rb");
    if(!in) {
        perror(argv[1]);
        return 1;
    }
    fseek(in, 0, SEEK_END);
    asset_size = (size_t)ftell(in);
    fseek(in, 0, SEEK_SET);
    asset = malloc(asset_size);
    if(fread(asset, 1, asset_size, in) != asset_size) return 1;
    fclose(in);
    install(asset, asset_size);

    printf("Name tables\n");
    FlipsoNames* names = flipso_names_alloc();
    test_tables();
    test_lifetime();
    test_damage();
    flipso_names_free(names);
    check("no lookup without an instance", !flipso_names_byte(FlipsoNamesPayment, 1));

    remove(ASSET);
    free(asset);
    printf("\n%s\n", failures ? "FAILED" : "All name table tests passed");
    return failures ? 1 : 0;
}
