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
            strcmp(loc_text(&loc), "Station 5230") == 0 && strcmp(loc_code(&loc), "5230") == 0);
    check(
        "whose first character runs on into letters",
        itso_retailer_location(0x8000 | (31 << 10) | 7, &loc) &&
            strcmp(loc_code(&loc), "V007") == 0);
    check(
        "and is not one past 999",
        !itso_retailer_location(0x8000 | 1000, &loc) && !itso_retailer_location(289, &loc));

    /* Which products' retailers are stations: TS 1000-2 table B2's gap is never
     * an OID, the retailer-only range above it is. */
    ItsoProduct sold = {.has_retailer = true, .typ = ItsoTypJourneyTicket};
    sold.retailer = 0x8000 | (5 << 10) | 631;
    check(
        "a TYP 23's retailer in table B2's gap is a station",
        itso_product_sold_at(&sold, &loc) && strcmp(loc_code(&loc), "5631") == 0);
    sold.typ = ItsoTypPeriodTicket;
    sold.retailer = 0x8000 | (23 << 10) | 999; /* N999, the gap's last NLC: 57319 */
    check(
        "so is a TYP 22's, to the end of the gap",
        itso_product_sold_at(&sold, &loc) && strcmp(loc_code(&loc), "N999") == 0);
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
        itso_product_sold_at(&sold, &loc) && strcmp(loc_code(&loc), "5230") == 0);
    sold.typ = ItsoTypStoredTravelRights;
    sold.retailer = 0x8000 | (5 << 10) | 631;
    check("a type RSPS3002 does not cover keeps its operator", !itso_product_sold_at(&sold, &loc));
    sold.typ = ItsoTypJourneyTicket;
    sold.retailer = 289;
    check("as does an OID below bit 15", !itso_product_sold_at(&sold, &loc));
    sold.has_retailer = false;
    sold.retailer = 0x8000 | (5 << 10) | 631;
    check("and a product with no retailer has no station", !itso_product_sold_at(&sold, &loc));

    /* RSPS3002's PTYP allocation, 3.6.2 and 3.7.2. */
    check(
        "the four season ticket PTYPs",
        strcmp(itso_rail_product_name(ItsoTypPeriodTicket, 0), "Season ticket") == 0 &&
            strcmp(itso_rail_product_name(ItsoTypPeriodTicket, 3), "Season ticket") == 0);
    check(
        "TYP 22's named PTYPs",
        strcmp(itso_rail_product_name(ItsoTypPeriodTicket, 5), "Flexi Season") == 0 &&
            strcmp(itso_rail_product_name(ItsoTypPeriodTicket, 19), "Travelcard") == 0 &&
            strcmp(itso_rail_product_name(ItsoTypPeriodTicket, 29), "PlusBus") == 0 &&
            strcmp(itso_rail_product_name(ItsoTypPeriodTicket, 31), "Staff pass") == 0);
    check(
        "TYP 23's",
        strcmp(itso_rail_product_name(ItsoTypJourneyTicket, 1), "Peak single") == 0 &&
            strcmp(itso_rail_product_name(ItsoTypJourneyTicket, 4), "Off-peak return") == 0 &&
            strcmp(itso_rail_product_name(ItsoTypJourneyTicket, 6), "Off-peak carnet") == 0);
    check(
        "a PTYP left unallocated names nothing",
        !itso_rail_product_name(ItsoTypPeriodTicket, 6) &&
            !itso_rail_product_name(ItsoTypJourneyTicket, 0) &&
            !itso_rail_product_name(ItsoTypJourneyTicket, 7));
    check(
        "nor does another type's",
        !itso_rail_product_name(ItsoTypReservationTicket, 1) &&
            !itso_rail_product_name(ItsoTypEntitlement, 1) &&
            !itso_rail_product_name(ItsoTypPeriodCompact, 2));

    /* Which products are rail's, so that the allocation applies. */
    static const uint8_t short_nlc[] = {203, 4, '5', '6', '3', '1'};
    static const uint8_t uic_nlc[] = {208, 6, 0x00, 0x70, '5', '6', '8', '5'};
    static const uint8_t uic_abroad[] = {208, 6, 0x00, 0x87, '1', '2', '3', '4'};
    static const uint8_t stop[] = {206, 4, 0x12, 0x34, 0x56, 0x78};
    ItsoProduct rail = {.typ = ItsoTypPeriodTicket, .ptyp = 29};
    check("a product with no ends and no retailer is not rail", !itso_product_rail(&rail));
    itso_parse_location(short_nlc, sizeof(short_nlc), ItsoLocStructLoc1, &rail.from);
    check("one that starts at an NLC is", itso_product_rail(&rail));
    rail.from.valid = false;
    itso_parse_location(uic_nlc, sizeof(uic_nlc), ItsoLocStructLoc1, &rail.to);
    check(
        "as is one that ends at a UIC station",
        itso_product_rail(&rail) && strcmp(loc_text(&rail.to), "Station 5685") == 0);
    itso_parse_location(uic_abroad, sizeof(uic_abroad), ItsoLocStructLoc1, &rail.to);
    check(
        "abroad too",
        itso_product_rail(&rail) && strcmp(loc_text(&rail.to), "Station 1234 (country 087)") == 0);
    itso_parse_location(stop, sizeof(stop), ItsoLocStructLoc1, &rail.from);
    itso_parse_location(stop, sizeof(stop), ItsoLocStructLoc1, &rail.to);
    check("but not one between bus stops", !itso_product_rail(&rail));
    rail.has_retailer = true;
    rail.retailer = 0x8000 | (5 << 10) | 631;
    check("unless a station sold it", itso_product_rail(&rail));
    rail.retailer = 289;
    check("an operator selling it says nothing", !itso_product_rail(&rail));

    check(
        "seat attributes in words",
        strcmp(itso_seat_attribute_name("TABL"), "Table") == 0 &&
            strcmp(itso_seat_attribute_name("AISL"), "Aisle") == 0 &&
            strcmp(itso_seat_attribute_name("HTMS"), "Hot meal at seat") == 0);
    check(
        "an unknown attribute is left as it stands",
        !itso_seat_attribute_name("ZQXV") && !itso_seat_attribute_name(""));
}
