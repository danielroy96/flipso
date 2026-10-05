/**
 * @file flipso_desfire.c
 * @brief The DESFire transport: ITSO customer media definitions 7 and 12.
 *
 * ITSO on DESFire (customer media definition 7, ITSO TS 1000-10 clause 8) stores
 * every data group in a file of the ITSO application, and every one of those
 * files is readable without authentication. So the whole read is: select the
 * application, pull the shell, pull the directory, then follow the Sector Chain
 * Table to just those files that actually hold a product. That is typically six
 * to ten short reads rather than a full card dump.
 *
 * A DESFire that carries no ITSO application is not necessarily a card we have
 * nothing to say about; flipso_desfire_media.c collects what one will say about
 * itself without a key.
 */
#include "flipso_desfire.h"
#include "flipso_transport.h"

#include <furi.h>
#include <lib/toolbox/simple_array.h>

#define TAG "Flipso"

/* ITSO's DESFire application identifier, sent least significant byte first. */
static const MfDesfireApplicationId flipso_itso_aid = {.data = {0x16, 0x02, 0xA0}};

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

struct FlipsoDesfire {
    /* What the read in progress decodes into, from flipso_desfire_read(). */
    ItsoCard* card;
    FlipsoMedia* media;
    FlipsoCapture* capture;

    /* Scratch buffers live here rather than on the NFC worker's stack. */
    uint8_t shell[FLIPSO_SHELL_BUF];
    uint8_t dir[FLIPSO_DIR_BUF];
    size_t dir_len;
    uint8_t group[ITSO_MAX_GROUP_LEN];
    uint8_t log[FLIPSO_LOG_BUF];

    /* Sector to file mapping derived from the shell (see flipso_sector_to_fid). */
    uint8_t shell_fid;
    bool descending;
    /** A read stopped because the card stopped answering, not because there was
     *  nothing to find. Reset per attempt in flipso_desfire_read(). */
    bool lost_card;
};

FlipsoDesfire* flipso_desfire_alloc(void) {
    FlipsoDesfire* desfire = malloc(sizeof(FlipsoDesfire));
    memset(desfire, 0, sizeof(FlipsoDesfire));
    return desfire;
}

void flipso_desfire_free(FlipsoDesfire* desfire) {
    furi_assert(desfire);
    free(desfire);
}

/**
 * Read a whole file into @p out.
 *
 * @param[out] error where the transport stopped, or MfDesfireErrorNone if it
 *             did not. May be NULL. A zero return says only that no bytes
 *             arrived, which covers both "there is no such file" and "the card
 *             left the field"; those want different answers from the caller,
 *             and the error code is the only thing that tells them apart. A
 *             short read - fewer bytes than the settings promised - reports
 *             MfDesfireErrorNone, because the card answered; the caller sees it
 *             as a length shorter than it asked for.
 * @return number of bytes read, or 0 on any failure.
 */
static size_t flipso_read_file(
    MfDesfirePoller* poller,
    MfDesfireFileId fid,
    uint8_t* out,
    size_t capacity,
    MfDesfireError* error) {
    if(error) *error = MfDesfireErrorNone;

    MfDesfireFileSettings settings = {0};
    MfDesfireError settings_error = mf_desfire_poller_read_file_settings(poller, fid, &settings);
    if(settings_error != MfDesfireErrorNone) {
        if(error) *error = settings_error;
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

    MfDesfireError data_error = mf_desfire_poller_read_file_data(poller, fid, 0, size, &data);
    if(data_error == MfDesfireErrorNone) {
        read = simple_array_get_count(data.data);
        if(read > capacity) read = capacity;
        if(read) memcpy(out, simple_array_cget_data(data.data), read);
        if(read < size) {
            FURI_LOG_W(
                TAG, "File %u: short read, %u of %u bytes", fid, (unsigned)read, (unsigned)size);
        }
    } else if(error) {
        *error = data_error;
    }

    simple_array_free(data.data);
    return read;
}

/** True if @p error means the card stopped answering rather than said no. */
static bool flipso_error_is_card_gone(MfDesfireError error) {
    return error == MfDesfireErrorNotPresent || error == MfDesfireErrorTimeout;
}

/**
 * flipso_read_file() for the ITSO read sequence, remembering a card that
 * stopped answering.
 *
 * Every step of the sequence treats a zero-length read as "nothing here and
 * carry on", which is right for a file the card does not have and wrong for a
 * file it simply did not live long enough to send. Only the error code tells
 * them apart, and only the caller can decide what to do about it, so this
 * records the distinction and leaves the length alone.
 */
static size_t flipso_desfire_read_file(
    FlipsoDesfire* desfire,
    MfDesfirePoller* poller,
    MfDesfireFileId fid,
    uint8_t* out,
    size_t capacity) {
    MfDesfireError error = MfDesfireErrorNone;
    size_t len = flipso_read_file(poller, fid, out, capacity, &error);
    if(flipso_error_is_card_gone(error)) {
        FURI_LOG_W(TAG, "Card left the field reading file %u", fid);
        desfire->lost_card = true;
    }
    return len;
}

/**
 * Map an ITSO logical sector number to a DESFire file number.
 *
 * TS 1000-10 table 66 lays CMD7 out back to front: logical sector 0 (the shell)
 * is file 15 and logical sector 15 (the directory) is file 0. Rather than hard
 * code that, we anchor on wherever the shell turned out to live.
 */
static uint8_t flipso_sector_to_fid(const FlipsoDesfire* desfire, uint8_t sector) {
    return desfire->descending ? (uint8_t)(desfire->shell_fid - sector) :
                                 (uint8_t)(desfire->shell_fid + sector);
}

/**
 * Decide what a shell-shaped file that would not parse tells us.
 *
 * Logged rather than acted on: which of these is the intermittent one seen on
 * the bench is not yet known, and a card that genuinely cannot be decoded must
 * keep getting a clear error rather than a retry loop.
 */
static void flipso_log_shell_rejected(const ItsoCard* card, uint8_t fid, size_t len) {
    FURI_LOG_W(
        TAG,
        "Shell in file %u rejected: %s (%u bytes, claims %u)",
        fid,
        itso_shell_reject_name(card->shell_reject),
        (unsigned)len,
        (unsigned)card->shell_len * ITSO_SHELL_BLOCK_LEN);
    /* The checksum separates the two explanations: bytes that are intact but
     * laid out in a way we do not understand, and bytes that did not survive
     * the read. Only the second is worth tapping again for. */
    if(card->secrc_checked) {
        FURI_LOG_W(
            TAG,
            "Shell checksum %s: stored %04X, computed %04X",
            card->secrc_valid ? "verifies" : "FAILS",
            card->secrc_stored,
            card->secrc_computed);
    } else {
        FURI_LOG_W(TAG, "Shell too short for its own checksum - read did not finish");
    }
}

/** Locate and decode the ITSO Shell Environment Data Group. */
static bool flipso_read_shell(FlipsoDesfire* desfire, MfDesfirePoller* poller) {
    /* Try the documented location first so the common case costs one read. */
    static const uint8_t preferred[] = {FLIPSO_SHELL_FID_DEFAULT, 0x00};

    for(size_t i = 0; i < COUNT_OF(preferred); i++) {
        size_t len = flipso_desfire_read_file(
            desfire, poller, preferred[i], desfire->shell, FLIPSO_SHELL_BUF);
        if(desfire->lost_card) return false;
        if(len && itso_looks_like_shell(desfire->shell, len)) {
            if(!itso_parse_shell(desfire->card, desfire->shell, len)) {
                flipso_log_shell_rejected(desfire->card, preferred[i], len);
                return false;
            }
            flipso_capture_add(desfire->capture, FlipsoBlockShell, 0, desfire->shell, len);
            desfire->shell_fid = preferred[i];
            return true;
        }
    }

    /* Unknown media definition: sweep the application for a shell-shaped file. */
    for(uint8_t fid = 0; fid <= FLIPSO_MAX_FID; fid++) {
        if(fid == FLIPSO_SHELL_FID_DEFAULT || fid == 0x00) continue;
        size_t len =
            flipso_desfire_read_file(desfire, poller, fid, desfire->shell, FLIPSO_SHELL_BUF);
        if(desfire->lost_card) return false;
        if(len && itso_looks_like_shell(desfire->shell, len)) {
            if(!itso_parse_shell(desfire->card, desfire->shell, len)) {
                flipso_log_shell_rejected(desfire->card, fid, len);
                return false;
            }
            flipso_capture_add(desfire->capture, FlipsoBlockShell, 0, desfire->shell, len);
            desfire->shell_fid = fid;
            return true;
        }
    }

    FURI_LOG_W(TAG, "No shell-shaped file anywhere in the ITSO application");
    return false;
}

/* One DESFire file per logical sector, for itso_read_chain(). A card that has
 * gone reads as nothing, so the walk stops there rather than carrying on with
 * the front of a product as though it were the whole of it. */
typedef struct {
    FlipsoDesfire* desfire;
    MfDesfirePoller* poller;
} FlipsoDesfireSource;

static size_t
    flipso_desfire_read_sector(void* context, uint8_t sector, uint8_t* out, size_t capacity) {
    FlipsoDesfireSource* source = context;
    FlipsoDesfire* desfire = source->desfire;
    size_t read = flipso_desfire_read_file(
        desfire, source->poller, flipso_sector_to_fid(desfire, sector), out, capacity);
    return desfire->lost_card ? 0 : read;
}

/* The DESFire side of a FlipsoGroupSource. */
static size_t flipso_desfire_read_group(void* context, uint8_t sector, const uint8_t** data) {
    FlipsoDesfireSource* source = context;
    FlipsoDesfire* desfire = source->desfire;
    *data = desfire->group;
    return itso_read_chain(
        desfire->card,
        desfire->dir,
        desfire->dir_len,
        sector,
        flipso_desfire_read_sector,
        source,
        desfire->group,
        ITSO_MAX_GROUP_LEN);
}

static size_t flipso_desfire_read_log(void* context, const uint8_t** data) {
    FlipsoDesfireSource* source = context;
    FlipsoDesfire* desfire = source->desfire;
    /* CMD7 reserves sector S-2 for the log: one cyclic file, not a chain. */
    uint8_t log_fid = flipso_sector_to_fid(desfire, desfire->card->sector_count - 2);
    *data = desfire->log;
    return flipso_desfire_read_file(
        desfire, source->poller, log_fid, desfire->log, FLIPSO_LOG_BUF);
}

static bool flipso_desfire_lost(void* context) {
    FlipsoDesfireSource* source = context;
    return source->desfire->lost_card;
}

FlipsoReaderStatus flipso_desfire_read(
    FlipsoDesfire* desfire,
    MfDesfirePoller* poller,
    ItsoCard* card,
    FlipsoMedia* media,
    FlipsoCapture* capture) {
    desfire->card = card;
    desfire->media = media;
    desfire->capture = capture;
    desfire->shell_fid = FLIPSO_SHELL_FID_DEFAULT;
    desfire->descending = true;
    itso_card_reset(card);
    /* What the chip says about itself is collected at the end of an ITSO read,
     * and instead of one for any other DESFire. The CMD2 transport leaves it
     * alone, so a card that DESFire could describe and CMD2 then turned out
     * not to hold ITSO either still has its description at the end. */
    flipso_media_reset(desfire->media);
    /* Each attempt starts clean: a transport that got half a card before losing
     * it must not leave those blocks for the next one to save. */
    flipso_capture_reset(desfire->capture);
    desfire->lost_card = false;

    MfDesfireError error = mf_desfire_poller_select_application(poller, &flipso_itso_aid);
    if(error != MfDesfireErrorNone) {
        /* The card answering "no such application" and the card having left the
         * field are different problems with different advice, and only the first
         * of them is worth trying the next transport for. */
        if(error == MfDesfireErrorNotPresent || error == MfDesfireErrorTimeout) {
            return FlipsoReaderStatusCardError;
        }
        return flipso_desfire_read_other(poller, desfire->media, desfire->capture);
    }

    if(!flipso_read_shell(desfire, poller)) {
        /* A shell that would not parse and a shell that never arrived are not
         * the same problem, and only the first is the card's. Measured on the
         * bench: a CMD7 SWR card dropped out reading file 15 and read cleanly
         * on the retry, having spent that tap on "Unreadable shell" - which is
         * a statement about the card, made on the strength of bytes that were
         * never seen. The select is already treated this way one step above. */
        if(desfire->lost_card) {
            FURI_LOG_W(TAG, "Shell read lost the card");
            return FlipsoReaderStatusCardLost;
        }
        FURI_LOG_W(TAG, "ITSO application present but no readable shell");
        return FlipsoReaderStatusBadShell;
    }

    flipso_log_shell_owner(card);

    /* The shell says how many sectors exist; that fixes where the directory is
     * and which direction sector numbers run relative to the shell's own file. */
    desfire->descending = (desfire->shell_fid + 1) >= card->sector_count;

    uint8_t dir_fid = flipso_sector_to_fid(desfire, card->sector_count - 1);
    desfire->dir_len =
        flipso_desfire_read_file(desfire, poller, dir_fid, desfire->dir, FLIPSO_DIR_BUF);
    if(desfire->lost_card) return FlipsoReaderStatusCardLost;
    /* Kept even when it will not parse: a saved card should hold what the card
     * said, so that a later build with a fix for it can be pointed at the file. */
    flipso_capture_add(desfire->capture, FlipsoBlockDirectory, 0, desfire->dir, desfire->dir_len);
    if(desfire->dir_len == 0 || !itso_parse_directory(card, desfire->dir, desfire->dir_len)) {
        FURI_LOG_W(TAG, "Directory read or parse failed (file %u)", dir_fid);
        /* The shell alone still gives the card number and expiry, so report
         * success and let the UI show what we have. */
        flipso_desfire_describe(poller, desfire->media, desfire->capture);
        return FlipsoReaderStatusSuccess;
    }

    FlipsoDesfireSource chain = {desfire, poller};
    const FlipsoGroupSource source = {
        .read_group = flipso_desfire_read_group,
        .read_log = flipso_desfire_read_log,
        .lost = flipso_desfire_lost,
        .context = &chain,
    };
    FlipsoReaderStatus status = flipso_transport_read_groups(card, desfire->capture, &source);
    if(status == FlipsoReaderStatusSuccess)
        flipso_desfire_describe(poller, desfire->media, desfire->capture);
    return status;
}
