/*
 * Host-side test for the icon list view.
 *
 * The view is hand-written rather than a firmware module, so the things that
 * would normally be someone else's problem - which item a row shows, where the
 * window sits after a wrap, whether a long label terminates - are tested here.
 * Rendering goes to an ASCII framebuffer so the layout can be eyeballed too.
 */
#include "flipso_menu_view.h"
#include "flipso_glyphs.h"

#include <gui/elements.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(const char* what, int ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if(!ok) failures++;
}

static const Icon icon_a = {10, 10, 'a'};
/* The header's warning triangle, which is drawn beside the title rather than
 * on a row of its own. */
static const Icon icon_warn = {10, 10, 'w'};
static const Icon icon_b = {10, 10, 'b'};
/* Taller than a row: the layout must not offset it upwards into the row above. */
static const Icon icon_tall = {10, 24, 't'};

static uint32_t last_id;
static int calls;
static void on_select(void* context, uint32_t id) {
    UNUSED(context);
    last_id = id;
    calls++;
}

static Canvas canvas;

static void render(FlipsoMenuView* menu) {
    View* view = flipso_menu_view_get_view(menu);
    canvas.color = ColorBlack;
    canvas.font = FontSecondary;
    view->draw(&canvas, view->model);
}

static void show(const char* title) {
    printf("\n  %s\n  +", title);
    for(int x = 0; x < STUB_W; x++)
        putchar('-');
    printf("+\n");
    for(int y = 0; y < STUB_H; y++) {
        printf("  |");
        for(int x = 0; x < STUB_W; x++)
            putchar(canvas.pixels[y][x]);
        printf("|\n");
    }
    printf("  +");
    for(int x = 0; x < STUB_W; x++)
        putchar('-');
    printf("+\n");
}

/** True when @p needle appears in a string drawn on the last rendered frame. */
static bool on_screen(const char* needle) {
    for(int i = 0; i < canvas.text_count; i++) {
        if(strstr(canvas.texts[i], needle)) return true;
    }
    return false;
}

/** True when some string drawn on the last frame is exactly @p want. */
static bool line_drawn(const char* want) {
    for(int i = 0; i < canvas.text_count; i++) {
        if(strcmp(canvas.texts[i], want) == 0) return true;
    }
    return false;
}

/**
 * Leftmost pixel of the header row, or -1 when nothing was drawn on it.
 *
 * The header is centred, so an over-wide one runs off the left edge as well as
 * the right, and the canvas clips both silently.
 */
static int header_left(void) {
    for(int x = 0; x < STUB_W; x++) {
        if(canvas.pixels[1][x] != '.') return x;
    }
    return -1;
}

/** Rightmost pixel of the header row, or -1 when nothing was drawn on it. */
static int header_right(void) {
    for(int x = STUB_W - 1; x >= 0; x--) {
        if(canvas.pixels[1][x] != '.') return x;
    }
    return -1;
}

/** Leftmost pixel of @p mark anywhere on the frame, or -1 if it was not drawn. */
static int mark_left(char mark) {
    for(int x = 0; x < STUB_W; x++) {
        for(int y = 0; y < STUB_H; y++) {
            if(canvas.pixels[y][x] == mark) return x;
        }
    }
    return -1;
}

/** Bottom-most row carrying @p mark, or -1 if it was not drawn. */
static int mark_bottom(char mark) {
    for(int y = STUB_H - 1; y >= 0; y--) {
        for(int x = 0; x < STUB_W; x++) {
            if(canvas.pixels[y][x] == mark) return y;
        }
    }
    return -1;
}

/** True when a scrollbar was drawn on the last rendered frame. */
static bool scrollbar_drawn(void) {
    for(int y = 0; y < STUB_H; y++) {
        for(int x = 0; x < STUB_W; x++) {
            if(canvas.pixels[y][x] == ':' || canvas.pixels[y][x] == 'H') return true;
        }
    }
    return false;
}

static void press(FlipsoMenuView* menu, InputKey key, InputType type) {
    View* view = flipso_menu_view_get_view(menu);
    InputEvent event = {.key = key, .type = type};
    view->input(&event, view->context);
}

int main(void) {
    printf("Icon list view\n");

    FlipsoMenuView* menu = flipso_menu_view_alloc();
    flipso_menu_view_set_callback(menu, on_select, NULL);

    /* --- A short list, which fits without scrolling. --- */
    flipso_menu_view_set_header(menu, "ITSO Card");
    flipso_menu_view_add_item(menu, "Card", &icon_a, 10);
    flipso_menu_view_add_item(menu, "Pay as you go", &icon_b, 11);
    flipso_menu_view_add_item(menu, "Products (5)", &icon_a, 12);

    render(menu);
    show("three items, header");
    check("header drawn", on_screen("ITSO Card"));
    check(
        "every item drawn",
        on_screen("Card") && on_screen("Pay as you go") && on_screen("Products (5)"));
    check("no scrollbar when everything fits", !scrollbar_drawn());

    /* OK reports the item's id, not its position. */
    calls = 0;
    press(menu, InputKeyOk, InputTypeShort);
    check("OK reports the first item's id", calls == 1 && last_id == 10);

    press(menu, InputKeyDown, InputTypeShort);
    press(menu, InputKeyOk, InputTypeShort);
    check("down then OK reports the second", last_id == 11);

    /* Wrap-around in both directions, as a submenu does. */
    press(menu, InputKeyDown, InputTypeShort);
    press(menu, InputKeyDown, InputTypeShort);
    press(menu, InputKeyOk, InputTypeShort);
    check("down past the end wraps to the first", last_id == 10);
    press(menu, InputKeyUp, InputTypeShort);
    press(menu, InputKeyOk, InputTypeShort);
    check("up past the start wraps to the last", last_id == 12);

    /* Held keys repeat; anything else is left to the scene manager. */
    press(menu, InputKeyDown, InputTypeRepeat);
    press(menu, InputKeyOk, InputTypeShort);
    check("a repeat moves the highlight", last_id == 10);
    /* Back and Left belong to the scene manager, so the view must not eat them. */
    View* view = flipso_menu_view_get_view(menu);
    InputEvent back = {.key = InputKeyBack, .type = InputTypeShort};
    InputEvent left = {.key = InputKeyLeft, .type = InputTypeShort};
    check("back is not consumed", !view->input(&back, view->context));
    check("left is not consumed", !view->input(&left, view->context));

    /* --- Tags, which keep a product's status readable however long its name. */
    flipso_menu_view_reset(menu);
    flipso_menu_view_add_tagged_item(menu, "Reserved journey ticket", "Expired", &icon_a, 30);
    flipso_menu_view_add_tagged_item(menu, "Card", "", &icon_a, 31);
    render(menu);
    show("a long label with a tag");
    check("the tag is drawn whole", line_drawn("Expired"));
    check("the label gives way to it", on_screen("...") && !on_screen("ticket"));
    int label_end = -1, tag_start = -1;
    for(int i = 0; i < canvas.text_count; i++) {
        size_t len = strlen(canvas.texts[i]);
        if(strncmp(canvas.texts[i], "Reserved", 8) == 0)
            label_end = canvas.text_x[i] + (int)len * STUB_GLYPH_W;
        if(strcmp(canvas.texts[i], "Expired") == 0) tag_start = canvas.text_x[i];
    }
    check("the label stops short of the tag", label_end >= 0 && label_end < tag_start);
    check("the tag ends inside the text column", tag_start + 7 * STUB_GLYPH_W <= 120);
    check("an empty tag draws nothing", line_drawn("Card"));

    /* --- Headers, which now carry a card's branding. --- */
    /* The title was the fixed string "ITSO Card" until the menu started showing
     * what the card is sold as, and a brand comes from a table the user can
     * edit: it has to be cut to the screen rather than drawn off both edges. */
    flipso_menu_view_reset(menu);
    flipso_menu_view_set_header(menu, "Freedom Pass");
    flipso_menu_view_add_item(menu, "Card", &icon_a, 10);
    render(menu);
    check("a header that fits is drawn whole", on_screen("Freedom Pass"));
    check("a header that fits starts on screen", header_left() >= 0);

    flipso_menu_view_set_header(menu, "South West Trains Smart, and then some more");
    render(menu);
    show("over-long header");
    check("an over-long header is truncated with an ellipsis", on_screen("..."));
    check("an over-long header still starts on screen", header_left() >= 0);
    check("an over-long header keeps its first word", on_screen("South"));

    /* --- A header icon, which the blocked-card warning uses. --- */
    /* The icon and the text are centred as one group. Centring the text alone
     * and hanging the icon off its left edge would push the pair off-centre and,
     * on a long brand, off the screen. */
    flipso_menu_view_reset(menu);
    flipso_menu_view_set_header(menu, "Blocked Card");
    flipso_menu_view_set_header_icon(menu, &icon_warn);
    flipso_menu_view_add_item(menu, "Card", &icon_a, 10);
    render(menu);
    show("header with a warning icon");
    check("header icon drawn", mark_left('w') >= 0);
    check("header text drawn beside it", on_screen("Blocked Card"));
    check("icon sits left of the text", mark_left('w') < header_left() + 1);
    /* The stub draws one pixel per glyph cell, so the rightmost pixel is where
     * the last character starts; its cell runs a glyph width further. */
    int text_end = header_right() + STUB_GLYPH_PRIMARY_W - 1;
    check(
        "icon and text are centred as one group",
        abs(header_left() - (STUB_W - 1 - text_end)) <= 1);
    /* The rule under the header is at FLIPSO_MENU_HEADER_BOTTOM - 1. */
    check("icon stays clear of the header rule", mark_bottom('w') < 11);

    /* An over-long header has to lose room to the icon, not overlap it. */
    flipso_menu_view_set_header(menu, "South West Trains Smart, and then some more");
    render(menu);
    check("an over-long header with an icon is truncated", on_screen("..."));
    check("an over-long header with an icon starts on screen", header_left() >= 0);
    check("the icon survives an over-long header", mark_left('w') >= 0);

    /* Reset drops the icon: the next card is not blocked just because the last
     * one was. */
    flipso_menu_view_reset(menu);
    flipso_menu_view_set_header(menu, "Freedom Pass");
    flipso_menu_view_add_item(menu, "Card", &icon_a, 10);
    render(menu);
    check("reset clears the header icon", mark_left('w') < 0);

    /* --- A full list, which has to scroll. --- */
    flipso_menu_view_reset(menu);
    flipso_menu_view_set_header(menu, "Products");
    char label[40];
    for(uint32_t i = 0; i < FLIPSO_MENU_MAX_ITEMS; i++) {
        snprintf(label, sizeof(label), "Item %lu", (unsigned long)i);
        flipso_menu_view_add_item(menu, label, &icon_a, 100 + i);
    }
    render(menu);
    show("a full list, top");
    check("first rows shown", on_screen("Item 0") && on_screen("Item 2"));
    check("later rows not shown yet", !on_screen("Item 5"));
    check("scrollbar present", scrollbar_drawn());

    /* Walk to the bottom: the window has to follow the highlight. */
    for(int i = 0; i < FLIPSO_MENU_MAX_ITEMS - 1; i++)
        press(menu, InputKeyDown, InputTypeShort);
    render(menu);
    show("a full list, bottom");
    char last[40];
    snprintf(last, sizeof(last), "Item %d", FLIPSO_MENU_MAX_ITEMS - 1);
    check("last row shown", on_screen(last));
    check("first row scrolled away", !on_screen("Item 0"));
    press(menu, InputKeyOk, InputTypeShort);
    check("OK at the bottom reports the last id", last_id == 100 + FLIPSO_MENU_MAX_ITEMS - 1);

    /* Wrapping from the last item back to the first must bring the window with it. */
    press(menu, InputKeyDown, InputTypeShort);
    render(menu);
    check("wrapping to the top scrolls back", on_screen("Item 0"));

    /* Restoring a selection scrolls it into view. */
    flipso_menu_view_set_selected(menu, 100 + 9);
    render(menu);
    show("selection restored to item 9");
    check("restored row is on screen", on_screen("Item 9"));
    press(menu, InputKeyOk, InputTypeShort);
    check("restored row is the highlighted one", last_id == 100 + 9);

    /* An id that is not in the list leaves the highlight alone. */
    flipso_menu_view_set_selected(menu, 9999);
    press(menu, InputKeyOk, InputTypeShort);
    check("an unknown id does not move the highlight", last_id == 100 + 9);

    /* --- Degenerate input. --- */
    flipso_menu_view_reset(menu);
    flipso_menu_view_set_header(menu, NULL);
    render(menu);
    check(
        "an empty list draws nothing and does not crash",
        !scrollbar_drawn() && canvas.text_count == 0);
    calls = 0;
    press(menu, InputKeyOk, InputTypeShort);
    press(menu, InputKeyDown, InputTypeShort);
    press(menu, InputKeyUp, InputTypeShort);
    check("an empty list ignores every key", calls == 0);

    /* A label far longer than the row, which the truncation loop has to end on. */
    flipso_menu_view_add_item(
        menu, "A product name far longer than any row could ever show", &icon_a, 1);
    flipso_menu_view_add_item(menu, "", NULL, 2);
    flipso_menu_view_add_item(menu, "Tall icon", &icon_tall, 3);
    render(menu);
    show("over-long label, no header");
    check("long label is truncated with an ellipsis", on_screen("..."));
    check("an item with no icon still draws", on_screen("Tall icon"));
    /* An icon taller than its row is clipped by the canvas, never drawn above
     * the row it belongs to. */
    check(
        "a tall icon starts at its own row",
        canvas.pixels[32][4] == 't' && canvas.pixels[31][4] != 't');

    /* More items than the list holds: the extra ones are dropped, not written
     * past the end of the array. */
    flipso_menu_view_reset(menu);
    for(uint32_t i = 0; i < FLIPSO_MENU_MAX_ITEMS * 3; i++) {
        snprintf(label, sizeof(label), "Over %lu", (unsigned long)i);
        flipso_menu_view_add_item(menu, label, &icon_a, i);
    }
    for(int i = 0; i < 200; i++)
        press(menu, InputKeyDown, InputTypeShort);
    render(menu);
    press(menu, InputKeyOk, InputTypeShort);
    check("overflowing the list is capped, not written past", last_id < FLIPSO_MENU_MAX_ITEMS);

    flipso_menu_view_free(menu);

    /* Copying into the fixed-size label, tag and header buffers: a two-byte
     * pound sign straddling the cut goes whole or not at all. */
    {
        char out[6];
        flipso_glyphs_copy(
            out,
            sizeof(out),
            "abcd\xC2\xA3"
            "5");
        check("a symbol that does not fit is left out whole", strcmp(out, "abcd") == 0);
        flipso_glyphs_copy(
            out,
            sizeof(out),
            "abc\xC2\xA3"
            "5");
        check("one that fits is kept whole", strcmp(out, "abc\xC2\xA3") == 0);
        flipso_glyphs_copy(out, sizeof(out), "ab");
        check("short text is copied as it is", strcmp(out, "ab") == 0);
    }

    printf("\n%s\n", failures ? "MENU VIEW TESTS FAILED" : "All icon list tests passed");
    return failures ? 1 : 0;
}
