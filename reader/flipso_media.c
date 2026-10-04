/**
 * @file flipso_media.c
 * @brief The unauthenticated DESFire card description: its model and helpers.
 *
 * Pure computation over the struct flipso_desfire_media.c fills in: no NFC, no GUI, so
 * it is exercised on the host by tools/test/test_media.c. What the screen says
 * about it is format/flipso_format_media.c's, like every other screen.
 */
#include "flipso_media.h"

#include <string.h>

void flipso_media_reset(FlipsoMedia* media) {
    memset(media, 0, sizeof(FlipsoMedia));
}

bool flipso_media_has_app(const FlipsoMedia* media, uint32_t aid) {
    for(uint8_t i = 0; i < media->app_count; i++) {
        if(media->apps[i] == aid) return true;
    }
    return false;
}

void flipso_media_add_app(FlipsoMedia* media, uint32_t aid) {
    if(flipso_media_has_app(media, aid)) return;
    if(media->app_count >= FLIPSO_MEDIA_MAX_APPS) {
        media->apps_truncated = true;
        return;
    }
    media->apps[media->app_count++] = aid;
}

FlipsoMediaChip flipso_media_chip_from_hw(uint8_t hw_type, uint8_t hw_major) {
    /* Type 1 is the DESFire family; the major version is the generation, and
     * NXP numbered them 0x00, 0x01, 0x12, 0x22, 0x33 rather than in sequence. */
    if(hw_type != 0x01) return FlipsoChipUnknown;

    switch(hw_major) {
    case 0x00:
        return FlipsoChipMf3Icd40;
    case 0x01:
        return FlipsoChipEv1;
    case 0x12:
        return FlipsoChipEv2;
    case 0x22:
        return FlipsoChipEv2Xl;
    case 0x33:
        return FlipsoChipEv3;
    default:
        return FlipsoChipUnknown;
    }
}

const char* flipso_media_chip_name(FlipsoMediaChip chip) {
    switch(chip) {
    case FlipsoChipMf3Icd40:
        return "MIFARE DESFire";
    case FlipsoChipEv1:
        return "MIFARE DESFire EV1";
    case FlipsoChipEv2:
        return "MIFARE DESFire EV2";
    case FlipsoChipEv2Xl:
        return "MIFARE DESFire EV2 XL";
    case FlipsoChipEv3:
        return "MIFARE DESFire EV3";
    default:
        return NULL;
    }
}

const char* flipso_media_app_name(uint32_t aid) {
    switch(aid) {
    case FLIPSO_AID_OYSTER:
        return "Oyster";
    case FLIPSO_AID_ITSO:
        return "ITSO";
    case 0x000000u:
        /* Not an application: selecting it is how a reader gets back to the
         * card itself, and it is where the application list lives. */
        return "Card master";
    default:
        return NULL;
    }
}

uint32_t flipso_media_storage_bytes(uint8_t code, bool* exact) {
    *exact = true;
    if(code == 0) return 0;

    uint8_t power = code >> 1;
    if(power >= 32) return 0;

    *exact = (code & 1) == 0;
    return (uint32_t)1 << power;
}

bool flipso_media_file_free_read(const FlipsoMediaFile* file) {
    if(!file->settings_valid) return false;
    return FLIPSO_ACCESS_READ(file->access) == FLIPSO_ACCESS_FREE ||
           FLIPSO_ACCESS_READ_WRITE(file->access) == FLIPSO_ACCESS_FREE;
}

/* ------------------------------------------------------------------ */
/* The chip block a saved card keeps                                   */
/* ------------------------------------------------------------------ */

bool flipso_media_parse_chip(FlipsoMedia* media, const uint8_t* data, size_t len) {
    if(!data || len < FLIPSO_MEDIA_VERSION_LEN) return false;

    /* GetVersion's three frames, in the order the card sends them: hardware,
     * software, then the UID, batch and production date. */
    media->valid = true;
    media->hw_vendor = data[0];
    media->hw_type = data[1];
    media->hw_subtype = data[2];
    media->hw_major = data[3];
    media->hw_minor = data[4];
    media->hw_storage = data[5];
    media->hw_proto = data[6];
    media->sw_major = data[10];
    media->sw_minor = data[11];
    media->sw_storage = data[12];
    media->sw_proto = data[13];
    memcpy(media->uid, data + 14, sizeof(media->uid));
    memcpy(media->batch, data + 21, sizeof(media->batch));
    media->prod_week = data[26];
    media->prod_year = data[27];
    media->chip = flipso_media_chip_from_hw(media->hw_type, media->hw_major);

    /* GetFreeMemory's three bytes, least significant first, when the card
     * answered it: the original DESFire does not. */
    if(len >= FLIPSO_MEDIA_CHIP_LEN) {
        media->free_memory_valid = true;
        media->free_memory = (uint32_t)data[28] | ((uint32_t)data[29] << 8) |
                             ((uint32_t)data[30] << 16);
    }
    return true;
}
