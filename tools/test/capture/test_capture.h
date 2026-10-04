/**
 * @file test_capture.h
 * @brief What the capture test files share: the synthetic card as a read
 * captures it, the helpers that rewrite it, and each file's tests.
 */
#pragma once

#include "../test.h"
#include "cards/flipso_capture.h"
#include "itso.h"
#include "../card_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- test_capture_util.c --- */

/** One product's chained sectors, as the reader assembles them. */
typedef struct {
    const uint8_t* data;
    size_t len;
} Group;

/* The five product groups of the synthetic CMD7 card, assembled the way the
 * reader assembles them: each chained sector appended at a sector boundary. */
extern uint8_t group1[128], group2[64], group3[128], group5[128];
extern uint8_t group4[sizeof(card_sector4) + sizeof(card_sector10)];
/** The five groups in entry order, set up by main(). */
extern Group groups[5];

/** The purse's value records: after its IPE sector, past the two-byte header. */
#define GROUP1_VALUE_0 (64 + 2)
#define GROUP1_VALUE_1 (64 + 2 + ITSO_VALUE_RECORD_LEN)

/** Assemble group1 to group5 from the synthetic card's sectors. */
void build_groups(void);
/** Decode the card the way flipso_desfire_read() does, with no capture involved. */
void reference_decode(ItsoCard* card);
/** Fill a capture with the same blocks that read would have produced. */
void fill(FlipsoCapture* capture, const ItsoCard* reference);
/** Render a capture to the lines of a saved file. Caller frees. */
char** to_lines(const FlipsoCapture* capture, size_t* count);
void free_lines(char** lines, size_t count);
/** A copy of tap record @p from, restamped, which is a different journey. */
void restamp_tap(uint8_t* out, const uint8_t* from, ItsoDts dts);
/** A copy of value record @p from with a new TS#, timestamp and balance. */
void restamp_value(uint8_t* out, const uint8_t* from, uint16_t ts, ItsoDts dts, int16_t amount);
/** True when a decoded card holds a journey stamped @p dts. */
bool holds_tap(const ItsoCard* card, ItsoDts dts);
/** Fill a capture with the card as it is "now", entry 1 and the log rewritten. */
void fill_now(
    FlipsoCapture* capture,
    const ItsoCard* reference,
    const uint8_t* dir,
    size_t dir_len,
    const uint8_t* group_one,
    size_t group_one_len,
    const uint8_t* log,
    size_t log_len);
/** The blocks flipso_type2.c keeps for a full-shell card. */
void capture_full(FlipsoCapture* capture, const uint8_t* pages, size_t len);

/* --- Save and load: test_round_trip.c --- */

void round_trip(void);
void chip_block(void);
void type2_card(void);
void full_type2_card(void);
void full_type2_refused(void);

/* --- Incomplete reads and limits: test_incomplete.c --- */

void partial(void);
void spare_entry(void);
void limits(void);

/* --- test_hostile_files.c --- */

void hostile_files(void);

/* --- A card read twice: test_merge.c --- */

void merge_history(void);
void merge_replaced_product(void);
void merge_cap(void);
void merge_new_product(void);
void merge_type2(void);
void merge_full_type2(void);

/* --- Products the card has dropped: test_gone.c --- */

void merge_gone_product(void);
void gone_product_cap(void);
void gone_needs_a_directory(void);
void gone_full_chain(void);

/* --- Value histories on the heap: test_history_memory.c --- */

void history_past_the_card(void);
void history_owned_by_each_card(void);
void history_reset_and_reread(void);
void history_load_merge_free_twice(void);
