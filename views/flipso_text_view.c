/**
 * @file flipso_text_view.c
 * @brief Word wrapping, drawing and input for the scrolling text panel.
 *
 * Wrapping happens in the draw callback because it is the only place a Canvas
 * exists, and the glyph widths it measures are what decide where a line breaks.
 * The whole text is walked on every frame: it is a few kilobytes at most, the
 * view is only redrawn on a key press, and caching it would mean invalidating
 * the cache on a font change that the view never sees.
 */
#include "flipso_text_view.h"
#include "flipso_glyphs.h"

#include <furi.h>
#include <gui/elements.h>

#define FLIPSO_TEXT_SCREEN_W 128
#define FLIPSO_TEXT_SCREEN_H 64
/* Baseline pitch. FontSecondary is 8px tall; 11 leaves it legible without
 * wasting a row of the six that fit. */
#define FLIPSO_TEXT_LINE_H 11
#define FLIPSO_TEXT_X      2
/* Text stops short of the right edge so a scrollbar never overlaps a glyph. */
#define FLIPSO_TEXT_RIGHT (FLIPSO_TEXT_SCREEN_W - 5)
/* Pixels per leading space. A fixed step rather than the font's own space,
 * which is narrow enough that a two-space indent barely shows. */
#define FLIPSO_TEXT_INDENT_STEP 3
/* How far the continuation of a wrapped "Label: value" line hangs in. */
#define FLIPSO_TEXT_HANG 6
/* Between a heading's icon and its text. */
#define FLIPSO_TEXT_ICON_GAP 3
/* Longest wrapped line we will assemble. No line of a 128px screen comes near
 * this; a word longer than it is broken like any word too wide to fit. */
#define FLIPSO_TEXT_LINE_MAX 96

typedef struct {
    FuriString* text;
    uint16_t scroll; /**< First wrapped line drawn, in lines from the top. */
    uint16_t lines; /**< Total wrapped lines, counted by the last draw. */
    const Icon* const* icons; /**< Heading icons, numbered from 1. */
    uint8_t icon_count;
} FlipsoTextModel;

struct FlipsoTextView {
    View* view;
};

/** One draw of the whole text: where the window is, and how far we have got. */
typedef struct {
    Canvas* canvas;
    uint16_t first; /**< First wrapped line inside the window. */
    uint16_t rows; /**< Rows the window holds. */
    uint16_t produced; /**< Wrapped lines so far, which the scrollbar needs. */
} FlipsoTextPass;

/** Rows of text that fit on screen. */
static uint16_t flipso_text_rows(void) {
    return FLIPSO_TEXT_SCREEN_H / FLIPSO_TEXT_LINE_H;
}

/**
 * Emit one wrapped line.
 *
 * Called for every line of the text in order, whether or not it is on screen,
 * because the total is what the scrollbar needs; only the ones inside the
 * window are drawn.
 */
static void flipso_text_emit(
    FlipsoTextPass* pass,
    const char* line,
    int32_t x,
    bool bold,
    const Icon* icon) {
    uint16_t line_no = pass->produced++;
    if(line_no < pass->first || line_no >= pass->first + pass->rows) return;

    int32_t top = (int32_t)(line_no - pass->first) * FLIPSO_TEXT_LINE_H;
    if(icon) {
        uint16_t icon_h = icon_get_height(icon);
        int32_t offset = icon_h < FLIPSO_TEXT_LINE_H ? (FLIPSO_TEXT_LINE_H - icon_h) / 2 : 0;
        canvas_draw_icon(pass->canvas, FLIPSO_TEXT_X, top + offset, icon);
    }
    canvas_set_font(pass->canvas, bold ? FontPrimary : FontSecondary);
    flipso_glyphs_draw(pass->canvas, x, top + FLIPSO_TEXT_LINE_H - 2, line);
}

/** True when the source line reads "Label: value", which wraps with a hanging indent. */
static bool flipso_text_is_labelled(const char* src, size_t len) {
    for(size_t i = 0; i + 1 < len; i++) {
        if(src[i] == ':' && src[i + 1] == ' ') return true;
    }
    return false;
}

/**
 * Break one source line into rows that fit, and emit each.
 *
 * Breaks at spaces. A single word too long for a row is broken at the character
 * that overflows, because the alternative is dropping it - and never inside a
 * UTF-8 sequence, which would leave half a currency symbol on each row.
 */
static void flipso_text_wrap(
    FlipsoTextPass* pass,
    const char* src,
    size_t len,
    bool bold,
    const Icon* icon) {
    Canvas* canvas = pass->canvas;

    /* Leading spaces are the indent, not part of the first word. */
    size_t spaces = 0;
    while(spaces < len && src[spaces] == ' ') {
        spaces++;
    }
    src += spaces;
    len -= spaces;

    int32_t first_x = FLIPSO_TEXT_X + (int32_t)spaces * FLIPSO_TEXT_INDENT_STEP;
    if(icon) first_x += icon_get_width(icon) + FLIPSO_TEXT_ICON_GAP;
    const int32_t next_x = first_x + (!bold && flipso_text_is_labelled(src, len) ? FLIPSO_TEXT_HANG : 0);

    /* Measuring is done against the font the line will actually be drawn in,
     * so a bold header wraps where a bold header breaks. */
    canvas_set_font(canvas, bold ? FontPrimary : FontSecondary);

    char buf[FLIPSO_TEXT_LINE_MAX];
    size_t fill = 0;
    int32_t x = first_x;
    bool emitted = false;

#define FLIPSO_TEXT_FLUSH()                                 \
    do {                                                    \
        buf[fill] = '\0';                                   \
        flipso_text_emit(pass, buf, x, bold, icon);         \
        canvas_set_font(canvas, bold ? FontPrimary : FontSecondary); \
        icon = NULL;                                        \
        x = next_x;                                         \
        fill = 0;                                           \
        emitted = true;                                     \
    } while(0)

    size_t i = 0;
    while(i < len) {
        /* Take the next word and the run of spaces after it. */
        const char* word = src + i;
        while(i < len && src[i] != ' ') i++;
        size_t word_len = (size_t)(src + i - word);
        while(i < len && src[i] == ' ') i++;
        if(word_len == 0) continue;

        /* Alongside what the row already holds, if it fits. */
        if(fill) {
            if(fill + 1 + word_len < sizeof(buf)) {
                buf[fill] = ' ';
                memcpy(buf + fill + 1, word, word_len);
                buf[fill + 1 + word_len] = '\0';
                if(flipso_glyphs_width(canvas, buf) <= FLIPSO_TEXT_RIGHT - x) {
                    fill += 1 + word_len;
                    continue;
                }
            }
            FLIPSO_TEXT_FLUSH();
        }

        /* At the start of a row. Whatever of the word does not fit is broken
         * off onto rows of its own until the rest does. */
        while(word_len) {
            size_t take = word_len < sizeof(buf) - 1 ? word_len : sizeof(buf) - 1;
            while(take < word_len && take > 1 && flipso_glyphs_is_continuation(word[take])) {
                take--;
            }
            memcpy(buf, word, take);
            buf[take] = '\0';
            while(take > 1 && flipso_glyphs_width(canvas, buf) > FLIPSO_TEXT_RIGHT - x) {
                take--;
                while(take > 1 && flipso_glyphs_is_continuation(word[take])) take--;
                buf[take] = '\0';
            }

            fill = take;
            if(take == word_len) break;
            FLIPSO_TEXT_FLUSH();
            word += take;
            word_len -= take;
        }
    }

    /* A source line with no words is still a line: blank ones are the spacing
     * between the sections the scenes build. */
    if(fill || !emitted) FLIPSO_TEXT_FLUSH();

#undef FLIPSO_TEXT_FLUSH
}

static void flipso_text_view_draw(Canvas* canvas, void* model) {
    FlipsoTextModel* m = model;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    FlipsoTextPass pass = {
        .canvas = canvas,
        .first = m->scroll,
        .rows = flipso_text_rows(),
        .produced = 0,
    };

    /* Stopping at the terminator rather than after it means a text that ends
     * in a newline, as every scene's does, has no blank row hanging off the
     * bottom of it. */
    const char* line = m->text ? furi_string_get_cstr(m->text) : "";
    while(*line) {
        const char* nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);

        /* "\e#" turns the rest of the source line into a bold header, and a
         * byte after it that numbers an icon puts that icon in front. */
        bool bold = len >= 2 && line[0] == '\e' && line[1] == '#';
        const char* body = bold ? line + 2 : line;
        size_t body_len = bold ? len - 2 : len;
        const Icon* icon = NULL;
        if(bold && body_len && m->icons) {
            uint8_t number = (uint8_t)((uint8_t)body[0] - FLIPSO_TEXT_ICON_BASE);
            if(number >= 1 && number <= m->icon_count) {
                icon = m->icons[number - 1];
                body++;
                body_len--;
            }
        }

        flipso_text_wrap(&pass, body, body_len, bold, icon);

        if(!nl) break;
        line = nl + 1;
    }

    /* The wrapped total is only knowable once the canvas has measured it, so
     * the scrollbar and the input clamp both use what this pass counted. */
    m->lines = pass.produced;

    if(pass.produced > pass.rows) {
        elements_scrollbar_pos(
            canvas, FLIPSO_TEXT_SCREEN_W, 0, FLIPSO_TEXT_SCREEN_H, m->scroll,
            (uint16_t)(pass.produced - pass.rows + 1));
    }
}

/** Scroll by @p delta lines, stopping at the ends. */
static bool flipso_text_step(FlipsoTextView* instance, int16_t delta) {
    bool moved = false;
    with_view_model(
        instance->view,
        FlipsoTextModel * model,
        {
            uint16_t rows = flipso_text_rows();
            /* Nothing to scroll until a draw has counted the lines. */
            uint16_t max = model->lines > rows ? (uint16_t)(model->lines - rows) : 0;
            int32_t next = (int32_t)model->scroll + delta;
            if(next < 0) next = 0;
            if(next > max) next = max;
            moved = (uint16_t)next != model->scroll;
            model->scroll = (uint16_t)next;
        },
        true);
    return moved;
}

static bool flipso_text_view_input(InputEvent* event, void* context) {
    FlipsoTextView* instance = context;

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    /* A screen at a time keeps the last row of one page as the first of the
     * next, so the eye has somewhere to pick up from. */
    const int16_t page = (int16_t)(flipso_text_rows() - 1);

    switch(event->key) {
    case InputKeyUp:
        flipso_text_step(instance, -1);
        return true;
    case InputKeyDown:
        flipso_text_step(instance, 1);
        return true;
    case InputKeyLeft:
        flipso_text_step(instance, (int16_t)-page);
        return true;
    case InputKeyRight:
        flipso_text_step(instance, page);
        return true;
    default:
        /* Back belongs to the scene manager, and OK does nothing here. */
        return false;
    }
}

FlipsoTextView* flipso_text_view_alloc(void) {
    FlipsoTextView* instance = malloc(sizeof(FlipsoTextView));
    memset(instance, 0, sizeof(FlipsoTextView));

    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(FlipsoTextModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, flipso_text_view_draw);
    view_set_input_callback(instance->view, flipso_text_view_input);

    with_view_model(
        instance->view, FlipsoTextModel * model, { model->text = furi_string_alloc(); }, false);
    return instance;
}

void flipso_text_view_free(FlipsoTextView* instance) {
    furi_assert(instance);
    /* The model owns its FuriString, so it has to go before the view does. */
    with_view_model(
        instance->view,
        FlipsoTextModel * model,
        {
            furi_string_free(model->text);
            model->text = NULL;
        },
        false);
    view_free(instance->view);
    free(instance);
}

View* flipso_text_view_get_view(FlipsoTextView* instance) {
    furi_assert(instance);
    return instance->view;
}

void flipso_text_view_set_text(FlipsoTextView* instance, const char* text) {
    furi_assert(instance);
    furi_assert(text);
    with_view_model(
        instance->view,
        FlipsoTextModel * model,
        {
            furi_string_set_str(model->text, text);
            model->scroll = 0;
            model->lines = 0;
        },
        true);
}

void flipso_text_view_set_icons(FlipsoTextView* instance, const Icon* const* icons, uint8_t count) {
    furi_assert(instance);
    furi_assert(count <= FLIPSO_TEXT_MAX_ICONS);
    with_view_model(
        instance->view,
        FlipsoTextModel * model,
        {
            model->icons = icons;
            model->icon_count = count;
        },
        false);
}
