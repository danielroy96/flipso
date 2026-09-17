/**
 * @file flipso_reader.c
 * @brief NFC transports for the ITSO decoder.
 *
 * ITSO on DESFire (customer media definition 7, ITSO TS 1000-10 clause 8) stores
 * every data group in a file of the ITSO application, and every one of those
 * files is readable without authentication. So the whole read is: select the
 * application, pull the shell, pull the directory, then follow the Sector Chain
 * Table to just those files that actually hold a product. That is typically six
 * to ten short reads rather than a full card dump.
 *
 * Not every ITSO card is a DESFire one, and the others do not answer DESFire
 * commands at all. This file therefore holds the DESFire transport plus the
 * poller plumbing shared with the ISO 7816 transport in flipso_cmd2.c; which one
 * runs is chosen by flipso_reader_next_transport().
 *
 * A DESFire that carries no ITSO application is not necessarily a card we have
 * nothing to say about. The Oyster is the case worth naming - it is a transport
 * card, people do point this app at one, and it is not an ITSO card at all - so
 * the DESFire transport also looks for it and, when it is there, collects what
 * the card will say about itself without a key. See flipso_read_other_card().
 */
#include "flipso_reader.h"
#include "flipso_cmd2.h"
#include "itso/itso_operators.h"

#include <furi.h>
#include <nfc/nfc.h>
#include <nfc/nfc_poller.h>
#include <nfc/protocols/mf_desfire/mf_desfire.h>
#include <nfc/protocols/mf_desfire/mf_desfire_poller.h>
#include <nfc/protocols/iso14443_4a/iso14443_4a_poller.h>
#include <lib/toolbox/simple_array.h>

#define TAG "Flipso"

/* ITSO's DESFire application identifier, sent least significant byte first. */
static const MfDesfireApplicationId flipso_itso_aid = {.data = {0x16, 0x02, 0xA0}};

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

/* The shell lives in file 15 under CMD7. Other media definitions place it
 * elsewhere, so fall back to a scan if it is not where we expect. */
#define FLIPSO_SHELL_FID_DEFAULT 0x0F
#define FLIPSO_MAX_FID           0x1F
#define FLIPSO_SHELL_BUF         64
#define FLIPSO_DIR_BUF           256
/* The cyclic log holds 48-byte records. CMD7's default file is four of them,
 * but TS 1000-10 clause 8.7.5.4 lets the shell issuer size it otherwise, so read
 * as many as the decoder is willing to keep. */
#define FLIPSO_LOG_BUF           (ITSO_MAX_TAPS * 48)
/* One product may chain across several sectors; cap the walk so a corrupt
 * chain cannot spin or blow the buffer. */
#define FLIPSO_MAX_CHAIN_HOPS    6

/** The command sets the reader knows, tried in this order. */
typedef enum {
    FlipsoTransportDesfire, /**< CMD7 and CMD12: native DESFire commands. */
    FlipsoTransportIso7816, /**< CMD2: an ISO 7816-4 file system. */
    FlipsoTransportCount,
} FlipsoTransport;

struct FlipsoReader {
    Nfc* nfc;
    NfcPoller* poller;
    bool running;
    FlipsoTransport transport;

    /** Allocated the first time the ISO 7816 transport is used. */
    FlipsoCmd2* cmd2;

    ItsoCard* card;
    FlipsoMedia* media;
    FlipsoReaderCallback callback;
    void* context;
    FlipsoReaderStatus status;

    /* Scratch buffers live here rather than on the NFC worker's stack. */
    uint8_t shell[FLIPSO_SHELL_BUF];
    uint8_t dir[FLIPSO_DIR_BUF];
    size_t dir_len;
    uint8_t group[ITSO_MAX_GROUP_LEN];
    uint8_t log[FLIPSO_LOG_BUF];

    /* Sector to file mapping derived from the shell (see flipso_sector_to_fid). */
    uint8_t shell_fid;
    bool descending;
};

/**
 * Read a whole file into @p out.
 * @return number of bytes read, or 0 on any failure.
 */
static size_t flipso_read_file(
    MfDesfirePoller* poller,
    MfDesfireFileId fid,
    uint8_t* out,
    size_t capacity) {
    MfDesfireFileSettings settings = {0};
    if(mf_desfire_poller_read_file_settings(poller, fid, &settings) != MfDesfireErrorNone) {
        return 0;
    }
    if(settings.type != MfDesfireFileTypeStandard && settings.type != MfDesfireFileTypeBackup) {
        return 0;
    }

    size_t size = settings.data.size;
    if(size == 0) return 0;
    if(size > capacity) size = capacity;

    MfDesfireFileData data = {.data = simple_array_alloc(&simple_array_config_uint8_t)};
    size_t read = 0;

    if(mf_desfire_poller_read_file_data(poller, fid, 0, size, &data) == MfDesfireErrorNone) {
        read = simple_array_get_count(data.data);
        if(read > capacity) read = capacity;
        if(read) memcpy(out, simple_array_cget_data(data.data), read);
    }

    simple_array_free(data.data);
    return read;
}

/**
 * Map an ITSO logical sector number to a DESFire file number.
 *
 * TS 1000-10 table 66 lays CMD7 out back to front: logical sector 0 (the shell)
 * is file 15 and logical sector 15 (the directory) is file 0. Rather than hard
 * code that, we anchor on wherever the shell turned out to live.
 */
static uint8_t flipso_sector_to_fid(const FlipsoReader* reader, uint8_t sector) {
    return reader->descending ? (uint8_t)(reader->shell_fid - sector) :
                                (uint8_t)(reader->shell_fid + sector);
}

void flipso_log_shell_owner(const ItsoCard* card) {
    if(!card->shell_valid) return;

    const char* name = itso_operator_name(card->oid);
    FURI_LOG_I(TAG, "Shell owner: OID %u (%s)", card->oid, name ? name : "unknown");
}

/** Locate and decode the ITSO Shell Environment Data Group. */
static bool flipso_read_shell(FlipsoReader* reader, MfDesfirePoller* poller) {
    /* Try the documented location first so the common case costs one read. */
    static const uint8_t preferred[] = {FLIPSO_SHELL_FID_DEFAULT, 0x00};

    for(size_t i = 0; i < COUNT_OF(preferred); i++) {
        size_t len = flipso_read_file(poller, preferred[i], reader->shell, FLIPSO_SHELL_BUF);
        if(len && itso_looks_like_shell(reader->shell, len)) {
            if(!itso_parse_shell(reader->card, reader->shell, len)) return false;
            reader->shell_fid = preferred[i];
            return true;
        }
    }

    /* Unknown media definition: sweep the application for a shell-shaped file. */
    for(uint8_t fid = 0; fid <= FLIPSO_MAX_FID; fid++) {
        if(fid == FLIPSO_SHELL_FID_DEFAULT || fid == 0x00) continue;
        size_t len = flipso_read_file(poller, fid, reader->shell, FLIPSO_SHELL_BUF);
        if(len && itso_looks_like_shell(reader->shell, len)) {
            if(!itso_parse_shell(reader->card, reader->shell, len)) return false;
            reader->shell_fid = fid;
            return true;
        }
    }

    return false;
}

/** Follow one product's sector chain, concatenating the sectors it occupies. */
static size_t flipso_read_product_group(
    FlipsoReader* reader,
    MfDesfirePoller* poller,
    uint8_t start_sector) {
    const ItsoCard* card = reader->card;
    size_t total = 0;
    uint8_t sector = start_sector;

    for(uint8_t hop = 0; hop < FLIPSO_MAX_CHAIN_HOPS; hop++) {
        if(sector == 0 || sector >= card->sector_count) break;
        if(total + card->sector_size > ITSO_MAX_GROUP_LEN) break;

        size_t read = flipso_read_file(
            poller,
            flipso_sector_to_fid(reader, sector),
            reader->group + total,
            ITSO_MAX_GROUP_LEN - total);
        if(read == 0) break;
        total += read;

        uint8_t next = itso_sct_entry(card, reader->dir, reader->dir_len, sector);
        /* Terminators: itself (unused), S-2 (blocked) or S-1 (in use). */
        if(next == sector || next == 0 || next == card->sector_count - 2 ||
           next == card->sector_count - 1) {
            break;
        }
        sector = next;
    }

    return total;
}

/* ------------------------------------------------------------------ */
/* Cards that are not ITSO ones                                        */
/* ------------------------------------------------------------------ */

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
static void flipso_read_media_picc(FlipsoReader* reader, MfDesfirePoller* poller) {
    FlipsoMedia* media = reader->media;

    if(mf_desfire_poller_select_application(poller, &flipso_picc_aid) != MfDesfireErrorNone) {
        return;
    }

    MfDesfireVersion version = {0};
    if(mf_desfire_poller_read_version(poller, &version) != MfDesfireErrorNone) return;

    media->valid = true;
    media->chip = flipso_media_chip_from_hw(version.hw_type, version.hw_major);
    media->hw_vendor = version.hw_vendor;
    media->hw_type = version.hw_type;
    media->hw_subtype = version.hw_subtype;
    media->hw_major = version.hw_major;
    media->hw_minor = version.hw_minor;
    media->hw_storage = version.hw_storage;
    media->hw_proto = version.hw_proto;
    media->sw_major = version.sw_major;
    media->sw_minor = version.sw_minor;
    media->sw_storage = version.sw_storage;
    media->sw_proto = version.sw_proto;
    memcpy(media->uid, version.uid, sizeof(media->uid));
    memcpy(media->batch, version.batch, sizeof(media->batch));
    media->prod_week = version.prod_week;
    media->prod_year = version.prod_year;

    MfDesfireFreeMemory free_memory = {0};
    if(mf_desfire_poller_read_free_memory(poller, &free_memory) == MfDesfireErrorNone &&
       free_memory.is_present) {
        media->free_memory_valid = true;
        media->free_memory = free_memory.bytes_free;
    }

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
    flipso_read_media_data(FlipsoReader* reader, MfDesfirePoller* poller, FlipsoMediaFile* out) {
    FlipsoMedia* media = reader->media;

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
        if(records) error = mf_desfire_poller_read_file_records(poller, out->id, 0, records, &data);
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
    flipso_read_media_file(FlipsoReader* reader, MfDesfirePoller* poller, MfDesfireFileId fid) {
    FlipsoMedia* media = reader->media;
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

    if(flipso_media_file_free_read(out)) flipso_read_media_data(reader, poller, out);
}

/** List an application's files and describe each one. */
static void flipso_read_media_app(
    FlipsoReader* reader,
    MfDesfirePoller* poller,
    const MfDesfireApplicationId* aid,
    uint32_t aid_value) {
    FlipsoMedia* media = reader->media;

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
            flipso_read_media_file(reader, poller, *fid);
        }
    }
    simple_array_free(file_ids);
}

/**
 * Identify a DESFire card that carries no ITSO application.
 *
 * Only one such card is recognised by name: the Oyster. Its data is TfL's own
 * scheme under TfL's own keys, so there is nothing for the ITSO decoder to do
 * with it - but saying "not an ITSO card" of a card that is plainly a transport
 * card, and one this app is often pointed at, is a poor answer. Naming it and
 * showing what the card will say about itself is a better one.
 *
 * Selecting the application is the test, rather than looking for it in the
 * application list, because a card is free to keep that list behind its master
 * key while still answering a select.
 */
static FlipsoReaderStatus flipso_read_other_card(FlipsoReader* reader, MfDesfirePoller* poller) {
    if(mf_desfire_poller_select_application(poller, &flipso_oyster_aid) != MfDesfireErrorNone) {
        return FlipsoReaderStatusNotItso;
    }

    FURI_LOG_D(TAG, "Oyster application present");
    flipso_media_reset(reader->media);
    flipso_read_media_picc(reader, poller);
    /* The select above proved it is there, whatever the listing said. */
    flipso_media_add_app(reader->media, FLIPSO_AID_OYSTER);
    flipso_read_media_app(reader, poller, &flipso_oyster_aid, FLIPSO_AID_OYSTER);

    return FlipsoReaderStatusOyster;
}

/** The whole read sequence, run on the NFC worker thread once a card responds. */
static FlipsoReaderStatus flipso_read_card(FlipsoReader* reader, MfDesfirePoller* poller) {
    ItsoCard* card = reader->card;
    itso_card_reset(card);

    MfDesfireError error = mf_desfire_poller_select_application(poller, &flipso_itso_aid);
    if(error != MfDesfireErrorNone) {
        /* The card answering "no such application" and the card having left the
         * field are different problems with different advice, and only the first
         * of them is worth trying the next transport for. */
        if(error == MfDesfireErrorNotPresent || error == MfDesfireErrorTimeout) {
            FURI_LOG_D(TAG, "Card left the field during select");
            return FlipsoReaderStatusCardError;
        }
        FURI_LOG_D(TAG, "No ITSO application on this card");
        return flipso_read_other_card(reader, poller);
    }

    if(!flipso_read_shell(reader, poller)) {
        FURI_LOG_W(TAG, "ITSO application present but no readable shell");
        return FlipsoReaderStatusBadShell;
    }

    flipso_log_shell_owner(card);

    /* The shell says how many sectors exist; that fixes where the directory is
     * and which direction sector numbers run relative to the shell's own file. */
    reader->descending = (reader->shell_fid + 1) >= card->sector_count;

    uint8_t dir_fid = flipso_sector_to_fid(reader, card->sector_count - 1);
    reader->dir_len = flipso_read_file(poller, dir_fid, reader->dir, FLIPSO_DIR_BUF);
    if(reader->dir_len == 0 || !itso_parse_directory(card, reader->dir, reader->dir_len)) {
        FURI_LOG_W(TAG, "Directory read or parse failed (file %u)", dir_fid);
        /* The shell alone still gives the card number and expiry, so report
         * success and let the UI show what we have. */
        return FlipsoReaderStatusSuccess;
    }

    for(uint8_t i = 0; i < card->product_count; i++) {
        ItsoProduct* product = &card->products[i];
        size_t len = flipso_read_product_group(reader, poller, product->dir_index);
        if(len) itso_parse_ipe(product, reader->group, len, card->sector_size);

        FURI_LOG_D(
            TAG,
            "E%u: TYP %u.%u, %u bytes, rev %u, bitmap 0x%02X",
            product->dir_index,
            product->typ,
            product->ptyp,
            (unsigned)len,
            product->format_rev,
            product->bitmap);
    }

    if(card->log_dir_index) {
        uint8_t log_fid = flipso_sector_to_fid(reader, card->sector_count - 2);
        size_t len = flipso_read_file(poller, log_fid, reader->log, FLIPSO_LOG_BUF);
        if(len) itso_parse_log(card, reader->log, len);
    }

    return FlipsoReaderStatusSuccess;
}

/* ------------------------------------------------------------------ */
/* Poller callbacks                                                    */
/* ------------------------------------------------------------------ */

/**
 * DESFire transport. Started in extended mode, so the events arriving here come
 * from the parent ISO14443-4A poller, which has already activated the card by
 * the time it reports Ready.
 */
static NfcCommand flipso_desfire_callback(NfcGenericEventEx event, void* context) {
    FlipsoReader* reader = context;
    const Iso14443_4aPollerEvent* iso_event = event.parent_event_data;

    if(iso_event->type != Iso14443_4aPollerEventTypeReady) {
        /* Activation failed: most likely nothing is in the field yet, or the card
         * was pulled away. Keep polling so the user can simply try again. */
        return NfcCommandContinue;
    }

    reader->status = flipso_read_card(reader, event.poller);
    reader->callback(reader->status, reader->context);
    return NfcCommandStop;
}

/**
 * ISO 7816 transport. Started in plain mode rather than extended, so that the
 * ISO14443-4A poller runs its own activation: as well as sending RATS, that is
 * what takes the frame waiting time from the card's ATS. Driving activation by
 * hand leaves the poller timing out every APDU after a few milliseconds.
 */
static NfcCommand flipso_iso7816_callback(NfcGenericEvent event, void* context) {
    FlipsoReader* reader = context;
    furi_assert(event.protocol == NfcProtocolIso14443_4a);

    const Iso14443_4aPollerEvent* iso_event = event.event_data;
    if(iso_event->type != Iso14443_4aPollerEventTypeReady) return NfcCommandContinue;

    reader->status = flipso_cmd2_read(reader->cmd2, event.instance, reader->card);
    reader->callback(reader->status, reader->context);
    return NfcCommandStop;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

FlipsoReader* flipso_reader_alloc(void) {
    FlipsoReader* reader = malloc(sizeof(FlipsoReader));
    memset(reader, 0, sizeof(FlipsoReader));
    reader->nfc = nfc_alloc();
    reader->status = FlipsoReaderStatusIdle;
    reader->transport = FlipsoTransportDesfire;
    return reader;
}

void flipso_reader_free(FlipsoReader* reader) {
    furi_assert(reader);
    flipso_reader_stop(reader);
    if(reader->cmd2) flipso_cmd2_free(reader->cmd2);
    nfc_free(reader->nfc);
    free(reader);
}

bool flipso_reader_next_transport(FlipsoReader* reader) {
    furi_assert(reader);
    furi_assert(!reader->running);

    if(reader->transport + 1 < FlipsoTransportCount) {
        reader->transport++;
        return true;
    }

    reader->transport = FlipsoTransportDesfire;
    return false;
}

void flipso_reader_reset_transport(FlipsoReader* reader) {
    furi_assert(reader);
    furi_assert(!reader->running);
    reader->transport = FlipsoTransportDesfire;
}

void flipso_reader_start(
    FlipsoReader* reader,
    ItsoCard* card,
    FlipsoMedia* media,
    FlipsoReaderCallback callback,
    void* context) {
    furi_assert(reader);
    furi_assert(card);
    furi_assert(media);
    furi_assert(callback);
    if(reader->running) return;

    reader->card = card;
    reader->media = media;
    reader->callback = callback;
    reader->context = context;
    reader->status = FlipsoReaderStatusIdle;
    reader->shell_fid = FLIPSO_SHELL_FID_DEFAULT;
    reader->descending = true;

    if(reader->transport == FlipsoTransportIso7816) {
        /* Only cards that are not DESFire get this far, so the buffers it owns
         * are worth allocating late rather than for every read. */
        if(!reader->cmd2) reader->cmd2 = flipso_cmd2_alloc();
        reader->poller = nfc_poller_alloc(reader->nfc, NfcProtocolIso14443_4a);
        nfc_poller_start(reader->poller, flipso_iso7816_callback, reader);
    } else {
        reader->poller = nfc_poller_alloc(reader->nfc, NfcProtocolMfDesfire);
        nfc_poller_start_ex(reader->poller, flipso_desfire_callback, reader);
    }
    reader->running = true;
}

void flipso_reader_stop(FlipsoReader* reader) {
    furi_assert(reader);
    if(!reader->running) return;

    nfc_poller_stop(reader->poller);
    nfc_poller_free(reader->poller);
    reader->poller = NULL;
    reader->running = false;
}
