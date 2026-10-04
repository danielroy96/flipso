/**
 * @file test_names.c
 * @brief How a saved card is named.
 */
#include "test_saved.h"

void names(void) {
    static ItsoCard card;
    itso_card_reset(&card);
    itso_parse_shell(&card, card_shell, sizeof(card_shell));

    char name[FLIPSO_SAVED_NAME_LEN];
    flipso_saved_suggest_name(name, sizeof(name), card.isrn, "Freedom Pass");
    check("a brand and the last four digits", strcmp(name, "Freedom Pass 3458") == 0);

    flipso_saved_suggest_name(name, sizeof(name), card.isrn, NULL);
    check("an unbranded card still gets a name", strcmp(name, "ITSO Card 3458") == 0);

    /* Characters a file name cannot carry are dropped, and the digits are kept
     * whatever the brand does: they are what tells two of these apart. */
    flipso_saved_suggest_name(name, sizeof(name), card.isrn, "Greater London (Freedom Pass)");
    check("a long brand keeps the digits", strcmp(name, "Greater London Freedom 3458") == 0);
    check("and still fits the field", strlen(name) < sizeof(name));

    flipso_saved_suggest_name(name, sizeof(name), card.isrn, "?*:/");
    check("a brand of nothing usable is just the digits", strcmp(name, "3458") == 0);

    /* A card whose shell never read has no number to append. */
    static ItsoCard blank;
    itso_card_reset(&blank);
    flipso_saved_suggest_name(name, sizeof(name), blank.isrn, NULL);
    check("a card with no number is named anyway", strcmp(name, "ITSO Card") == 0);

    /* A paper ticket's card number is the same on every one (the compact shell's
     * implied 633597 8189 0000000 3), so it is named from the chip serial it is
     * matched on instead - otherwise every ticket would be offered "... 0003". */
    FlipsoCapture* ticket = flipso_capture_alloc();
    flipso_capture_add(ticket, FlipsoBlockType2, 0, cmd4_pages, sizeof(cmd4_pages));
    char identity[ITSO_ISRN_DIGITS + 1];
    check("a paper ticket has an identity", flipso_capture_card_number(ticket, identity));
    flipso_saved_suggest_name(name, sizeof(name), identity, "SPT Subway");
    check("a paper ticket is named from its chip serial", strcmp(name, "SPT Subway E6F7") == 0);
    flipso_capture_free(ticket);

    char tiny[4];
    flipso_saved_suggest_name(tiny, sizeof(tiny), card.isrn, "Freedom Pass");
    check("a buffer too small to say much is not overrun", strlen(tiny) < sizeof(tiny));

    FuriString* shown = furi_string_alloc();
    flipso_saved_name(shown, "/ext/apps_data/flipso/cards/Mum's pass.flipso");
    check(
        "a saved card is shown by its file name",
        strcmp(furi_string_get_cstr(shown), "Mum's pass") == 0);
    furi_string_free(shown);
}
