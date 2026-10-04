/**
 * @file itso_location.h
 * @brief A decoded ITSO location (TS 1000-1 clause 4.2.4), and the stations rail
 * retailers are named by.
 */
#pragma once

#include "itso_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Which national register @c ItsoLocation::code is a key into.
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
 * A decoded location.
 *
 * @c text is a self-contained rendering that is always safe to display. @c code
 * carries the bare location code for the types where something outside the
 * decoder can do better - a rail NLC can be turned into a station name, and a
 * bus stop code into a stop name, given lookup tables the decoder itself has no
 * business owning.
 */
typedef struct {
    bool valid;
    uint8_t def_type; /**< LocDefType, ITSO TS 1000-1 table 6. */
    uint8_t code_kind; /**< ItsoLocCodeKind; meaningless while @c code is empty. */
    char text[ITSO_LOC_LEN];
    char code[ITSO_LOC_CODE_LEN]; /**< Bare code, empty when not resolvable. */
    /** Further stops a LocDefType 212 lists after the one in @c code, so a
     * screen that names that stop can still say there are others. */
    uint8_t more;
} ItsoLocation;

/**
 * The register @c code belongs to, or ItsoLocCodeNone when there is no code.
 *
 * A malformed field leaves @c code empty while @c text still describes what the
 * card said, so the emptiness test is what distinguishes "not resolvable" from
 * "resolvable and of kind zero".
 */
static inline ItsoLocCodeKind itso_location_code_kind(const ItsoLocation* location) {
    if(location->code[0] == '\0') return ItsoLocCodeNone;
    return (ItsoLocCodeKind)location->code_kind;
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
