/**
 * @file flipso_media.c
 * @brief Rendering of the unauthenticated DESFire card description.
 *
 * Pure computation over the struct flipso_reader.c fills in: no NFC, no GUI, so
 * it is exercised on the host by tools/test/test_media.c.
 *
 * The file type and communication codes here are the DESFire wire values, which
 * is also what the firmware's MfDesfireFileType and
 * MfDesfireFileCommunicationSettings enumerations are. They are repeated rather
 * than included so that this file stays free of the NFC stack.
 */
#include "flipso_media.h"

#include <string.h>

/* Access rights are one 16-bit word per file: four key numbers, four bits each.
 * Key 14 is "anyone" and key 15 is "nobody"; anything else names a key that has
 * to be authenticated with first. */
#define FLIPSO_ACCESS_FREE  0x0E
#define FLIPSO_ACCESS_NEVER 0x0F

#define FLIPSO_ACCESS_READ(a)       (((a) >> 12) & 0x0F)
#define FLIPSO_ACCESS_WRITE(a)      (((a) >> 8) & 0x0F)
#define FLIPSO_ACCESS_READ_WRITE(a) (((a) >> 4) & 0x0F)
#define FLIPSO_ACCESS_CHANGE(a)     ((a) & 0x0F)

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
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

static const char* flipso_media_file_type_name(uint8_t type) {
    switch(type) {
    case 0:
        return "Standard";
    case 1:
        return "Backup";
    case 2:
        return "Value";
    case 3:
        return "Linear record";
    case 4:
        return "Cyclic record";
    case 5:
        return "Transaction MAC";
    default:
        return "Unknown type";
    }
}

static const char* flipso_media_comm_name(uint8_t comm) {
    switch(comm) {
    case 0:
        return "None";
    case 1:
        return "Signed";
    case 3:
        return "Encrypted";
    default:
        return "Unknown";
    }
}

static void flipso_media_cat_hex(FuriString* out, const uint8_t* data, size_t len) {
    for(size_t i = 0; i < len; i++) {
        furi_string_cat_printf(out, "%02X", data[i]);
    }
}

/** Two BCD digits, which is how the production date is stored. */
static uint8_t flipso_media_bcd(uint8_t value) {
    return (uint8_t)(((value >> 4) & 0x0F) * 10 + (value & 0x0F));
}

/** "Anyone", "Nobody" or "Key 3": who may do a thing to a file. */
static void flipso_media_cat_right(FuriString* out, const char* label, uint8_t key) {
    if(key == FLIPSO_ACCESS_FREE) {
        furi_string_cat_printf(out, "%s: Anyone\n", label);
    } else if(key == FLIPSO_ACCESS_NEVER) {
        furi_string_cat_printf(out, "%s: Nobody\n", label);
    } else {
        furi_string_cat_printf(out, "%s: Key %u\n", label, key);
    }
}

void flipso_media_cat_chip_summary(FuriString* out, const FlipsoMedia* media) {
    const char* name = flipso_media_chip_name(media->chip);
    if(name) {
        furi_string_cat_printf(out, "Chip: %s\n", name);
    } else {
        /* An unrecognised generation still has a readable version number, and
         * that is what would identify it. */
        furi_string_cat_printf(
            out, "Chip: Unknown (%02X.%02X)\n", media->hw_type, media->hw_major);
    }

    bool exact = true;
    uint32_t bytes = flipso_media_storage_bytes(media->hw_storage, &exact);
    if(bytes) {
        furi_string_cat_printf(
            out, "Storage: %s%lu bytes\n", exact ? "" : "up to ", (unsigned long)bytes);
    }
    if(media->free_memory_valid) {
        furi_string_cat_printf(out, "Free space: %lu bytes\n", (unsigned long)media->free_memory);
    }

    furi_string_cat(out, "UID: ");
    flipso_media_cat_hex(out, media->uid, sizeof(media->uid));
    furi_string_push_back(out, '\n');

    /* Week 0 is what a card that does not carry a date reports. */
    uint8_t week = flipso_media_bcd(media->prod_week);
    if(week >= 1 && week <= 53) {
        furi_string_cat_printf(
            out, "Made: Week %u of 20%02u\n", week, flipso_media_bcd(media->prod_year));
    }
}

static void flipso_media_cat_chip(FuriString* out, const FlipsoMedia* media) {
    furi_string_cat(out, "\e#Chip\n");
    flipso_media_cat_chip_summary(out, media);

    furi_string_cat(out, "Batch: ");
    flipso_media_cat_hex(out, media->batch, sizeof(media->batch));
    furi_string_push_back(out, '\n');

    /* In hex, because NXP's version numbers are codes rather than counts: the
     * EV3's hardware major version is 0x33, and printing that as 51 invites the
     * reader to make something of a number that does not mean anything. */
    furi_string_cat_printf(
        out,
        "Hardware: %02X.%02X\nSoftware: %02X.%02X\nVendor code: %02X\nProtocol: %02X\n",
        media->hw_major,
        media->hw_minor,
        media->sw_major,
        media->sw_minor,
        media->hw_vendor,
        media->hw_proto);
}

static void flipso_media_cat_apps(FuriString* out, const FlipsoMedia* media) {
    furi_string_cat(out, "\n\e#Applications\n");

    if(!media->app_list_valid) {
        /* A card may keep its directory behind the master key, in which case
         * all we know of is whatever we went looking for by name. */
        furi_string_cat(out, "Card will not list them.\n");
    }

    if(media->app_count == 0) {
        if(media->app_list_valid) furi_string_cat(out, "None.\n");
        return;
    }

    for(uint8_t i = 0; i < media->app_count; i++) {
        uint32_t aid = media->apps[i];
        const char* name = flipso_media_app_name(aid);
        if(name) {
            furi_string_cat_printf(out, "%06lX  %s\n", (unsigned long)aid, name);
        } else {
            furi_string_cat_printf(out, "%06lX\n", (unsigned long)aid);
        }
    }
    if(media->apps_truncated) {
        furi_string_cat(out, "...and more\n");
    }
}

static void flipso_media_cat_file(FuriString* out, const FlipsoMedia* media, const FlipsoMediaFile* file) {
    furi_string_cat_printf(out, "\nFile %u", file->id);

    if(!file->settings_valid) {
        furi_string_cat(out, "\nSettings: Locked\n");
        return;
    }

    furi_string_cat_printf(out, ": %s\n", flipso_media_file_type_name(file->type));

    switch(file->type) {
    case 3: /* Linear record. */
    case 4: /* Cyclic record. */
        furi_string_cat_printf(
            out,
            "Records: %lu of %lu, %lu bytes each\n",
            (unsigned long)file->record.cur,
            (unsigned long)file->record.max,
            (unsigned long)file->record.size);
        break;
    case 2:
        /* A value file has no length: it holds one counter, and what the card
         * will say about it without a key is the range it is kept within. */
        furi_string_cat_printf(
            out,
            "Range: %ld to %ld\n",
            (long)(int32_t)file->value.lo_limit,
            (long)(int32_t)file->value.hi_limit);
        break;
    case 5:
        /* A transaction MAC file has no length of its own to report. */
        break;
    default:
        furi_string_cat_printf(out, "Size: %lu bytes\n", (unsigned long)file->data.size);
        break;
    }

    furi_string_cat_printf(out, "Encryption: %s\n", flipso_media_comm_name(file->comm));
    furi_string_cat_printf(out, "Access rights: %04X\n", file->access);
    flipso_media_cat_right(out, "  Read", FLIPSO_ACCESS_READ(file->access));
    flipso_media_cat_right(out, "  Write", FLIPSO_ACCESS_WRITE(file->access));

    if(file->data_len) {
        furi_string_cat(out, "Contents:\n");
        /* Eight bytes to the line: sixteen hex digits is what fits across the
         * screen without the scroll element wrapping mid-byte. */
        for(uint8_t i = 0; i < file->data_len; i += 8) {
            uint8_t run = file->data_len - i;
            if(run > 8) run = 8;
            flipso_media_cat_hex(out, media->data + file->data_offset + i, run);
            furi_string_push_back(out, '\n');
        }
    } else if(flipso_media_file_free_read(file)) {
        /* The rights said anyone could read it and the read still failed, which
         * is worth distinguishing from a file that is simply locked. */
        furi_string_cat(out, "Contents: Could not be read\n");
    } else {
        furi_string_cat(out, "Contents: Locked\n");
    }
}

static void flipso_media_cat_files(FuriString* out, const FlipsoMedia* media) {
    /* Named where we can: the application list above has already paired the
     * name with its number, so repeating the number here says nothing. */
    const char* name = flipso_media_app_name(media->selected_aid);
    if(name) {
        furi_string_cat_printf(out, "\n\e#Files in %s\n", name);
    } else {
        furi_string_cat_printf(out, "\n\e#Files in %06lX\n", (unsigned long)media->selected_aid);
    }

    if(media->file_count == 0) {
        furi_string_cat(out, "None listed.\n");
        return;
    }

    for(uint8_t i = 0; i < media->file_count; i++) {
        flipso_media_cat_file(out, media, &media->files[i]);
    }
    if(media->files_truncated) {
        furi_string_cat(out, "\n...and more\n");
    }
}

void flipso_media_cat(FuriString* out, const FlipsoMedia* media) {
    if(!media->valid) {
        furi_string_cat(out, "The card said nothing\nabout itself.\n");
        return;
    }

    flipso_media_cat_chip(out, media);
    flipso_media_cat_apps(out, media);
    if(media->has_files) flipso_media_cat_files(out, media);
}
