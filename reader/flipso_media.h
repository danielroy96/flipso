/**
 * @file flipso_media.h
 * @brief What a DESFire card will say about itself without a key.
 *
 * A card Flipso cannot decode is not a card it knows nothing about. Every
 * DESFire answers GetVersion, GetFreeMemory and GetApplicationIDs at PICC level,
 * and an application that permits directory listing will name its files and
 * their settings too - all of it before authentication, because none of it is
 * the data the keys protect. That is enough to say what chip is in the card,
 * when it was made, what is on it and what shape the data takes, which is a far
 * more useful answer than "read failed" for a card such as an Oyster whose
 * contents are locked.
 *
 * This is the data model, kept free of the NFC stack so that it builds and is
 * tested on the host: flipso_desfire_media.c fills it in, and flipso_format_media.c turns
 * it into the screen. Sizes are capped rather than grown, so a card with more
 * applications or files than we keep is reported as truncated rather than
 * silently shortened.
 *
 * The chip half is held for every DESFire, ITSO ones included - the Card screen
 * shows it. The files of an application and what could be read of them are
 * most of the size and only ever come from a card that is not an ITSO one, so
 * they are a FlipsoMediaApp allocated when such a card's files are listed.
 */
#pragma once

#include <furi.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DESFire application identifiers, as the 24-bit numbers the card reports
 * (the wire order is least significant byte first). */
#define FLIPSO_AID_ITSO   0xA00216u /**< ITSO's registered AID. */
#define FLIPSO_AID_OYSTER 0x4F5931u /**< Transport for London: ASCII "OY1". */

/* A DESFire holds up to 28 applications and 32 files per application. Neither
 * limit is worth the RAM for a screen that has to be scrolled to be read. */
#define FLIPSO_MEDIA_MAX_APPS  12
#define FLIPSO_MEDIA_MAX_FILES 16
/** Total bytes of freely readable file content kept for display. */
#define FLIPSO_MEDIA_MAX_DATA  128

/* Access rights are one 16-bit word per file: four key numbers, four bits each.
 * Key 14 is "anyone" and key 15 is "nobody"; anything else names a key that has
 * to be authenticated with first. */
#define FLIPSO_ACCESS_FREE  0x0E
#define FLIPSO_ACCESS_NEVER 0x0F

#define FLIPSO_ACCESS_READ(a)       (((a) >> 12) & 0x0F)
#define FLIPSO_ACCESS_WRITE(a)      (((a) >> 8) & 0x0F)
#define FLIPSO_ACCESS_READ_WRITE(a) (((a) >> 4) & 0x0F)
#define FLIPSO_ACCESS_CHANGE(a)     ((a) & 0x0F)

/* File types and communication settings, as the DESFire wire values - which is
 * also what the firmware's MfDesfireFileType and
 * MfDesfireFileCommunicationSettings are. Repeated rather than included so
 * that this stays free of the NFC stack. */
#define FLIPSO_FILE_STANDARD      0
#define FLIPSO_FILE_BACKUP        1
#define FLIPSO_FILE_VALUE         2
#define FLIPSO_FILE_LINEAR_RECORD 3
#define FLIPSO_FILE_CYCLIC_RECORD 4
#define FLIPSO_FILE_TRANSACTION   5

/** Chip generation, derived from the hardware version GetVersion reports. */
typedef enum {
    FlipsoChipUnknown,
    FlipsoChipMf3Icd40, /**< The original DESFire. */
    FlipsoChipEv1,
    FlipsoChipEv2,
    FlipsoChipEv2Xl,
    FlipsoChipEv3,
} FlipsoMediaChip;

/**
 * One file of the application we looked inside.
 *
 * The access rights word is kept whole as well as split, because the split is
 * an interpretation and the word is what the card said.
 */
typedef struct {
    uint8_t id;
    uint8_t type; /**< MfDesfireFileType: standard, backup, value, record... */
    uint8_t comm; /**< MfDesfireFileCommunicationSettings. */
    uint16_t access; /**< Read / write / read-write / change key numbers. */
    bool settings_valid; /**< False when the card refused to describe the file. */
    uint8_t data_offset; /**< Into FlipsoMediaApp::data. */
    uint8_t data_len; /**< 0 when nothing could be read without a key. */
    /* Shaped by the file type, as DESFire's own file settings are. */
    union {
        struct {
            uint32_t size;
        } data;
        struct {
            uint32_t lo_limit;
            uint32_t hi_limit;
        } value;
        struct {
            uint32_t size; /**< Bytes per record. */
            uint32_t cur; /**< Records written. */
            uint32_t max; /**< Records the file holds. */
        } record;
    };
} FlipsoMediaFile;

/** The application we looked inside: its files, and what of them had no key on them. */
typedef struct {
    uint32_t aid; /**< The application the files belong to. */
    bool files_truncated;
    uint8_t file_count;
    FlipsoMediaFile files[FLIPSO_MEDIA_MAX_FILES];

    uint8_t data[FLIPSO_MEDIA_MAX_DATA];
    uint8_t data_len;
} FlipsoMediaApp;

/**
 * What a DESFire said about itself. Zeroed storage is an empty one, and from
 * then on flipso_media_reset() is what empties it, because it may own an
 * application's files.
 */
typedef struct {
    /** False until a card has answered GetVersion. Nothing else is meaningful. */
    bool valid;

    FlipsoMediaChip chip;
    uint8_t hw_vendor, hw_type, hw_subtype, hw_major, hw_minor, hw_storage, hw_proto;
    uint8_t sw_major, sw_minor, sw_storage, sw_proto;
    uint8_t uid[7];
    uint8_t batch[5];
    uint8_t prod_week, prod_year; /**< BCD, as the card reports them. */

    bool free_memory_valid; /**< GetFreeMemory is EV1 and later only. */
    uint32_t free_memory;

    uint32_t apps[FLIPSO_MEDIA_MAX_APPS];
    uint8_t app_count;
    bool app_list_valid; /**< False when the card would not list them. */
    bool apps_truncated;

    /** The application whose files the card listed; NULL when none was. */
    FlipsoMediaApp* app;
} FlipsoMedia;

/** Forget the last card, releasing its application's files. */
void flipso_media_reset(FlipsoMedia* media);

/**
 * Start describing application @p aid's files, emptying any described before.
 *
 * @return where they go, owned by @p media until the next reset; NULL when
 *         there is no room for them.
 */
FlipsoMediaApp* flipso_media_open_app(FlipsoMedia* media, uint32_t aid);

/** Record an application identifier, ignoring duplicates and overflow. */
void flipso_media_add_app(FlipsoMedia* media, uint32_t aid);

/** True when the card listed, or we found, this application. */
bool flipso_media_has_app(const FlipsoMedia* media, uint32_t aid);

/** Chip generation for a GetVersion hardware major version. */
FlipsoMediaChip flipso_media_chip_from_hw(uint8_t hw_type, uint8_t hw_major);

/** "DESFire EV1", or NULL for a chip we cannot name. */
const char* flipso_media_chip_name(FlipsoMediaChip chip);

/** The scheme that owns an AID ("Oyster"), or NULL for one we do not know. */
const char* flipso_media_app_name(uint32_t aid);

/**
 * Storage size in bytes from the GetVersion code.
 *
 * The code is a power of two in its upper seven bits; the bottom bit says the
 * real size is somewhere between that and the next power up, which is how the
 * odd sizes (such as the 4K that is really 3840 bytes) are expressed.
 *
 * @param[out] exact set false for such a card.
 * @return bytes, or 0 for a code that is not a size at all.
 */
uint32_t flipso_media_storage_bytes(uint8_t code, bool* exact);

/** True when the file can be read with no key, per its access rights. */
bool flipso_media_file_free_read(const FlipsoMediaFile* file);

/** GetVersion's reply: seven bytes of hardware, seven of software, then the
 * UID, batch number and production week and year. */
#define FLIPSO_MEDIA_VERSION_LEN 28
/** That, then GetFreeMemory's three bytes: the block a saved card keeps. */
#define FLIPSO_MEDIA_CHIP_LEN    (FLIPSO_MEDIA_VERSION_LEN + 3)

/**
 * Fill in the chip half of @p media from the bytes the card answered with.
 *
 * The reader builds these bytes from GetVersion and GetFreeMemory, and a saved
 * card keeps them, so a card opened from the SD card describes its chip as it
 * did when it was read. The free memory is optional: @p len of
 * FLIPSO_MEDIA_VERSION_LEN is a card that did not report it.
 *
 * @return false, leaving @p media alone, when there are too few bytes.
 */
bool flipso_media_parse_chip(FlipsoMedia* media, const uint8_t* data, size_t len);

#ifdef __cplusplus
}
#endif
