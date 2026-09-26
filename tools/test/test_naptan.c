/*
 * Host-side test for the packed NaPTAN stop table reader.
 *
 * Checks both indexes against a table built by hand, that the two of them share
 * one blob of names, that the user file wins over the packaged asset, and that a
 * damaged or self-contradictory file is refused rather than searched. Built
 * under ASan/UBSan, so an off-the-end read from a corrupt header is a test
 * failure rather than a subtle one on the device.
 *
 * The fixtures are built here rather than by build_naptan.py because the
 * builder's smallest useful output is a whole ATCO area. A real table is checked
 * at the end if one has been built into this directory.
 */
#include "flipso_naptan.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- storage stubs ------------------------------------------------------ */

struct File {
    FILE* handle;
};

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
    file->handle = fopen(path, "rb");
    return file->handle != NULL;
}

void storage_file_close(struct File* file) {
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

static int failures = 0;

static void check(const char* what, int ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if(!ok) failures++;
}

static void write_file(const char* path, const void* data, size_t size) {
    FILE* out = fopen(path, "wb");
    if(!out) {
        perror(path);
        exit(1);
    }
    fwrite(data, 1, size, out);
    fclose(out);
}

static uint8_t* read_file(const char* path, size_t* size) {
    FILE* in = fopen(path, "rb");
    if(!in) return NULL;
    fseek(in, 0, SEEK_END);
    *size = (size_t)ftell(in);
    fseek(in, 0, SEEK_SET);
    uint8_t* data = malloc(*size);
    if(fread(data, 1, *size, in) != *size) exit(1);
    fclose(in);
    return data;
}

static void put32(uint8_t* at, uint32_t value) {
    at[0] = (uint8_t)value;
    at[1] = (uint8_t)(value >> 8);
    at[2] = (uint8_t)(value >> 16);
    at[3] = (uint8_t)(value >> 24);
}

static void put24(uint8_t* at, uint32_t value) {
    at[0] = (uint8_t)value;
    at[1] = (uint8_t)(value >> 8);
    at[2] = (uint8_t)(value >> 16);
}

/* Each lookup opens the table from scratch, as the app does once per run, so a
 * leak or a stale handle shows up as a failure rather than as a passing test. */
static const char* stop(const char* digits) {
    FlipsoNaptan* naptan = flipso_naptan_alloc();
    static char copy[FLIPSO_NAPTAN_NAME_MAX + 1];
    const char* name = flipso_naptan_stop(naptan, digits);
    if(name) snprintf(copy, sizeof(copy), "%s", name);
    flipso_naptan_free(naptan);
    return name ? copy : NULL;
}

static const char* atco(const char* code) {
    FlipsoNaptan* naptan = flipso_naptan_alloc();
    static char copy[FLIPSO_NAPTAN_NAME_MAX + 1];
    const char* name = flipso_naptan_atco(naptan, code);
    if(name) snprintf(copy, sizeof(copy), "%s", name);
    flipso_naptan_free(naptan);
    return name ? copy : NULL;
}

static bool opens(void) {
    FlipsoNaptan* naptan = flipso_naptan_alloc();
    bool ok = flipso_naptan_available(naptan);
    flipso_naptan_free(naptan);
    return ok;
}

static bool is(const char* got, const char* want) {
    return got && strcmp(got, want) == 0;
}

#define NAME_A "Ashton-under-Lyne Interchange"
#define NAME_B "Ivygreen Road (E-bound), Chorlton"

/*
 * Two stops, each reachable by either code, with both indexes pointing at one
 * copy of each name - which is the arrangement build_naptan.py produces and the
 * thing most likely to be got wrong by a reader that assumes one index.
 *
 * Stop codes are the folded digits of "MANAG" and "manwpwjm"; ATCO codes are
 * real Greater Manchester ones. Both indexes are in the ascending order the
 * search requires.
 */
static size_t make_table(uint8_t* out, uint32_t stops, uint32_t atcos) {
    size_t a = strlen(NAME_A), b = strlen(NAME_B);
    uint32_t atco_at = 24 + stops * 8;
    uint32_t names_at = atco_at + atcos * 16;

    memset(out, 0, names_at);
    memcpy(out, "FNPT", 4);
    out[4] = 1; /* version */
    out[5] = (uint8_t)(a > b ? a : b);
    put32(out + 8, stops);
    put32(out + 12, atcos);
    put32(out + 16, atco_at);
    put32(out + 20, names_at);

    if(stops >= 1) {
        put32(out + 24, 62624); /* MANAG, five characters, so a short code */
        put24(out + 28, 0);
        out[31] = (uint8_t)a;
    }
    if(stops >= 2) {
        put32(out + 32, 62697956); /* manwpwjm, the full eight */
        put24(out + 36, (uint32_t)a);
        out[39] = (uint8_t)b;
    }
    if(atcos >= 1) {
        memcpy(out + atco_at, "1800ALTRNHM0", 12);
        put24(out + atco_at + 12, 0); /* the same name as the first stop */
        out[atco_at + 15] = (uint8_t)a;
    }
    if(atcos >= 2) {
        memcpy(out + atco_at + 16, "1800EB00131", 11); /* eleven, so zero padded */
        put24(out + atco_at + 28, (uint32_t)a);
        out[atco_at + 31] = (uint8_t)b;
    }

    memcpy(out + names_at, NAME_A, a);
    memcpy(out + names_at + a, NAME_B, b);
    return names_at + a + b;
}

/** The full two-and-two fixture, as the packaged asset. */
static size_t fixture(uint8_t* out) {
    return make_table(out, 2, 2);
}

int main(void) {
    uint8_t table[512];
    size_t size;

    remove("stub_data_naptan.dat");
    remove("stub_assets_naptan.dat");

    printf("NaptanCode index\n");
    size = fixture(table);
    write_file("stub_assets_naptan.dat", table, size);
    check("table opens", opens());
    check("first entry", is(stop("00062624"), NAME_A));
    check("last entry", is(stop("62697956"), NAME_B));
    /* The card zero-pads a short code into the field, and the app parses the
     * field back to a number, so the two spellings have to agree. */
    check("leading zeros are insignificant", is(stop("62624"), NAME_A));
    check("absent code below", stop("00000001") == NULL);
    check("absent code between", stop("01000000") == NULL);
    check("absent code above", stop("99999999") == NULL);
    check("nine digits refused", stop("123456789") == NULL);
    check("non-numeric refused", stop("0006262F") == NULL);
    check("empty refused", stop("") == NULL);
    check("null refused", stop(NULL) == NULL);

    printf("AtcoCode index\n");
    check("first entry", is(atco("1800ALTRNHM0"), NAME_A));
    check("last entry, shorter than the field", is(atco("1800EB00131"), NAME_B));
    check("matching ignores case", is(atco("1800eb00131"), NAME_B));
    check("absent code below", atco("1800AAAAAAAA") == NULL);
    check("absent code between", atco("1800B") == NULL);
    check("absent code above", atco("9999ZZZZZZZZ") == NULL);
    /* A prefix must not match the longer code it is a prefix of: the index is
     * compared over the whole padded field, not up to the first difference. */
    check("prefix of a present code", atco("1800EB0013") == NULL);
    check("thirteen characters refused", atco("1800ALTRNHM00") == NULL);
    check("punctuation refused", atco("1800-ALTRNHM") == NULL);
    check("empty refused", atco("") == NULL);
    check("null refused", atco(NULL) == NULL);

    printf("The two indexes share one blob of names\n");
    check(
        "both codes for the first stop agree",
        is(stop("00062624"), NAME_A) && is(atco("1800ALTRNHM0"), NAME_A));
    check(
        "both codes for the second stop agree",
        is(stop("62697956"), NAME_B) && is(atco("1800EB00131"), NAME_B));

    printf("One index only\n");
    size = make_table(table, 2, 0);
    write_file("stub_data_naptan.dat", table, size);
    check("stops without ATCO codes opens", opens());
    check("stop still resolves", is(stop("00062624"), NAME_A));
    check("ATCO lookup finds nothing", atco("1800ALTRNHM0") == NULL);

    size = make_table(table, 0, 2);
    write_file("stub_data_naptan.dat", table, size);
    check("ATCO codes without stops opens", opens());
    check("ATCO still resolves", is(atco("1800EB00131"), NAME_B));
    check("stop lookup finds nothing", stop("00062624") == NULL);

    printf("User file wins\n");
    size = fixture(table);
    /* Rename the first stop, length byte and all, so the two files disagree
     * about something the test can see. The blob starts after both indexes. */
    memcpy(table + 24 + 2 * 8 + 2 * 16, "Mine", 4);
    table[31] = 4;
    write_file("stub_data_naptan.dat", table, size);
    check("user entry used", is(stop("00062624"), "Mine"));

    printf("Damaged tables are refused\n");
    /* Each of these falls through to the asset, which is the undamaged fixture,
     * so a table that is wrongly accepted shows up as the wrong name. */
    size = fixture(table);
    table[0] = 'X';
    write_file("stub_data_naptan.dat", table, size);
    check("bad magic", is(stop("00062624"), NAME_A));

    size = fixture(table);
    table[4] = 99;
    write_file("stub_data_naptan.dat", table, size);
    check("unknown version", is(stop("00062624"), NAME_A));

    size = fixture(table);
    table[5] = FLIPSO_NAPTAN_NAME_MAX + 1;
    write_file("stub_data_naptan.dat", table, size);
    check("name longer than the app's buffer", is(stop("00062624"), NAME_A));

    size = fixture(table);
    put32(table + 8, 0);
    put32(table + 12, 0);
    write_file("stub_data_naptan.dat", table, size);
    check("no entries at all", is(stop("00062624"), NAME_A));

    size = fixture(table);
    put32(table + 8, 1000); /* counts far past the end of the file */
    write_file("stub_data_naptan.dat", table, size);
    check("impossible stop count", is(stop("00062624"), NAME_A));

    size = fixture(table);
    put32(table + 12, 0xFFFFFFF0);
    write_file("stub_data_naptan.dat", table, size);
    check("ATCO count that would overflow the offset", is(stop("00062624"), NAME_A));

    size = fixture(table);
    put32(table + 16, 24); /* ATCO index overlapping the stop index */
    write_file("stub_data_naptan.dat", table, size);
    check("ATCO index disagreeing with the count", is(stop("00062624"), NAME_A));

    size = fixture(table);
    put32(table + 20, 24); /* names before the indexes that point into them */
    write_file("stub_data_naptan.dat", table, size);
    check("name blob disagreeing with the counts", is(stop("00062624"), NAME_A));

    size = fixture(table);
    write_file("stub_data_naptan.dat", table, 30); /* truncated mid-index */
    check("truncated file", is(stop("00062624"), NAME_A));

    printf("Damaged entries are refused\n");
    remove("stub_assets_naptan.dat"); /* nothing to fall through to now */
    size = fixture(table);
    put24(table + 28, 0xFFFFFF); /* name offset past the end of the blob */
    write_file("stub_data_naptan.dat", table, size);
    check("out-of-range name offset in the stop index", stop("00062624") == NULL);
    check("the other entry still reads", is(stop("62697956"), NAME_B));

    size = fixture(table);
    table[24 + 2 * 8 + 15] = 200; /* name length past the end of the blob */
    write_file("stub_data_naptan.dat", table, size);
    check("out-of-range name length in the ATCO index", atco("1800ALTRNHM0") == NULL);

    size = fixture(table);
    table[24 + 2 * 8 + 15] = 0; /* zero-length name */
    write_file("stub_data_naptan.dat", table, size);
    check("empty name", atco("1800ALTRNHM0") == NULL);

    printf("Empty and missing\n");
    remove("stub_data_naptan.dat");
    check("no table at all", !opens());
    check("stop lookup without a table", stop("00062624") == NULL);
    check("ATCO lookup without a table", atco("1800ALTRNHM0") == NULL);

    /*
     * Finally, the table we actually ship. It is far too large to look every
     * entry up under ASan on every run, so the walk strides through it with a
     * prime step - which still visits every part of both indexes and every
     * depth of the search - and the ends and a few known stops are checked
     * outright.
     */
    const char* shipped = "../../data/naptan.dat";
    size_t shipped_size;
    uint8_t* real = read_file(shipped, &shipped_size);
    if(!real) {
        printf("Shipped table: %s missing - run tools/naptan/build_naptan.py\n", shipped);
        failures++;
    } else {
        write_file("stub_assets_naptan.dat", real, shipped_size);
        printf("Shipped table (%s, %zu bytes)\n", shipped, shipped_size);
        check("opens", opens());

        /* "bstgwpa" folded onto the keypad of TS 1000-1 table 28, as a card
         * would carry it, and the AtcoCode for the same stop: the two indexes
         * have to arrive at one name. */
        check("a NaptanCode resolves", is(stop("02784972"), "Temple Gate (T3), Temple Meads"));
        check(
            "the same stop by its AtcoCode",
            is(atco("0100BRP90310"), "Temple Gate (T3), Temple Meads"));
        /* Around 26,000 stops have no NaptanCode at all, so the AtcoCode index
         * is the only way to reach them. */
        check(
            "an AtcoCode-only stop resolves",
            is(atco("1800ALTRNHM0"), "Altrincham Rail Station (NE Entrance)"));
        check("an absent NaptanCode", stop("00000001") == NULL);
        check("an absent AtcoCode", atco("9999ZZZZZZZZ") == NULL);

        uint32_t stops = (uint32_t)real[8] | ((uint32_t)real[9] << 8) |
                         ((uint32_t)real[10] << 16) | ((uint32_t)real[11] << 24);
        uint32_t atcos = (uint32_t)real[12] | ((uint32_t)real[13] << 8) |
                         ((uint32_t)real[14] << 16) | ((uint32_t)real[15] << 24);
        uint32_t atco_at = (uint32_t)real[16] | ((uint32_t)real[17] << 8) |
                           ((uint32_t)real[18] << 16) | ((uint32_t)real[19] << 24);
        uint32_t names = (uint32_t)real[20] | ((uint32_t)real[21] << 8) |
                         ((uint32_t)real[22] << 16) | ((uint32_t)real[23] << 24);

        /* One instance for the whole walk: reopening it per lookup would turn
         * seconds of work into minutes. */
        FlipsoNaptan* naptan = flipso_naptan_alloc();
        uint32_t sorted = 0, resolved = 0, visited = 0;
        uint32_t previous = 0;

        /* Sortedness is checked over every entry - it is what the binary search
         * depends on, and it costs one comparison rather than a lookup. */
        for(uint32_t i = 0; i < stops; i++) {
            const uint8_t* entry = real + 24 + i * 8;
            uint32_t code = (uint32_t)entry[0] | ((uint32_t)entry[1] << 8) |
                            ((uint32_t)entry[2] << 16) | ((uint32_t)entry[3] << 24);
            if(i > 0 && code <= previous) sorted++;
            previous = code;

            if(i % 97 != 0 && i != stops - 1) continue;
            uint32_t at = (uint32_t)entry[4] | ((uint32_t)entry[5] << 8) |
                          ((uint32_t)entry[6] << 16);
            uint8_t length = entry[7];
            char digits[9];
            snprintf(digits, sizeof(digits), "%08lu", (unsigned long)code);
            const char* got = flipso_naptan_stop(naptan, digits);
            visited++;
            if(!got || strlen(got) != length || memcmp(got, real + names + at, length) != 0)
                resolved++;
        }
        printf(
            "      %lu NaptanCodes, %lu looked up\n",
            (unsigned long)stops,
            (unsigned long)visited);
        check("NaptanCode index is sorted", sorted == 0);
        check("every NaptanCode visited resolves to its own name", resolved == 0);

        sorted = 0;
        resolved = 0;
        visited = 0;
        const uint8_t* last = NULL;
        for(uint32_t i = 0; i < atcos; i++) {
            const uint8_t* entry = real + atco_at + i * 16;
            if(last && memcmp(entry, last, 12) <= 0) sorted++;
            last = entry;

            if(i % 97 != 0 && i != atcos - 1) continue;
            uint32_t at = (uint32_t)entry[12] | ((uint32_t)entry[13] << 8) |
                          ((uint32_t)entry[14] << 16);
            uint8_t length = entry[15];
            char code[13];
            memcpy(code, entry, 12);
            code[12] = '\0';
            const char* got = flipso_naptan_atco(naptan, code);
            visited++;
            if(!got || strlen(got) != length || memcmp(got, real + names + at, length) != 0)
                resolved++;
        }
        printf(
            "      %lu AtcoCodes, %lu looked up\n", (unsigned long)atcos, (unsigned long)visited);
        check("AtcoCode index is sorted", sorted == 0);
        check("every AtcoCode visited resolves to its own name", resolved == 0);

        flipso_naptan_free(naptan);
        free(real);
        remove("stub_assets_naptan.dat");
    }

    printf("%s\n", failures ? "FAILED" : "All stop table tests passed");
    return failures ? 1 : 0;
}
