/**
 * @file test_parse.c
 * @brief The ITSO decoder's host tests: the synthetic CMD7 card, then the
 * media, product types and hostile inputs each test file covers.
 *
 * The synthetic card is built by build_card.py into card_data.h, and is read
 * the way a tap reads one - shell, directory, each product's group, then the
 * log - so each step's checks run on the card the steps before it left.
 */
#include "test_parse.h"

int main(void) {
    static ItsoCard card;
    itso_card_reset(&card);

    synthetic_shell(&card);
    synthetic_directory(&card);
    synthetic_entries(&card);
    synthetic_purse(&card);
    synthetic_id(&card);
    synthetic_period(&card);
    synthetic_journey(&card);
    synthetic_loyalty(&card);
    product_families(&card);
    synthetic_log(&card);

    printf("\n== Shell checksum ==\n");
    shell_checksum();

    printf("\n== CMD2 card ==\n");
    cmd2_card();
    compact_shell();
    full_shell_type2();
    space_saving_types();

    printf("\n== Fields from the TS 1000-5 review ==\n");
    spec_review_fields();
    reservation_ticket();
    rail_profile();

    printf("\n== Robustness ==\n");
    bus_stop_locations();
    location_rendering();
    shell_reject_reasons();
    sector_chains();
    log_sectors();
    oversized_directory();
    card_arrays();
    robustness();

    synthetic_review(&card);

    printf(
        "\n%s (%d failure%s)\n",
        failures ? "FAILED" : "ALL PASSED",
        failures,
        failures == 1 ? "" : "s");
    return failures != 0;
}
