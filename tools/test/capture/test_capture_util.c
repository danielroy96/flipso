/**
 * @file test_capture_util.c
 * @brief The synthetic card as a read captures it, and the helpers the capture tests share.
 */
#include "test_capture.h"

/* The five product groups of the synthetic CMD7 card, assembled the way the
 * reader assembles them: each chained sector appended at a sector boundary. */
uint8_t group1[128], group2[64], group3[128], group5[128];
uint8_t group4[sizeof(card_sector4) + sizeof(card_sector10)];

void build_groups(void) {
    memcpy(group1, card_sector1, sizeof(card_sector1));
    memcpy(group1 + 64, card_sector9, sizeof(card_sector9));

    memcpy(group2, card_sector2, sizeof(card_sector2));

    memcpy(group3, card_sector3, sizeof(card_sector3));
    memcpy(group3 + 64, card_sector11, sizeof(card_sector11));

    memcpy(group4, card_sector4, sizeof(card_sector4));
    memcpy(group4 + sizeof(card_sector4), card_sector10, sizeof(card_sector10));

    memcpy(group5, card_sector5, sizeof(card_sector5));
    memcpy(group5 + 64, card_sector12, sizeof(card_sector12));
}

Group groups[5];

/** Decode the card the way flipso_desfire_read() does, with no capture involved. */
void reference_decode(ItsoCard* card) {
    itso_card_reset(card);
    itso_parse_shell(card, card_shell, sizeof(card_shell));
    itso_parse_directory(card, card_dir, sizeof(card_dir));
    for(uint8_t i = 0; i < card->product_count && i < 5; i++) {
        itso_parse_ipe(&card->products[i], groups[i].data, groups[i].len, card->sector_size);
    }
    itso_parse_log(card, card_log, sizeof(card_log));
}

/** Fill a capture with the same blocks that read would have produced. */
void fill(FlipsoCapture* capture, const ItsoCard* reference) {
    flipso_capture_add(capture, FlipsoBlockShell, 0, card_shell, sizeof(card_shell));
    flipso_capture_add(capture, FlipsoBlockDirectory, 0, card_dir, sizeof(card_dir));
    for(uint8_t i = 0; i < reference->product_count && i < 5; i++) {
        flipso_capture_add(
            capture,
            FlipsoBlockProduct,
            reference->products[i].dir_index,
            groups[i].data,
            groups[i].len);
    }
    flipso_capture_add(capture, FlipsoBlockLog, 0, card_log, sizeof(card_log));
    flipso_capture_set_time(capture, 1758400000u);
}

/** Render a capture to the lines of a saved file. Caller frees. */
char** to_lines(const FlipsoCapture* capture, size_t* count) {
    *count = flipso_capture_lines(capture);
    char** lines = malloc(sizeof(char*) * *count);
    for(size_t i = 0; i < *count; i++) {
        lines[i] = malloc(FLIPSO_CAPTURE_LINE_MAX);
        if(!flipso_capture_line(capture, i, lines[i], FLIPSO_CAPTURE_LINE_MAX)) {
            check("every line fits FLIPSO_CAPTURE_LINE_MAX", 0);
            lines[i][0] = '\0';
        }
    }
    return lines;
}

void free_lines(char** lines, size_t count) {
    for(size_t i = 0; i < count; i++) {
        free(lines[i]);
    }
    free(lines);
}

/** A copy of tap record @p from, restamped, which is a different journey. */
void restamp_tap(uint8_t* out, const uint8_t* from, uint32_t dts) {
    memcpy(out, from, ITSO_TAP_RECORD_LEN);
    /* DateTimeStamp is 24 bits at bit 32, so bytes 4 to 6. */
    out[4] = (uint8_t)(dts >> 16);
    out[5] = (uint8_t)(dts >> 8);
    out[6] = (uint8_t)dts;
}

/** A copy of value record @p from with a new TS#, timestamp and balance. */
void restamp_value(uint8_t* out, const uint8_t* from, uint16_t ts, uint32_t dts, int16_t amount) {
    memcpy(out, from, ITSO_VALUE_RECORD_LEN);
    /* TS# is 12 bits at bit 4 and the DTS 24 bits at bit 16; a purse keeps its
     * balance in the two bytes at 10 (TS 1000-5 table 4). */
    out[0] = (uint8_t)((out[0] & 0xF0) | ((ts >> 8) & 0x0F));
    out[1] = (uint8_t)ts;
    out[2] = (uint8_t)(dts >> 16);
    out[3] = (uint8_t)(dts >> 8);
    out[4] = (uint8_t)dts;
    out[10] = (uint8_t)((uint16_t)amount >> 8);
    out[11] = (uint8_t)amount;
}

/** True when a decoded card holds a journey stamped @p dts. */
bool holds_tap(const ItsoCard* card, uint32_t dts) {
    for(uint8_t i = 0; i < card->tap_count; i++) {
        if(card->taps[i].dts == dts) return true;
    }
    return false;
}

/**
 * Fill a capture with the card as it is "now": every block as the read found
 * it, except that entry 1 and the log are whatever the caller has rewritten.
 */
void fill_now(
    FlipsoCapture* capture,
    const ItsoCard* reference,
    const uint8_t* dir,
    size_t dir_len,
    const uint8_t* group_one,
    size_t group_one_len,
    const uint8_t* log,
    size_t log_len) {
    flipso_capture_add(capture, FlipsoBlockShell, 0, card_shell, sizeof(card_shell));
    flipso_capture_add(capture, FlipsoBlockDirectory, 0, dir, dir_len);
    flipso_capture_add(capture, FlipsoBlockProduct, 1, group_one, group_one_len);
    for(uint8_t i = 1; i < reference->product_count && i < 5; i++) {
        flipso_capture_add(
            capture,
            FlipsoBlockProduct,
            reference->products[i].dir_index,
            groups[i].data,
            groups[i].len);
    }
    flipso_capture_add(capture, FlipsoBlockLog, 0, log, log_len);
}

/** The blocks flipso_type2.c keeps for a full-shell card. */
void capture_full(FlipsoCapture* capture, const uint8_t* pages, size_t len) {
    static ItsoCard scratch;
    check("a whole CMD9 is kept", flipso_capture_add_type2_full(capture, &scratch, pages, len));
}
