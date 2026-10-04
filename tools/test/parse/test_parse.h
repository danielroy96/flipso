/**
 * @file test_parse.h
 * @brief What the decoder's test files share: the helpers, and each file's tests
 * for test_parse.c to run in order.
 */
#pragma once

#include "../test.h"
#include "itso.h"
#include "itso_i.h"
#include "../card_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* --- test_parse_util.c --- */

/** "2026-09-14 08:41" for a Unix time, in UTC; the buffer is reused per call. */
const char* fmt_unix(ItsoUnixTime t);

/**
 * A location's text and code, rendered as a screen would render them. Each
 * returns one of a few buffers in turn, so several may be held at once.
 */
const char* loc_text(const ItsoLocation* loc);
const char* loc_code(const ItsoLocation* loc);

/** Which register loc_code()'s code is a key into. */
ItsoLocCodeKind loc_kind(const ItsoLocation* loc);

/** Print a location under @p label, when there is one. */
void dump_location(const char* label, const ItsoLocation* loc);

/**
 * Decode one IPE group of type @p typ from an exact-length heap copy of @p src
 * into @p p, so that an over-read is the sanitiser's to catch.
 *
 * @p p is zeroed or what an earlier call left, whose value history this
 * releases; release the last with itso_product_free().
 */
void parse_group(ItsoProduct* p, uint8_t typ, bool vgp, const uint8_t* src, size_t len);

/* --- The synthetic card, in the order a read decodes it --- */

void synthetic_shell(ItsoCard* card); /* test_synthetic_card.c */
void synthetic_directory(ItsoCard* card);
void synthetic_entries(ItsoCard* card);
void synthetic_purse(ItsoCard* card); /* test_synthetic_products.c */
void synthetic_id(ItsoCard* card);
void synthetic_period(ItsoCard* card);
void synthetic_journey(ItsoCard* card);
void synthetic_loyalty(ItsoCard* card);
void synthetic_log(ItsoCard* card); /* test_synthetic_log.c */
void synthetic_review(ItsoCard* card); /* test_synthetic_review.c */
void product_families(const ItsoCard* card); /* test_families.c */

/* --- Media --- */

void shell_checksum(void); /* test_shell.c */
void shell_reject_reasons(void);
void cmd2_card(void); /* test_cmd2.c */
void compact_shell(void); /* test_cmd4.c */
void space_saving_types(void);
void full_shell_type2(void); /* test_type2_full.c */
void sector_chains(void); /* test_chains.c */
void log_sectors(void);

/* --- Products and locations --- */

void spec_review_fields(void); /* test_spec_review.c */
void reservation_ticket(void); /* test_reservation.c */
void rail_profile(void); /* test_rail_profile.c */
void bus_stop_locations(void); /* test_locations.c */
void location_rendering(void);

/* --- Hostile input: test_robustness.c --- */

void robustness(void);
void card_arrays(void);
void oversized_directory(void);
