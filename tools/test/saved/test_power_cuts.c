/**
 * @file test_power_cuts.c
 * @brief What a save interrupted at each step leaves behind.
 */
#include "test_saved.h"

/*
 * Saving over a card moves the old record aside, puts the new one in its place
 * and then removes the old - so a power cut at any point leaves a whole copy
 * under a name recovery knows, rather than none under the card's.
 */
void power_cuts(void) {
    clean();
    flipso_saved_mkdir();

    /* The normal case leaves nothing behind. */
    leave("Card", "", 1000);
    FlipsoCapture* newer = make(card_shell, sizeof(card_shell), card_dir, sizeof(card_dir), 2000);
    FuriString* path = furi_string_alloc();
    flipso_saved_path(path, "Card");
    check("saving over a card works", flipso_saved_write(newer, furi_string_get_cstr(path)));
    check("with the new copy in place", read_time("Card") == 2000);
    check("and nothing left beside it", !exists("Card.flipso.old") && !exists("Card.flipso.tmp"));

    /* A failed rename puts the old record back. */
    stub_rename_fails = true;
    check(
        "a save whose move fails reports it",
        !flipso_saved_write(newer, furi_string_get_cstr(path)));
    stub_rename_fails = false;
    check("and leaves the old record as it was", read_time("Card") == 2000);
    check("with no copy left over", !exists("Card.flipso.tmp"));
    clean();
    flipso_saved_mkdir();

    /* Cut after the old record stepped aside, before the new one moved in:
     * the new copy is whole, so it wins. */
    leave("A", ".old", 1000);
    leave("A", ".tmp", 2000);
    /* Cut with only the old record aside and no new copy: it goes back. */
    leave("B", ".old", 1000);
    /* Cut after the new copy moved in, before the old was removed. */
    leave("C", "", 2000);
    leave("C", ".old", 1000);
    /* Cut while a copy was being written: the record itself is untouched. */
    leave("D", "", 1000);
    leave("D", ".tmp", 2000);
    /* Cut in the middle of a change of case. */
    leave("E", ".ren", 1000);
    /* A first save cut short: nothing to go back to, and the copy may be half. */
    leave("F", ".tmp", 2000);

    flipso_saved_recover();
    check("a whole new copy is put in place", read_time("A") == 2000);
    check("an old record with no new copy goes back", read_time("B") == 1000);
    check("a finished save keeps its new copy", read_time("C") == 2000);
    check("a write cut short leaves the record alone", read_time("D") == 1000);
    check("a change of case goes back to its name", read_time("E") == 1000);
    check("a first save cut short is not guessed at", !exists("F.flipso"));
    check(
        "and nothing is left over",
        !exists("A.flipso.old") && !exists("A.flipso.tmp") && !exists("B.flipso.old") &&
            !exists("C.flipso.old") && !exists("D.flipso.tmp") && !exists("E.flipso.ren") &&
            !exists("F.flipso.tmp"));

    furi_string_free(path);
    flipso_capture_free(newer);
    clean();
}
