/**
 * @file test_rail_profile.c
 * @brief What RSPS3002, National Rail's profile for ITSO, adds to the decode.
 */
#include "test_parse.h"

/*
 * The parts of RSPS3002, National Rail's profile for ITSO, that give TYP 24's
 * user-defined elements a meaning: railcard codes, the retailing NLC, the
 * whole-percent discount, and the seat attributes.
 */
void rail_profile(void) {
    printf("\nNational Rail's TYP 24 profile\n");
    bool card = false;
    check(
        "a railcard code names its railcard",
        strcmp(
            itso_railcard_name((const uint8_t*)"DIS  ", 5, &card), "Disabled Persons Railcard") ==
                0 &&
            card);
    check(
        "the 16-25 Railcard by its old name's code",
        strcmp(itso_railcard_name((const uint8_t*)"YNG", 3, NULL), "16-25 Railcard") == 0);
    check(
        "GroupSave is a discount, not a card",
        strcmp(itso_railcard_name((const uint8_t*)"GS3\0\0", 5, &card), "GroupSave") == 0 &&
            !card);
    check(
        "an unknown code, a short one and a long one name nothing",
        !itso_railcard_name((const uint8_t*)"ABC  ", 5, NULL) &&
            !itso_railcard_name((const uint8_t*)"YN", 2, NULL) &&
            !itso_railcard_name((const uint8_t*)"YNGXX", 5, NULL));
    check(
        "a discount taken from the card",
        itso_discount_from_card((const uint8_t*)"XXXXX", 5) &&
            !itso_discount_from_card((const uint8_t*)"YNG  ", 5));

    ItsoDiscount itso = {.type = 9, .percentage = 333};
    check(
        "a discount outside rail's code types keeps ITSO's tenths",
        itso_discount_tenths(&itso) == 333);

    ItsoLocation loc;
    check(
        "a retailer with bit 15 set is an NLC",
        itso_retailer_location(0x8000 | (5 << 10) | 230, &loc) &&
            strcmp(loc.text, "Station 5230") == 0 && strcmp(loc.code, "5230") == 0);
    check(
        "whose first character runs on into letters",
        itso_retailer_location(0x8000 | (31 << 10) | 7, &loc) && strcmp(loc.code, "V007") == 0);
    check(
        "and is not one past 999",
        !itso_retailer_location(0x8000 | 1000, &loc) && !itso_retailer_location(289, &loc));

    /* Which products' retailers are stations: TS 1000-2 table B2's gap is never
     * an OID, the retailer-only range above it is. */
    ItsoProduct sold = {.has_retailer = true, .typ = ItsoTypJourneyTicket};
    sold.retailer = 0x8000 | (5 << 10) | 631;
    check(
        "a TYP 23's retailer in table B2's gap is a station",
        itso_product_sold_at(&sold, &loc) && strcmp(loc.code, "5631") == 0);
    sold.typ = ItsoTypPeriodTicket;
    sold.retailer = 0x8000 | (23 << 10) | 999; /* N999, the gap's last NLC: 57319 */
    check(
        "so is a TYP 22's, to the end of the gap",
        itso_product_sold_at(&sold, &loc) && strcmp(loc.code, "N999") == 0);
    sold.retailer = 57344; /* The first OID past the gap, which would be O000. */
    check(
        "but a TYP 22's retailer-only OID is an operator",
        !itso_product_sold_at(&sold, &loc) && !loc.valid);
    sold.retailer = 57345;
    sold.typ = ItsoTypJourneyTicket;
    check("and so is a TYP 23's", !itso_product_sold_at(&sold, &loc));
    sold.typ = ItsoTypReservationTicket;
    check("and a TYP 24's", !itso_product_sold_at(&sold, &loc));
    sold.retailer = 0x8000 | (5 << 10) | 230;
    check(
        "where a TYP 24's in the gap is rail's NLC",
        itso_product_sold_at(&sold, &loc) && strcmp(loc.code, "5230") == 0);
    sold.typ = ItsoTypStoredTravelRights;
    sold.retailer = 0x8000 | (5 << 10) | 631;
    check("a type RSPS3002 does not cover keeps its operator", !itso_product_sold_at(&sold, &loc));
    sold.typ = ItsoTypJourneyTicket;
    sold.retailer = 289;
    check("as does an OID below bit 15", !itso_product_sold_at(&sold, &loc));
    sold.has_retailer = false;
    sold.retailer = 0x8000 | (5 << 10) | 631;
    check("and a product with no retailer has no station", !itso_product_sold_at(&sold, &loc));

    check(
        "seat attributes in words",
        strcmp(itso_seat_attribute_name("TABL"), "Table") == 0 &&
            strcmp(itso_seat_attribute_name("AISL"), "Aisle") == 0 &&
            strcmp(itso_seat_attribute_name("HTMS"), "Hot meal at seat") == 0);
    check(
        "an unknown attribute is left as it stands",
        !itso_seat_attribute_name("ZQXV") && !itso_seat_attribute_name(""));
}
