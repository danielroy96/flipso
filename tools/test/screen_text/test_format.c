/**
 * @file test_format.c
 * @brief Host-side test for the text of the detail screens.
 *
 * Two kinds of check. The synthetic card from build_card.py is rendered and
 * specific lines looked for, which pins the wording of the things a review
 * found wrong: a doubled label, a tap-out's two unlabelled times, "Tapped:
 * OUT" on a bus card. Then every screen of every demo card - which between
 * them reach every screen, every product type and both command sets - is held
 * to the house style flipso_format.h sets out, line by line:
 *
 *   - a value after "Label: " starts with a capital or a digit;
 *   - an indented line is itself "Label: Value";
 *   - no label appears twice on one line;
 *   - money is "£", never "GBP".
 *
 *     test_format <directory of demo .flipso files>
 */
#include "test_format.h"

int main(int argc, char** argv) {
    printf("Screen text\n");

    FlipsoFormat f = {.now = FLIPSO_TEST_NOW};

    /* --- The synthetic card. --- */
    FlipsoCapture* capture = synthetic_capture();
    static ItsoCard card;
    check("the synthetic card decodes", flipso_capture_decode(capture, &card));
    f.capture = capture;

    FuriString* text = furi_string_alloc();
    synthetic_screens(&f, &card, text);
    paper_ticket_screens(&f, text);
    product_lines(&f, &card, text);
    about_screens(text);
    media_screens(text);
    furi_string_free(text);
    flipso_capture_free(capture);

    /* --- Every demo card, every screen. --- */
    if(argc > 1) demo_cards(argv[1], f);

    printf("\n%s\n", failures ? "FAILED" : "All screen text tests passed");
    return failures ? 1 : 0;
}
