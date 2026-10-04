/**
 * @file test_renaming.c
 * @brief Renaming a saved card.
 */
#include "test_saved.h"

void renaming(void) {
    clean();
    flipso_saved_mkdir();

    FlipsoCapture* card = make(card_shell, sizeof(card_shell), card_dir, sizeof(card_dir), 1000);
    FuriString* from = furi_string_alloc();
    FuriString* to = furi_string_alloc();

    flipso_saved_path(from, "Before");
    flipso_saved_write(card, furi_string_get_cstr(from));
    flipso_saved_path(to, "After");

    check(
        "a card can be renamed",
        flipso_saved_rename(furi_string_get_cstr(from), furi_string_get_cstr(to)));
    check("the old name has gone", access(furi_string_get_cstr(from), F_OK) != 0);
    check("and the new one is there", access(furi_string_get_cstr(to), F_OK) == 0);

    /* The card is the same card, so it is still found by its number, now under
     * the new name. */
    FuriString* found = furi_string_alloc();
    check("it is still its own record", flipso_saved_find(card, found, NULL));
    check(
        "under the new name",
        strcmp(furi_string_get_cstr(found), FLIPSO_SAVED_FOLDER "/After.flipso") == 0);

    check(
        "renaming to the name it has is allowed",
        flipso_saved_rename(furi_string_get_cstr(to), furi_string_get_cstr(to)));
    check("and leaves it there", access(furi_string_get_cstr(to), F_OK) == 0);

    /* Onto a name another card holds: refused, and neither file is touched. */
    FlipsoCapture* other = make(cmd2_shell, sizeof(cmd2_shell), cmd2_dir, sizeof(cmd2_dir), 2000);
    flipso_saved_path(from, "Occupied");
    flipso_saved_write(other, furi_string_get_cstr(from));
    check(
        "renaming onto a name in use is refused",
        !flipso_saved_rename(furi_string_get_cstr(to), furi_string_get_cstr(from)));
    check("the card stays where it was", access(furi_string_get_cstr(to), F_OK) == 0);
    check("and the card in the way is untouched", access(furi_string_get_cstr(from), F_OK) == 0);
    {
        /* Only the case of the name changing. The SD card, like this host's
         * disk, ignores case, so this is a rename onto a name that "exists" -
         * the card's own - and must neither be refused nor lose the card. */
        FuriString* lower = furi_string_alloc();
        flipso_saved_path(lower, "after");
        check(
            "changing only the case of a name works",
            flipso_saved_rename(furi_string_get_cstr(to), furi_string_get_cstr(lower)));
        check("and the card is still there", access(furi_string_get_cstr(lower), F_OK) == 0);
        FuriString* where = furi_string_alloc();
        check("and still found by its number", flipso_saved_find(card, where, NULL));
        flipso_saved_rename(furi_string_get_cstr(lower), furi_string_get_cstr(to));
        furi_string_free(where);
        furi_string_free(lower);
    }

    FuriString* missing = furi_string_alloc();
    flipso_saved_path(missing, "Never existed");
    flipso_saved_path(to, "Somewhere else");
    check(
        "renaming a card that is not there fails",
        !flipso_saved_rename(furi_string_get_cstr(missing), furi_string_get_cstr(to)));

    furi_string_free(missing);
    furi_string_free(found);
    furi_string_free(to);
    furi_string_free(from);
    flipso_capture_free(other);
    flipso_capture_free(card);
    clean();
}
