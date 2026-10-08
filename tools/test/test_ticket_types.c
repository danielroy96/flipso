/*
 * Host-side test for the packed ticket type table reader.
 *
 * Checks the reader against a table built by hand and against the one that
 * ships, that the user file wins over the packaged asset, that a damaged or
 * unrelated file is refused rather than searched, and - since the reader
 * opens the file for each lookup rather than holding it - that every open is
 * closed again, failed ones included. Built under ASan/UBSan.
 */
#include "test.h"
#include "lookup/flipso_ticket_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- storage stubs ------------------------------------------------------ */

struct File {
    FILE* handle;
};

/* Opens tried and closes made: the storage service needs a close for every
 * open, failed or not, or the path stays registered and the next open waits
 * for ever (CLAUDE.md, "Close a storage handle even when its open failed"). */
static int opens_tried, closes_made;

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
    opens_tried++;
    file->handle = fopen(path, "rb");
    return file->handle != NULL;
}

void storage_file_close(struct File* file) {
    closes_made++;
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
    if(!in) {
        perror(path);
        exit(1);
    }
    fseek(in, 0, SEEK_END);
    *size = (size_t)ftell(in);
    fseek(in, 0, SEEK_SET);
    uint8_t* data = malloc(*size);
    if(fread(data, 1, *size, in) != *size) exit(1);
    fclose(in);
    return data;
}

/* One instance for the whole run, as the app keeps one. */
static FlipsoTicketTypes* types;

static const char* lookup(const char* code) {
    return flipso_ticket_types_name(types, (const uint8_t*)code);
}

static bool named(const char* code, const char* want) {
    const char* got = lookup(code);
    return got && strcmp(got, want) == 0;
}

/** A two-entry table, "CDR" then "SOR", built by hand. */
static size_t make_table(uint8_t* out, const char* name_a, const char* name_b) {
    size_t a = strlen(name_a), b = strlen(name_b);
    memset(out, 0, 16);
    memcpy(out, "FTKT", 4);
    out[4] = 1; /* version */
    out[5] = (uint8_t)(a > b ? a : b);
    out[8] = 2; /* count, little-endian u32 */
    out[12] = 16 + 2 * 7; /* names offset */

    uint8_t* index = out + 16;
    memcpy(index, "CDR", 3);
    index[3] = 0, index[4] = 0, index[5] = 0;
    index[6] = (uint8_t)a;
    memcpy(index + 7, "SOR", 3);
    index[10] = (uint8_t)a, index[11] = 0, index[12] = 0;
    index[13] = (uint8_t)b;

    memcpy(out + 30, name_a, a);
    memcpy(out + 30 + a, name_b, b);
    return 30 + a + b;
}

int main(void) {
    uint8_t table[256];
    size_t size;
    types = flipso_ticket_types_alloc();

    printf("Packaged asset\n");
    remove("stub_data_ticket_types.dat");
    size = make_table(table, "Off-Peak Day Return", "Anytime Return");
    write_file("stub_assets_ticket_types.dat", table, size);
    check("it counts its entries", flipso_ticket_types_count(types) == 2);
    check("first entry", named("CDR", "Off-Peak Day Return"));
    check("last entry", named("SOR", "Anytime Return"));
    check("absent code below", lookup("AAA") == NULL);
    check("absent code between", lookup("GGG") == NULL);
    check("absent code above", lookup("ZZZ") == NULL);
    check("a blank code is in no table", lookup("   ") == NULL);
    check("nor one of zeros", lookup("\0\0\0") == NULL);
    check("a null code", lookup(NULL) == NULL);

    printf("User file wins\n");
    size = make_table(table, "Mine", "Also mine");
    write_file("stub_data_ticket_types.dat", table, size);
    check("user entry used", named("SOR", "Also mine"));

    printf("Damaged tables are refused\n");
    size = make_table(table, "Off-Peak Day Return", "Anytime Return");
    table[0] = 'X';
    write_file("stub_data_ticket_types.dat", table, size);
    check("bad magic falls through to the asset", named("SOR", "Anytime Return"));

    size = make_table(table, "Off-Peak Day Return", "Anytime Return");
    table[4] = 99; /* future version */
    write_file("stub_data_ticket_types.dat", table, size);
    check("unknown version refused", named("SOR", "Anytime Return"));

    size = make_table(table, "Off-Peak Day Return", "Anytime Return");
    table[8] = 200; /* count far past the end of the file */
    write_file("stub_data_ticket_types.dat", table, size);
    check("impossible count refused", named("SOR", "Anytime Return"));

    size = make_table(table, "Off-Peak Day Return", "Anytime Return");
    write_file("stub_data_ticket_types.dat", table, 20); /* truncated mid-index */
    check("truncated file refused", named("SOR", "Anytime Return"));

    size = make_table(table, "Off-Peak Day Return", "Anytime Return");
    table[16 + 10] = 0xFF; /* name offset points past the blob */
    table[16 + 11] = 0xFF;
    table[16 + 12] = 0xFF;
    write_file("stub_data_ticket_types.dat", table, size);
    check("out-of-range name offset refused", lookup("SOR") == NULL);

    printf("Empty and missing\n");
    remove("stub_data_ticket_types.dat");
    remove("stub_assets_ticket_types.dat");
    check("no table counts nothing", flipso_ticket_types_count(types) == 0);
    check("lookup without a table", lookup("SOR") == NULL);

    /* Finally, the table we actually ship. */
    const char* shipped = "../../assets/ticket_types.dat";
    size_t shipped_size;
    uint8_t* data = read_file(shipped, &shipped_size);
    write_file("stub_assets_ticket_types.dat", data, shipped_size);
    free(data);
    printf("Shipped table (%s)\n", shipped);
    check("SOR is an Anytime Return", named("SOR", "Anytime Return"));
    check("CDR is an Off-Peak Day Return", named("CDR", "Off-Peak Day Return"));
    check("7DS is a weekly season", named("7DS", "7 Day Season Ticket"));
    /* A code with no customer-facing name is named as it is printed. */
    check("2XR falls back to its printed name", named("2XR", "DUMMY DO NOT USE"));
    check("an unknown code is absent", lookup("~~~") == NULL);

    /* Walk the index and look every code up, so the search is exercised over
     * the whole table rather than at a handful of probes. */
    data = read_file(shipped, &shipped_size);
    uint32_t count = (uint32_t)data[8] | ((uint32_t)data[9] << 8) | ((uint32_t)data[10] << 16) |
                     ((uint32_t)data[11] << 24);
    uint32_t names = (uint32_t)data[12] | ((uint32_t)data[13] << 8) | ((uint32_t)data[14] << 16) |
                     ((uint32_t)data[15] << 24);
    check("it counts the shipped table", flipso_ticket_types_count(types) == count);
    uint32_t wrong = 0;
    for(uint32_t i = 0; i < count; i++) {
        const uint8_t* entry = data + 16 + i * 7;
        if(i > 0 && memcmp(entry - 7, entry, 3) >= 0) wrong++; /* the search needs them sorted */
        uint32_t at = (uint32_t)entry[3] | ((uint32_t)entry[4] << 8) | ((uint32_t)entry[5] << 16);
        uint8_t length = entry[6];
        char code[4] = {(char)entry[0], (char)entry[1], (char)entry[2], '\0'};
        const char* got = lookup(code);
        if(!got || strlen(got) != length || memcmp(got, data + names + at, length) != 0) wrong++;
    }
    free(data);
    check("every shipped code is found, in order", wrong == 0);

    flipso_ticket_types_free(types);
    check("every open was closed, the failed ones too", opens_tried == closes_made);

    remove("stub_data_ticket_types.dat");
    remove("stub_assets_ticket_types.dat");
    printf("%s\n", failures ? "FAILED" : "All ticket type table tests passed");
    return failures ? 1 : 0;
}
