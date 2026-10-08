/**
 * @file test_finding.c
 * @brief Finding a card's own record, so that a second read updates it.
 */
#include "test_saved.h"

/*
 * The same card read twice should update the record it already has rather than
 * leaving two of them: what changes between reads is the products on the card,
 * which is the thing worth keeping current.
 */
void finding(void) {
    clean();
    flipso_saved_mkdir();

    FlipsoCapture* mine = make(card_shell, sizeof(card_shell), card_dir, sizeof(card_dir), 1000);
    FlipsoCapture* other = make(cmd2_shell, sizeof(cmd2_shell), cmd2_dir, sizeof(cmd2_dir), 2000);

    char a[ITSO_ISRN_DIGITS + 1], b[ITSO_ISRN_DIGITS + 1];
    flipso_capture_card_number(mine, a);
    flipso_capture_card_number(other, b);
    check("the two test cards are different cards", strcmp(a, b) != 0);

    FuriString* found = furi_string_alloc();
    ItsoUnixTime read_at = 0;
    check("an empty folder holds no record", !flipso_saved_find(mine, found, &read_at));

    /* Somebody else's card, and a file that is not a card at all, both under
     * names that say nothing about which card is in them. */
    FuriString* path = furi_string_alloc();
    flipso_saved_path(path, "A card");
    flipso_saved_write(other, furi_string_get_cstr(path));
    write_text(FLIPSO_SAVED_FOLDER "/notes.txt", "not a card at all\n");
    write_text(FLIPSO_SAVED_FOLDER "/junk.flipso", "Filetype: Flipper NFC device\n");
    check("a different card is not a match", !flipso_saved_find(mine, found, &read_at));

    flipso_saved_path(path, "Another card");
    check("saving it writes a second file", flipso_saved_write(mine, furi_string_get_cstr(path)));

    check("now its record is found", flipso_saved_find(mine, found, &read_at));
    check(
        "under the name it was given, not the card's",
        strcmp(furi_string_get_cstr(found), FLIPSO_SAVED_FOLDER "/Another card.flipso") == 0);
    check("with the time that record was read", read_at == 1000);

    /* And the other card still finds its own, so the walk is matching rather
     * than returning whatever it reached first. */
    check("the other card finds its own record", flipso_saved_find(other, found, &read_at));
    check(
        "which is the other file",
        strcmp(furi_string_get_cstr(found), FLIPSO_SAVED_FOLDER "/A card.flipso") == 0);
    check("with its own read time", read_at == 2000);

    /* The same card read again: written over its own record, under the name it
     * already had, leaving the file count where it was. */
    size_t before = count_files();
    flipso_capture_set_time(mine, 3000);
    flipso_saved_find(mine, found, NULL);
    check("updating writes over it", flipso_saved_write(mine, furi_string_get_cstr(found)));
    check("and adds no file", count_files() == before);
    flipso_saved_find(mine, found, &read_at);
    check("the record now carries the newer read", read_at == 3000);

    /* An update that runs out of room part way must leave the record it was
     * replacing alone: that file is the only copy of what has rolled off the
     * card since, and losing it to a full SD card loses it for good. */
    flipso_capture_set_time(mine, 4000);
    stub_write_budget = 100;
    check(
        "an update that runs out of room fails",
        !flipso_saved_write(mine, furi_string_get_cstr(found)));
    stub_write_budget = -1;
    flipso_saved_find(mine, found, &read_at);
    check("and the record it was replacing is untouched", read_at == 3000);
    FlipsoCapture* reread = flipso_capture_alloc();
    check("and still loads", flipso_saved_read(reread, furi_string_get_cstr(found)));
    flipso_capture_free(reread);
    check("and nothing is left beside it", count_files() == before);
    {
        FuriString* temp = furi_string_alloc();
        furi_string_printf(temp, "%s.tmp", furi_string_get_cstr(found));
        check("not even the half-written file", access(furi_string_get_cstr(temp), F_OK) != 0);
        furi_string_free(temp);
    }

    /* The same for a write that completes and then cannot be moved into place:
     * the rename is the step that replaces the record, and when it fails the
     * record must still be there - not removed to make way for a second try. */
    stub_rename_fails = true;
    check(
        "an update whose rename fails fails",
        !flipso_saved_write(mine, furi_string_get_cstr(found)));
    stub_rename_fails = false;
    flipso_saved_find(mine, found, &read_at);
    check("and the record it was replacing survives it", read_at == 3000);
    check("with nothing left beside it", count_files() == before);

    /* A capture with nothing in it cannot match anything, and must not be
     * answered with somebody else's file. */
    FlipsoCapture* empty = flipso_capture_alloc();
    check("an empty capture matches nothing", !flipso_saved_find(empty, found, &read_at));
    flipso_capture_free(empty);

    /* The list of saved cards. The browser lets Back leave only from its base
     * folder, compared against the real path it lists; and it heads every
     * folder below the storage root with a ".." row, which a list opened on
     * the folder put under the cursor - so it opens on a card instead. */
    FuriString* picked = furi_string_alloc();
    flipso_saved_pick(picked, NULL);
    check(
        "the saved list's base folder is the real path, not the alias",
        strcmp(last_browser_base, "resolved/" FLIPSO_SAVED_FOLDER) == 0);
    const size_t base_len = strlen(last_browser_base);
    const size_t start_len = strlen(last_browser_start);
    const size_t ext_len = strlen(FLIPSO_SAVED_EXTENSION);
    check(
        "and it opens on a card in it, not on the row that leaves it",
        strncmp(last_browser_start, last_browser_base, base_len) == 0 &&
            last_browser_start[base_len] == '/' && start_len > base_len + ext_len &&
            strcmp(last_browser_start + start_len - ext_len, FLIPSO_SAVED_EXTENSION) == 0);
    flipso_saved_pick(picked, found);
    check(
        "coming back to a card starts under the same real path",
        strncmp(last_browser_start, last_browser_base, strlen(last_browser_base)) == 0);
    furi_string_free(picked);

    furi_string_free(path);
    furi_string_free(found);
    flipso_capture_free(other);
    flipso_capture_free(mine);
    clean();
}
