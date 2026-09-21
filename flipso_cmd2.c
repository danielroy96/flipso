/**
 * @file flipso_cmd2.c
 * @brief ISO 7816 transport for ITSO customer media definition 2.
 *
 * TS 1000-10 clause 3 puts the ITSO Application in a Dedicated File named by the
 * ITSO AID. Under it sits a read-only Parameter EF and a run of Storage Sector
 * DFs, one per logical sector, each holding a single Elementary File of B bytes.
 * Logical sector n lives in DF (0100 + n), EF 0001 - so unlike CMD7, where the
 * sector number maps onto a DESFire file number, here it maps onto a directory
 * path.
 *
 * Every read is unconditional: as on DESFire, ITSO seals the data for integrity
 * rather than encrypting it, so no keys are needed.
 *
 * Two things differ from the DESFire layout beyond addressing:
 *
 *   - CMD2 uses software anti-tear, so the last two sectors are two copies of
 *     the Directory rather than a Directory and a cyclic log. The copy with the
 *     newer sequence number is the live one.
 *   - The cyclic log therefore has no reserved sector. It is an ordinary data
 *     group whose starting sector is its Directory entry number, like a product.
 */
#include "flipso_cmd2.h"
#include "flipso_reader.h"

#include <furi.h>
#include <toolbox/bit_buffer.h>

#define TAG "Flipso"

/* ITSO's registered application identifier: RID A0 00 00 02 16 followed by the
 * PIX "ITSO-1" (TS 1000-10 clause 3.8). */
static const uint8_t flipso_cmd2_aid[] =
    {0xA0, 0x00, 0x00, 0x02, 0x16, 0x49, 0x54, 0x53, 0x4F, 0x2D, 0x31};

/* Storage Sector DFs are numbered sequentially from 0100 (TS 1000-10 3.7.3.1). */
#define FLIPSO_CMD2_DF_BASE      0x0100
#define FLIPSO_CMD2_SFI_DEFAULT  0x01 /* Storage EF short id, parameter tag C3. */
#define FLIPSO_CMD2_PARAM_SFI    0x0F /* Parameter EF, fixed by clause 3.7.2.1. */

#define FLIPSO_CMD2_APDU_MAX     32
#define FLIPSO_CMD2_RESP_MAX     264 /* Largest sector or directory, plus slack. */
#define FLIPSO_CMD2_PATH_MAX     8
#define FLIPSO_CMD2_DIR_MAX      256
#define FLIPSO_CMD2_MAX_CHAIN_HOPS 6

struct FlipsoCmd2 {
    BitBuffer* tx;
    BitBuffer* rx;

    uint8_t resp[FLIPSO_CMD2_RESP_MAX];
    size_t resp_len;
    uint16_t sw;
    /* Set when the card stopped answering rather than answering "no". The two
     * need different advice on screen, and only the first is worth retrying. */
    bool link_error;

    /* Scratch for the read itself, kept off the NFC worker's stack. */
    uint8_t dir[FLIPSO_CMD2_DIR_MAX];
    size_t dir_len;
    uint8_t group[ITSO_MAX_GROUP_LEN];

    /* Learned from the Parameter EF. */
    uint8_t sfi;
    uint8_t path[FLIPSO_CMD2_PATH_MAX];
    uint8_t path_len; /* 0 when the card does not support selection by path. */
    uint16_t df_fid;
    bool df_fid_known;
};

FlipsoCmd2* flipso_cmd2_alloc(void) {
    FlipsoCmd2* cmd2 = malloc(sizeof(FlipsoCmd2));
    memset(cmd2, 0, sizeof(FlipsoCmd2));
    cmd2->tx = bit_buffer_alloc(FLIPSO_CMD2_APDU_MAX);
    cmd2->rx = bit_buffer_alloc(FLIPSO_CMD2_RESP_MAX + 2);
    return cmd2;
}

void flipso_cmd2_free(FlipsoCmd2* cmd2) {
    furi_assert(cmd2);
    bit_buffer_free(cmd2->tx);
    bit_buffer_free(cmd2->rx);
    free(cmd2);
}

/* ------------------------------------------------------------------ */
/* APDU exchange                                                       */
/* ------------------------------------------------------------------ */

/** Send one APDU, splitting the response into data and status word. */
static bool flipso_cmd2_send(
    FlipsoCmd2* cmd2,
    Iso14443_4aPoller* poller,
    const uint8_t* apdu,
    size_t len) {
    bit_buffer_reset(cmd2->tx);
    bit_buffer_append_bytes(cmd2->tx, apdu, len);

    if(iso14443_4a_poller_send_block(poller, cmd2->tx, cmd2->rx) != Iso14443_4aErrorNone) {
        cmd2->link_error = true;
        return false;
    }

    size_t received = bit_buffer_get_size_bytes(cmd2->rx);
    if(received < 2) {
        cmd2->link_error = true;
        return false;
    }

    const uint8_t* data = bit_buffer_get_data(cmd2->rx);
    cmd2->sw = (uint16_t)((data[received - 2] << 8) | data[received - 1]);
    cmd2->resp_len = received - 2;
    if(cmd2->resp_len > sizeof(cmd2->resp)) cmd2->resp_len = sizeof(cmd2->resp);
    if(cmd2->resp_len) memcpy(cmd2->resp, data, cmd2->resp_len);
    return true;
}

/**
 * Send an APDU and follow up on the two status words that mean "ask again":
 * 61xx (response waiting) and 6Cxx (wrong Le). Everything else is left to the
 * caller to judge.
 *
 * @return true when the card answered 9000 with the data, if any, in resp.
 */
static bool flipso_cmd2_transceive(
    FlipsoCmd2* cmd2,
    Iso14443_4aPoller* poller,
    const uint8_t* apdu,
    size_t len) {
    if(!flipso_cmd2_send(cmd2, poller, apdu, len)) return false;

    /* Only rewrite the last byte when it really is an Le of zero: the fallback
     * select sends no Le at all, and patching that would corrupt the AID. */
    if((cmd2->sw >> 8) == 0x6C && len >= 5 && len <= FLIPSO_CMD2_APDU_MAX &&
       apdu[len - 1] == 0x00) {
        /* The card wants an exact Le, which it has just told us. */
        uint8_t retry[FLIPSO_CMD2_APDU_MAX];
        memcpy(retry, apdu, len);
        retry[len - 1] = (uint8_t)(cmd2->sw & 0xFF);
        if(!flipso_cmd2_send(cmd2, poller, retry, len)) return false;
    }

    if((cmd2->sw >> 8) == 0x61) {
        const uint8_t get_response[] = {0x00, 0xC0, 0x00, 0x00, (uint8_t)(cmd2->sw & 0xFF)};
        if(!flipso_cmd2_send(cmd2, poller, get_response, sizeof(get_response))) return false;
    }

    return cmd2->sw == 0x9000;
}

/* ------------------------------------------------------------------ */
/* Parameter EF                                                        */
/* ------------------------------------------------------------------ */

/**
 * Walk the BER-TLV objects of the Parameter EF (TS 1000-10 3.7.2.3) and keep the
 * two that decide how sectors are addressed: the storage EF short id, and either
 * a path to the ITSO DF or its file id.
 *
 * Every object here is primitive with a single-byte length, so a flat walk is
 * enough; anything longer or malformed just ends the walk.
 */
static void flipso_cmd2_parse_parameters(FlipsoCmd2* cmd2, const uint8_t* data, size_t len) {
    size_t offset = 0;
    while(offset + 2 <= len) {
        uint8_t tag = data[offset];
        uint8_t length = data[offset + 1];
        const uint8_t* value = data + offset + 2;
        if(offset + 2 + length > len) break;

        switch(tag) {
        case 0xC3: /* Storage EF short file id. */
            if(length == 1 && value[0] <= 0x1F) cmd2->sfi = value[0];
            break;
        case 0xC6: /* ITSO DF file id, when the card selects by FID. */
            if(length == 2) {
                cmd2->df_fid = (uint16_t)((value[0] << 8) | value[1]);
                cmd2->df_fid_known = true;
            }
            break;
        case 0xC7: /* Path to the ITSO DF, when the card selects by path. */
            if(length && length <= sizeof(cmd2->path)) {
                memcpy(cmd2->path, value, length);
                cmd2->path_len = length;
            }
            break;
        default:
            break;
        }

        offset += 2 + length;
    }
}

/**
 * Pull out one constructed or primitive object from the FCI returned by the
 * application select. Only the shallow nesting the spec defines is handled:
 * 6F wraps 84 (DF name) and A5, and A5 wraps C0 (shell) and E0 (parameters).
 *
 * @return pointer to the value, or NULL; @p out_len receives its length.
 */
static const uint8_t*
    flipso_cmd2_fci_find(const uint8_t* data, size_t len, uint8_t tag, size_t* out_len) {
    size_t offset = 0;
    while(offset + 2 <= len) {
        uint8_t current = data[offset];
        uint8_t length = data[offset + 1];
        const uint8_t* value = data + offset + 2;
        if(offset + 2 + length > len) return NULL;

        if(current == tag) {
            *out_len = length;
            return value;
        }

        /* Descend into the two constructed templates the FCI may use. */
        if(current == 0x6F || current == 0xA5) {
            const uint8_t* found = flipso_cmd2_fci_find(value, length, tag, out_len);
            if(found) return found;
        }

        offset += 2 + length;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Sector access                                                       */
/* ------------------------------------------------------------------ */

/** Select the ITSO Application by AID. */
static bool flipso_cmd2_select_application(FlipsoCmd2* cmd2, Iso14443_4aPoller* poller) {
    uint8_t apdu[5 + sizeof(flipso_cmd2_aid) + 1] = {
        0x00, 0xA4, 0x04, 0x00, sizeof(flipso_cmd2_aid)};
    memcpy(apdu + 5, flipso_cmd2_aid, sizeof(flipso_cmd2_aid));
    apdu[sizeof(apdu) - 1] = 0x00; /* Le: return the FCI. */

    if(flipso_cmd2_transceive(cmd2, poller, apdu, sizeof(apdu))) return true;

    /* Platforms that reject a case 4 APDU still answer without the Le byte. */
    return flipso_cmd2_transceive(cmd2, poller, apdu, sizeof(apdu) - 1);
}

/**
 * Select the Storage Sector DF holding logical sector @p sector.
 *
 * Selection by path takes one command. Without it the card has to be walked back
 * to the ITSO DF first, because a Storage Sector DF is a sibling rather than a
 * child of the one currently selected (TS 1000-10 3.11.1.2).
 */
static bool
    flipso_cmd2_select_sector(FlipsoCmd2* cmd2, Iso14443_4aPoller* poller, uint8_t sector) {
    if(cmd2->path_len) {
        uint8_t apdu[6 + FLIPSO_CMD2_PATH_MAX + 2];
        uint8_t data_len = (uint8_t)(cmd2->path_len + 2);
        apdu[0] = 0x00;
        apdu[1] = 0xA4;
        apdu[2] = 0x08; /* Select by path. */
        apdu[3] = 0x00;
        apdu[4] = data_len;
        memcpy(apdu + 5, cmd2->path, cmd2->path_len);
        apdu[5 + cmd2->path_len] = FLIPSO_CMD2_DF_BASE >> 8;
        apdu[6 + cmd2->path_len] = sector;
        apdu[7 + cmd2->path_len] = 0x00; /* Le */
        if(flipso_cmd2_transceive(cmd2, poller, apdu, 8u + cmd2->path_len)) return true;
        /* Fall through and try by file id: the path may have been rejected. */
    }

    if(cmd2->df_fid_known) {
        const uint8_t to_root[] = {
            0x00, 0xA4, 0x00, 0x00, 0x02, (uint8_t)(cmd2->df_fid >> 8),
            (uint8_t)cmd2->df_fid, 0x00};
        if(!flipso_cmd2_transceive(cmd2, poller, to_root, sizeof(to_root))) return false;
    } else if(!flipso_cmd2_select_application(cmd2, poller)) {
        return false;
    }

    const uint8_t apdu[] = {
        0x00, 0xA4, 0x00, 0x00, 0x02, FLIPSO_CMD2_DF_BASE >> 8, sector, 0x00};
    return flipso_cmd2_transceive(cmd2, poller, apdu, sizeof(apdu));
}

/**
 * Read a whole logical sector into @p out.
 * @return number of bytes read, or 0 on any failure.
 */
static size_t flipso_cmd2_read_sector(
    FlipsoCmd2* cmd2,
    Iso14443_4aPoller* poller,
    uint8_t sector,
    uint8_t* out,
    size_t capacity) {
    if(!flipso_cmd2_select_sector(cmd2, poller, sector)) return 0;

    /* Le of zero asks for everything the EF holds, so the sector size does not
     * have to be known before the shell has been read. */
    const uint8_t apdu[] = {0x00, 0xB0, (uint8_t)(0x80 | cmd2->sfi), 0x00, 0x00};
    if(!flipso_cmd2_transceive(cmd2, poller, apdu, sizeof(apdu))) return 0;

    size_t len = cmd2->resp_len;
    if(len > capacity) len = capacity;
    if(len) memcpy(out, cmd2->resp, len);
    return len;
}

/* ------------------------------------------------------------------ */
/* The read sequence                                                   */
/* ------------------------------------------------------------------ */

/** Byte offset of the Directory sequence number, which follows the SCT. */
static size_t flipso_cmd2_dir_sequence_offset(const ItsoCard* card) {
    return (size_t)2 + (size_t)card->dir_entries * 5 + card->sct_len;
}

/**
 * Read the live Directory.
 *
 * Software anti-tear keeps two copies, in sectors S-2 and S-1, and the one with
 * the higher sequence number is current. TS 1000-2 5.1.6 defines that comparison
 * as wrapping, so FF followed by 00 means 00 is the newer of the two.
 */
static bool
    flipso_cmd2_read_directory(FlipsoCmd2* cmd2, Iso14443_4aPoller* poller, ItsoCard* card) {
    const uint8_t copy_a = (uint8_t)(card->sector_count - 2);
    const uint8_t copy_b = (uint8_t)(card->sector_count - 1);
    const size_t sequence_offset = flipso_cmd2_dir_sequence_offset(card);

    size_t len_a =
        flipso_cmd2_read_sector(cmd2, poller, copy_a, cmd2->dir, sizeof(cmd2->dir));
    bool a_usable = len_a > sequence_offset;
    uint8_t sequence_a = a_usable ? cmd2->dir[sequence_offset] : 0;

    /* Try copy B second so that the common case - B newer, as anti-tear leaves
     * it after an odd number of updates - needs no third read. */
    size_t len_b =
        flipso_cmd2_read_sector(cmd2, poller, copy_b, cmd2->dir, sizeof(cmd2->dir));
    bool b_usable = len_b > sequence_offset;
    uint8_t sequence_b = b_usable ? cmd2->dir[sequence_offset] : 0;

    bool use_b = b_usable && (!a_usable || (uint8_t)(sequence_b - sequence_a) < 0x80);

    if(use_b) {
        cmd2->dir_len = len_b;
    } else if(a_usable) {
        cmd2->dir_len = flipso_cmd2_read_sector(cmd2, poller, copy_a, cmd2->dir, sizeof(cmd2->dir));
    } else {
        cmd2->dir_len = len_b; /* Neither looked complete; use whatever we have. */
    }

    FURI_LOG_D(
        TAG,
        "CMD2 directory: copy A seq %u (%u bytes), copy B seq %u (%u bytes), using %c",
        sequence_a,
        (unsigned)len_a,
        sequence_b,
        (unsigned)len_b,
        use_b ? 'B' : 'A');

    if(cmd2->dir_len == 0) return false;
    if(itso_parse_directory(card, cmd2->dir, cmd2->dir_len)) return true;

    /* The copy we picked did not decode; the other one may still be intact. */
    uint8_t other = use_b ? copy_a : copy_b;
    cmd2->dir_len = flipso_cmd2_read_sector(cmd2, poller, other, cmd2->dir, sizeof(cmd2->dir));
    return cmd2->dir_len && itso_parse_directory(card, cmd2->dir, cmd2->dir_len);
}

/** Follow one data group's sector chain, concatenating the sectors it occupies. */
static size_t flipso_cmd2_read_group(
    FlipsoCmd2* cmd2,
    Iso14443_4aPoller* poller,
    const ItsoCard* card,
    uint8_t start_sector) {
    size_t total = 0;
    uint8_t sector = start_sector;

    for(uint8_t hop = 0; hop < FLIPSO_CMD2_MAX_CHAIN_HOPS; hop++) {
        if(sector == 0 || sector >= card->sector_count) break;
        if(total + card->sector_size > ITSO_MAX_GROUP_LEN) break;

        size_t read = flipso_cmd2_read_sector(
            cmd2, poller, sector, cmd2->group + total, ITSO_MAX_GROUP_LEN - total);
        if(read == 0) break;
        total += read;

        uint8_t next = itso_sct_entry(card, cmd2->dir, cmd2->dir_len, sector);
        /* Terminators: itself (unused), S-2 (blocked) or S-1 (in use). */
        if(next == sector || next == 0 || next == card->sector_count - 2 ||
           next == card->sector_count - 1) {
            break;
        }
        sector = next;
    }

    return total;
}

FlipsoReaderStatus flipso_cmd2_read(
    FlipsoCmd2* cmd2,
    Iso14443_4aPoller* poller,
    ItsoCard* card,
    FlipsoCapture* capture) {
    furi_assert(cmd2);
    furi_assert(poller);
    furi_assert(card);
    furi_assert(capture);

    itso_card_reset(card);
    flipso_capture_reset(capture);
    cmd2->sfi = FLIPSO_CMD2_SFI_DEFAULT;
    cmd2->path_len = 0;
    cmd2->df_fid_known = false;
    cmd2->dir_len = 0;
    cmd2->link_error = false;

    if(!flipso_cmd2_select_application(cmd2, poller)) {
        if(cmd2->link_error) {
            FURI_LOG_D(TAG, "Card left the field during select (CMD2)");
            return FlipsoReaderStatusCardError;
        }
        FURI_LOG_D(TAG, "No ITSO application on this card (CMD2)");
        return FlipsoReaderStatusNotItso;
    }
    /* From here on the application is known to be there, so a link error means
     * the card went away rather than that we asked it the wrong thing. */
    cmd2->link_error = false;

    /* The File Control Information often carries the Parameter EF, and with it
     * the whole Shell Environment, inline (TS 1000-10 3.8.3.4.2). When it does,
     * the card is readable without a single extra command. */
    size_t fci_len = 0;
    const uint8_t* parameters =
        flipso_cmd2_fci_find(cmd2->resp, cmd2->resp_len, 0xE0, &fci_len);
    if(parameters) flipso_cmd2_parse_parameters(cmd2, parameters, fci_len);

    size_t shell_len = 0;
    const uint8_t* shell = flipso_cmd2_fci_find(cmd2->resp, cmd2->resp_len, 0xC0, &shell_len);
    bool shell_ok = shell && itso_looks_like_shell(shell, shell_len) &&
                    itso_parse_shell(card, shell, shell_len);
    if(shell_ok) flipso_capture_add(capture, FlipsoBlockShell, 0, shell, shell_len);

    if(!parameters) {
        /* Ask for the Parameter EF directly: short id 0F, implicit selection. */
        const uint8_t apdu[] = {0x00, 0xB0, 0x80 | FLIPSO_CMD2_PARAM_SFI, 0x00, 0x00};
        if(flipso_cmd2_transceive(cmd2, poller, apdu, sizeof(apdu))) {
            flipso_cmd2_parse_parameters(cmd2, cmd2->resp, cmd2->resp_len);
        }
    }

    if(!shell_ok) {
        /* The Shell Environment is the first storage sector. */
        uint8_t buffer[64];
        size_t len = flipso_cmd2_read_sector(cmd2, poller, 0, buffer, sizeof(buffer));
        shell_ok = len && itso_looks_like_shell(buffer, len) && itso_parse_shell(card, buffer, len);
        if(shell_ok) flipso_capture_add(capture, FlipsoBlockShell, 0, buffer, len);
    }

    if(!shell_ok) {
        if(cmd2->link_error) {
            FURI_LOG_D(TAG, "Card left the field before the shell was read (CMD2)");
            return FlipsoReaderStatusCardError;
        }
        FURI_LOG_W(TAG, "ITSO application present but no readable shell (CMD2)");
        return FlipsoReaderStatusBadShell;
    }

    flipso_log_shell_owner(card);

    FURI_LOG_D(
        TAG,
        "CMD2 shell: FVC %u, B=%u S=%u E=%u SCTL=%u, SFI %u, path %u bytes",
        card->fvc,
        card->sector_size,
        card->sector_count,
        card->dir_entries,
        card->sct_len,
        cmd2->sfi,
        cmd2->path_len);

    if(card->sector_count < 4 || card->sector_size == 0) {
        FURI_LOG_W(TAG, "CMD2 geometry unusable");
        return FlipsoReaderStatusBadShell;
    }

    if(!flipso_cmd2_read_directory(cmd2, poller, card)) {
        /* Same distinction the shell read above makes: a directory that would
         * not parse is a card we cannot decode, a directory that never arrived
         * is a card that went away, and only the second is worth tapping again
         * for. Reported rather than shrugged off, because carrying on would
         * finish with a card that looks read and has nothing on it. */
        if(cmd2->link_error) {
            FURI_LOG_W(TAG, "Card left the field reading the directory (CMD2)");
            return FlipsoReaderStatusCardLost;
        }
        FURI_LOG_W(TAG, "CMD2 directory read or parse failed");
        /* The shell alone still gives the card number and expiry, so report
         * success and let the UI show what we have. Nothing is captured: unlike
         * CMD7 there is no single directory file here, and which of the two
         * copies cmd2->dir ended up holding is not something a saved card
         * should be built on. */
        return FlipsoReaderStatusSuccess;
    }
    flipso_capture_add(capture, FlipsoBlockDirectory, 0, cmd2->dir, cmd2->dir_len);

    for(uint8_t i = 0; i < card->product_count; i++) {
        ItsoProduct* product = &card->products[i];
        size_t len = flipso_cmd2_read_group(cmd2, poller, card, product->dir_index);
        if(len && !cmd2->link_error) {
            flipso_capture_add(
                capture, FlipsoBlockProduct, product->dir_index, cmd2->group, len);
            itso_parse_ipe(product, cmd2->group, len, card->sector_size);
        }

        FURI_LOG_D(
            TAG,
            "E%u: TYP %u.%u, %u bytes, rev %u, bitmap 0x%02X",
            product->dir_index,
            product->typ,
            product->ptyp,
            (unsigned)len,
            product->format_rev,
            product->bitmap);

        if(cmd2->link_error) {
            FURI_LOG_W(
                TAG, "Card left the field at entry %u of %u (CMD2)", i + 1, card->product_count);
            return FlipsoReaderStatusCardLost;
        }
    }

    /* CMD2 reserves no sector for the cyclic log: it is an ordinary data group
     * starting at the sector its Directory entry names, exactly like a product. */
    if(card->log_dir_index) {
        size_t len = flipso_cmd2_read_group(cmd2, poller, card, card->log_dir_index);
        if(cmd2->link_error) {
            FURI_LOG_W(TAG, "Card left the field reading the journey log (CMD2)");
            return FlipsoReaderStatusCardLost;
        }
        if(len) {
            flipso_capture_add(capture, FlipsoBlockLog, 0, cmd2->group, len);
            itso_parse_log(card, cmd2->group, len);
        }
    }

    return FlipsoReaderStatusSuccess;
}
