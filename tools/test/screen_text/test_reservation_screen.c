/**
 * @file test_reservation_screen.c
 * @brief A TYP 24 reserved journey's screen, page by page.
 */
#include "test_format.h"

/* Wording pinned against the demo card that carries every product type. */
/**
 * A TYP 24 reserved journey's screen: the ticket's terms from ItsoProduct, the
 * rest of its dataset and its reserved legs decoded on demand from the capture,
 * and its codes under Technical.
 */
void reservation_screen(const FlipsoFormat* f, const ItsoCard* card) {
    FuriString* text = furi_string_alloc();
    static ItsoProduct p;

    /* The product decodes from the capture, as the screen's on-demand part
     * does: directory slot 9, where the synthetic card has nothing. */
    FlipsoCapture* capture = flipso_capture_alloc();
    flipso_capture_add(
        capture, FlipsoBlockProduct, 9, reservation_group, sizeof(reservation_group));
    FlipsoFormat with = *f;
    with.capture = capture;
    product_screen(
        text,
        &with,
        card,
        &p,
        ItsoTypReservationTicket,
        true,
        reservation_group,
        sizeof(reservation_group));
    printf("\n%s\n", furi_string_get_cstr(text));

    check(
        "a test ticket says so first",
        strncmp(furi_string_get_cstr(text), "\e#", 2) == 0 &&
            strstr(strchr(furi_string_get_cstr(text), '\n') + 1, "Test ticket: Yes\n") ==
                strchr(furi_string_get_cstr(text), '\n') + 1);
    check(
        "the railcard it is not valid without, on the first page, with the number it carries",
        on_page(
            text,
            "Reserved journey",
            "Valid only with: 16-25 Railcard\n  Railcard number: Ends 4567\nOperator: "));
    check(
        "the journeys it has left lead its details",
        page_starts(text, "Details", "Journeys left: 1\n"));
    check("a rail retailer the table cannot name", shows(text, "Sold by: Retailer 5685\n"));
    check("a return of two journeys", shows(text, "Sold as: Return\n  Journeys sold: 2\n"));
    check(
        "the outward portion and its last day",
        shows(text, "Outward: 01/10/2026 to 02/10/2026\n"));
    check("the return portion", shows(text, "Return: 03/10/2026 to 02/11/2026\n"));
    check("its own start is not a second Valid from", !shows(text, "Valid from: "));
    check(
        "only the flags that are set, each on its page",
        on_page(text, "Purchase", "Duplicate: Yes\n") &&
            on_page(text, "Restrictions", "Seat reservation required: Yes\n") &&
            on_page(text, "Details", "Part-way through a leg: Yes\n") &&
            !shows(text, "Replacement: Yes"));
    check(
        "the clear ones under Technical",
        technical(
            text,
            "Replacement: No\nFollow-on renewal: No\nUnfulfilled warrant: No\nCarnet: No\n"
            "Companion allowed: No\n") &&
            !technical(text, "Test ticket: No"));
    check("rail's 511 transfers are unlimited", shows(text, "Transfers left: Unlimited\n"));
    check("the renewal window", shows(text, "Renews until: 14 days after expiry\n"));
    check("the passenger", shows(text, "Passenger: A N OTHER\n  Gender: Female\n"));
    check("the alternative origin", shows(text, "Or from: Station 0035\n"));
    check("a null alternative is left out", !shows(text, "Or to: "));
    check("the days", shows(text, "Valid days: Mon-Fri\n  Public holidays: No\n"));
    check("the restricted days", shows(text, "Restrictions apply: Sat Sun\n"));
    check("one operator only", shows(text, "Only on operator: GR\n"));
    check("the railcard IPE it was sold with", shows(text, "Part of this ticket: ITSO ID\n"));
    check("a via", shows(text, "Via: Station 1555\n"));
    check(
        "an out-of-station interchange",
        shows(
            text,
            "Change stations at: London Bridge\n  Continue from: Station 5143\n"
            "  Time allowed: 45 min\n"));
    check("break of journey, transfer type 2", shows(text, "Break of journey: Allowed\n"));
    check(
        "valid times, and the journeys they apply to",
        shows(text, "Valid times: Outside 07:00-09:30\n  Applies to: Outward departures\n"));
    check(
        "a train it may not be used on",
        shows(text, "Not valid on train: GR1234\n  From: Station 1444\n  Departs: 18:30\n"));
    check(
        "where it was sold, the retailer's station, is not said twice", !shows(text, "Sold at: "));
    check(
        "the price",
        shows(
            text,
            "Price paid: \xC2\xA3"
            "89.50\n  Paid by: "));
    check(
        "the booking reference leads the purchase",
        page_starts(text, "Purchase", "Booking reference: ABC12345\n"));
    check(
        "the last validation leads the history",
        page_starts(text, "History", "Last validated: 01/10/2026 08:02\n  At: London Waterloo\n"));
    check(
        "the outward leg, its padding gone and its window named, coach and seat first",
        page_starts(
            text,
            "Leg 1",
            "Departs: 01/10/2026 08:30\nFrom: London Waterloo\nTo: Station 1444\nCoach: C\n"
            "Seat: 42\nReserved: Seat\nFacing: Forwards\nFeature: Window\nTrain: GR1234\n"));
    check(
        "the return leg, a shared upper berth with an attribute as it stands",
        on_page(
            text,
            "Leg 2",
            "Coach: D\nBerth: 17A\nReserved: Sleeper berth\nFacing: Airline style\n"
            "Feature: ZQXV\nBunk: Upper\n  Cabin shared: Yes\n"));
    check(
        "the legs come straight after the first page",
        find_page(text, "Leg 1") && find_page(text, "Leg 1") < find_page(text, "Restrictions"));
    check("every leg read", !shows(text, "Not read: "));
    check(
        "no type code left on the screen",
        !shows(text, "Type code: 0") && !shows(text, "Type code: 1\n"));
    check("the ticket number, under Technical", technical(text, "Ticket number: 123456\n"));
    check("the fare type", technical(text, "Fare type: SOR\nRestriction code: OP\nID type: 1\n"));
    check(
        "the discount's code, rail's whole percent and its type under Technical",
        technical(
            text,
            "Discount code: YNG\n  Percentage: 33%\n  Code type: Status code\nSupplement: SLP\n"));
    check("the route code", technical(text, "Route code: 00700\n"));

    /* Sold through another station's retailer: VendorLoc says where. */
    p.retailer = 0x8000 | (1 << 10) | 72;
    furi_string_reset(text);
    flipso_format_product(text, &with, card, &p);
    check(
        "where it was sold, when the retailer is elsewhere",
        shows(text, "Sold at: Station 5685\n"));

    /* A discount that is not a card to carry has no railcard to number. */
    memcpy(p.terms.ticket.discount, "GS3  ", sizeof(p.terms.ticket.discount));
    furi_string_reset(text);
    flipso_format_product(text, &with, card, &p);
    check(
        "a GroupSave's ID is not a railcard",
        shows(text, "Discount: GroupSave\nRailcard or photocard number: Ends 4567\n"));
    house_style("reservation, GroupSave", text);

    /* Read again without its capture: what ItsoProduct holds still shows, and
     * nothing claims the reservations are there. */
    product_screen(
        text,
        f,
        card,
        &p,
        ItsoTypReservationTicket,
        true,
        reservation_group,
        sizeof(reservation_group));
    check("without the capture, the portions still show", shows(text, "Outward: 01/10/2026"));
    check("and the legs cannot be read", shows(text, "Reservations: Could not be read\n"));

    /* The summary carries the test flag under the product's line. */
    ItsoCard one = *card;
    one.products = &p;
    one.product_count = 1;
    furi_string_reset(text);
    flipso_format_summary(text, f, &one);
    check(
        "the summary says journeys, the railcard, and that it is a test",
        shows(text, "  Journeys left: 1\n  Valid only with: 16-25 Railcard\n  Test ticket: Yes\n"));
    house_style("reservation summary", text);

    itso_product_free(&p);
    flipso_capture_free(capture);
    furi_string_free(text);
}
