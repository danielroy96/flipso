/**
 * @file test_capture.c
 * @brief Host-side test for the saved-card capture: the raw blocks a read yields, the
 * file they are written to, and the card they decode back into.
 *
 * The property that matters is that saving and loading changes nothing. A card
 * decoded straight from the blocks a read produced and the same card decoded
 * after a trip through the file must be identical, field for field - which is
 * checkable here with itso_card_equal(), which compares every field and the
 * products themselves.
 */
#include "test_capture.h"

int main(void) {
    build_groups();
    groups[0] = (Group){group1, sizeof(group1)};
    groups[1] = (Group){group2, sizeof(group2)};
    groups[2] = (Group){group3, sizeof(group3)};
    groups[3] = (Group){group4, sizeof(group4)};
    groups[4] = (Group){group5, sizeof(group5)};

    printf("Save and load\n");
    round_trip();
    chip_block();
    type2_card();
    full_type2_card();
    full_type2_refused();
    printf("\nIncomplete reads\n");
    partial();
    spare_entry();
    printf("\nLimits\n");
    limits();
    printf("\nHostile files\n");
    hostile_files();
    printf("\nA card read twice\n");
    merge_history();
    merge_replaced_product();
    merge_cap();
    merge_new_product();
    merge_type2();
    merge_full_type2();
    printf("\nProducts the card has dropped\n");
    merge_gone_product();
    gone_product_cap();
    gone_needs_a_directory();
    gone_full_chain();
    printf("\nValue histories on the heap\n");
    history_past_the_card();
    history_owned_by_each_card();
    history_reset_and_reread();
    history_load_merge_free_twice();

    printf("\n%s\n", failures ? "FAILURES" : "All capture tests passed");
    return failures ? 1 : 0;
}
