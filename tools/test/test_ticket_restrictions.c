/*
 * Host-side test for the packed ticket restriction table reader.
 *
 * Checks the reader against a table built by hand and against the one that
 * ships, that the user file wins over the packaged asset, that a damaged or
 * unrelated file is refused rather than searched, and that every open is
 * closed again, failed ones included - the reader opens the file for each
 * lookup. Built under ASan/UBSan.
 */
#include "test.h"
#include "lookup/flipso_ticket_restrictions.h"

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
static FlipsoTicketRestrictions* restrictions;
/* What the last lookup found: the three strings the screen would pass. */
static FuriString *found_name, *found_outward, *found_back;
static struct {
    const char* name;
    const char* outward;
    const char* back;
} found;

static bool lookup(const char* code) {
    bool hit = flipso_ticket_restrictions_find(
        restrictions, (const uint8_t*)code, found_name, found_outward, found_back);
    found.name = furi_string_get_cstr(found_name);
    found.outward = furi_string_get_cstr(found_outward);
    found.back = furi_string_get_cstr(found_back);
    return hit;
}

/**
 * A two-entry table built by hand: "AB", with a name and its own words each
 * way, then "OP", one text both entries of which point at, and no name.
 */
static size_t make_table(uint8_t* out, const char* name, const char* outward, const char* back) {
    size_t n = strlen(name), o = strlen(outward), b = strlen(back);
    memset(out, 0, 16 + 2 * 14); /* the header and the index, high offset bytes included */
    memcpy(out, "FRST", 4);
    out[4] = 1; /* version */
    out[5] = (uint8_t)(o > b ? (o > n ? o : n) : (b > n ? b : n));
    out[8] = 2; /* count, little-endian u32 */
    out[12] = 16 + 2 * 14; /* text offset */

    uint8_t* e = out + 16;
    memcpy(e, "AB", 2);
    e[2] = 0, e[5] = (uint8_t)n; /* name at 0 */
    e[6] = (uint8_t)n, e[9] = (uint8_t)o; /* outward after it */
    e[10] = (uint8_t)(n + o), e[13] = (uint8_t)b; /* return after that */
    e += 14;
    memcpy(e, "OP", 2);
    e[5] = 0; /* no name */
    e[6] = (uint8_t)(n + o), e[9] = (uint8_t)b; /* both ways the same text */
    e[10] = (uint8_t)(n + o), e[13] = (uint8_t)b;

    uint8_t* blob = out + 16 + 2 * 14;
    memcpy(blob, name, n);
    memcpy(blob + n, outward, o);
    memcpy(blob + n + o, back, b);
    return 16 + 2 * 14 + n + o + b;
}

static bool says(const char* code, const char* want_outward) {
    return lookup(code) && strcmp(found.outward, want_outward) == 0;
}

int main(void) {
    uint8_t table[512];
    size_t size;
    restrictions = flipso_ticket_restrictions_alloc();
    found_name = furi_string_alloc();
    found_outward = furi_string_alloc();
    found_back = furi_string_alloc();

    printf("Packaged asset\n");
    remove("stub_data_ticket_restrictions.dat");
    size = make_table(table, "Named", "Not before 09:30", "Any train");
    write_file("stub_assets_ticket_restrictions.dat", table, size);
    check("it counts its entries", flipso_ticket_restrictions_count(restrictions) == 2);
    check(
        "an entry's three texts",
        lookup("AB") && strcmp(found.name, "Named") == 0 &&
            strcmp(found.outward, "Not before 09:30") == 0 &&
            strcmp(found.back, "Any train") == 0);
    check(
        "a text both ways, and no name",
        lookup("OP") && found.name[0] == '\0' && strcmp(found.outward, "Any train") == 0 &&
            strcmp(found.back, "Any train") == 0);
    check("absent code below", !lookup("AA") && found.outward[0] == '\0');
    check("absent code between", !lookup("CD"));
    check("absent code above", !lookup("ZZ"));
    check("a blank code is in no table", !lookup("  "));
    check("nor one of zeros", !lookup("\0\0"));
    check(
        "a null code",
        !flipso_ticket_restrictions_find(
            restrictions, NULL, found_name, found_outward, found_back));

    printf("User file wins\n");
    size = make_table(table, "Mine", "My outward", "My return");
    write_file("stub_data_ticket_restrictions.dat", table, size);
    check("user entry used", says("AB", "My outward"));

    printf("Damaged tables are refused\n");
    size = make_table(table, "Named", "Not before 09:30", "Any train");
    table[0] = 'X';
    write_file("stub_data_ticket_restrictions.dat", table, size);
    check("bad magic falls through to the asset", says("AB", "Not before 09:30"));

    size = make_table(table, "Named", "Not before 09:30", "Any train");
    table[4] = 99; /* future version */
    write_file("stub_data_ticket_restrictions.dat", table, size);
    check("unknown version refused", says("AB", "Not before 09:30"));

    size = make_table(table, "Named", "Not before 09:30", "Any train");
    table[8] = 200; /* count far past the end of the file */
    write_file("stub_data_ticket_restrictions.dat", table, size);
    check("impossible count refused", says("AB", "Not before 09:30"));

    size = make_table(table, "Named", "Not before 09:30", "Any train");
    write_file("stub_data_ticket_restrictions.dat", table, 30); /* truncated mid-index */
    check("truncated file refused", says("AB", "Not before 09:30"));

    size = make_table(table, "Named", "Not before 09:30", "Any train");
    table[16 + 6] = 0xFF; /* the outward text points past the blob */
    table[16 + 7] = 0xFF;
    table[16 + 8] = 0xFF;
    write_file("stub_data_ticket_restrictions.dat", table, size);
    check(
        "an out-of-range text is left out, the others kept",
        lookup("AB") && found.outward[0] == '\0' && strcmp(found.name, "Named") == 0 &&
            strcmp(found.back, "Any train") == 0);

    printf("Empty and missing\n");
    remove("stub_data_ticket_restrictions.dat");
    remove("stub_assets_ticket_restrictions.dat");
    check("no table counts nothing", flipso_ticket_restrictions_count(restrictions) == 0);
    check("lookup without a table", !lookup("OP"));

    /* Finally, the table we actually ship. */
    const char* shipped = "../../assets/ticket_restrictions.dat";
    size_t shipped_size;
    uint8_t* data = read_file(shipped, &shipped_size);
    write_file("stub_assets_ticket_restrictions.dat", data, shipped_size);
    free(data);
    printf("Shipped table (%s)\n", shipped);
    check(
        "OP is the off-peak rule, the same both ways",
        lookup("OP") && found.name[0] == '\0' &&
            strcmp(found.outward, "Not valid on trains timed to depart before 09.30") == 0 &&
            strcmp(found.back, found.outward) == 0);
    check(
        "O1 has a name of its own",
        lookup("O1") && strcmp(found.name, "Chiltern Off-Peak (From 0930)") == 0);
    check(
        "a long restriction is cut, and says where the rest is",
        lookup("FL") && strstr(found.outward, " ... More at nationalrail.co.uk") != NULL &&
            strlen(found.outward) <= FLIPSO_TICKET_RESTRICTION_TEXT_MAX);
    check(
        "a text longer than one read comes through whole",
        lookup("FL") && strncmp(found.outward, "Customers can either travel as booked", 37) == 0);
    check(
        "a placeholder is found, with nothing to say",
        lookup("22") && !found.name[0] && !found.outward[0] && !found.back[0]);
    check("an unknown code is absent", !lookup("~~"));

    /* Walk the index and look every code up, so the search is exercised over
     * the whole table rather than at a handful of probes. */
    data = read_file(shipped, &shipped_size);
    uint32_t count = (uint32_t)data[8] | ((uint32_t)data[9] << 8) | ((uint32_t)data[10] << 16) |
                     ((uint32_t)data[11] << 24);
    check("it counts the shipped table", flipso_ticket_restrictions_count(restrictions) == count);
    uint32_t wrong = 0;
    for(uint32_t i = 0; i < count; i++) {
        const uint8_t* entry = data + 16 + i * 14;
        if(i > 0 && memcmp(entry - 14, entry, 2) >= 0) wrong++; /* the search needs them sorted */
        char code[3] = {(char)entry[0], (char)entry[1], '\0'};
        if(!lookup(code)) wrong++;
        if(strlen(found.outward) != entry[9] || strlen(found.back) != entry[13]) wrong++;
    }
    free(data);
    check("every shipped code is found, in order, its texts whole", wrong == 0);

    furi_string_free(found_name);
    furi_string_free(found_outward);
    furi_string_free(found_back);
    flipso_ticket_restrictions_free(restrictions);
    check("every open was closed, the failed ones too", opens_tried == closes_made);

    remove("stub_data_ticket_restrictions.dat");
    remove("stub_assets_ticket_restrictions.dat");
    printf("%s\n", failures ? "FAILED" : "All ticket restriction table tests passed");
    return failures ? 1 : 0;
}
