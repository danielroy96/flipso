/*
 * Host-side test for the packed station table reader.
 *
 * Checks the reader against a table the builder wrote, that the user file wins
 * over the packaged asset, and that a damaged or unrelated file is refused
 * rather than searched. Built under ASan/UBSan, so an off-the-end read from a
 * corrupt header is a test failure rather than a subtle one on the device.
 */
#include "test.h"
#include "lookup/flipso_stations.h"

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
    struct File* file = calloc(1, sizeof(struct File));
    return file;
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

static const char* lookup(const char* nlc) {
    FlipsoStations* stations = flipso_stations_alloc();
    static char copy[FLIPSO_STATION_NAME_MAX + 1];
    const char* name = flipso_stations_name(stations, nlc);
    if(name) {
        snprintf(copy, sizeof(copy), "%s", name);
    }
    flipso_stations_free(stations);
    return name ? copy : NULL;
}

static bool opens(void) {
    FlipsoStations* stations = flipso_stations_alloc();
    bool ok = flipso_stations_available(stations);
    flipso_stations_free(stations);
    return ok;
}

/** Build a two-entry table by hand so the test does not need the network. */
static size_t make_table(uint8_t* out, const char* name_a, const char* name_b) {
    size_t a = strlen(name_a), b = strlen(name_b);
    memset(out, 0, 16);
    memcpy(out, "FSTN", 4);
    out[4] = 1; /* version */
    out[5] = (uint8_t)(a > b ? a : b);
    out[8] = 2; /* count, little-endian u32 */
    out[12] = 16 + 2 * 6; /* names offset */

    uint8_t* index = out + 16;
    /* 0035 */
    index[0] = 35;
    index[1] = 0;
    index[2] = 0;
    index[3] = 0;
    index[4] = 0;
    index[5] = (uint8_t)a;
    /* 5685 */
    index[6] = 5685 & 0xFF;
    index[7] = 5685 >> 8;
    index[8] = (uint8_t)a;
    index[9] = 0;
    index[10] = 0;
    index[11] = (uint8_t)b;

    memcpy(out + 28, name_a, a);
    memcpy(out + 28 + a, name_b, b);
    return 28 + a + b;
}

int main(void) {
    uint8_t table[256];
    size_t size;

    printf("Packaged asset\n");
    remove("stub_data_stations.dat");
    size = make_table(table, "London Zone R1256", "Woking");
    write_file("stub_assets_stations.dat", table, size);
    check("table opens", opens());
    check("first entry", lookup("0035") && strcmp(lookup("0035"), "London Zone R1256") == 0);
    check("last entry", lookup("5685") && strcmp(lookup("5685"), "Woking") == 0);
    check("absent code below", lookup("0001") == NULL);
    check("absent code between", lookup("1000") == NULL);
    check("absent code above", lookup("9999") == NULL);
    check("short code", lookup("123") == NULL);
    check("long code", lookup("12345") == NULL);
    check("non-numeric code", lookup("12A4") == NULL);
    check("null code", lookup(NULL) == NULL);

    printf("User file wins\n");
    size = make_table(table, "Mine", "Also mine");
    write_file("stub_data_stations.dat", table, size);
    check("user entry used", lookup("5685") && strcmp(lookup("5685"), "Also mine") == 0);

    printf("Damaged tables are refused\n");
    size = make_table(table, "London Zone R1256", "Woking");
    table[0] = 'X';
    write_file("stub_data_stations.dat", table, size);
    check(
        "bad magic falls through to the asset",
        lookup("5685") && strcmp(lookup("5685"), "Woking") == 0);

    size = make_table(table, "London Zone R1256", "Woking");
    table[4] = 99; /* future version */
    write_file("stub_data_stations.dat", table, size);
    check("unknown version refused", lookup("5685") && strcmp(lookup("5685"), "Woking") == 0);

    size = make_table(table, "London Zone R1256", "Woking");
    table[8] = 200; /* count far past the end of the file */
    write_file("stub_data_stations.dat", table, size);
    check("impossible count refused", lookup("5685") && strcmp(lookup("5685"), "Woking") == 0);

    size = make_table(table, "London Zone R1256", "Woking");
    write_file("stub_data_stations.dat", table, 20); /* truncated mid-index */
    check("truncated file refused", lookup("5685") && strcmp(lookup("5685"), "Woking") == 0);

    size = make_table(table, "London Zone R1256", "Woking");
    table[16 + 8] = 0xFF; /* name offset points past the blob */
    table[16 + 9] = 0xFF;
    table[16 + 10] = 0xFF;
    write_file("stub_data_stations.dat", table, size);
    check("out-of-range name offset refused", lookup("5685") == NULL);

    printf("Empty and missing\n");
    remove("stub_data_stations.dat");
    remove("stub_assets_stations.dat");
    check("no table at all", !opens());
    check("lookup without a table", lookup("5685") == NULL);

    /* Finally, the table we actually ship. */
    const char* shipped = "../../assets/stations.dat";
    size_t shipped_size;
    uint8_t* data = read_file(shipped, &shipped_size);
    write_file("stub_assets_stations.dat", data, shipped_size);
    free(data);
    printf("Shipped table (%s)\n", shipped);
    check("opens", opens());
    check("5685 is Woking", lookup("5685") && strcmp(lookup("5685"), "Woking") == 0);
    check("1444 is London Euston", lookup("1444") && strcmp(lookup("1444"), "London Euston") == 0);
    /* Fare groups are journey endpoints too, and come from the wider source. */
    check(
        "1072 is a fare group", lookup("1072") && strcmp(lookup("1072"), "London Stations") == 0);
    check(
        "0035 is a zone group",
        lookup("0035") && strcmp(lookup("0035"), "London Zone R1256") == 0);
    check("0000 is absent", lookup("0000") == NULL);

    /* Walk the index and look every code up, so the search is exercised over
     * the whole table rather than at a handful of probes. */
    data = read_file(shipped, &shipped_size);
    uint32_t count = (uint32_t)data[8] | ((uint32_t)data[9] << 8) | ((uint32_t)data[10] << 16) |
                     ((uint32_t)data[11] << 24);
    uint32_t names = (uint32_t)data[12] | ((uint32_t)data[13] << 8) | ((uint32_t)data[14] << 16) |
                     ((uint32_t)data[15] << 24);
    uint32_t wrong = 0, previous = 0;
    for(uint32_t i = 0; i < count; i++) {
        const uint8_t* entry = data + 16 + i * 6;
        uint32_t code = (uint32_t)entry[0] | ((uint32_t)entry[1] << 8);
        uint32_t at = (uint32_t)entry[2] | ((uint32_t)entry[3] << 8) | ((uint32_t)entry[4] << 16);
        uint8_t length = entry[5];

        if(i > 0 && code <= previous) wrong++; /* the search needs them sorted */
        previous = code;

        char nlc[5];
        snprintf(nlc, sizeof(nlc), "%04lu", (unsigned long)code);
        const char* got = lookup(nlc);
        if(!got || strlen(got) != length || memcmp(got, data + names + at, length) != 0) wrong++;
    }
    free(data);
    printf("      checked %lu codes\n", (unsigned long)count);
    check("every code resolves to its own name", wrong == 0);

    remove("stub_assets_stations.dat");
    printf("%s\n", failures ? "FAILED" : "All station table tests passed");
    return failures ? 1 : 0;
}
