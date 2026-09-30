/*
 * Every screen of a saved card, as the device would draw it, on this machine.
 *
 * Built and run by screens.py, which is the way to use it. The screens come
 * from the real flipso_format*.c, and the station and stop names from the real
 * tables through the real readers - screens.py links the packaged
 * stations.dat and data/naptan.dat in under the names the storage stub below
 * opens. So what this prints is what the Flipper shows, less the pixels, in a
 * second rather than a scroll through each screen on the device.
 *
 * Then it lists every operator number on the card, where it came from, whether
 * the table names it, and whether it is a number ITSO can issue at all: TS
 * 1000-2 table B2 leaves gaps that "shall not be used", so a value in one is a
 * field that is not holding an OID, however plainly it decodes as one.
 *
 *     screens <card.flipso> <now, unix seconds>
 */
#include "format/flipso_format.h"
#include "lookup/flipso_naptan.h"
#include "lookup/flipso_stations.h"
#include "itso/itso_operators.h"
#include "itso_i.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- storage stubs: device paths are files in the working directory ----- */

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

/* ---- operators: the built-in table, which is what a new entry changes --- */

const char* flipso_operators_name(const FlipsoOperators* instance, uint16_t oid) {
    (void)instance;
    return itso_operator_name(oid);
}

const char* flipso_operators_brand(const FlipsoOperators* instance, uint16_t oid) {
    (void)instance;
    return itso_operator_brand(oid);
}

/* ---- the operators on the card ------------------------------------------ */

/** What TS 1000-2 table B2 allows a number to be. */
static const char* oid_range(uint16_t oid) {
    if(oid == 0) return "none recorded";
    if(oid <= 8000) return "any role";
    if(oid <= 8191) return "ITSO special or reserved";
    if(oid <= 16383) return "product owner, operator or retailer";
    if(oid >= 24576 && oid <= 32767) return "operator or retailer only";
    if(oid >= 57344) return "operator or retailer only";
    return "NOT A VALID OID - table B2 gap";
}

static void oid_line(const char* role, uint16_t oid) {
    const char* name = itso_operator_name(oid);
    const char* brand = itso_operator_brand(oid);
    printf(
        "  %-40s %5u  %-28s %s%s%s\n",
        role,
        oid,
        name ? name : "(not in the table)",
        oid_range(oid),
        brand ? ", brands " : "",
        brand ? brand : "");
}

static void isam_line(const char* role, uint32_t isam) {
    if(!isam) return;
    char label[64];
    snprintf(label, sizeof(label), "%s %08lX", role, (unsigned long)isam);
    oid_line(label, itso_isam_oid(isam));
}

static void operators(const ItsoCard* card) {
    printf("==== Operators on this card ====\n");
    printf("  %-40s %5s  %-28s %s\n", "where", "OID", "name", "TS 1000-2 table B2");
    oid_line("shell owner", card->oid);
    if(itso_card_issuer_oid(card) != card->oid) {
        /* A compact shell's OID is generic; the product owner titles it. */
        oid_line("card issuer (titles the menu)", itso_card_issuer_oid(card));
    }
    isam_line("directory last written by", card->dir_isam);
    for(uint8_t i = 0; i < card->product_count; i++) {
        const ItsoProduct* p = &card->products[i];
        char role[64];
        snprintf(role, sizeof(role), "product %u owner (%s)", i + 1, flipso_product_title(p));
        oid_line(role, p->oid);
        if(p->has_retailer) {
            snprintf(role, sizeof(role), "product %u retailer", i + 1);
            oid_line(role, p->retailer);
        }
        snprintf(role, sizeof(role), "product %u created by", i + 1);
        isam_line(role, p->isam_id);
        snprintf(role, sizeof(role), "product %u value record by", i + 1);
        isam_line(role, p->value_isam);
    }
    for(uint8_t i = 0; i < card->tap_count; i++) {
        const ItsoTap* t = &card->taps[i];
        char role[64];
        snprintf(role, sizeof(role), "tap %u reader", i);
        isam_line(role, t->writer_isam);
        if(t->has_entry_oid) {
            snprintf(role, sizeof(role), "tap %u entered with", i);
            oid_line(role, t->entry_oid);
        }
        if(t->has_entry) {
            snprintf(role, sizeof(role), "tap %u tap-in reader", i);
            isam_line(role, t->entry_isam);
        }
    }
    printf(
        "\n  The menu title comes from the card issuer: %s\n",
        itso_operator_brand(itso_card_issuer_oid(card)) ?
            itso_operator_brand(itso_card_issuer_oid(card)) :
            "no brand, so \"ITSO card\"");
}

/* ---- main --------------------------------------------------------------- */

static void show(const char* title, FuriString* text) {
    printf("==== %s ====\n%s\n", title, furi_string_get_cstr(text));
    furi_string_reset(text);
}

int main(int argc, char** argv) {
    if(argc != 3) {
        fprintf(stderr, "usage: screens <card.flipso> <now, unix seconds>\n");
        return 2;
    }

    FlipsoCapture* capture = flipso_capture_alloc();
    FILE* file = fopen(argv[1], "r");
    if(!file) {
        perror(argv[1]);
        return 1;
    }
    static char line[FLIPSO_CAPTURE_LINE_MAX + 2];
    while(fgets(line, sizeof(line), file)) {
        if(!flipso_capture_parse_line(capture, line)) {
            fprintf(stderr, "%s: not a saved card this build can read: %s", argv[1], line);
            return 1;
        }
    }
    fclose(file);

    static ItsoCard card;
    if(!flipso_capture_valid(capture) || !flipso_capture_decode(capture, &card)) {
        fprintf(stderr, "%s: the blocks do not decode as an ITSO card\n", argv[1]);
        return 1;
    }

    FlipsoStations* stations = flipso_stations_alloc();
    FlipsoNaptan* naptan = flipso_naptan_alloc();
    FlipsoFormat f = {
        .stations = stations,
        .naptan = naptan,
        .capture = capture,
        .now = (uint32_t)strtoul(argv[2], NULL, 10),
    };

    static FlipsoMedia media;
    flipso_media_reset(&media);
    size_t chip_len = 0;
    const uint8_t* chip = flipso_capture_chip(capture, &chip_len);
    if(chip) {
        flipso_media_parse_chip(&media, chip, chip_len);
        f.media = &media;
    }

    const char* base = strrchr(argv[1], '/');
    base = base ? base + 1 : argv[1];
    char name[128];
    snprintf(name, sizeof(name), "%s", base);
    char* ext = strrchr(name, '.');
    if(ext) *ext = '\0';

    FuriString* text = furi_string_alloc();
    flipso_format_summary(text, &f, &card);
    show("Summary", text);
    flipso_format_card(text, &f, &card, name, false, 0);
    show("Card", text);
    flipso_format_id(text, &f, &card);
    show("ID & entitlement", text);
    flipso_format_payg(text, &f, &card);
    show("Pay as you go", text);
    flipso_format_taps(text, &f, &card);
    show("Journeys", text);
    for(uint8_t i = 0; i < card.product_count; i++) {
        char title[64];
        snprintf(
            title, sizeof(title), "Product %u: %s", i + 1, flipso_product_title(&card.products[i]));
        flipso_format_product(text, &f, &card, &card.products[i]);
        show(title, text);
    }
    furi_string_free(text);

    operators(&card);

    flipso_naptan_free(naptan);
    flipso_stations_free(stations);
    flipso_capture_free(capture);
    return 0;
}
