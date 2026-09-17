/**
 * @file flipso_menu_view.c
 * @brief Drawing and input for the icon list.
 */
#include "flipso_menu_view.h"

#include <furi.h>
#include <gui/elements.h>

#define FLIPSO_MENU_HEADER_LEN 32
#define FLIPSO_MENU_ROW_HEIGHT 16
/* Cleared by the header rule when there is one, and the whole screen when not. */
#define FLIPSO_MENU_HEADER_BOTTOM 13
#define FLIPSO_MENU_SCREEN_W 128
#define FLIPSO_MENU_SCREEN_H 64
/* The header is centred, so an over-wide one runs off both edges at once. Keep
 * it a couple of pixels clear of each. */
#define FLIPSO_MENU_HEADER_MARGIN 2
/* Icon column, then text, then the gutter the scrollbar lives in. */
#define FLIPSO_MENU_ICON_X 4
#define FLIPSO_MENU_TEXT_X 18
#define FLIPSO_MENU_TEXT_RIGHT 120

typedef struct {
    char label[FLIPSO_MENU_LABEL_LEN];
    const Icon* icon;
    uint32_t id;
} FlipsoMenuItem;

typedef struct {
    char header[FLIPSO_MENU_HEADER_LEN];
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
static void flipso_menu_fit(
    Canvas* canvas,
    const char* label,
    uint16_t width,
    char* out,
    size_t out_len) {
    furi_assert(out_len >= FLIPSO_MENU_LABEL_LEN + 4);

    /* Capped four short of the buffer, not one: the ellipsis below is written
     * in place at whatever the length has been trimmed to. */
    size_t len = strlen(label);
    if(len > out_len - 4) len = out_len - 4;
    memcpy(out, label, len);
    out[len] = '\0';

    if(canvas_string_width(canvas, out) <= width) return;

    /* Drop characters until the text and its ellipsis fit between the icon and
     * the scrollbar. The ellipsis is written in place, which the length cap
     * above has already left room for. */
    while(len > 0) {
        len--;
        memcpy(out + len, "...", 4);
        if(canvas_string_width(canvas, out) <= width) return;
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
        char fitted[FLIPSO_MENU_LABEL_LEN + 4];
        flipso_menu_fit(
            canvas,
            m->header,
            FLIPSO_MENU_SCREEN_W - 2 * FLIPSO_MENU_HEADER_MARGIN,
            fitted,
            sizeof(fitted));
        canvas_draw_str_aligned(canvas, 64, 1, AlignCenter, AlignTop, fitted);
        canvas_draw_line(canvas, 0, FLIPSO_MENU_HEADER_BOTTOM - 1, 127, FLIPSO_MENU_HEADER_BOTTOM - 1);
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
            uint8_t offset =
                icon_h < FLIPSO_MENU_ROW_HEIGHT ?
                    (uint8_t)((FLIPSO_MENU_ROW_HEIGHT - icon_h) / 2) :
                    0;
            canvas_draw_icon(canvas, FLIPSO_MENU_ICON_X, (uint8_t)(y + offset), item->icon);
        }

        canvas_set_font(canvas, FontSecondary);
        char fitted[FLIPSO_MENU_LABEL_LEN + 4];
        flipso_menu_fit(canvas, item->label, text_width, fitted, sizeof(fitted));
        canvas_draw_str(canvas, FLIPSO_MENU_TEXT_X, y + 12, fitted);

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
     * it: labels are inline arrays and icons are const app data. */
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
                snprintf(model->header, sizeof(model->header), "%s", header);
                model->has_header = true;
            } else {
                model->header[0] = '\0';
                model->has_header = false;
            }
            flipso_menu_reveal(model);
        },
        true);
}

void flipso_menu_view_add_item(
    FlipsoMenuView* instance,
    const char* label,
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
                snprintf(item->label, sizeof(item->label), "%s", label);
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
