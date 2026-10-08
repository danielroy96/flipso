/**
 * @file test_demo_screens.c
 * @brief What particular demo cards have to say, beyond the house style.
 */
#include "test_format.h"

/*
 * The encodings Demo 07 carries from a real GWR Touch card, pinned by the lines
 * only they produce: a gate check-in and check-out in the revision 4 shapes a
 * rail gate writes, each naming the reader that wrote it; a revision 2 period
 * ticket with and without CPICC, and no value record; an ID with an empty
 * bitmap; and a Directory InstanceID with a 16-bit extended ISAM OID.
 */
void demo_seven(const FlipsoFormat* f, const ItsoCard* card) {
    FuriString* text = furi_string_alloc();

    flipso_format_taps(text, f, card);
    check(
        "a check-in names the operator whose gate it was",
        page_starts(text, "Tap in 18/09", "When: 18/09/2026 17:52\n") &&
            on_page(text, "Tap in 18/09", "Tapped in with: Transport for London\n"));
    /* The stub table knows no GWR stations, so the check is the shape, not the
     * names: a dated tap out with a destination and no fare line. */
    check(
        "a check-out with no amount is still a journey",
        page_starts(text, "Tap out 18/09", "When: 18/09/2026 18:49\nFrom: ") &&
            on_page(text, "Tap out 18/09", "\nTo: ") && !shows(text, "Fare: "));
    check(
        "each record names the reader that wrote it, under Technical",
        technical(text, "\e#Tap out 18/09\n  When: 18/09/2026 18:49\n  Reader: FF00A3C7\n"));

    furi_string_reset(text);
    flipso_format_card(text, f, card, NULL, false, 0);
    check(
        "the directory's last writer is decoded from an extended ISAM",
        technical(text, "Last updated by machine: 004E30F3\n  Operator: Transport for London\n"));
    check(
        "with the version of the key its seal is made with, even at 0",
        technical(text, "  Operator: Transport for London\nDirectory seal key version: 0\n"));
    check("160-byte sectors are the layout", shows(text, "Layout: 16 sectors of 160 bytes\n"));

    /* Its ID never expires and nor does the entitlement on it, in the other
     * of the two encodings of "never": one line says so, not two. */
    furi_string_reset(text);
    flipso_format_id(text, f, card);
    check(
        "an entitlement that never ends is not a second No expiry",
        shows(text, "Expires: No expiry\n") && !shows(text, "Entitlement until: "));

    furi_string_reset(text);
    flipso_format_id(text, f, card);
    check("an ID with nothing optional says so", shows(text, "Name: Not stored\n"));
    check("and still has its language", shows(text, "Language: English\n"));

    for(uint8_t i = 0; i < card->product_count; i++) {
        const ItsoProduct* p = &card->products[i];
        if(p->typ != ItsoTypPeriodTicket) continue;
        furi_string_reset(text);
        flipso_format_product(text, f, card, p);
        check("a revision 2 season ticket has its price", shows(text, "Price paid: \xC2\xA3"));
        check("and its end time", shows(text, "Ends at: 04:30 the day after expiry\n"));
        check("and its validity code", shows(text, "Validity code: 17\n"));
        check(
            "its CPICC shows only when the bitmap says it is there",
            shows(text, "Issuer code: ") == ((p->bitmap & 0x10) != 0));
        check("its expiry comes from the directory", shows(text, "Expires: "));
    }
    furi_string_free(text);
}

/*
 * The two full-shell Type 2 cards: the chip named from the media definition,
 * the lock bytes judged against what TS 1000-10 clause 10.23.1 recommends, a
 * CMD9's Abacus, and a history that takes both copies of the value records.
 */
void demo_type2_full(const FlipsoFormat* f, const ItsoCard* card, bool ntag) {
    FuriString* text = furi_string_alloc();

    flipso_format_card(text, f, card, NULL, false, 0);
    check("a full-shell tag has a card number of its own", page_starts(text, "Card", "633597 "));
    check("its shell pages are locked", shows(text, "Locked pages: 4-11\n  Shell locked: Yes\n"));
    if(ntag) {
        check("CMD9 names its chip", shows(text, "Chip: NTAG215\n"));
        check("and its memory", shows(text, "Memory: 540 bytes\n"));
        check("and its media", shows(text, "Card type: NTAG (CMD9)\n"));
        check(
            "its Abacus counts down its uses", shows(text, "Uses left: 10\n  Abacus: 5 of 16\n"));
        check("64-byte sectors are the layout", shows(text, "Layout: 9 sectors of 64 bytes\n"));

        /* A ride off the carnet, which the reader left unspecified: the fare
         * is the ride's value, and nothing claims a purse paid it. */
        FuriString* taps = furi_string_alloc();
        flipso_format_taps(taps, f, card);
        check(
            "a carnet ride names its fare and its carnet, and no purse",
            shows(
                taps,
                "Fare: \xC2\xA3"
                "1.80\nProduct: Journey ticket\n") &&
                !shows(taps, "Paid by: "));
        furi_string_reset(taps);
        flipso_format_product(taps, f, card, &card->products[0]);
        check(
            "a bus ticket's route code is the operator's, and left as it is",
            !shows(taps, "Any permitted"));
        furi_string_free(taps);

        /* The same card with its Abacus run out (TS 1000-10 table 107). A copy
         * borrows the card's product and journey arrays, so it lives only as
         * long as this block and is never reset. */
        ItsoCard retired = *card;
        retired.chip_abacus = 16;
        furi_string_reset(text);
        flipso_format_card(text, f, &retired, NULL, false, 0);
        house_style("a retired CMD9's card screen", text);
        check("a retired CMD9 says so", shows(text, "Status: Retired\n"));
        check(
            "and has no uses left", shows(text, "Uses left: None, retired\n  Abacus: 16 of 16\n"));
        furi_string_reset(text);
        flipso_format_summary(text, f, &retired);
        check("its summary leads with it", shows(text, "Card: Retired\n"));
        furi_string_reset(text);
        flipso_format_card(text, f, card, NULL, false, 0);
    } else {
        check("CMD10 names its chip", shows(text, "Chip: Ultralight EV1\n"));
        check("and its media", shows(text, "Card type: Ultralight EV1 (CMD10)\n"));
        check("and has no Abacus", !shows(text, "Abacus"));
        check("its rotated shell keeps its MCRN", shows(text, "Card reference: 4917250331\n"));
    }

    furi_string_reset(text);
    flipso_format_product(text, f, card, &card->products[0]);
    check(
        ntag ? "CMD9 history reaches into the other copy" :
               "CMD10 history reaches into the other copy",
        shows(text, ntag ? "Rides left: 7\n" : "Passes left: 1\n"));

    furi_string_reset(text);
    flipso_format_taps(text, f, card);
    check(
        "both log records are journeys, a page each",
        card->tap_count == 2 &&
            pages_with_icon(text, FlipsoIconTaps) == 2 + (card->log_entry_valid ? 1 : 0));
    furi_string_free(text);
}

void demo_one(const FlipsoFormat* f, const ItsoCard* card) {
    FuriString* text = furi_string_alloc();

    /* Its pages, which between them have every kind of product but paper, in
     * the order the holder reads them. */
    static const struct {
        uint8_t typ;
        const char* pages;
    } kinds[] = {
        {ItsoTypPeriodTicket, "Period ticket|Passes|Conditions|Purchase|History|Technical"},
        {ItsoTypJourneyTicket, "Journey ticket|Rides|Conditions|Purchase|History|Technical"},
        {ItsoTypReservationTicket,
         "Reserved journey|Leg 1|Leg 2|Restrictions|Route|Details|Purchase|History|Technical"},
        {ItsoTypChargeToAccount2, "Charge to account|Account|History|Technical"},
        {ItsoTypVoucher, "Voucher|Conditions|Purchase|History|Technical"},
        {ItsoTypLoyalty1, "Loyalty|History|Technical"},
        {ItsoTypTolling, "Toll pass|Conditions|Purchase|History|Technical"},
    };
    for(size_t k = 0; k < COUNT_OF(kinds); k++) {
        for(uint8_t i = 0; i < card->product_count; i++) {
            if(card->products[i].typ != kinds[k].typ) continue;
            furi_string_reset(text);
            flipso_format_product(text, f, card, &card->products[i]);
            char what[128];
            snprintf(what, sizeof(what), "a %s has its pages in order", kinds[k].pages);
            check(what, titles_are(text, kinds[k].pages));
        }
    }
    /* The toll pass, from the saved file: its owner data is read from the
     * capture as the screen is drawn, and is text. */
    for(uint8_t i = 0; i < card->product_count; i++) {
        if(card->products[i].typ != ItsoTypTolling) continue;
        furi_string_reset(text);
        flipso_format_product(text, f, card, &card->products[i]);
        check(
            "the toll pass has never been used, and counts crossings",
            on_page(text, "Toll pass", "Crossings left: 10\n") &&
                on_page(text, "Toll pass", "Status: Unused\n") &&
                on_page(text, "Toll pass", "  Renewal adds: 10 crossings\n"));
        check("the toll pass's vehicle class", on_page(text, "Conditions", "Vehicle class: 2\n"));
        check(
            "the toll pass's owner data, as text, from the saved file",
            technical(text, "Operator's own data: DC00417\n") &&
                technical(text, "Passback timeout: Set by the operator\n"));
    }
    furi_string_reset(text);
    flipso_format_payg(text, f, card);
    check("the purse's pages", titles_are(text, "Pay as you go|Top-up|History|Technical"));
    check(
        "the purse's history says what each transaction did",
        page_starts(
            text,
            "History",
            "Multi-leg journey\n  When: 19/09/2026 12:31\n"
            "  Amount: -\xC2\xA3"
            "3.50\n  Balance: \xC2\xA3"
            "24.15\nFare paid\n  When: 18/09/2026 08:12\n  Amount: -\xC2\xA3"
            "3.55\n  Balance: \xC2\xA3"
            "27.65\nTop-up\n  When: 12/08/2026 18:05\n  Balance: \xC2\xA3"
            "31.20\n"));
    check(
        "a purse names who sold it after whose it is",
        on_page(text, "Pay as you go", "Operator: Southeastern\nSold by: National Rail purse\n"));
    {
        /* Two purses: each one's pages, and one Technical page for both, last. */
        const ItsoProduct* purse = flipso_find_product(card, ItsoTypStoredTravelRights);
        static ItsoProduct two[2];
        two[0] = two[1] = *purse;
        ItsoCard purses = *card;
        purses.products = two;
        purses.product_count = 2;
        furi_string_reset(text);
        flipso_format_payg(text, f, &purses);
        house_style("two purses", text);
        check(
            "two purses share one Technical page, last",
            titles_are(
                text, "Pay as you go|Top-up|History|Pay as you go|Top-up|History|Technical") &&
                on_page(text, "Technical", "\e#Pay as you go\nITSO type: 2\n") &&
                on_page(text, "Technical", "\n\n\e#Pay as you go\nITSO type: 2\n"));
    }
    furi_string_reset(text);
    flipso_format_id(text, f, card);
    check(
        "the ID and the entitlement, then their codes on one page",
        titles_are(text, "ITSO ID|Holder|ID terms|Entitlement|Entitlement terms|Technical") &&
            on_page(text, "Technical", "\e#ITSO ID\nITSO type: 16\n  Operator's sub-type: 1\n") &&
            on_page(text, "Technical", "\n\n\e#Entitlement\nITSO type: 14\n"));
    check(
        "the ID's first page is who and what the holder is",
        page_starts(
            text,
            "ITSO ID",
            "Name: JAMIE OKONKWO-LEE\nStatus: Active\nExpires: 31/08/2031\n  Time left: 5 years\n") &&
            on_page(text, "ITSO ID", "Operator: SEFT Central Products\n"));
    check("the holder's page", page_starts(text, "Holder", "Born: 14/05/1978\nGender: Male\n"));
    furi_string_reset(text);
    flipso_format_card(text, f, card, "Demo 01", true, 0);
    check("the card's pages, the codes last", titles_are(text, "Card|Chip|Demo card|Technical"));
    check(
        "the card page has the number, the state and the issuer",
        page_starts(
            text,
            "Card",
            "633597 0289 0100 0016\nStatus: Active\nExpires: 31/08/2031\n  Time left: 5 years\n"
            "Operator: Southeastern\n"));
    furi_string_reset(text);
    flipso_format_summary(text, f, card);
    check("the summary's pages", titles_are(text, "Summary|Not valid"));
    check(
        "the tickets that can be used today lead it, a season by the pass in use",
        page_starts(text, "Summary", "Period ticket: Until 20/10/2026\n  Passes left: 5\n"));
    check(
        "then the money, the holder and who they are to the scheme",
        on_page(
            text,
            "Summary",
            "\nPay as you go: \xC2\xA3"
            "24.15\nHolder: JAMIE OKONKWO-LEE\nConcession: Commuter\n"));
    check(
        "then the card, and the last tap",
        on_page(
            text,
            "Summary",
            "\nCard: Active\nCard expires: 31/08/2031\nLast tap: London Bridge\n"));
    check(
        "an account is good until it stops paying, not until it leaves the card",
        on_page(text, "Summary", "Charge to account: Until 05/04/2027\n"));
    check(
        "and the ones that cannot be used, a page of their own",
        on_page(text, "Not valid", "Loyalty: Blocked\n") &&
            !on_page(text, "Summary", "Loyalty: Blocked\n"));
    furi_string_reset(text);
    flipso_format_taps(text, f, card);
    check(
        "the journeys: the last tap, a page a journey titled by its day, then the readers",
        titles_are(
            text, "Last tap|Tap in 21/09|Tap out 21/09|Tap out 19/09|Tap out 18/09|Technical"));

    flipso_format_summary(text, f, card);
    check(
        "an ITSO ID is summed up by its concession, not as the ID",
        shows(text, "Concession: Commuter\n") && !shows(text, "ITSO ID: "));

    furi_string_reset(text);
    flipso_format_taps(text, f, card);
    check(
        "the products a gate checked are one line",
        shows(text, "\nProducts checked: Period ticket, Pay as you go, Journey ticket\n"));
    check(
        "the last tap says where it was",
        page_starts(text, "Last tap", "When: 21/09/2026 17:46\nAt: London Bridge\n"));
    check(
        "and whether the holder is inside the gates, last of all",
        on_page(text, "Last tap", "Passback timeout: 20 min\nInside ticket gates: Yes\n"));
    {
        /* The place belongs to the newest record, so it is only said when that
         * record is the entry's: not after an update that wrote none, and not
         * when the entry is newer than the record. A copy borrows the card's
         * arrays, so it is never reset. */
        ItsoCard unrecorded = *card;
        unrecorded.log_normal_mode = false;
        FuriString* other = furi_string_alloc();
        flipso_format_taps(other, f, &unrecorded);
        check(
            "an entry with no journey record names no place",
            !on_page(other, "Last tap", "At: ") &&
                on_page(other, "Last tap", "Journey details: Not recorded\n"));
        ItsoCard later = *card;
        later.log_dts += 60;
        furi_string_reset(other);
        flipso_format_taps(other, f, &later);
        check("nor one newer than the newest record", !on_page(other, "Last tap", "At: "));
        furi_string_free(other);
    }
    check(
        "a journey's page leads with its times, in the order they happened",
        strstr(
            furi_string_get_cstr(text),
            "\e#\x14Tap out 21/09\nIn: 21/09/2026 07:12\nOut: 21/09/2026 08:03\nJourney time: 51 min\n") !=
            NULL);
    check(
        "a reader names its machine, then its operator, under Technical",
        technical(text, "  Tap-in reader: 01020304\n    Operator: "));
    check(
        "and the tap-in record's sequence number with them",
        technical(
            text, "  Tap-in reader: 01020304\n    Operator: Unknown (32)\n    Sequence: 9\n"));
    check(
        "and the journey it belongs to keeps only where and when",
        !before_technical(text, "Tapped in on: ") && !before_technical(text, "reader: "));
    check("passback is called passback", shows(text, "Passback timeout: 20 min\n"));
    /* The gates wrote ITSO's own IIN, in BCD as every IIN is: ITSO's network,
     * not one outside it. */
    check("a record from ITSO's own network does not say otherwise", !shows(text, "Outside ITSO"));

    /* The purse and the ID have menu rows of their own, so the product list
     * leaves them out - and their own screens carry the Technical section the
     * list's detail screen would have. */
    uint8_t listed = 0;
    for(uint8_t i = 0; i < card->product_count; i++) {
        if(flipso_product_listed(&card->products[i])) listed++;
    }
    check(
        "the product list leaves out the purse, the ID and the entitlement",
        listed == card->product_count - 3);
    ItsoProduct dropped = card->products[0];
    dropped.on_card = false;
    check("but lists a purse the card has dropped", flipso_product_listed(&dropped));
    furi_string_reset(text);
    flipso_format_payg(text, f, card);
    check(
        "the purse screen has its technical details",
        page_starts(text, "Technical", "ITSO type: 2\n  Operator's sub-type: 0\n"));
    furi_string_reset(text);
    flipso_format_id(text, f, card);
    check(
        "the ID screen has technical details for each product",
        shows(text, "ITSO type: 16\n") && shows(text, "ITSO type: 14\n"));

    /* An identity document that is another product names it, a loyalty
     * scheme's own bytes are shown as they stand, and an owner numbered by
     * another network says so beside its number. */
    for(uint8_t i = 0; i < card->product_count; i++) {
        const ItsoProduct* p = &card->products[i];
        furi_string_reset(text);
        flipso_format_product(text, f, card, p);
        if(p->typ == ItsoTypPeriodTicket) {
            check(
                "a period ticket names the ID it needs, near the top",
                shows(text, "Valid only with: ITSO ID\nOperator: "));
            check(
                "its passback is an instruction to the gate, under Technical",
                technical(text, "Passback timeout: Set by the operator\n"));
            check(
                "its type is ITSO's, then the operator's own sub-type",
                technical(text, "ITSO type: 22\n  Operator's sub-type: 2\n"));
            check(
                "its bitmap is the elements it names, from table 3.28",
                technical(text, "Optional fields: Locations and route, ID document\n"));
            check(
                "and the remote changes its value record has had",
                technical(text, "Remote changes applied: 3\n"));
        } else if(p->typ == ItsoTypReservationTicket) {
            check(
                "a reserved journey is named by its ticket type, after where it goes",
                on_page(text, "Reserved journey", "\nTicket type: Anytime Return\nStatus: "));
            check(
                "an off-peak band from midnight is a time not to travel before",
                on_page(
                    text,
                    "Restrictions",
                    "Valid times: Not before 09:30\n  Days: Mon-Fri\n"
                    "  Applies to: Outward departures\n"));
            check(
                "a reservation's bitmap, from table 137",
                technical(
                    text,
                    "Optional fields: Passenger, Discounts, routes and restrictions, "
                    "Reservations\n"));
        } else if(p->typ == ItsoTypChargeToAccount2) {
            check(
                "an account's end date says what ends",
                on_page(text, "Charge to account", "Account ends: 05/04/2027\n") &&
                    !shows(text, "Valid to: "));
        } else if(p->typ == ItsoTypLoyalty2) {
            check(
                "an owner in the extended numbering range says so, not its low bits",
                technical(text, "Operator number: 13870\n  Range: Extended\n"));
        } else if(p->typ == ItsoTypLoyalty1) {
            check("loyalty shows its owner's data", shows(text, "Operator's own data: 321\n"));
        } else if(p->typ == ItsoTypJourneyTicket) {
            check(
                "an owner on another network is a detail of its number",
                technical(text, "Operator number: 289\n  Network: Not the card's own\n"));
            check(
                "a rail ticket's route 00000 is any permitted route",
                technical(text, "Route code: Any permitted (00000)\n"));
        }
    }

    /* Its reserved journey is in use as far as the card's chain says, and out
     * of date as far as its expiry, 31/03/2026, says: the second is the one to
     * show. Checked on 2026-09-21, after that expiry. */
    FlipsoFormat after = *f;
    after.now = 1790000000u;
    for(uint8_t i = 0; i < card->product_count; i++) {
        if(card->products[i].typ != ItsoTypReservationTicket) continue;
        furi_string_reset(text);
        flipso_format_product(text, &after, card, &card->products[i]);
        check("an expired product is not called active", shows(text, "Status: Expired\n"));
        check("and says so once", !shows(text, "Status: Active\n"));
        /* A revision 2 TYP 24, chained across five sectors, its two seats in
         * the VGXRef 3 extension after its one value record. */
        check(
            "the reserved journey is a return with a day out and a month back",
            on_page(text, "Details", "Sold as: Return\n  Journeys sold: 2\n") &&
                on_page(
                    text,
                    "Reserved journey",
                    "Outward: 13/03/2026 only\nReturn: 13/03/2026 to 12/04/2026\n"));
        check(
            "it is valid only with its railcard, on the first page",
            on_page(
                text,
                "Reserved journey",
                "Valid only with: Disabled Persons Railcard\n"
                "  Railcard number: Ends 1372\nOperator: Southeastern\n") &&
                page_starts(text, "Details", "Journeys left: 0\n") &&
                on_page(text, "Purchase", "Sold by: Retailer 5230\n"));
        check(
            "it names the railcard product it goes with",
            shows(text, "Part of this ticket: Entitlement\n"));
        check(
            "and its two seats, a page each",
            page_starts(text, "Purchase", "Booking reference: 8KQ2TX4M\n") &&
                page_starts(text, "Leg 1", "Departs: 13/03/2026 10:00\n") &&
                page_starts(text, "Leg 2", "Departs: 20/03/2026 14:00\n") &&
                !page_of(text, "Leg 3") && !shows(text, "Not read: "));
        check(
            "with the discount's code and percentage under Technical",
            technical(text, "Discount code: DIS\n  Percentage: 33%\n"));
        check(
            "and its seats' positions in words",
            on_page(text, "Leg 1", "Feature: Table\n") &&
                on_page(text, "Leg 2", "Feature: Aisle\n"));
    }
    furi_string_free(text);
}

/* A paper carnet's pages, and its summary on one page as a ticket's is. */
void demo_fourteen(const FlipsoFormat* f, const ItsoCard* card) {
    FuriString* text = furi_string_alloc();
    flipso_format_product(text, f, card, &card->products[0]);
    check(
        "a paper ticket's pages",
        titles_are(text, "Book of tickets|Conditions|Use|Purchase|Technical"));
    check(
        "the days a carnet was used are its use, with the passes no tick records",
        page_starts(
            text, "Use", "Used on day of issue: Yes\nTicket kept for last day: No\nDay used: "));
    check("its operator ends its first page", on_page(text, "Book of tickets", "Operator: SPT"));
    furi_string_reset(text);
    flipso_format_summary(text, f, card);
    check("a paper ticket's summary is one page", titles_are(text, "Summary"));
    furi_string_free(text);
}

/* A card whose saved file remembers journeys the card has dropped. */
void demo_four(const FlipsoFormat* f, const ItsoCard* card) {
    FuriString* text = furi_string_alloc();
    flipso_format_taps(text, f, card);
    uint8_t on_card = 0, past = 0;
    for(uint8_t i = 0; i < card->tap_count; i++) {
        if(card->taps[i].on_card) {
            on_card++;
        } else {
            past++;
        }
    }
    check(
        "each journey the card holds has a page in the taps icon",
        past > 0 && pages_with_icon(text, FlipsoIconTaps) == on_card + 1);
    check(
        "and each the file remembers one in the clock",
        pages_with_icon(text, FlipsoIconPast) == past);
    /* And says so in words, as a dropped product does. */
    int said = 0;
    for(const char* p = furi_string_get_cstr(text); (p = strchr(p, '\f')) != NULL; p++) {
        if(p[1] == '\e' && p[2] == '#' && p[3] == (char)(FLIPSO_TEXT_ICON_BASE + FlipsoIconPast) &&
           strstr(p, "\nOn card: No longer\n") == strchr(p, '\n')) {
            said++;
        }
    }
    check("every journey the file remembers says it is no longer on the card", said == past);
    check("and none the card holds does", occurrences_of(text, "On card: No longer\n") == past);

    /* Its purse's history runs on into the file's, with TS# 206 missing: the
     * fare after the gap has a balance and no amount. */
    furi_string_reset(text);
    flipso_format_payg(text, f, card);
    check(
        "the last transaction is an auto top-up, signed",
        page_starts(
            text,
            "History",
            "Auto top-up\n  When: 20/09/2026 18:31\n  Amount: +\xC2\xA3"
            "15.00\n  Balance: \xC2\xA3"
            "26.45\n"));
    check(
        "the amount runs across from the card to the file",
        page_starts(
            text,
            "Off card",
            "Fare paid\n  When: 01/09/2026 08:01\n  Amount: -\xC2\xA3"
            "3.55\n"));
    check(
        "a fare after a gap in TS# has no amount",
        on_page(
            text,
            "Off card",
            "Fare paid\n  When: 31/07/2026 17:40\n  Balance: \xC2\xA3"
            "14.90\n"));
    check(
        "nor does the oldest record",
        on_page(
            text,
            "Off card",
            "Top-up\n  When: 02/06/2026 12:15\n  Balance: \xC2\xA3"
            "22.00\n"));
    furi_string_free(text);
}
