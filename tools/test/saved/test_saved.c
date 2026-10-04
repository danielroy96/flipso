/**
 * @file test_saved.c
 * @brief Host-side test for saved cards on disk: the naming, the file, and what
 * happens to each when something is wrong.
 *
 * The capture test next door covers the blocks and the format. This covers the
 * layer around them, against real files in a real directory, so that a write
 * that half-succeeds or a read of somebody else's file is a test failure here
 * rather than a puzzle on the device.
 */
#include "test_saved.h"

int main(void) {
    printf("Naming\n");
    names();
    printf("\nSaving and loading\n");
    round_trip();
    printf("\nFiles that are not ours\n");
    bad_files();
    printf("\nFinding a card's own record\n");
    finding();
    printf("\nRenaming\n");
    renaming();
    printf("\nPower cuts\n");
    power_cuts();
    printf("\nDemo cards\n");
    demos();

    printf("\n%s\n", failures ? "FAILURES" : "All saved card tests passed");
    return failures ? 1 : 0;
}
