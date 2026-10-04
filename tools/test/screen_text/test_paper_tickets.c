/**
 * @file test_paper_tickets.c
 * @brief The screens of the Space Saving paper tickets a CMD4 carries.
 */
#include "test_format.h"

/** The Space Saving IPEs of a Type 2 paper ticket, which hold their facts in
 *  places a full IPE does not - a counter, a place last used, day ticks. */
void paper_ticket_screens(const FlipsoFormat* f, FuriString* text) {
    FlipsoFormat later = *f;
    later.now = FLIPSO_TEST_LATER;
    {
        static ItsoCard t2;
        const struct {
            const char* name;
            const uint8_t* pages;
            size_t len;
            const char* expect[5];
        } tickets[] = {
            {"TYP 27 day ticket",
             cmd4_pages,
             sizeof(cmd4_pages),
             {"Fare code: 0\n",
              "Ends at: Set by the operator\n",
              "Off-peak only: No\n",
              "Event 2: Tap out\n",
              "  Operator: SPT (Strathclyde)\n"}},
            {"TYP 29 return",
             cmd4_return,
             sizeof(cmd4_return),
             {"Backup count: 1\n  Agrees with rides left: Yes\n",
              "Last got off: Hillhead\n",
              "Price paid: \xC2\xA3"
              "3.30\n",
              "Ends at: Set by the operator\n",
              "Weekdays only: No\n"}},
            {"TYP 28 carnet",
             cmd4_carnet,
             sizeof(cmd4_carnet),
             {"Passes left: 3\n",
              "Day used: ",
              "Valid on day of issue: Yes\n",
              "Valid on day of expiry: Yes\n",
              "Ends at: 23:59 on the expiry date\n"}},
            {"TYP 29 multi-leg",
             cmd4_multileg,
             sizeof(cmd4_multileg),
             {"Rides left: 7\n",
              "  Journeys that day: 2\n",
              "Journey began: ",
              "Daily journey limit: 4\n",
              "  Changes made: 1\n"}},
            {"TYP 29 unused single",
             cmd4_unused,
             sizeof(cmd4_unused),
             {"Last used: Never\n", "Rides left: 1\n", "Area: ", "Paid by: Cash\n", "Class: "}},
            {"TYP 27 by fare value",
             cmd4_fare_value,
             sizeof(cmd4_fare_value),
             {"Area: Set by fare value\n  Fare value: \xC2\xA3"
              "1.75\n",
              "Event 1: Tap in\n",
              "Photocard number: 424242\n",
              "Passback timeout: Set by the operator\n",
              "Travellers: 1 adult\n"}},
            {"TYP 27 by location",
             cmd4_location,
             sizeof(cmd4_location),
             {"Area: Zones 1,2,3\n",
              "Photocard number: None\n",
              "Last used: Never\n",
              "Event 1: Other\n",
              "Issued: "}},
            {"TYP 28 between stations",
             cmd4_journey_area,
             sizeof(cmd4_journey_area),
             {"From: London Waterloo\n",
              "To: Station 1444\n",
              "Passes left: 6\n",
              "Valid on day of issue: No\n",
              "Ends at: 23:59 on the expiry date\n"}},
            {"TYP 29 scaled backup",
             cmd4_backup_scaled,
             sizeof(cmd4_backup_scaled),
             {"Backup count: Up to 12\n  Step: 4\n  Agrees with rides left: Yes\n",
              "Price paid: \xC2\xA3"
              "15.00\n",
              "Last used: Never\n",
              "Area: Set by the operator\n",
              "Class: Standard\n"}},
            {"TYP 29 torn backup",
             cmd4_backup_torn,
             sizeof(cmd4_backup_torn),
             {"Backup count: 3\n  Agrees with rides left: No\n",
              "Last used: Never\n",
              "Weekdays only: No\n",
              "Area: Set by the operator\n",
              "Issued: "}},
        };
        for(size_t i = 0; i < COUNT_OF(tickets); i++) {
            itso_card_reset(&t2);
            check("a whole CMD4 decodes", itso_parse_type2(&t2, tickets[i].pages, tickets[i].len));
            every_screen(tickets[i].name, f, &t2);
            furi_string_reset(text);
            flipso_format_product(text, f, &t2, &t2.products[0]);
            for(size_t e = 0; e < COUNT_OF(tickets[i].expect); e++) {
                char what[160];
                snprintf(what, sizeof(what), "%s shows %s", tickets[i].name, tickets[i].expect[e]);
                check(what, shows(text, tickets[i].expect[e]));
            }
            /* The owner's codes and the backup's cross-check are there for
             * whoever is debugging the ticket, not for its holder. */
            {
                char what[160];
                snprintf(
                    what, sizeof(what), "%s keeps its codes under Technical", tickets[i].name);
                check(
                    what,
                    (!shows(text, "Fare code: ") || technical(text, "Fare code: ")) &&
                        (!shows(text, "Backup count: ") || technical(text, "Backup count: ")));
            }
            /* The place a ticket was last used is not the start of a journey,
             * and a product with no Sector Chain Table claims no status. Only
             * an area recorded as a journey's two ends has a From line. */
            char what[160];
            if(!t2.space->area[1].valid) {
                snprintf(what, sizeof(what), "%s has no From line", tickets[i].name);
                check(what, !shows(text, "From: "));
            }
            snprintf(what, sizeof(what), "%s claims no status", tickets[i].name);
            check(what, !shows(text, "Status: "));
        }

        /* The Card screen of a paper ticket: its implied shell is not presented
         * as the card's own data, its UID is, and its state is its product's. */
        itso_card_reset(&t2);
        itso_parse_type2(&t2, cmd4_pages, sizeof(cmd4_pages));
        furi_string_reset(text);
        flipso_format_card(text, f, &t2, NULL, false, 0);
        check(
            "a paper ticket's number is under Technical, for what it is",
            technical(
                text,
                "Layout: Compact shell\n  Implied card number: 633597 8189 0000 0003\n"
                "  Shell operator number: 8189\n"));
        check(
            "and its issuer's number is with them",
            technical(text, "Operator number: 8323\n") && !shows(text, "(compact)"));
        check("and is not the screen's headline", !shows(text, "Card number\n"));
        check("a paper ticket shows its UID", shows(text, "UID: 04A2B3C4D5E6F7\n"));
        check("and its chip maker", shows(text, "Maker: NXP\n"));
        check("and its memory", shows(text, "Memory: 64 bytes\n"));
        check(
            "and which pages are locked, as ITSO requires",
            shows(text, "Locked pages: 6-13\n  As ITSO requires: Yes\n"));
        check("and that no lock bits are frozen", shows(text, "Lock bits frozen: None\n"));
        check("a paper ticket names its card type", shows(text, "Card type: Ultralight (CMD4)\n"));
        check("a compact shell says so", shows(text, "Layout: Compact shell\n"));
        check(
            "a compact shell shows no implied geometry",
            !shows(text, "sectors") && !shows(text, "Directory: ") && !shows(text, "Key set: ") &&
                !shows(text, "Update count: "));
        check("a paper ticket shows no 2041 expiry", !shows(text, "2041"));
        check("an in-date paper ticket is active", shows(text, "Status: Active\n"));
        furi_string_reset(text);
        flipso_format_card(text, &later, &t2, NULL, false, 0);
        check("an expired paper ticket says so", shows(text, "Status: Expired "));
        furi_string_reset(text);
        flipso_format_summary(text, &later, &t2);
        check(
            "and its summary says so once, of its ticket",
            shows(text, "\nPaper period ticket: Expired ") && !shows(text, "Ticket: Expired "));
        check("with no card expiry line", !shows(text, "Card expires"));

        /* A paper ticket's Summary answers what its holder asks: is it good,
         * how much is left on it, when or where it was last used, and what it
         * cost - all from its one product, since it keeps no log. */
        furi_string_reset(text);
        flipso_format_summary(text, f, &t2);
        check(
            "a day ticket's summary has its state, last use and price",
            shows(
                text,
                "Ticket: Active\n"
                "Paper period ticket: Until 27/09/2026\n"
                "Last used: 27/09/2026 17:47\n"
                "Price paid: \xC2\xA3"
                "4.45\n"));

        itso_card_reset(&t2);
        itso_parse_type2(&t2, cmd4_return, sizeof(cmd4_return));
        furi_string_reset(text);
        flipso_format_summary(text, f, &t2);
        check(
            "a return's summary has its rides left and where it was last used",
            shows(
                text,
                "Ticket: Active\n"
                "Multi-use ticket: Until 26/09/2026\n"
                "  Rides left: 1\n"
                "Last used: Hillhead\n"
                "Price paid: \xC2\xA3"
                "3.30\n"));
        check("a place with no time claims no time", !shows(text, "When: "));

        itso_card_reset(&t2);
        itso_parse_type2(&t2, cmd4_multileg, sizeof(cmd4_multileg));
        furi_string_reset(text);
        flipso_format_summary(text, f, &t2);
        check(
            "a multi-leg ticket's summary has when it was last used",
            shows(text, "  Rides left: 7\nLast used: 21/09/2026 08:20\n"));

        itso_card_reset(&t2);
        itso_parse_type2(&t2, cmd4_location, sizeof(cmd4_location));
        furi_string_reset(text);
        flipso_format_summary(text, f, &t2);
        check("a day ticket never used says so", shows(text, "Last used: Never\n"));

        itso_card_reset(&t2);
        itso_parse_type2(&t2, cmd4_unused, sizeof(cmd4_unused));
        furi_string_reset(text);
        flipso_format_summary(text, f, &t2);
        check("a single never used says so", shows(text, "Last used: Never\n"));
        furi_string_reset(text);
        flipso_format_card(text, f, &t2, NULL, false, 0);
        check("an Infineon chip is named", shows(text, "Maker: Infineon\n"));
        check(
            "a ticket locked short of ITSO's rule says what is still writable",
            shows(text, "Locked pages: 6-9\n  As ITSO requires: No\n  Still writable: 10-13\n"));

        itso_card_reset(&t2);
        itso_parse_type2(&t2, cmd4_fare_value, sizeof(cmd4_fare_value));
        furi_string_reset(text);
        flipso_format_card(text, f, &t2, NULL, false, 0);
        check("frozen lock bits are listed", shows(text, "Lock bits frozen: 3-15\n"));

        itso_card_reset(&t2);
        itso_parse_type2(&t2, cmd4_spent, sizeof(cmd4_spent));
        furi_string_reset(text);
        flipso_format_summary(text, f, &t2);
        check(
            "a ticket with no rides left is used up, said once",
            shows(text, "Multi-use ticket: Used up\n") && !shows(text, "Ticket: Used up\n"));

        itso_card_reset(&t2);
        itso_parse_type2(&t2, cmd4_blocked, sizeof(cmd4_blocked));
        every_screen("blocked paper ticket", f, &t2);
        furi_string_reset(text);
        flipso_format_summary(text, f, &t2);
        check(
            "a zero-Seal ticket is blocked, said once",
            shows(text, "ticket: Blocked\n") && !shows(text, "\nTicket: Blocked\n"));
    }
}
