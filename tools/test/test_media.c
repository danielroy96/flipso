/*
 * Host-side test for the card media description.
 *
 * The first case is a real Oyster - an EV1 whose GetVersion, free memory and
 * application list were captured with libfreefare's mifare-desfire-info, and
 * whose eight files are all behind TfL's keys. The capture is at
 * https://gist.github.com/ryanamaral/8ca0d7b80c3442ee46c7f94ad1ed30bd, and it
 * is also where the application identifier in flipso_desfire_media.c comes from. That is the card the screen exists for, so it is the
 * card the rendering is pinned to: every number on screen has to be the one the
 * card reported, and a locked file has to say so rather than look empty.
 *
 * The second case is the opposite card: one that will not list its applications
 * and does hand over file contents, which exercises the branches the Oyster
 * never reaches.
 */
#include "test.h"
#include "format/flipso_format.h"
#include "reader/flipso_media.h"

#include <stdio.h>

/* The screen builders link against the lookup tables; a DESFire's description
 * asks none of them anything, so they answer nothing. */
const char* flipso_operators_name(const FlipsoOperators* instance, uint16_t oid) {
    (void)instance;
    (void)oid;
    return NULL;
}

const char* flipso_stations_name(FlipsoStations* instance, const char* nlc) {
    (void)instance;
    (void)nlc;
    return NULL;
}

const char* flipso_naptan_stop(FlipsoNaptan* instance, const char* digits) {
    (void)instance;
    (void)digits;
    return NULL;
}

const char* flipso_naptan_atco(FlipsoNaptan* instance, const char* atco) {
    (void)instance;
    (void)atco;
    return NULL;
}

static void shows(const FuriString* text, const char* needle) {
    check(needle, strstr(furi_string_get_cstr(text), needle) != NULL);
}

static void hides(const FuriString* text, const char* needle) {
    printf(
        "  [%s] no \"%s\"\n",
        strstr(furi_string_get_cstr(text), needle) ? "FAIL" : "PASS",
        needle);
    if(strstr(furi_string_get_cstr(text), needle)) failures++;
}

static int occurrences(const FuriString* text, const char* needle) {
    int count = 0;
    for(const char* p = furi_string_get_cstr(text); (p = strstr(p, needle)); p++)
        count++;
    return count;
}

static void dump(const char* title, const FuriString* text) {
    printf("\n  %s\n  ----------------------------------------\n", title);
    const char* p = furi_string_get_cstr(text);
    printf("  ");
    for(; *p; p++) {
        if(*p == '\n') {
            printf("\n  ");
        } else if(*p == '\e') {
            /* The scroll element's header marker; show it rather than emit it. */
            printf("<");
        } else {
            putchar(*p);
        }
    }
    printf("\n  ----------------------------------------\n");
}

/* 048B1FF1AD2680, made in week 42 of 2008: a 2K EV1 Oyster. */
static void build_oyster(FlipsoMedia* media) {
    flipso_media_reset(media);

    media->valid = true;
    media->chip = flipso_media_chip_from_hw(0x01, 0x01);
    media->hw_vendor = 0x04;
    media->hw_type = 0x01;
    media->hw_subtype = 0x01;
    media->hw_major = 0x01;
    media->hw_minor = 0x00;
    media->hw_storage = 0x16;
    media->hw_proto = 0x05;
    media->sw_major = 0x01;
    media->sw_minor = 0x03;
    media->sw_storage = 0x16;
    media->sw_proto = 0x05;
    memcpy(media->uid, "\x04\x8B\x1F\xF1\xAD\x26\x80", 7);
    media->prod_week = 0x42;
    media->prod_year = 0x08;
    media->free_memory_valid = true;
    media->free_memory = 1248;

    media->app_list_valid = true;
    flipso_media_add_app(media, FLIPSO_AID_OYSTER);

    FlipsoMediaApp* app = flipso_media_open_app(media, FLIPSO_AID_OYSTER);
    app->file_count = 8;
    for(uint8_t i = 0; i < 8; i++) {
        FlipsoMediaFile* file = &app->files[i];
        file->id = i;
        file->settings_valid = true;
        file->type = 0; /* Standard. */
        file->comm = 3; /* Enciphered. */
        /* Read, write, read-write and change all behind key 1. */
        file->access = 0x1111;
        file->data.size = 8 + i;
    }
}

int main(void) {
    printf("Card media\n");

    FlipsoMedia media = {0};
    FuriString* text = furi_string_alloc();

    /* Nothing scanned yet: the screen must still say something. */
    flipso_media_reset(&media);
    flipso_format_media(text, &media);
    check("an unread card renders", furi_string_size(text) > 0);

    build_oyster(&media);
    furi_string_reset(text);
    flipso_format_media(text, &media);
    dump("Oyster", text);

    shows(text, "MIFARE DESFire EV1");
    shows(text, "Storage: 2048 bytes");
    shows(text, "Free space: 1248 bytes");
    shows(text, "UID: 048B1FF1AD2680");
    shows(text, "Made: Week 42 of 2008");
    shows(text, "4F5931: Oyster");
    shows(text, "Files read from: Oyster\n");
    /* A page to each file, titled with its number. */
    shows(text, "File 0\nType: Standard");
    shows(text, "Size: 8 bytes");
    shows(text, "\nEncryption: Encrypted\nAccess rights: 1111\n  Read: Key 1");
    shows(text, "Read: Key 1");
    shows(text, "Contents: Locked");
    shows(text, "File 7\n");
    /* Nothing came off this card, so nothing may be shown as having done. */
    check("every file says it is locked", occurrences(text, "\nContents: Locked\n") == 8);
    hides(text, "Could not be read");

    /* A card that keeps its directory to itself, and files that are not locked. */
    flipso_media_reset(&media);
    media.valid = true;
    media.chip = flipso_media_chip_from_hw(0x01, 0x33);
    media.hw_type = 0x01;
    media.hw_major = 0x33;
    media.hw_storage = 0x1A; /* 8K, and the odd bit says "up to". */
    media.hw_storage |= 1;
    flipso_media_add_app(&media, 0xABCDEFu);
    FlipsoMediaApp* app = flipso_media_open_app(&media, FLIPSO_AID_OYSTER);
    app->file_count = 1;
    check(
        "opening another application starts its files afresh",
        flipso_media_open_app(&media, 0xABCDEFu) == app && app->file_count == 0 &&
            app->aid == 0xABCDEFu);
    app->file_count = 4;

    app->files[0].id = 1;
    app->files[0].settings_valid = true;
    app->files[0].type = 0;
    app->files[0].comm = 0;
    app->files[0].access = 0xEEEE; /* Free to everyone. */
    app->files[0].data.size = 4;
    app->files[0].data_offset = 0;
    app->files[0].data_len = 4;
    memcpy(app->data, "\xDE\xAD\xBE\xEF", 4);
    app->data_len = 4;

    app->files[1].id = 2;
    app->files[1].settings_valid = true;
    app->files[1].type = 2; /* Value. */
    app->files[1].comm = 1;
    app->files[1].access = 0xEF2F; /* Free to read, never written. */
    app->files[1].value.lo_limit = 0;
    app->files[1].value.hi_limit = 5000;

    app->files[2].id = 3;
    app->files[2].settings_valid = true;
    app->files[2].type = 4; /* Cyclic record. */
    app->files[2].comm = 3;
    app->files[2].access = 0xE11F;
    app->files[2].record.size = 16;
    app->files[2].record.cur = 2;
    app->files[2].record.max = 4;
    /* Free to read, and the read still failed: that is its own answer. */

    app->files[3].id = 4;
    app->files[3].settings_valid = false;

    furi_string_reset(text);
    flipso_format_media(text, &media);
    dump("Unlisted applications, readable files", text);

    shows(text, "MIFARE DESFire EV3");
    shows(text, "Storage: Up to 8192 bytes");
    shows(text, "Listed by the card: No");
    shows(text, "ABCDEF: Unknown\n");
    shows(text, "\nContents: DEADBEEF\n");
    shows(text, "Range: 0 to 5000");
    shows(text, "Records: 2 of 4, 16 bytes each");
    shows(text, "Contents: Could not be read");
    shows(text, "File 4\nDetails: Locked\n");
    shows(text, "Write: Nobody");
    /* No free memory was reported, so no line may claim any. */
    hides(text, "Free space:");

    /* Storage codes: a power of two in the top seven bits, the bottom bit
     * meaning "somewhere between this and the next". */
    bool exact = false;
    check("2K is exact", flipso_media_storage_bytes(0x16, &exact) == 2048 && exact);
    check("4K is not", flipso_media_storage_bytes(0x18 | 1, &exact) == 4096 && !exact);
    check("zero is no size", flipso_media_storage_bytes(0x00, &exact) == 0);
    check("an absurd code is no size", flipso_media_storage_bytes(0xFE, &exact) == 0);

    check("EV1 is named", flipso_media_chip_from_hw(0x01, 0x01) == FlipsoChipEv1);
    check("EV2 is named", flipso_media_chip_from_hw(0x01, 0x12) == FlipsoChipEv2);
    check(
        "another family is not a DESFire",
        flipso_media_chip_from_hw(0x08, 0x01) == FlipsoChipUnknown);
    check("an unknown DESFire is unknown", flipso_media_chip_name(FlipsoChipUnknown) == NULL);

    /* The application list is a set: the reader adds what it found by name on
     * top of what the card listed, and the two overlap. */
    flipso_media_reset(&media);
    check("a reset lets the files go", media.app == NULL);
    flipso_media_add_app(&media, FLIPSO_AID_OYSTER);
    flipso_media_add_app(&media, FLIPSO_AID_OYSTER);
    check("an application is listed once", media.app_count == 1);
    check("and is found", flipso_media_has_app(&media, FLIPSO_AID_OYSTER));
    check("others are not", !flipso_media_has_app(&media, FLIPSO_AID_ITSO));

    for(uint32_t i = 0; i < FLIPSO_MEDIA_MAX_APPS * 2; i++)
        flipso_media_add_app(&media, i + 1);
    check("the list stops at its capacity", media.app_count == FLIPSO_MEDIA_MAX_APPS);
    check("and says it was cut short", media.apps_truncated);

    /* The chip block a saved card keeps: GetVersion then GetFreeMemory, as the
     * card sent them. The same Oyster as above. */
    static const uint8_t chip[FLIPSO_MEDIA_CHIP_LEN] = {
        0x04, 0x01, 0x01, 0x01, 0x00, 0x16, 0x05, /* hardware */
        0x04, 0x01, 0x01, 0x01, 0x03, 0x16, 0x05, /* software */
        0x04, 0x8B, 0x1F, 0xF1, 0xAD, 0x26, 0x80, /* UID */
        0xBA, 0x34, 0xCD, 0x56, 0xEF, /* batch */
        0x42, 0x08, /* week 42 of 2008 */
        0xE0, 0x04, 0x00, /* 1248 bytes free */
    };
    flipso_media_reset(&media);
    check("a chip block parses", flipso_media_parse_chip(&media, chip, sizeof(chip)));
    check("into the chip it names", media.valid && media.chip == FlipsoChipEv1);
    check("with its UID", memcmp(media.uid, "\x04\x8B\x1F\xF1\xAD\x26\x80", 7) == 0);
    check("its batch", memcmp(media.batch, "\xBA\x34\xCD\x56\xEF", 5) == 0);
    check("its software version", media.sw_major == 0x01 && media.sw_minor == 0x03);
    check("its date", media.prod_week == 0x42 && media.prod_year == 0x08);
    check("and its free space", media.free_memory_valid && media.free_memory == 1248);

    flipso_media_reset(&media);
    check(
        "one with no free memory still parses",
        flipso_media_parse_chip(&media, chip, FLIPSO_MEDIA_VERSION_LEN));
    check("and claims none", !media.free_memory_valid);

    flipso_media_reset(&media);
    check("a short block is refused", !flipso_media_parse_chip(&media, chip, 20));
    check("and leaves the card undescribed", !media.valid);

    flipso_media_reset(&media);
    furi_string_free(text);

    printf("\n%s\n", failures ? "MEDIA TESTS FAILED" : "All card media tests passed");
    return failures ? 1 : 0;
}
