/**
 * @file flipso_menu_view.c
 * @brief Drawing and input for the icon list.
 */
#include "flipso_menu_view.h"
#include "flipso_glyphs.h"

#include <furi.h>
#include <gui/elements.h>

#define FLIPSO_MENU_HEADER_LEN      32
#define FLIPSO_MENU_ROW_HEIGHT      16
/* Cleared by the header rule when there is one, and the whole screen when not.
 * The rule is on the row above this, where the scan screen draws its own, with
 * two clear rows over it so the header's icon and descenders do not touch it. */
#define FLIPSO_MENU_HEADER_BOTTOM   14
#define FLIPSO_MENU_SCREEN_W        128
#define FLIPSO_MENU_SCREEN_H        64
/* The header is centred, so an over-wide one runs off both edges at once. Keep
 * it a couple of pixels clear of each. */
#define FLIPSO_MENU_HEADER_MARGIN   2
/* Between a header icon and the text it belongs to. */
#define FLIPSO_MENU_HEADER_ICON_GAP 3
/* Icon column, then text, then the gutter the scrollbar lives in. */
#define FLIPSO_MENU_ICON_X          4
#define FLIPSO_MENU_TEXT_X          18
#define FLIPSO_MENU_TEXT_RIGHT      120
/* Between a label and the tag at the end of its row. */
#define FLIPSO_MENU_TAG_GAP         4

typedef struct {
    const char* label; /**< The caller's; see flipso_menu_view_add_item(). */
    char tag[FLIPSO_MENU_TAG_LEN]; /**< Empty for none. */
    const Icon* icon;
    uint32_t id;
} FlipsoMenuItem;

typedef struct {
    char header[FLIPSO_MENU_HEADER_LEN];
    /** Kept whole after the header, which is cut to make room for it: the
     *  caller's, a string that outlives the view. NULL for none. */
    const char* header_suffix;
    const Icon* header_icon;
    bool has_header;
    FlipsoMenuItem items[FLIPSO_MENU_MAX_ITEMS];
    uint8_t count;
    uint8_t position; /**< Index of the highlighted item. */
    uint8_t window; /**< Index of the item drawn on the top row. */
} FlipsoMenuModel;

struct FlipsoMenuView {
    View* view;
    FlipsoMenuViewCallback callback;
    void* context;
};

/** Rows that fit below the header, which is the unit the window scrolls by. */
static uint8_t flipso_menu_rows(const FlipsoMenuModel* model) {
    uint8_t top = model->has_header ? FLIPSO_MENU_HEADER_BOTTOM : 0;
    return (uint8_t)((FLIPSO_MENU_SCREEN_H - top) / FLIPSO_MENU_ROW_HEIGHT);
}

/** Scroll the window so the highlighted row is on screen. */
static void flipso_menu_reveal(FlipsoMenuModel* model) {
    uint8_t rows = flipso_menu_rows(model);

    if(model->count <= rows) {
        model->window = 0;
        return;
    }
    if(model->position < model->window) {
        model->window = model->position;
    } else if(model->position >= model->window + rows) {
        model->window = (uint8_t)(model->position - rows + 1);
    }
    if(model->window + rows > model->count) {
        model->window = (uint8_t)(model->count - rows);
    }
}

/**
 * Copy @p label into @p out, trimming it to @p width pixels with an ellipsis.
 *
 * Done here rather than with elements_string_fit_width because that works on a
 * FuriString, and a draw callback is the wrong place to be allocating one.
 *
 * @param out_len must leave room for the ellipsis as well as the label, so at
 *                least FLIPSO_MENU_LABEL_LEN + 4.
 */
static void
    flipso_menu_fit(Canvas* canvas, const char* label, uint16_t width, char* out, size_t out_len) {
    furi_assert(out_len >= FLIPSO_MENU_LABEL_LEN + 4);

    /* Capped four short of the buffer, not one: the ellipsis below is written
     * in place at whatever the length has been trimmed to. */
    flipso_glyphs_copy(out, out_len - 3, label);
    size_t len = strlen(out);
    if(flipso_glyphs_width(canvas, out) <= width) return;

    /* Drop characters until the text and its ellipsis fit between the icon and
     * the scrollbar. The ellipsis is written in place, which the length cap
     * above has already left room for; the cut steps back over a whole UTF-8
     * sequence, so it never leaves half a currency symbol. */
    while(len > 0) {
        len--;
        while(len && flipso_glyphs_is_continuation(out[len]))
            len--;
        memcpy(out + len, "...", 4);
        if(flipso_glyphs_width(canvas, out) <= width) return;
        out[len] = '\0';
    }
}

static void flipso_menu_view_draw(Canvas* canvas, void* model) {
    FlipsoMenuModel* m = model;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    uint8_t top = 0;
    if(m->has_header) {
        canvas_set_font(canvas, FontPrimary);
        /* Fitted like a row label: the header is a card's branding, which comes
         * from a table the user can edit and is longer than "ITSO Card" ever
         * was. */
        char fitted[FLIPSO_MENU_LABEL_LEN + 4 + FLIPSO_MENU_SUFFIX_LEN];
        uint16_t icon_w = m->header_icon ? (uint16_t)(icon_get_width(m->header_icon) +
                                                      FLIPSO_MENU_HEADER_ICON_GAP) :
                                           0;
        /* A suffix - " (Blocked)" - is what the header is for, so it is kept
         * whole and the branding before it is what gets cut. */
        char suffix[FLIPSO_MENU_SUFFIX_LEN] = "";
        if(m->header_suffix) flipso_glyphs_copy(suffix, sizeof(suffix), m->header_suffix);
        uint16_t suffix_w = flipso_glyphs_width(canvas, suffix);
        uint16_t room = (uint16_t)(FLIPSO_MENU_SCREEN_W - 2 * FLIPSO_MENU_HEADER_MARGIN - icon_w);
        flipso_menu_fit(
            canvas,
            m->header,
            room > suffix_w ? (uint16_t)(room - suffix_w) : 0,
            fitted,
            FLIPSO_MENU_LABEL_LEN + 4);
        const size_t fitted_len = strlen(fitted);
        snprintf(fitted + fitted_len, sizeof(fitted) - fitted_len, "%s", suffix);

        /* The icon and the text are centred as one group, so the header stays
         * balanced rather than the text sitting centred with an icon hung off
         * its left edge. */
        uint16_t text_w = flipso_glyphs_width(canvas, fitted);
        uint16_t group_w = (uint16_t)(icon_w + text_w);
        uint8_t group_x = (uint8_t)((FLIPSO_MENU_SCREEN_W - group_w) / 2);

        if(m->header_icon) {
            /* Centred in the band above the rule, which is where the text sits
             * too - the icon is taller than the glyphs, so aligning their tops
             * would leave it hanging into the rule. */
            uint16_t icon_h = icon_get_height(m->header_icon);
            uint8_t icon_y = icon_h < FLIPSO_MENU_HEADER_BOTTOM - 1 ?
                                 (uint8_t)((FLIPSO_MENU_HEADER_BOTTOM - 1 - icon_h) / 2) :
                                 0;
            canvas_draw_icon(canvas, group_x, icon_y, m->header_icon);
        }
        canvas_draw_str_aligned(
            canvas, (uint8_t)(group_x + icon_w), 1, AlignLeft, AlignTop, fitted);
        canvas_draw_line(
            canvas, 0, FLIPSO_MENU_HEADER_BOTTOM - 1, 127, FLIPSO_MENU_HEADER_BOTTOM - 1);
        top = FLIPSO_MENU_HEADER_BOTTOM;
    }

    uint8_t rows = flipso_menu_rows(m);
    bool scrolling = m->count > rows;
    uint16_t text_width = (uint16_t)(FLIPSO_MENU_TEXT_RIGHT - FLIPSO_MENU_TEXT_X);

    for(uint8_t row = 0; row < rows; row++) {
        uint8_t index = (uint8_t)(m->window + row);
        if(index >= m->count) break;

        const FlipsoMenuItem* item = &m->items[index];
        uint8_t y = (uint8_t)(top + row * FLIPSO_MENU_ROW_HEIGHT);

        if(index == m->position) {
            elements_slightly_rounded_box(canvas, 1, y + 1, FLIPSO_MENU_TEXT_RIGHT, 14);
            canvas_set_color(canvas, ColorWhite);
        }

        if(item->icon) {
            /* Vertically centred in the row for the 10px icons this app ships;
             * anything taller than a row sits at the top of it rather than
             * being offset upwards into the row above. */
            uint16_t icon_h = icon_get_height(item->icon);
            uint8_t offset = icon_h < FLIPSO_MENU_ROW_HEIGHT ?
                                 (uint8_t)((FLIPSO_MENU_ROW_HEIGHT - icon_h) / 2) :
                                 0;
            canvas_draw_icon(canvas, FLIPSO_MENU_ICON_X, (uint8_t)(y + offset), item->icon);
        }

        canvas_set_font(canvas, FontSecondary);
        uint16_t label_width = text_width;
        if(item->tag[0]) {
            uint16_t tag_w = flipso_glyphs_width(canvas, item->tag);
            flipso_glyphs_draw(canvas, FLIPSO_MENU_TEXT_RIGHT - 2 - tag_w, y + 12, item->tag);
            label_width = tag_w + FLIPSO_MENU_TAG_GAP + 2 < text_width ?
                              (uint16_t)(text_width - tag_w - FLIPSO_MENU_TAG_GAP - 2) :
                              0;
        }
        char fitted[FLIPSO_MENU_LABEL_LEN + 4];
        flipso_menu_fit(canvas, item->label, label_width, fitted, sizeof(fitted));
        flipso_glyphs_draw(canvas, FLIPSO_MENU_TEXT_X, y + 12, fitted);

        canvas_set_color(canvas, ColorBlack);
    }

    if(scrolling) {
        elements_scrollbar_pos(
            canvas, FLIPSO_MENU_SCREEN_W, top, FLIPSO_MENU_SCREEN_H - top, m->position, m->count);
    }
}

/** Move the highlight by @p delta, wrapping at both ends as a submenu does. */
static void flipso_menu_step(FlipsoMenuView* instance, int8_t delta) {
    with_view_model(
        instance->view,
        FlipsoMenuModel * model,
        {
            if(model->count) {
                model->position =
                    (uint8_t)((model->position + model->count + delta) % model->count);
                flipso_menu_reveal(model);
            }
        },
        true);
}

static bool flipso_menu_view_input(InputEvent* event, void* context) {
    FlipsoMenuView* instance = context;

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    if(event->key == InputKeyUp) {
        flipso_menu_step(instance, -1);
        return true;
    }
    if(event->key == InputKeyDown) {
        flipso_menu_step(instance, 1);
        return true;
    }
    if(event->key != InputKeyOk || event->type != InputTypeShort) return false;

    /* Read the id under the model lock, then call out from outside it: the
     * callback switches scenes, which reaches back into the view. */
    bool selected = false;
    uint32_t id = 0;
    with_view_model(
        instance->view,
        FlipsoMenuModel * model,
        {
            if(model->position < model->count) {
                id = model->items[model->position].id;
                selected = true;
            }
        },
        false);

    if(!selected || instance->callback == NULL) return false;
    instance->callback(instance->context, id);
    return true;
}

FlipsoMenuView* flipso_menu_view_alloc(void) {
    FlipsoMenuView* instance = malloc(sizeof(FlipsoMenuView));
    memset(instance, 0, sizeof(FlipsoMenuView));

    instance->view = view_alloc();
    /* The model owns no pointers of its own, so view_free is enough to release
     * it: tags are inline arrays, and labels and icons are the caller's. */
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(FlipsoMenuModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, flipso_menu_view_draw);
    view_set_input_callback(instance->view, flipso_menu_view_input);
    return instance;
}

void flipso_menu_view_free(FlipsoMenuView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* flipso_menu_view_get_view(FlipsoMenuView* instance) {
    furi_assert(instance);
    return instance->view;
}

void flipso_menu_view_set_callback(
    FlipsoMenuView* instance,
    FlipsoMenuViewCallback callback,
    void* context) {
    furi_assert(instance);
    instance->callback = callback;
    instance->context = context;
}

void flipso_menu_view_reset(FlipsoMenuView* instance) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        FlipsoMenuModel * model,
        {
            model->count = 0;
            model->position = 0;
            model->window = 0;
            model->has_header = false;
            model->header[0] = '\0';
            model->header_suffix = NULL;
            model->header_icon = NULL;
        },
        true);
}

void flipso_menu_view_set_header(FlipsoMenuView* instance, const char* header) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        FlipsoMenuModel * model,
        {
            if(header) {
                flipso_glyphs_copy(model->header, sizeof(model->header), header);
                model->has_header = true;
            } else {
                model->header[0] = '\0';
                model->has_header = false;
            }
            flipso_menu_reveal(model);
        },
        true);
}

void flipso_menu_view_set_header_suffix(FlipsoMenuView* instance, const char* suffix) {
    furi_assert(instance);
    with_view_model(
        instance->view, FlipsoMenuModel * model, { model->header_suffix = suffix; }, true);
}

void flipso_menu_view_set_header_icon(FlipsoMenuView* instance, const Icon* icon) {
    furi_assert(instance);
    with_view_model(instance->view, FlipsoMenuModel * model, { model->header_icon = icon; }, true);
}

void flipso_menu_view_add_item(
    FlipsoMenuView* instance,
    const char* label,
    const Icon* icon,
    uint32_t id) {
    flipso_menu_view_add_tagged_item(instance, label, NULL, icon, id);
}

void flipso_menu_view_add_tagged_item(
    FlipsoMenuView* instance,
    const char* label,
    const char* tag,
    const Icon* icon,
    uint32_t id) {
    furi_assert(instance);
    furi_assert(label);
    with_view_model(
        instance->view,
        FlipsoMenuModel * model,
        {
            if(model->count < FLIPSO_MENU_MAX_ITEMS) {
                FlipsoMenuItem* item = &model->items[model->count++];
                item->label = label;
                /* Copied without splitting a UTF-8 sequence, as the label is when
                 * it is drawn. */
                flipso_glyphs_copy(item->tag, sizeof(item->tag), tag ? tag : "");
                item->icon = icon;
                item->id = id;
            }
        },
        true);
}

void flipso_menu_view_set_selected(FlipsoMenuView* instance, uint32_t id) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        FlipsoMenuModel * model,
        {
            for(uint8_t i = 0; i < model->count; i++) {
                if(model->items[i].id != id) continue;
                model->position = i;
                break;
            }
            flipso_menu_reveal(model);
        },
        true);
}
