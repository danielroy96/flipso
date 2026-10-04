/**
 * @file flipso_desfire_media.c
 * @brief What a DESFire says about itself without a key, for a card that is not
 * an ITSO one.
 *
 * The Oyster is the case worth naming - it is a transport card, people do point
 * this app at one, and it is not an ITSO card at all - so the DESFire transport
 * also looks for it and, when it is there, collects what the card will say
 * about itself without a key. See flipso_desfire_read_other().
 */
#include "flipso_desfire.h"

#include <furi.h>
#include <lib/toolbox/simple_array.h>

#define TAG "Flipso"

/* Transport for London's, likewise: the bytes spell "OY1" read the other way.
 * An Oyster is a DESFire, but what it holds is TfL's own scheme under TfL's own
 * keys rather than an ITSO shell, so this is a card to recognise and explain
 * rather than one to decode. */
static const MfDesfireApplicationId flipso_oyster_aid = {.data = {0x31, 0x59, 0x4F}};

/* Application 0 is the card itself. Selecting it is how a reader gets back out
 * of an application to the commands that describe the card as a whole. */
static const MfDesfireApplicationId flipso_picc_aid = {.data = {0x00, 0x00, 0x00}};

/* The poller fills these arrays; they hold plain values, so the element size is
 * the whole of the configuration they need. */
static const SimpleArrayConfig flipso_app_id_array_config = {
    .init = NULL,
    .reset = NULL,
    .copy = NULL,
    .type_size = sizeof(MfDesfireApplicationId),
};

/* Enough of a file to show what shape its contents are without turning the
 * screen into a hex dump, plus a budget across all of them so that a card full
 * of readable files cannot fill the buffer. */
#define FLIPSO_MEDIA_FILE_BYTES 32

/**
 * PICC-level description: what chip this is, when it was made, what is on it.
 *
 * None of this needs a key. GetVersion and GetFreeMemory are answered by every
 * card; the application list is answered by every card whose master key
 * settings permit a free directory listing, which most transit cards do because
 * their own readers have to find the application before authenticating.
 */
void flipso_desfire_describe(MfDesfirePoller* poller, FlipsoMedia* media, FlipsoCapture* capture) {
    if(mf_desfire_poller_select_application(poller, &flipso_picc_aid) != MfDesfireErrorNone) {
        return;
    }

    MfDesfireVersion version = {0};
    if(mf_desfire_poller_read_version(poller, &version) != MfDesfireErrorNone) return;

    /* Put back into the bytes the card sent, which is both what describes the
     * chip here and what a saved card keeps: one layout, read one way. */
    uint8_t chip[FLIPSO_MEDIA_CHIP_LEN];
    const uint8_t fields[] = {
        version.hw_vendor,
        version.hw_type,
        version.hw_subtype,
        version.hw_major,
        version.hw_minor,
        version.hw_storage,
        version.hw_proto,
        version.sw_vendor,
        version.sw_type,
        version.sw_subtype,
        version.sw_major,
        version.sw_minor,
        version.sw_storage,
        version.sw_proto,
    };
    _Static_assert(sizeof(fields) == 14, "GetVersion's first two frames are 7 bytes each");
    memcpy(chip, fields, sizeof(fields));
    memcpy(chip + 14, version.uid, 7);
    memcpy(chip + 21, version.batch, 5);
    chip[26] = version.prod_week;
    chip[27] = version.prod_year;
    size_t chip_len = FLIPSO_MEDIA_VERSION_LEN;

    MfDesfireFreeMemory free_memory = {0};
    if(mf_desfire_poller_read_free_memory(poller, &free_memory) == MfDesfireErrorNone &&
       free_memory.is_present) {
        chip[28] = (uint8_t)free_memory.bytes_free;
        chip[29] = (uint8_t)(free_memory.bytes_free >> 8);
        chip[30] = (uint8_t)(free_memory.bytes_free >> 16);
        chip_len = FLIPSO_MEDIA_CHIP_LEN;
    }

    flipso_media_parse_chip(media, chip, chip_len);
    flipso_capture_add(capture, FlipsoBlockChip, 0, chip, chip_len);

    SimpleArray* app_ids = simple_array_alloc(&flipso_app_id_array_config);
    if(mf_desfire_poller_read_application_ids(poller, app_ids) == MfDesfireErrorNone) {
        media->app_list_valid = true;
        uint32_t count = simple_array_get_count(app_ids);
        for(uint32_t i = 0; i < count; i++) {
            const MfDesfireApplicationId* id = simple_array_cget(app_ids, i);
            /* Sent least significant byte first, as AIDs are on the wire. */
            flipso_media_add_app(
                media,
                (uint32_t)id->data[0] | ((uint32_t)id->data[1] << 8) |
                    ((uint32_t)id->data[2] << 16));
        }
    }
    simple_array_free(app_ids);
}

/** Read as much of one file as its access rights and the budget allow. */
static void
    flipso_read_media_data(FlipsoMedia* media, MfDesfirePoller* poller, FlipsoMediaFile* out) {
    size_t budget = FLIPSO_MEDIA_MAX_DATA - media->data_len;
    if(budget > FLIPSO_MEDIA_FILE_BYTES) budget = FLIPSO_MEDIA_FILE_BYTES;
    if(budget == 0) return;

    MfDesfireFileData data = {.data = simple_array_alloc(&simple_array_config_uint8_t)};
    MfDesfireError error = MfDesfireErrorCommandNotSupported;

    switch(out->type) {
    case MfDesfireFileTypeStandard:
    case MfDesfireFileTypeBackup: {
        size_t size = out->data.size;
        if(size > budget) size = budget;
        if(size) error = mf_desfire_poller_read_file_data(poller, out->id, 0, size, &data);
        break;
    }
    case MfDesfireFileTypeValue:
        error = mf_desfire_poller_read_file_value(poller, out->id, &data);
        break;
    case MfDesfireFileTypeLinearRecord:
    case MfDesfireFileTypeCyclicRecord: {
        /* Records are asked for by the record rather than by the byte. */
        if(out->record.size == 0) break;
        uint32_t records = out->record.cur;
        uint32_t fits = (uint32_t)(budget / out->record.size);
        if(records > fits) records = fits;
        if(records)
            error = mf_desfire_poller_read_file_records(poller, out->id, 0, records, &data);
        break;
    }
    default:
        break;
    }

    if(error == MfDesfireErrorNone) {
        size_t len = simple_array_get_count(data.data);
        if(len > budget) len = budget;
        if(len) {
            memcpy(media->data + media->data_len, simple_array_cget_data(data.data), len);
            out->data_offset = media->data_len;
            out->data_len = (uint8_t)len;
            media->data_len += (uint8_t)len;
        }
    }

    simple_array_free(data.data);
}

/** Record one file: its settings, and its contents if it has no key on them. */
static void
    flipso_read_media_file(FlipsoMedia* media, MfDesfirePoller* poller, MfDesfireFileId fid) {
    if(media->file_count >= FLIPSO_MEDIA_MAX_FILES) return;

    FlipsoMediaFile* out = &media->files[media->file_count++];
    memset(out, 0, sizeof(*out));
    out->id = fid;

    MfDesfireFileSettings settings = {0};
    if(mf_desfire_poller_read_file_settings(poller, fid, &settings) != MfDesfireErrorNone) {
        /* The file is there - the card named it - but describing it needs a
         * key. Worth saying, rather than leaving the file out. */
        return;
    }

    out->settings_valid = true;
    out->type = settings.type;
    out->comm = settings.comm;
    /* One word per key for a card with several; the first is the one that
     * governs the file for everybody else. */
    if(settings.access_rights_len) out->access = settings.access_rights[0];

    switch(settings.type) {
    case MfDesfireFileTypeStandard:
    case MfDesfireFileTypeBackup:
        out->data.size = settings.data.size;
        break;
    case MfDesfireFileTypeValue:
        out->value.lo_limit = settings.value.lo_limit;
        out->value.hi_limit = settings.value.hi_limit;
        break;
    case MfDesfireFileTypeLinearRecord:
    case MfDesfireFileTypeCyclicRecord:
        out->record.size = settings.record.size;
        out->record.cur = settings.record.cur;
        out->record.max = settings.record.max;
        break;
    default:
        /* A transaction MAC file has no length of its own to report. */
        break;
    }

    if(flipso_media_file_free_read(out)) flipso_read_media_data(media, poller, out);
}

/** List an application's files and describe each one. */
static void flipso_read_media_app(
    FlipsoMedia* media,
    MfDesfirePoller* poller,
    const MfDesfireApplicationId* aid,
    uint32_t aid_value) {
    if(mf_desfire_poller_select_application(poller, aid) != MfDesfireErrorNone) return;

    SimpleArray* file_ids = simple_array_alloc(&simple_array_config_uint8_t);
    if(mf_desfire_poller_read_file_ids(poller, file_ids) == MfDesfireErrorNone) {
        media->has_files = true;
        media->selected_aid = aid_value;

        uint32_t count = simple_array_get_count(file_ids);
        if(count > FLIPSO_MEDIA_MAX_FILES) {
            count = FLIPSO_MEDIA_MAX_FILES;
            media->files_truncated = true;
        }
        for(uint32_t i = 0; i < count; i++) {
            const uint8_t* fid = simple_array_cget(file_ids, i);
            flipso_read_media_file(media, poller, *fid);
        }
    }
    simple_array_free(file_ids);
}

/**
 * Describe a DESFire card that carries no ITSO application.
 *
 * Only one such card is recognised by name: the Oyster. Its data is TfL's own
 * scheme under TfL's own keys, so there is nothing for the ITSO decoder to do
 * with it - but saying "not an ITSO card" of a card that is plainly a transport
 * card, and one this app is often pointed at, is a poor answer. Naming it and
 * showing what the card will say about itself is a better one, and any other
 * DESFire gets the second half of that too: what chip it is and what
 * applications it holds, which no key is needed for.
 *
 * Selecting the application is the test, rather than looking for it in the
 * application list, because a card is free to keep that list behind its master
 * key while still answering a select.
 */
FlipsoReaderStatus
    flipso_desfire_read_other(MfDesfirePoller* poller, FlipsoMedia* media, FlipsoCapture* capture) {
    flipso_media_reset(media);

    if(mf_desfire_poller_select_application(poller, &flipso_oyster_aid) != MfDesfireErrorNone) {
        flipso_desfire_describe(poller, media, capture);
        return FlipsoReaderStatusNotItso;
    }

    FURI_LOG_D(TAG, "Oyster application present");
    flipso_desfire_describe(poller, media, capture);
    /* The select above proved it is there, whatever the listing said. */
    flipso_media_add_app(media, FLIPSO_AID_OYSTER);
    flipso_read_media_app(media, poller, &flipso_oyster_aid, FLIPSO_AID_OYSTER);

    return FlipsoReaderStatusOyster;
}
