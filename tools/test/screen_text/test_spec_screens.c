/**
 * @file test_spec_screens.c
 * @brief The lines a review of TS 1000-5 added to the product screens.
 */
#include "test_format.h"

/** Render one product decoded from @p group as its product screen. */
void product_screen(
    FuriString* text,
    const FlipsoFormat* f,
    const ItsoCard* card,
    ItsoProduct* p,
    uint8_t typ,
    bool vgp,
    const uint8_t* group,
    size_t len) {
    itso_product_free(p);
    memset(p, 0, sizeof(*p));
    p->on_card = true;
    p->typ = typ;
    p->value_group = vgp;
    p->dir_index = 9;
    itso_parse_ipe(p, group, len, 64);
    furi_string_reset(text);
    flipso_format_product(text, f, card, p);
    house_style("review product", text);
}

/*
 * The lines the TS 1000-5 review (2026-09-29) added: elements that were on a
 * card and not shown, and ones that were shown wrong.
 */
void spec_review(const FlipsoFormat* f, const ItsoCard* card) {
    FuriString* text = furi_string_alloc();
    static ItsoProduct p;

    furi_string_reset(text);
    flipso_format_product(text, f, card, &card->products[0]);
    check(
        "a purse's print flags are under Technical",
        shows(text, "Print ticket: Yes\nPrint receipt: Yes\n"));
    furi_string_reset(text);
    flipso_format_product(text, f, card, &card->products[1]);
    check(
        "an ID has PrintTicket and no PrintReceipt",
        shows(text, "Print ticket: No\n") && !shows(text, "Print receipt: "));
    furi_string_reset(text);
    flipso_format_product(text, f, card, &card->products[3]);
    check("a rail RouteCode reads as text", shows(text, "Route code: 00000\n"));
    check(
        "a rail journey ticket was sold by a station",
        on_page(text, "Journey ticket", "Operator: South Western Railway\n") &&
            on_page(text, "Purchase", "Sold by: Station 5631\n"));
    furi_string_reset(text);
    flipso_format_product(text, f, card, &card->products[2]);
    check(
        "a period ticket's retailer-only OID is still an operator",
        shows(text, "Sold by: Unknown (57345)\n"));
    furi_string_reset(text);
    flipso_format_product(text, f, card, &card->products[4]);
    check("loyalty has its owner's data", shows(text, "Owner data: 4660\n"));

    product_screen(
        text,
        f,
        card,
        &p,
        ItsoTypStoredTravelRights,
        true,
        purse_scaled_group,
        sizeof(purse_scaled_group));
    check(
        "a scaled purse's limit scales with its balance",
        shows(
            text,
            "Balance limit: \xC2\xA3"
            "900.00\n"));

    product_screen(
        text, f, card, &p, ItsoTypChargeToAccount1, true, charge1_group, sizeof(charge1_group));
    check(
        "a TYP 4 deposit has its VAT",
        shows(
            text,
            "Deposit: \xC2\xA3"
            "15.00\n  Paid by: Card\n  VAT: 17.50%\n"));

    product_screen(
        text, f, card, &p, ItsoTypChargeToAccount2, true, charge2_group, sizeof(charge2_group));
    check(
        "MaxValue5 in the value record's currency",
        shows(
            text,
            "Spending limit: \xE2\x82\xAC"
            "250.00\n"));
    check("a TYP 5 is used first", shows(text, "Used first: Yes\n"));
    check("and has a receipt printed", shows(text, "Print ticket: No\nPrint receipt: Yes\n"));

    product_screen(
        text,
        f,
        card,
        &p,
        ItsoTypEntitlement,
        false,
        entitlement_rev2_group,
        sizeof(entitlement_rev2_group));
    check(
        "an entitlement names its pass issuer, under Technical",
        technical(text, "Pass issuer code: 1620\n"));
    check("and its holder", technical(text, "Holder number: 11259375\n"));
    check("and how fares round", shows(text, "Fare rounding: Down to 5p\n"));
    check(
        "and its deposit",
        shows(
            text,
            "Deposit: \xC2\xA3"
            "10.00\n  Paid by: Card\n  VAT: 20.00%\n  Refundable: Yes\n"));
    check("and not as an Issuer code under Technical", !shows(text, "Issuer code: "));

    product_screen(
        text,
        f,
        card,
        &p,
        ItsoTypJourneyTicket,
        false,
        journey_rev3_group,
        sizeof(journey_rev3_group));
    check(
        "a revision 3 return and its limits",
        shows(
            text,
            "Ticket use: Return, journeys in pairs\n  Changes allowed: 1\n"
            "  Time between legs: 45 min 30 s\n"));
    check(
        "a ride's value in its own currency",
        shows(
            text,
            "Value of a ride: \xE2\x82\xAC"
            "60.00\n"));

    product_screen(
        text,
        f,
        card,
        &p,
        ItsoTypPeriodTicket,
        false,
        period_rev3_id_group,
        sizeof(period_rev3_id_group));
    check("a period ticket's identity document", shows(text, "Valid only with: ID RC123456\n"));
    check("a rail period ticket was sold by a station", shows(text, "Sold by: London Waterloo\n"));
    check(
        "what a top-up does with expired passes", shows(text, "Expired passes at top-up: Kept\n"));

    product_screen(
        text,
        f,
        card,
        &p,
        ItsoTypPeriodTicket,
        false,
        period_rev3_long_id_group,
        sizeof(period_rev3_long_id_group));
    check(
        "a long identity number is hex, with what is not kept counted",
        shows(text, "Valid only with: ID 0102030405060708090A0B0C0D0E0F10 and 4 more bytes\n"));
    check(
        "a revision 3 period ticket's default",
        shows(text, "Expired passes at top-up: Written off\n"));

    /* Fare capping is decoded from the capture as the screen is drawn, so each
     * purse is put in one at the directory slot product_screen() gives it. Its
     * Technical page says which of the two capping records the card keeps. */
    for(uint8_t ref = 1; ref <= 2; ref++) {
        const uint8_t* group = ref == 1 ? capping1_group : capping2_group;
        size_t len = ref == 1 ? sizeof(capping1_group) : sizeof(capping2_group);
        FlipsoCapture* capture = flipso_capture_alloc();
        flipso_capture_add(capture, FlipsoBlockProduct, 9, group, len);
        FlipsoFormat with = *f;
        with.capture = capture;
        product_screen(text, &with, card, &p, ItsoTypStoredTravelRights, true, group, len);
        check("a capped purse has its capping page", page_of(text, "Fare capping") != NULL);
        check(
            ref == 1 ? "a reduced capping record says so, under Technical" :
                       "a full capping record says so, under Technical",
            technical(
                text,
                ref == 1 ? "Capping record: Reduced (type 1)\nCapping rules: 7\n" :
                           "Capping record: Full (type 2)\nCapping rules: 7\n"));
        flipso_capture_free(capture);
    }

    itso_product_free(&p);
    furi_string_free(text);
}
