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
    bool ok = used == n && loc.valid && strcmp(loc_text(&loc), text) == 0 &&
              strcmp(loc_code(&loc), code) == 0 && loc_kind(&loc) == kind;
    if(!ok) {
        printf(
            "      got \"%s\" code \"%s\" kind %u, %zu of %zu bytes\n",
            loc_text(&loc),
            loc_code(&loc),
            loc_kind(&loc),
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
        check("212 keeps the count of the other stops", itso_location_more(&many) == 2);
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
        check("and a single stop has no others", itso_location_more(&one) == 0);
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
     * than ITSO_LOC_CODE_LEN; half a code would find the wrong stop, so none
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
        loc_text(&loc);
        loc_code(&loc);
        parse_exact(service_stop, len, ItsoLocStructLoc2, &loc);
        loc_text(&loc);
        loc_code(&loc);
    }
    check("truncated bus stop locations survive", 1);
}

/** True when @p s ends inside @p len bytes. */
static bool terminated(const char* s, size_t len) {
    return memchr(s, '\0', len) != NULL;
}

/*
 * An ItsoLocation keeps only the first ITSO_LOC_BODY_LEN bytes of a record and
 * renders its text and code when a screen asks. A LOC1 may run to 255 bytes
 * (TS 1000-1 clause 4.2.4.2.2), so a record that long must still render as the
 * bytes kept say, count what was not kept, and never read past either.
 */
void location_rendering(void) {
    /* A 212 at the longest a LOC1 can be: 63 stops and three bytes of padding.
     * Only the first stop is kept, but the 62 after it are still counted. */
    uint8_t many[2 + 255];
    memset(many, 0x11, sizeof(many));
    many[0] = 212;
    many[1] = 255;
    many[2] = 0x00;
    many[3] = 0x06;
    many[4] = 0x26;
    many[5] = 0x24;
    check_location(
        "212 at the longest a LOC1 can be",
        many,
        sizeof(many),
        ItsoLocStructLoc1,
        "Stop 00062624 and 62 more",
        "00062624",
        ItsoLocCodeNaptan);
    {
        ItsoLocation loc;
        parse_exact(many, sizeof(many), ItsoLocStructLoc1, &loc);
        check(
            "and its further stops are counted from the length, past the bytes kept",
            loc.length == 255 && itso_location_more(&loc) == 62);
    }

    /* A 211 as long: the text shows as much as it has room for, and the code,
     * far longer than any AtcoCode, is not offered. */
    uint8_t atco[2 + 255];
    memset(atco, 'A', sizeof(atco));
    atco[0] = 211;
    atco[1] = 255;
    check_location(
        "211 at the longest a LOC1 can be",
        atco,
        sizeof(atco),
        ItsoLocStructLoc1,
        "Stop AAAAAAAAAAAAAAA",
        "",
        ItsoLocCodeNone);

    /* The Subway's stage is its station only on an SPT ticket, which the
     * decoder marks; a stage the table has no station for stays a stage. */
    {
        static const uint8_t stage[] = {202, 0x5F, 0x28, 0x00, 0x04, 0x00, 0x00};
        ItsoLocation loc;
        parse_exact(stage, sizeof(stage), ItsoLocStructLoc2, &loc);
        check(
            "a fare stage is a fare stage unless marked as the Subway's",
            strcmp(loc_text(&loc), "Fare stage 4 (6236160)") == 0);
        loc.subway_station = true;
        check("marked, it is the station", strcmp(loc_text(&loc), "Hillhead") == 0);
        loc.body[3] = 99;
        check(
            "and a stage with no station stays a stage",
            strcmp(loc_text(&loc), "Fare stage 99 (6236160)") == 0);
    }

    /* A location not made by the parser - every type, any length, every bit set -
     * renders something terminated into any buffer, offers a code only with
     * its kind, and reads nothing it does not hold. ASan and UBSan are the
     * other half of the assertion. */
    static const uint8_t lengths[] = {0, 1, 3, 4, 6, 8, 9, 14, 15, 16, 255};
    static const size_t sizes[] = {1, 2, 6, ITSO_LOC_LEN};
    bool safe = true;
    for(int t = 0; t < 256; t++) {
        for(size_t l = 0; l < sizeof(lengths); l++) {
            for(int fill = 0; fill < 2; fill++) {
                ItsoLocation loc;
                memset(&loc, 0, sizeof(loc));
                loc.valid = true;
                loc.def_type = (uint8_t)t;
                loc.length = lengths[l];
                loc.subway_station = fill;
                memset(loc.body, fill ? 0xFF : 0x00, sizeof(loc.body));
                for(size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
                    char text[ITSO_LOC_LEN];
                    itso_location_text(&loc, text, sizes[s]);
                    if(!terminated(text, sizes[s])) safe = false;
                    if(sizes[s] == ITSO_LOC_LEN && text[0] == '\0') safe = false;
                    char code[ITSO_LOC_CODE_LEN];
                    size_t code_len = sizes[s] < sizeof(code) ? sizes[s] : sizeof(code);
                    ItsoLocCodeKind kind = itso_location_code(&loc, code, code_len);
                    if(!terminated(code, code_len)) safe = false;
                    if((kind == ItsoLocCodeNone) != (code[0] == '\0')) safe = false;
                }
            }
        }
    }
    check("any location renders safely into any buffer", safe);
}
