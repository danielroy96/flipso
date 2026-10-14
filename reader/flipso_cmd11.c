/**
 * @file flipso_cmd11.c
 * @brief Transport for ITSO customer media definition 11: see flipso_cmd11.h.
 *
 * Every object this reads is described in TS 1000-10 clause 12.4: the FCI's
 * tags in table 113 and figures 9 and 10, and the three read commands in
 * clauses 12.4.8 to 12.4.10. A card read before authentication sends its IPE
 * and value group InstanceIDs and seals as 0xFF, so as not to be traced by
 * them; the decoder says so rather than naming the ISAM they appear to be.
 *
 * Like CMD9 and CMD10, the card is decoded from the blocks it is saved as, so a
 * live read and the saved file show the same card by construction.
 */
#include "flipso_cmd11.h"
#include "flipso_cmd2_i.h"
#include "flipso_transport.h"

#include <furi.h>

#define TAG "Flipso"

/* FCI tags (TS 1000-10 table 113). */
#define FLIPSO_CMD11_TAG_DIR        0xC2
#define FLIPSO_CMD11_TAG_OID_LIST   0xC3
#define FLIPSO_CMD11_TAG_PSEUDO_MID 0xC6

/* The read commands (table 112): CLA 90, the IPE's directory entry in P1. */
#define FLIPSO_CMD11_CLA      0x90
#define FLIPSO_CMD11_READ_IPE 0x12
#define FLIPSO_CMD11_READ_VRG 0x14
#define FLIPSO_CMD11_READ_LOG 0x16

/* ReadLog's P1 is a record count, and five is every record (clause 12.4.10). */
#define FLIPSO_CMD11_LOG_RECORDS 5

/** Send one of the read commands: no data, and Le 00 for all of it. */
static bool flipso_cmd11_read_command(
    FlipsoCmd2* cmd2,
    Iso14443_4aPoller* poller,
    uint8_t ins,
    uint8_t p1) {
    const uint8_t apdu[] = {FLIPSO_CMD11_CLA, ins, p1, 0x00, 0x00};
    return flipso_cmd2_transceive(cmd2, poller, apdu, sizeof(apdu));
}

/**
 * Read one product's IPE and, when VGP says it has one, its value group, into
 * the group buffer, the value group straight after the IPE's.
 *
 * @return the bytes gathered, 0 when the card would not give the IPE.
 */
static size_t flipso_cmd11_read_product(
    FlipsoCmd2* cmd2,
    Iso14443_4aPoller* poller,
    const ItsoProduct* product) {
    if(!flipso_cmd11_read_command(cmd2, poller, FLIPSO_CMD11_READ_IPE, product->dir_index)) {
        return 0;
    }

    /* The response opens with the IPE's directory entry, a copy of the one
     * the FCI's directory already gave, so it is dropped rather than kept. */
    const uint8_t* ipe = NULL;
    size_t len = itso_cmd11_ipe_group(cmd2->resp, cmd2->resp_len, &ipe);
    if(len > sizeof(cmd2->group)) len = sizeof(cmd2->group);
    if(len == 0) return 0;
    memcpy(cmd2->group, ipe, len);

    if(!product->value_group) return len;
    if(!flipso_cmd11_read_command(cmd2, poller, FLIPSO_CMD11_READ_VRG, product->dir_index)) {
        /* The IPE without its records, as a chain that ended early leaves it. */
        return len;
    }
    const uint8_t* values = NULL;
    size_t values_len = itso_cmd11_value_group(cmd2->resp, cmd2->resp_len, &values);
    if(values_len > sizeof(cmd2->group) - len) values_len = sizeof(cmd2->group) - len;
    if(values_len) memcpy(cmd2->group + len, values, values_len);
    return len + values_len;
}

FlipsoReaderStatus flipso_cmd11_read(
    FlipsoCmd2* cmd2,
    Iso14443_4aPoller* poller,
    ItsoCard* card,
    FlipsoCapture* capture) {
    /* Everything wanted from the FCI is taken before the next command, which
     * reuses the buffer it is in. */
    size_t len = 0;
    const uint8_t* mid =
        itso_fci_find(cmd2->resp, cmd2->resp_len, FLIPSO_CMD11_TAG_PSEUDO_MID, &len);
    if(mid) {
        /* A card using random MIDs (clause 12.4.3): the shell is the operator's
         * pseudo-shell, and this card's own is given only to a reader that has
         * authenticated. What follows is read all the same; the capture keeps
         * the pseudo-MID so that nothing takes the shared number for this
         * card's. */
        flipso_capture_add(capture, FlipsoBlockPseudoMid, 0, mid, len);
    }

    const uint8_t* oids =
        itso_fci_find(cmd2->resp, cmd2->resp_len, FLIPSO_CMD11_TAG_OID_LIST, &len);
    if(oids) flipso_capture_add(capture, FlipsoBlockOidList, 0, oids, len);

    const uint8_t* dir = itso_fci_find(cmd2->resp, cmd2->resp_len, FLIPSO_CMD11_TAG_DIR, &len);
    cmd2->dir_len = 0;
    if(dir && len <= sizeof(cmd2->dir)) {
        memcpy(cmd2->dir, dir, len);
        cmd2->dir_len = len;
    }

    if(!cmd2->dir_len || !itso_parse_directory(card, cmd2->dir, cmd2->dir_len)) {
        /* Figure 10 leaves the directory out of a random-MID card's FCI, where
         * the text of clause 12.4.3 puts it in, so a card without one is not
         * necessarily faulty. Either way the shell still says who issued it
         * and until when, so that is shown. */
        FURI_LOG_W(TAG, "CMD11: no directory in the FCI");
        return flipso_capture_decode(capture, card) ? FlipsoReaderStatusSuccess :
                                                      FlipsoReaderStatusBadShell;
    }
    flipso_capture_add(capture, FlipsoBlockDirectory, 0, cmd2->dir, cmd2->dir_len);

    for(uint8_t i = 0; i < card->product_count; i++) {
        const ItsoProduct* product = &card->products[i];
        size_t got = flipso_cmd11_read_product(cmd2, poller, product);
        /* A product the card refused is left as its directory entry, as one
         * whose sectors would not read is; one the card left the field during
         * stops the read, for the reason flipso_transport_read_groups() gives. */
        if(cmd2->link_error) {
            FURI_LOG_W(TAG, "Card left the field at entry %u of %u", i + 1, card->product_count);
            return FlipsoReaderStatusCardLost;
        }
        if(got)
            flipso_capture_add(capture, FlipsoBlockProduct, product->dir_index, cmd2->group, got);
    }

    if(card->log_dir_index) {
        bool read = flipso_cmd11_read_command(
            cmd2, poller, FLIPSO_CMD11_READ_LOG, FLIPSO_CMD11_LOG_RECORDS);
        if(cmd2->link_error) {
            FURI_LOG_W(TAG, "Card left the field reading the journey log");
            return FlipsoReaderStatusCardLost;
        }
        /* Five records, newest first; the decoder orders them by time anyway. */
        size_t log_len = cmd2->resp_len - cmd2->resp_len % ITSO_TAP_RECORD_LEN;
        if(read && log_len) flipso_capture_add(capture, FlipsoBlockLog, 0, cmd2->resp, log_len);
    }

    if(!flipso_capture_decode(capture, card)) return FlipsoReaderStatusBadShell;
    FURI_LOG_I(
        TAG,
        "CMD11 card: %u product(s), %u tap(s)%s",
        card->product_count,
        card->tap_count,
        card->shell_pseudo ? ", shared shell" : "");
    return FlipsoReaderStatusSuccess;
}
