/**
 * @file itso_location.h
 * @brief An ITSO location (TS 1000-1 clause 4.2.4), what it renders as, and the
 * stations rail retailers are named by.
 */
#pragma once

#include "itso_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Which national register a location's code (itso_location_code()) is a key
 * into.
 *
 * Several LocDefTypes carry the same kind of code - 206, 212 and 216 all end up
 * holding a NaptanCode - so what a caller needs in order to look one up is the
 * kind of code, not the type of location that happened to carry it.
 */
typedef enum {
    ItsoLocCodeNone, /**< Nothing to look up; the rendered text is all there is. */
    ItsoLocCodeNlc, /**< Four-character rail National Location Code. */
    ItsoLocCodeNaptan, /**< NaptanCode bus stop, as the digits the card stores. */
    ItsoLocCodeAtco, /**< AtcoCode bus stop, up to twelve ASCII characters. */
} ItsoLocCodeKind;

/**
 * A location as the card stored it: the LocDefType and the head of the record's
 * body, which itso_location_text() and itso_location_code() decode when a screen
 * is drawn.
 *
 * Kept raw because a location is held many times over - three in every tap of
 * the log, two in every product - and is only read by the screen that shows
 * it. Its text takes up to ITSO_LOC_LEN bytes and its code ITSO_LOC_CODE_LEN,
 * where the body both are made from takes ITSO_LOC_BODY_LEN.
 */
typedef struct {
    bool valid;
    uint8_t def_type; /**< LocDefType, ITSO TS 1000-1 table 6. */
    /** Body bytes the record holds: a LOC1's length element, 6 for a LOC2. Up
     *  to 255, of which only the first ITSO_LOC_BODY_LEN are kept in @c body;
     *  the rest are only counted, as a LocDefType 212's further stops are. */
    uint8_t length;
    /** A fare stage (202) on an SPT Subway ticket, whose stage number is the
     *  station (see itso_space_saving.c). The product's OID says so, which the
     *  location's own bytes cannot. */
    bool subway_station;
    /** The record's first bytes after the tag and any length; zero past
     *  @c length. */
    uint8_t body[ITSO_LOC_BODY_LEN];
} ItsoLocation;

/**
 * Render @p location as text that is always safe to display: "Station 1072",
 * "Zones 1,3", "Unknown location (type 230)". ITSO_LOC_LEN always holds it; a
 * smaller @p len truncates it.
 */
void itso_location_text(const ItsoLocation* location, char* out, size_t len);

/**
 * The bare code a national register could name @p location by, for the types
 * where something outside the decoder can do better than the text - a rail NLC
 * can be turned into a station name, and a bus stop code into a stop name,
 * given lookup tables the decoder itself has no business owning.
 *
 * @param out ITSO_LOC_CODE_LEN bytes; left empty when there is no code, and
 *            when @p len is too short for the one there is.
 * @return    which register @p out is a key into, or ItsoLocCodeNone.
 */
ItsoLocCodeKind itso_location_code(const ItsoLocation* location, char* out, size_t len);

/**
 * Further stops a LocDefType 212 lists after the one its code names, so a
 * screen that names that stop can still say there are others (TS 1000-1
 * 4.2.4.3.13). Counted from the record's length, so the stops past those kept
 * in @c body count too.
 */
static inline uint8_t itso_location_more(const ItsoLocation* location) {
    if(location->def_type != 212 || location->length < 8) return 0;
    return (uint8_t)(location->length / 4 - 1);
}

/**
 * The station a rail product's ProductRetailer names, when it names one.
 *
 * RSPS3002 sections 3.6.3, 3.7.3 and 3.8.3: rail sets bit 15 to say the
 * element is not an OID but a retailing NLC - the first character in bits 10
 * to 14 ('0'-'9' then 'A'-'V'), the last three digits in the low ten bits.
 *
 * @return false for a retailer that is an operator, leaving @p out empty.
 */
bool itso_retailer_location(uint16_t retailer, ItsoLocation* out);

#ifdef __cplusplus
}
#endif
