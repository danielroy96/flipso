/**
 * @file test_saved_files.c
 * @brief Saving and loading through real files, and files that are not ours.
 */
#include "test_saved.h"

void round_trip(void) {
    clean();

    FlipsoCapture* capture = flipso_capture_alloc();
    flipso_capture_add(capture, FlipsoBlockShell, 0, card_shell, sizeof(card_shell));
    flipso_capture_add(capture, FlipsoBlockDirectory, 0, card_dir, sizeof(card_dir));
    flipso_capture_add(capture, FlipsoBlockLog, 0, card_log, sizeof(card_log));
    flipso_capture_set_time(capture, 1758400000u);

    check("nothing is saved yet", !flipso_saved_any());

    FuriString* path = furi_string_alloc();
    flipso_saved_path(path, "Test Card");
    check(
        "the path is the folder, the name and the extension",
        strcmp(furi_string_get_cstr(path), FLIPSO_SAVED_FOLDER "/Test Card.flipso") == 0);

    /* The folder does not exist yet: writing has to make it. */
    check("writing creates the folder", flipso_saved_write(capture, furi_string_get_cstr(path)));
    check("and there is now a saved card", flipso_saved_any());

    FlipsoCapture* loaded = flipso_capture_alloc();
    check("it reads back", flipso_saved_read(loaded, furi_string_get_cstr(path)));
    check("with its read time", flipso_capture_time(loaded) == 1758400000u);

    static ItsoCard from_file, from_memory;
    check("and decodes", flipso_capture_decode(loaded, &from_file));
    flipso_capture_decode(capture, &from_memory);
    check("to the card that was saved", itso_card_equal(&from_file, &from_memory));

    check("deleting it works", flipso_saved_delete(furi_string_get_cstr(path)));
    check("and it is gone", !flipso_saved_any());
    check("deleting it twice does not", !flipso_saved_delete(furi_string_get_cstr(path)));

    furi_string_free(path);
    flipso_capture_free(loaded);
    flipso_capture_free(capture);
}

void bad_files(void) {
    clean();
    flipso_saved_mkdir();

    FlipsoCapture* capture = flipso_capture_alloc();

    check(
        "a card that is not there does not read",
        !flipso_saved_read(capture, FLIPSO_SAVED_FOLDER "/missing.flipso"));

    write_text(FLIPSO_SAVED_FOLDER "/other.flipso", "Filetype: Flipper NFC device\nVersion: 4\n");
    check(
        "somebody else's file is refused",
        !flipso_saved_read(capture, FLIPSO_SAVED_FOLDER "/other.flipso"));
    check("and leaves nothing behind", !flipso_capture_valid(capture));

    write_text(
        FLIPSO_SAVED_FOLDER "/future.flipso",
        "Filetype: Flipso card\nVersion: 99\nShell: 18 11\n");
    check(
        "a card from a later Flipso is refused",
        !flipso_saved_read(capture, FLIPSO_SAVED_FOLDER "/future.flipso"));

    write_text(
        FLIPSO_SAVED_FOLDER "/empty.flipso", "Filetype: Flipso card\nVersion: 1\nRead at: 1\n");
    check(
        "a card with no shell in it is refused",
        !flipso_saved_read(capture, FLIPSO_SAVED_FOLDER "/empty.flipso"));

    /* Writing somewhere that cannot be written must not leave a file that later
     * looks like a card. */
    check(
        "a write that cannot open its file fails",
        !flipso_saved_write(capture, FLIPSO_SAVED_FOLDER "/nope/deep.flipso"));

    flipso_capture_free(capture);

    /* The browser has nobody in front of it, so it says no; the alert is the
     * only other thing that reaches a person, and it must not crash. */
    FuriString* picked = furi_string_alloc();
    check("a browser nobody answers picks nothing", !flipso_saved_pick(picked, NULL));
    furi_string_free(picked);

    flipso_saved_alert("Cannot save card", "Check the SD card.");
    check("the alert says what went wrong", strcmp(last_alert, "Cannot save card") == 0);

    clean();
}
