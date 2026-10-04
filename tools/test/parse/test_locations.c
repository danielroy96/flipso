/**
 * @file test_locations.c
 * @brief Location records: bus stops, NaPTAN and ATCO codes, and what they render as.
 */
#include "test_parse.h"

/*
 * A shell may claim more directory entries than the product array holds - CMD7
 * allows up to 31, and a CMD2 card already uses 16. Every entry being a product
 * must fill the array and stop, not run off the end of it.
 */
/*
 * Bus stop locations: LocDefTypes 206, 211, 212 and 216 (ITSO TS 1000-1 clauses
 * 4.2.4.3.4, .9, .13 and .15).
 *
 * What is being checked is not only the text but the code and the kind, because
 * those are what flipso_cat_location hands to the stop table, and a location
 * that renders correctly while reporting the wrong kind would look right on
 * screen and never resolve.
 */
static size_t
    parse_exact(const uint8_t* record, size_t n, ItsoLocStruct variant, ItsoLocation* out) {
    /* An exact-length heap copy: a record read off the end of an oversized
     * static buffer lands inside it and the sanitiser sees nothing. */
    uint8_t* exact = malloc(n);
    memcpy(exact, record, n);
    size_t used = itso_parse_location(exact, n, variant, out);
    free(exact);
    return used;
}

/** Parse @p record and check its text, the code it offers and which register. */
static void check_location(
    const char* what,
    const uint8_t* record,
    size_t n,
    ItsoLocStruct variant,
    const char* text,
    const char* code,
    ItsoLocCodeKind kind) {
    ItsoLocation loc;
    size_t used = parse_exact(record, n, variant, &loc);
    bool ok = used == n && loc.valid && strcmp(loc.text, text) == 0 &&
              strcmp(loc.code, code) == 0 && itso_location_code_kind(&loc) == kind;
    if(!ok) {
        printf(
            "      got \"%s\" code \"%s\" kind %u, %zu of %zu bytes\n",
            loc.text,
            loc.code,
            itso_location_code_kind(&loc),
            used,
            n);
    }
    check(what, ok);
}

void bus_stop_locations(void) {
    /* "MANAG" folded onto the keypad is 62624, right justified in eight digits. */
    static const uint8_t naptan_loc1[] = {206, 4, 0x00, 0x06, 0x26, 0x24};
    check_location(
        "206 NaptanCode in LOC1",
        naptan_loc1,
        sizeof(naptan_loc1),
        ItsoLocStructLoc1,
        "Stop 00062624",
        "00062624",
        ItsoLocCodeNaptan);

    /* LOC2 is a fixed seven bytes: the tag, the four of code, then padding. */
    static const uint8_t naptan_loc2[] = {206, 0x00, 0x06, 0x26, 0x24, 0x00, 0x00};
    check_location(
        "206 NaptanCode in LOC2",
        naptan_loc2,
        sizeof(naptan_loc2),
        ItsoLocStructLoc2,
        "Stop 00062624",
        "00062624",
        ItsoLocCodeNaptan);

    /* A nibble above nine is not a digit any register holds, so it is shown but
     * never offered for lookup. */
    static const uint8_t naptan_bad[] = {206, 4, 0x00, 0x06, 0x2A, 0x24};
    check_location(
        "206 with a non-decimal nibble offers no code",
        naptan_bad,
        sizeof(naptan_bad),
        ItsoLocStructLoc1,
        "Stop 00062F24",
        "",
        ItsoLocCodeNone);

    /* Three stops, so the first is named and the other two are counted. */
    static const uint8_t naptan_many[] = {
        212, 12, 0x00, 0x06, 0x26, 0x24, 0x12, 0x34, 0x56, 0x78, 0x87, 0x65, 0x43, 0x21};
    check_location(
        "212 multiple NaptanCodes",
        naptan_many,
        sizeof(naptan_many),
        ItsoLocStructLoc1,
        "Stop 00062624 and 2 more",
        "00062624",
        ItsoLocCodeNaptan);

    {
        /* The count is kept apart too, for a screen that names the first stop
         * and so loses the text that carried it. */
        ItsoLocation many;
        itso_parse_location(naptan_many, sizeof(naptan_many), ItsoLocStructLoc1, &many);
        check("212 keeps the count of the other stops", many.more == 2);
    }

    static const uint8_t naptan_one[] = {212, 4, 0x00, 0x06, 0x26, 0x24};
    check_location(
        "212 holding a single NaptanCode",
        naptan_one,
        sizeof(naptan_one),
        ItsoLocStructLoc1,
        "Stop 00062624",
        "00062624",
        ItsoLocCodeNaptan);
    {
        ItsoLocation one;
        itso_parse_location(naptan_one, sizeof(naptan_one), ItsoLocStructLoc1, &one);
        check("and a single stop has no others", one.more == 0);
    }

    /* An AtcoCode is stored whole, so unlike a NaptanCode it needs no unfolding. */
    static const uint8_t atco[] = {
        211, 12, '1', '8', '0', '0', 'A', 'L', 'T', 'R', 'N', 'H', 'M', '0'};
    check_location(
        "211 AtcoCode",
        atco,
        sizeof(atco),
        ItsoLocStructLoc1,
        "Stop 1800ALTRNHM0",
        "1800ALTRNHM0",
        ItsoLocCodeAtco);

    static const uint8_t atco_short[] = {211, 8, '1', '8', '0', '0', 'E', 'B', '0', '1'};
    check_location(
        "211 AtcoCode shorter than the maximum",
        atco_short,
        sizeof(atco_short),
        ItsoLocStructLoc1,
        "Stop 1800EB01",
        "1800EB01",
        ItsoLocCodeAtco);

    /* Thirteen characters is longer than TS 1000-1 table 40 allows and longer
     * than ItsoLocation::code; half a code would find the wrong stop, so none
     * is offered. */
    static const uint8_t atco_long[] = {
        211, 13, '1', '8', '0', '0', 'A', 'L', 'T', 'R', 'N', 'H', 'M', '0', '0'};
    check_location(
        "211 over-long AtcoCode offers no code",
        atco_long,
        sizeof(atco_long),
        ItsoLocStructLoc1,
        "Stop 1800ALTRNHM00",
        "",
        ItsoLocCodeNone);

    /* OID, then service "42" as four 6-bit SNCODE2 characters padded with 0x3F,
     * then the stop: the code starts at bit 40 of the body. */
    static const uint8_t service_stop[] = {
        216, 9, 0x00, 0x01, 0xFF, 0xF1, 0x02, 0x00, 0x06, 0x26, 0x24};
    check_location(
        "216 service number and NaptanCode",
        service_stop,
        sizeof(service_stop),
        ItsoLocStructLoc1,
        "Route 42@00062624",
        "00062624",
        ItsoLocCodeNaptan);

    /* Rail codes keep working, and now say which register they belong to. */
    static const uint8_t nlc[] = {203, 4, '1', '4', '4', '4'};
    check_location(
        "203 rail NLC",
        nlc,
        sizeof(nlc),
        ItsoLocStructLoc1,
        "Station 1444",
        "1444",
        ItsoLocCodeNlc);

    /* Types that name no code at all must offer none, whatever they render. */
    static const uint8_t zones[] = {204, 3, 0x15, 0x00, 0x00};
    check_location(
        "204 zone bit map offers no code",
        zones,
        sizeof(zones),
        ItsoLocStructLoc1,
        "Zones 1,3,5",
        "",
        ItsoLocCodeNone);

    /* Truncations must not read past the end; ASan is the assertion. */
    for(size_t len = 0; len <= sizeof(service_stop); len++) {
        ItsoLocation loc;
        parse_exact(service_stop, len, ItsoLocStructLoc1, &loc);
        parse_exact(service_stop, len, ItsoLocStructLoc2, &loc);
    }
    check("truncated bus stop locations survive", 1);
}
