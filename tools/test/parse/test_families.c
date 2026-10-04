/**
 * @file test_families.c
 * @brief Which ItsoTerms each product of the synthetic card reads as its own.
 *
 * A product's family terms share their room with the other families' (see
 * ItsoTerms), so a purse's bytes are under an ID's name. The accessors are what
 * stops a screen reading them as one: each product must see its own family's
 * terms, and nothing set in the other two.
 */
#include "test_parse.h"

/** The synthetic card after its five products have been decoded. */
void product_families(const ItsoCard* card) {
    printf("\n== Product families ==\n");

    const ItsoProduct* purse = &card->products[0];
    check("a purse is of the purse family", itso_product_family(purse->typ) == ItsoFamilyPurse);
    check("a purse reads its own balance", itso_product_purse(purse)->balance.valid);
    check(
        "a purse has no holder and no ticket terms",
        !itso_product_id(purse)->has_name && !itso_product_ticket(purse)->valid);

    const ItsoProduct* id = &card->products[1];
    check("an ID is of the ID family", itso_product_family(id->typ) == ItsoFamilyId);
    check("an ID reads its own holder", itso_product_id(id)->has_name);
    check(
        "an ID's name is not read as a balance",
        !itso_product_purse(id)->balance.valid && !itso_product_purse(id)->has_limits);

    const ItsoProduct* period = &card->products[2];
    check("a period ticket is a ticket", itso_product_family(period->typ) == ItsoFamilyTicket);
    check(
        "a period ticket reads its own terms, and no holder",
        itso_product_ticket(period)->valid && !itso_product_id(period)->has_name);

    const ItsoProduct* loyalty = &card->products[4];
    check(
        "loyalty is of no family, and reads every family's terms as unset",
        itso_product_family(loyalty->typ) == ItsoFamilyOther &&
            !itso_product_purse(loyalty)->balance.valid && !itso_product_id(loyalty)->has_name &&
            !itso_product_ticket(loyalty)->valid);
}
