/**
 * @file test_synthetic_screens.c
 * @brief The synthetic card's screens, and the lines a product shows wherever it is.
 */
#include "test_format.h"

/** Every screen of the synthetic card, now and decades on. */
void synthetic_screens(const FlipsoFormat* f, const ItsoCard* card, FuriString* text) {
    flipso_format_payg(text, f, card);
    printf("\n%s\n", furi_string_get_cstr(text));
    check(
        "the balance is in pounds",
        shows(
            text,
            "Balance: \xC2\xA3"
            "12.34"));
    check("the purse terms are labelled", shows(text, "Auto top-up: "));
    check("a top-up's detail is indented and labelled", shows(text, "  When below: \xC2\xA3"));
    check(
        "earlier transactions have a page of their own",
        page_of(text, "History") && on_page(text, "History", "\n  When: "));
    check("each one says when", shows(text, "  When: "));

    furi_string_reset(text);
    flipso_format_taps(text, f, card);
    printf("\n%s\n", furi_string_get_cstr(text));
    check("the in/out state is where the holder is", shows(text, "Inside ticket gates: "));
    check("not a bare IN or OUT", !shows(text, ": IN\n") && !shows(text, ": OUT\n"));
    check("a tap's time is labelled as every time is", shows(text, "\nWhen: "));
    check("with no other word for it", !shows(text, "\nTime: "));
    check("a tap out says which time is which", shows(text, "\nOut: ") && shows(text, "\nIn: "));
    check("and how long the journey took", shows(text, "\nJourney time: "));
    check("stations are named", shows(text, "London Waterloo"));

    furi_string_reset(text);
    flipso_format_id(text, f, card);
    printf("\n%s\n", furi_string_get_cstr(text));
    check("the ID has its holder", shows(text, "Name: "));
    check(
        "the photo flag is capitalised",
        shows(text, "Photo on card: Yes") || shows(text, "Photo on card: No"));
    check("an entitlement's area is not a journey's end", !shows(text, "\nFrom: "));
    spec_review(f, card);
    reservation_screen(f, card);

    furi_string_reset(text);
    flipso_format_card(text, f, card, NULL, false, 0);
    printf("\n%s\n", furi_string_get_cstr(text));
    check("the card number is grouped", shows(text, "633597 1234 0012 3458"));
    check("the checksum is stated", shows(text, "Checksum: Correct"));
    check("the card type is named", shows(text, "Card type: DESFire (CMD7)"));
    check(
        "the directory's seal key version is under Technical",
        technical(text, "Directory seal key version: 1\n"));
    check("no saved card section for a card just read", !shows(text, "Saved card"));

    furi_string_reset(text);
    flipso_format_summary(text, f, card);
    printf("\n%s\n", furi_string_get_cstr(text));
    check("the summary leads with the card's state", shows(text, "Card: "));
    check(
        "the summary has the balance",
        shows(
            text,
            "Pay as you go: \xC2\xA3"
            "12.34"));
    check("the summary has the last tap", shows(text, "Last tap: ") && shows(text, "  When: "));

    every_screen("synthetic", f, card);

    /* The same card decades on, when it and everything on it has expired: the
     * wording changes with the clock, and the house style has to hold for both.
     * Pinning one date alone once hid a "Card: Expired: ..." on every card-> */
    FlipsoFormat later = *f;
    later.now = FLIPSO_TEST_LATER;
    every_screen("synthetic, expired", &later, card);
}

/** A product's tag, heading, area, stops and removal, each on a variant of the purse. */
void product_lines(const FlipsoFormat* f, const ItsoCard* card, FuriString* text) {
    FlipsoFormat later = *f;
    later.now = FLIPSO_TEST_LATER;
    furi_string_reset(text);
    flipso_format_summary(text, &later, card);
    check("an expired card's summary says so", shows(text, "Card: Expired "));

    /* The product list's tags. */
    check("an in-date product has no tag", flipso_product_tag(&card->products[0], f->now) == NULL);
    ItsoProduct gone = card->products[0];
    gone.on_card = false;
    check(
        "a dropped product says so first",
        strcmp(flipso_product_tag(&gone, f->now), "Off card") == 0);

    /* Heading icons are one byte after the markup, above '\n'. */
    furi_string_reset(text);
    flipso_cat_heading(text, FlipsoIconPast, "Off card");
    const char* heading = furi_string_get_cstr(text);
    check("a heading's icon byte is never a newline", heading[2] != '\n');
    check("and the heading is one line", strchr(heading, '\n') == heading + strlen(heading) - 1);

    /* A ticket good within a set of zones has an area, not a journey. */
    {
        static const uint8_t zones[] = {204, 3, 0x07, 0x00, 0x00};
        ItsoProduct ticket = card->products[0];
        itso_parse_location(zones, sizeof(zones), ItsoLocStructLoc1, &ticket.from);
        ticket.to.valid = false;
        furi_string_reset(text);
        flipso_format_product(text, f, card, &ticket);
        check(
            "a ticket's zone map is where it is valid",
            shows(text, "Valid in: Zones 1,2,3\n") && !shows(text, "From: Zones"));
    }

    /* A location listing several stops, the first of which the stop table
     * names: the name replaces the code, and the others are still counted. */
    {
        static const uint8_t stops[] = {
            212, 12, 0x00, 0x06, 0x26, 0x24, 0x12, 0x34, 0x56, 0x78, 0x87, 0x65, 0x43, 0x21};
        ItsoProduct ticket = card->products[0];
        itso_parse_location(stops, sizeof(stops), ItsoLocStructLoc1, &ticket.from);
        furi_string_reset(text);
        flipso_format_product(text, f, card, &ticket);
        check(
            "a named stop keeps the count of the others", shows(text, "High Street and 2 more\n"));
    }

    /* A GWR season ticket keeps its product one day past expiry; RemoveDate
     * says how long any machine must wait, or that none may. */
    {
        ItsoProduct ticket = card->products[0];
        ticket.has_remove_date = true;
        ticket.remove_date = 1;
        furi_string_reset(text);
        flipso_format_product(text, f, card, &ticket);
        check("one day is not days", shows(text, "Removable: 1 day after expiry\n"));
        ticket.remove_date = 30;
        furi_string_reset(text);
        flipso_format_product(text, f, card, &ticket);
        check("but thirty are", shows(text, "Removable: 30 days after expiry\n"));
        ticket.remove_date = 0;
        furi_string_reset(text);
        flipso_format_product(text, f, card, &ticket);
        check("and none is on expiry", shows(text, "Removable: Once expired\n"));
        ticket.remove_date = 255;
        furi_string_reset(text);
        flipso_format_product(text, f, card, &ticket);
        check(
            "255 is the operator's to remove, not the holder's",
            shows(text, "Removable: Only by the operator\n"));
    }
}
