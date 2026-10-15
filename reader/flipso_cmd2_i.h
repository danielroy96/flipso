/**
 * @file flipso_cmd2_i.h
 * @brief The CMD2 transport's context and APDU exchange, for the transport that
 * shares them: CMD11 answers the same SELECT (TS 1000-10 clause 12.4.6), so a
 * card is only known to be one once the CMD2 transport has its shell, and it
 * reads on with the same buffers rather than a second set.
 */
#pragma once

#include "flipso_cmd2.h"
#include "flipso_payment.h"

#include <toolbox/bit_buffer.h>

#define FLIPSO_CMD2_APDU_MAX 32
/* Largest response, plus slack: a CMD2 sector or directory, a CMD11 FCI (209
 * bytes at its largest geometry, table 114), a ReadIPE's entry, IPE and seal
 * (5 + 247, clause 12.4.11), or ReadLog's five records. */
#define FLIPSO_CMD2_RESP_MAX 264
#define FLIPSO_CMD2_PATH_MAX 8
#define FLIPSO_CMD2_DIR_MAX  256

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

    /** FlipsoPaymentScheme, set when the card turned out to be a bank card. */
    uint8_t payment;
};

/**
 * Send an APDU and follow up on the two status words that mean "ask again":
 * 61xx (response waiting) and 6Cxx (wrong Le). Everything else is left to the
 * caller to judge.
 *
 * @return true when the card answered 9000 with the data, if any, in resp.
 */
bool flipso_cmd2_transceive(
    FlipsoCmd2* cmd2,
    Iso14443_4aPoller* poller,
    const uint8_t* apdu,
    size_t len);
