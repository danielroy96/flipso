/*
 * Host-side test for the scrolling text panel.
 *
 * The panel exists because the firmware's text scroll element breaks a line
 * wherever the pixels run out, which splits the operator and station names this
 * app shows constantly. What is tested here is therefore mostly the wrapping:
 * that a word is never cut in half, that a word too long for the screen is
 * still shown rather than dropped, and that the scroll stops at both ends.
 *
 * The stub canvas is a fixed 5px per character in the secondary font, so a line
 * holds 24 of them - narrow enough that the real names wrap, which is the case
 * worth testing.
 */
#include "flipso_text_view.h"

#include <gui/elements.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(const char* what, int ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if(!ok) failures++;
}

static Canvas canvas;

static void render(FlipsoTextView* text) {
    View* view = flipso_text_view_get_view(text);
    canvas.color = ColorBlack;
    canvas.font = FontSecondary;
    view->draw(&canvas, view->model);
}

static void show(const char* title) {
    printf("\n  %s\n  +", title);
    for(int x = 0; x < STUB_W; x++) putchar('-');
    printf("+\n");
    for(int y = 0; y < STUB_H; y++) {
        printf("  |");
        for(int x = 0; x < STUB_W; x++) putchar(canvas.pixels[y][x]);
        printf("|\n");
    }
    printf("  +");
    for(int x = 0; x < STUB_W; x++) putchar('-');
    printf("+\n");
}

/** True when some line drawn on the last frame is exactly @p want. */
static bool line_drawn(const char* want) {
    for(int i = 0; i < canvas.text_count; i++) {
        if(strcmp(canvas.texts[i], want) == 0) return true;
    }
    return false;
}

/** True when @p needle appears anywhere in a line drawn on the last frame. */
static bool on_screen(const char* needle) {
    for(int i = 0; i < canvas.text_count; i++) {
        if(strstr(canvas.texts[i], needle)) return true;
    }
    return false;
}

static void press(FlipsoTextView* text, InputKey key, InputType type) {
    View* view = flipso_text_view_get_view(text);
    InputEvent event = {.key = key, .type = type};
    view->input(&event, view->context);
}

int main(void) {
    printf("Scrolling text view\n");

    FlipsoTextView* text = flipso_text_view_alloc();

    /* --- The regression this view exists for. --- */
    flipso_text_view_set_text(text, "Operator: South Western Railway\n");
    render(text);
    show("a long operator name");
    check("the name is not cut in half", !line_drawn("way") && !on_screen("Rail\n"));
    check("the word moves to the next line whole", line_drawn("Railway"));
    check("the line before it is filled", line_drawn("Operator: South Western"));

    /* The other name that used to split, from an ENCTS pass. */
    flipso_text_view_set_text(text, "Operator: Greater London (Freedom Pass)\n");
    render(text);
    show("a bracketed operator name");
    check("no fragment of the bracketed word", !line_drawn("eedom Pass)"));
    check("the bracketed word is intact", on_screen("(Freedom"));

    /* --- Headers, blank lines and ordinary body text. --- */
    flipso_text_view_set_text(
        text, "\e#Card number\n633597 0109 0114 5686\n\n\e#Expiry\nExpires: 25/10/2027\n");
    render(text);
    show("headers and a blank line");
    check("the header is drawn without its markup", line_drawn("Card number"));
    check("no escape leaks into the text", !on_screen("\e#"));
    check("the card number is drawn", line_drawn("633597 0109 0114 5686"));
    check("the second header is drawn", line_drawn("Expiry"));

    /* --- A word longer than the screen must still be shown. --- */
    flipso_text_view_set_text(text, "UID: 0123456789ABCDEF0123456789ABCDEF0123\n");
    render(text);
    show("an over-long token");
    /* 24 characters fit, so the tail has to appear on a line of its own. */
    check("the token is broken, not dropped", on_screen("0123456789"));
    /* Wrapping drops the space it breaks at, so the invariant is that every
     * non-space character is still on screen, in order. */
    char drawn[256] = "";
    for(int i = 0; i < canvas.text_count; i++) strcat(drawn, canvas.texts[i]);
    char squashed[256];
    size_t n = 0;
    for(const char* c = drawn; *c; c++) {
        if(*c != ' ') squashed[n++] = *c;
    }
    squashed[n] = '\0';
    check(
        "every character survives the break",
        strcmp(squashed, "UID:0123456789ABCDEF0123456789ABCDEF0123") == 0);

    /* --- Scrolling. --- */
    FuriString* many = furi_string_alloc();
    for(int i = 0; i < 20; i++) furi_string_cat_printf(many, "Line %d\n", i);
    flipso_text_view_set_text(text, furi_string_get_cstr(many));
    furi_string_free(many);

    render(text);
    show("twenty lines, top");
    check("the first line is shown", line_drawn("Line 0"));
    check("a line past the bottom is not", !line_drawn("Line 9"));

    /* Up at the top does nothing; the view must not scroll past the start. */
    press(text, InputKeyUp, InputTypeShort);
    render(text);
    check("up at the top stays put", line_drawn("Line 0"));

    press(text, InputKeyDown, InputTypeShort);
    render(text);
    show("scrolled down one line");
    check("the top line has gone", !line_drawn("Line 0"));
    check("the next line is now first", line_drawn("Line 1"));

    /* Held keys repeat, and the scroll stops at the last line rather than
     * running off the end of the text. */
    for(int i = 0; i < 50; i++) press(text, InputKeyDown, InputTypeRepeat);
    render(text);
    show("scrolled to the end");
    check("the last line is shown", line_drawn("Line 19"));
    check("it does not scroll past the end", on_screen("Line 1"));

    /* --- Keys that belong to the scene manager. --- */
    View* view = flipso_text_view_get_view(text);
    InputEvent back = {.key = InputKeyBack, .type = InputTypeShort};
    InputEvent ok = {.key = InputKeyOk, .type = InputTypeShort};
    check("back is not consumed", !view->input(&back, view->context));
    check("ok is not consumed", !view->input(&ok, view->context));

    /* --- An empty panel, which is what a scene leaves behind on exit. --- */
    flipso_text_view_set_text(text, "");
    render(text);
    check("an empty panel draws nothing", canvas.text_count == 0 || line_drawn(""));

    flipso_text_view_free(text);

    printf("\n%s\n", failures ? "FAILED" : "All text view tests passed");
    return failures ? 1 : 0;
}
