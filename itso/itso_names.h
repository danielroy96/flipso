/**
 * @file itso_names.h
 * @brief Human-readable names for coded values.
 */
#pragma once

#include "itso_types.h"

#ifdef __cplusplus
extern "C" {
#endif

const char* itso_typ_name(uint8_t typ);

/**
 * SPT Glasgow Subway station name for its 1-15 station id, or NULL if out of
 * range. Scheme-specific, not from the ITSO spec: see the table's provenance in
 * itso_names.c. The Subway's tickets are a compact-shell Type 2 medium
 * (@c shell_compact) and number their stations this way rather than by NLC.
 */
const char* itso_spt_subway_station(uint8_t id);

/**
 * The OID that owns the product on an SPT Subway paper ticket (read from real
 * tickets, 2026-09-27). The ticket's compact shell names no operator, so this is
 * what identifies one - and what says its fare stages are Subway stations.
 */
#define ITSO_OID_SPT_SUBWAY_TICKET 8323

const char* itso_entitlement_name(uint8_t code);
const char* itso_profile_name(uint8_t code);
const char* itso_transaction_name(uint8_t code);
const char* itso_status_name(ItsoProductStatus status);
const char* itso_shell_reject_name(ItsoShellVerdict verdict);

/** EN1545 PaymentMeansCode, e.g. "Cash" (TS 1000-5 annex A.12). */
const char* itso_payment_name(uint8_t code);

/** ITSO language code (TS 1000-5 annex A.24) as ISO 639-1, e.g. "en". False if unknown. */
bool itso_language_code(uint8_t code, char out[3]);

/** English name for the languages of the British Isles, else NULL. */
const char* itso_language_name(uint8_t code);

/** EN1545 AccommodationClassCode, e.g. "Standard". NULL for 0, "unknown". */
const char* itso_class_name(uint8_t code);

/**
 * A National Rail railcard's name from its code, e.g. "Disabled Persons
 * Railcard" for "DIS": the code a ticket's DiscountCode holds (RSPS3002
 * 3.8.3). NULL for one the table does not know. Trailing spaces are ignored.
 *
 * @param card set when the discount is a card the holder must carry, and clear
 *             for one that is not, such as GroupSave. May be NULL.
 */
const char* itso_railcard_name(const uint8_t* code, size_t len, bool* card);

/** True for the DiscountCode a discount taken from an entitlement on the card
 *  carries: "XXXXX" (RSPS3002 3.8.3). */
bool itso_discount_from_card(const uint8_t* code, size_t len);

/**
 * What a reserved seat's AccommodationAttribute says about it, e.g. "Table" -
 * NULL for a code the table does not know, which is then shown as it stands.
 */
const char* itso_seat_attribute_name(const char* code);

/** Label for a product counter, e.g. "Rides left". NULL for ItsoCountNone. */
const char* itso_count_name(ItsoCountKind kind);

/**
 * What a product counter counts, e.g. "Rides", to label a change in it -
 * "Rides: -2" - where "Change: -2" left the unit to be guessed. NULL for
 * ItsoCountNone.
 */
const char* itso_count_unit(ItsoCountKind kind);

/**
 * What bit @p bit of a product's IPEBitMap says is present, in a few words -
 * "Pass length" - from the type's own table in TS 1000-5. NULL for a bit the
 * table leaves RFU for that type and format revision.
 */
const char* itso_bitmap_element_name(uint8_t typ, uint8_t format_rev, uint8_t bit);

/** Gender recorded in IDFlags, or NULL when it is not known or not specified. */
const char* itso_gender_name(uint8_t id_flags);

#ifdef __cplusplus
}
#endif
