/**
 * @file flipso_format_media.c
 * @brief What a DESFire says about itself without a key: the chip, its applications
 * and files.
 */
#include "flipso_format_i.h"

/** Two BCD digits, which is how the production date is stored. */
static uint8_t flipso_bcd(uint8_t value) {
    return (uint8_t)(((value >> 4) & 0x0F) * 10 + (value & 0x0F));
}

/**
 * The few lines of chip description worth showing beside a decoded ITSO card:
 * what chip it is, its UID, storage and when it was made.
 */
void flipso_cat_chip_summary(FuriString* out, const FlipsoMedia* media) {
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
            out, "Storage: %s%lu bytes\n", exact ? "" : "Up to ", (unsigned long)bytes);
    }
    if(media->free_memory_valid) {
        furi_string_cat_printf(out, "Free space: %lu bytes\n", (unsigned long)media->free_memory);
    }

    furi_string_cat(out, "UID: ");
    flipso_cat_hex(out, media->uid, sizeof(media->uid));
    furi_string_push_back(out, '\n');

    /* Week 0 is what a card that does not carry a date reports. */
    uint8_t week = flipso_bcd(media->prod_week);
    if(week >= 1 && week <= 53) {
        furi_string_cat_printf(
            out, "Made: Week %u of 20%02u\n", week, flipso_bcd(media->prod_year));
    }
}

static const char* flipso_file_type_name(uint8_t type) {
    switch(type) {
    case FLIPSO_FILE_STANDARD:
        return "Standard";
    case FLIPSO_FILE_BACKUP:
        return "Backup";
    case FLIPSO_FILE_VALUE:
        return "Value";
    case FLIPSO_FILE_LINEAR_RECORD:
        return "Linear record";
    case FLIPSO_FILE_CYCLIC_RECORD:
        return "Cyclic record";
    case FLIPSO_FILE_TRANSACTION:
        return "Transaction MAC";
    default:
        return "Unknown type";
    }
}

static const char* flipso_file_comm_name(uint8_t comm) {
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

/** "Anyone", "Nobody" or "Key 3": who may do a thing to a file. */
static void flipso_cat_file_right(FuriString* out, const char* label, uint8_t key) {
    if(key == FLIPSO_ACCESS_FREE) {
        furi_string_cat_printf(out, "  %s: Anyone\n", label);
    } else if(key == FLIPSO_ACCESS_NEVER) {
        furi_string_cat_printf(out, "  %s: Nobody\n", label);
    } else {
        furi_string_cat_printf(out, "  %s: Key %u\n", label, key);
    }
}

/** One file, as a page of its own titled with its number. */
static void
    flipso_cat_media_file(FuriString* out, const FlipsoMediaApp* app, const FlipsoMediaFile* file) {
    char title[12];
    snprintf(title, sizeof(title), "File %u", file->id);
    flipso_cat_page(out, FlipsoIconFile, title);
    if(!file->settings_valid) {
        /* The card named the file and would not describe it without a key. */
        furi_string_cat(out, "Details: Locked\n");
        return;
    }
    furi_string_cat_printf(out, "Type: %s\n", flipso_file_type_name(file->type));

    switch(file->type) {
    case FLIPSO_FILE_LINEAR_RECORD:
    case FLIPSO_FILE_CYCLIC_RECORD:
        furi_string_cat_printf(
            out,
            "Records: %lu of %lu, %lu bytes each\n",
            (unsigned long)file->record.cur,
            (unsigned long)file->record.max,
            (unsigned long)file->record.size);
        break;
    case FLIPSO_FILE_VALUE:
        /* A value file has no length: it holds one counter, and what the card
         * will say about it without a key is the range it is kept within. */
        furi_string_cat_printf(
            out,
            "Range: %ld to %ld\n",
            (long)(int32_t)file->value.lo_limit,
            (long)(int32_t)file->value.hi_limit);
        break;
    case FLIPSO_FILE_TRANSACTION:
        /* A transaction MAC file has no length of its own to report. */
        break;
    default:
        furi_string_cat_printf(out, "Size: %lu bytes\n", (unsigned long)file->data.size);
        break;
    }

    furi_string_cat_printf(out, "Encryption: %s\n", flipso_file_comm_name(file->comm));
    /* The word as the card gave it, then what it means. */
    furi_string_cat_printf(out, "Access rights: %04X\n", file->access);
    flipso_cat_file_right(out, "Read", FLIPSO_ACCESS_READ(file->access));
    flipso_cat_file_right(out, "Write", FLIPSO_ACCESS_WRITE(file->access));

    if(file->data_len) {
        /* Four bytes to a group, so a long run wraps between groups rather than
         * in the middle of a byte. */
        furi_string_cat(out, "Contents:");
        for(uint8_t i = 0; i < file->data_len; i += 4) {
            uint8_t run = (uint8_t)(file->data_len - i);
            if(run > 4) run = 4;
            furi_string_push_back(out, ' ');
            flipso_cat_hex(out, app->data + file->data_offset + i, run);
        }
        furi_string_push_back(out, '\n');
    } else if(flipso_media_file_free_read(file)) {
        /* The rights said anyone could read it and the read still failed, which
         * is worth distinguishing from a file that is simply locked. */
        furi_string_cat(out, "Contents: Could not be read\n");
    } else {
        furi_string_cat(out, "Contents: Locked\n");
    }
}

void flipso_format_media(FuriString* out, const FlipsoMedia* media) {
    flipso_cat_page(out, FlipsoIconChip, "Chip");
    if(!media->valid) {
        furi_string_cat(out, "The card did not describe itself.\n");
        return;
    }

    flipso_cat_chip_summary(out, media);
    furi_string_cat(out, "Batch: ");
    flipso_cat_hex(out, media->batch, sizeof(media->batch));
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

    flipso_cat_page(out, FlipsoIconApps, "Applications");
    if(!media->app_list_valid) {
        /* A card may keep its directory behind the master key, in which case
         * all we know of is whatever we went looking for by name. */
        furi_string_cat(out, "Listed by the card: No\n");
    } else if(media->app_count == 0) {
        furi_string_cat(out, "Applications: None\n");
    }
    for(uint8_t i = 0; i < media->app_count; i++) {
        const char* name = flipso_media_app_name(media->apps[i]);
        furi_string_cat_printf(
            out, "%06lX: %s\n", (unsigned long)media->apps[i], name ? name : "Unknown");
    }
    if(media->apps_truncated) furi_string_cat(out, "More: Too many to list\n");

    const FlipsoMediaApp* app = media->app;
    if(!app) return;

    /* Which application the file pages after this one are from, named where
     * we can: the list above has already paired the name with its number. */
    const char* name = flipso_media_app_name(app->aid);
    if(name) {
        furi_string_cat_printf(out, "Files read from: %s\n", name);
    } else {
        furi_string_cat_printf(out, "Files read from: %06lX\n", (unsigned long)app->aid);
    }
    if(app->file_count == 0) furi_string_cat(out, "Files: None listed\n");
    if(app->files_truncated) furi_string_cat(out, "More files: Too many to list\n");

    for(uint8_t i = 0; i < app->file_count; i++) {
        flipso_cat_media_file(out, app, &app->files[i]);
    }
}

/** The ISRN as it prints: issuer, operator, then serial - 633597 1234 0012 3458. */
