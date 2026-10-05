/**
 * @file flipso_text_view.c
 * @brief Word wrapping, paging, drawing and input for the text panel.
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

#define FLIPSO_TEXT_SCREEN_W    128
#define FLIPSO_TEXT_SCREEN_H    64
/* Baseline pitch. FontSecondary is 8px tall; 11 leaves it legible without
 * wasting a row of the six that fit. */
#define FLIPSO_TEXT_LINE_H      11
#define FLIPSO_TEXT_X           2
/* Text stops short of the right edge so a scrollbar never overlaps a glyph. */
#define FLIPSO_TEXT_RIGHT       (FLIPSO_TEXT_SCREEN_W - 5)
/* Pixels per leading space. A fixed step rather than the font's own space,
 * which is narrow enough that a two-space indent barely shows. */
#define FLIPSO_TEXT_INDENT_STEP 3
/* How far the continuation of a wrapped "Label: value" line hangs in. */
#define FLIPSO_TEXT_HANG        6
/* Between a heading's icon and its text. */
#define FLIPSO_TEXT_ICON_GAP    3
/* Longest wrapped line we will assemble. No line of a 128px screen comes near
 * this; a word longer than it is broken like any word too wide to fit. */
#define FLIPSO_TEXT_LINE_MAX    96
/* A page's arrows: a triangle this wide, and twice this less one tall, at the
 * edge of the title row. */
#define FLIPSO_TEXT_ARROW_W     4
/* The title starts this far in when the page has arrows to make room for. */
#define FLIPSO_TEXT_ARROW_ROOM  (FLIPSO_TEXT_ARROW_W + 3)
/* A page's title: the icon list's header, so every screen titles itself the
 * same way - icon and text centred as one, over a rule on the row above this
 * (FLIPSO_MENU_HEADER_BOTTOM). */
#define FLIPSO_TEXT_TITLE_H     14
/* Between the rule and the page's first line, which without it sits close
 * enough under the rule to read as crowded. */
#define FLIPSO_TEXT_TITLE_GAP   1

typedef struct {
    FuriString* text;
    uint16_t page; /**< The page shown, from 0. */
    uint16_t pages; /**< Pages in the text: one more than its page breaks. */
    uint16_t scroll; /**< First wrapped line drawn, in lines from the top. */
    uint16_t lines; /**< Wrapped lines the page scrolls, counted by the last draw. */
    uint16_t rows; /**< Rows the page scrolls in, set by the last draw. */
    const Icon* const* icons; /**< Heading icons, numbered from 1. */
    uint8_t icon_count;
} FlipsoTextModel;

struct FlipsoTextView {
    View* view;
};

/** One draw of the whole text: where the window is, and how far we have got. */
typedef struct {
    Canvas* canvas;
    int32_t top; /**< Where the window starts, in pixels from the top. */
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

    int32_t top = pass->top + (int32_t)(line_no - pass->first) * FLIPSO_TEXT_LINE_H;
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
    const int32_t next_x =
        first_x + (!bold && flipso_text_is_labelled(src, len) ? FLIPSO_TEXT_HANG : 0);

    /* Measuring is done against the font the line will actually be drawn in,
     * so a bold header wraps where a bold header breaks. */
    canvas_set_font(canvas, bold ? FontPrimary : FontSecondary);

    char buf[FLIPSO_TEXT_LINE_MAX];
    size_t fill = 0;
    int32_t x = first_x;
    bool emitted = false;

#define FLIPSO_TEXT_FLUSH()                                          \
    do {                                                             \
        buf[fill] = '\0';                                            \
        flipso_text_emit(pass, buf, x, bold, icon);                  \
        canvas_set_font(canvas, bold ? FontPrimary : FontSecondary); \
        icon = NULL;                                                 \
        x = next_x;                                                  \
        fill = 0;                                                    \
        emitted = true;                                              \
    } while(0)

    size_t i = 0;
    while(i < len) {
        /* Take the next word and the run of spaces after it. */
        const char* word = src + i;
        while(i < len && src[i] != ' ')
            i++;
        size_t word_len = (size_t)(src + i - word);
        while(i < len && src[i] == ' ')
            i++;
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
                while(take > 1 && flipso_glyphs_is_continuation(word[take]))
                    take--;
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

/** Where page @p page of @p text starts, and through @p end where it stops. */
static const char* flipso_text_page(const char* text, uint16_t page, const char** end) {
    const char* start = text;
    for(uint16_t i = 0; i < page; i++) {
        const char* next = strchr(start, FLIPSO_TEXT_PAGE);
        if(!next) break;
        start = next + 1;
    }
    const char* stop = strchr(start, FLIPSO_TEXT_PAGE);
    *end = stop ? stop : start + strlen(start);
    return start;
}

/** Split a "\e#" heading's body into its icon, if it names one, and its text. */
static const Icon*
    flipso_text_heading_icon(const FlipsoTextModel* m, const char** body, size_t* len) {
    if(!*len || !m->icons) return NULL;
    uint8_t number = (uint8_t)((uint8_t)(*body)[0] - FLIPSO_TEXT_ICON_BASE);
    if(number < 1 || number > m->icon_count) return NULL;
    (*body)++;
    (*len)--;
    return m->icons[number - 1];
}

/** A page arrow: a triangle pointing left from @p x, or right to it. */
static void flipso_text_arrow(Canvas* canvas, int32_t x, bool left) {
    /* Centred in the band above the rule, as the title is. */
    const int32_t mid = (FLIPSO_TEXT_TITLE_H - 2) / 2;
    for(int32_t i = 0; i < FLIPSO_TEXT_ARROW_W; i++) {
        const int32_t col = left ? x + i : x - i;
        canvas_draw_line(canvas, col, mid - i, col, mid + i);
    }
}

/**
 * The page's title row, drawn as the icon list draws its header: the icon and
 * the heading centred as one group, cut short with dots if it would run into
 * the arrows, a rule under it, and an arrow at each side that has another page.
 */
static void flipso_text_title(
    Canvas* canvas,
    const FlipsoTextModel* m,
    const char* body,
    size_t len,
    const Icon* icon) {
    const bool paged = m->pages > 1;
    const int32_t margin = paged ? FLIPSO_TEXT_ARROW_ROOM : FLIPSO_TEXT_X;
    const int32_t icon_w = icon ? icon_get_width(icon) + FLIPSO_TEXT_ICON_GAP : 0;
    const int32_t room = FLIPSO_TEXT_SCREEN_W - 2 * margin - icon_w;

    if(paged && m->page > 0) flipso_text_arrow(canvas, 0, true);
    if(paged && m->page + 1 < m->pages) flipso_text_arrow(canvas, FLIPSO_TEXT_SCREEN_W - 1, false);

    /* Titles are written to fit; one that does not is cut, with dots to say
     * so, rather than drawn under an arrow. */
    canvas_set_font(canvas, FontPrimary);
    char buf[FLIPSO_TEXT_LINE_MAX];
    size_t take = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, body, take);
    buf[take] = '\0';
    if(flipso_glyphs_width(canvas, buf) > room) {
        /* Room for the dots and the terminator after the cut. */
        if(take > sizeof(buf) - 4) take = sizeof(buf) - 4;
        while(take > 0) {
            take--;
            while(take > 0 && flipso_glyphs_is_continuation(body[take]))
                take--;
            memcpy(buf + take, "...", 4);
            if(flipso_glyphs_width(canvas, buf) <= room) break;
        }
    }

    const int32_t group_w = icon_w + flipso_glyphs_width(canvas, buf);
    const int32_t x = (FLIPSO_TEXT_SCREEN_W - group_w) / 2;
    if(icon) {
        /* Centred in the band above the rule, which is where the text sits
         * too - the icon is taller than the glyphs. */
        const int32_t icon_h = icon_get_height(icon);
        const int32_t band = FLIPSO_TEXT_TITLE_H - 1;
        canvas_draw_icon(canvas, x, icon_h < band ? (band - icon_h) / 2 : 0, icon);
    }
    canvas_draw_str_aligned(canvas, x + icon_w, 1, AlignLeft, AlignTop, buf);
    canvas_draw_line(
        canvas, 0, FLIPSO_TEXT_TITLE_H - 1, FLIPSO_TEXT_SCREEN_W - 1, FLIPSO_TEXT_TITLE_H - 1);
}

static void flipso_text_view_draw(Canvas* canvas, void* model) {
    FlipsoTextModel* m = model;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    const char* end;
    const char* line =
        flipso_text_page(m->text ? furi_string_get_cstr(m->text) : "", m->page, &end);

    /* A page that opens with a heading keeps it at the top as the page's
     * title, and scrolls the rest under it. */
    FlipsoTextPass pass = {
        .canvas = canvas,
        .top = 0,
        .first = m->scroll,
        .rows = flipso_text_rows(),
        .produced = 0,
    };
    if(end - line >= 2 && line[0] == '\e' && line[1] == '#') {
        const char* nl = memchr(line, '\n', (size_t)(end - line));
        const char* body = line + 2;
        size_t body_len = (size_t)((nl ? nl : end) - body);
        const Icon* icon = flipso_text_heading_icon(m, &body, &body_len);
        flipso_text_title(canvas, m, body, body_len, icon);
        line = nl ? nl + 1 : end;
        pass.top = FLIPSO_TEXT_TITLE_H + FLIPSO_TEXT_TITLE_GAP;
        pass.rows = (FLIPSO_TEXT_SCREEN_H - pass.top) / FLIPSO_TEXT_LINE_H;
    }

    /* Stopping at the end rather than after it means a page that ends in a
     * newline, as every scene's does, has no blank row hanging off the
     * bottom of it. */
    while(line < end) {
        const char* nl = memchr(line, '\n', (size_t)(end - line));
        size_t len = (size_t)((nl ? nl : end) - line);

        /* "\e#" turns the rest of the source line into a bold header, and a
         * byte after it that numbers an icon puts that icon in front. */
        bool bold = len >= 2 && line[0] == '\e' && line[1] == '#';
        const char* body = bold ? line + 2 : line;
        size_t body_len = bold ? len - 2 : len;
        const Icon* icon = bold ? flipso_text_heading_icon(m, &body, &body_len) : NULL;

        flipso_text_wrap(&pass, body, body_len, bold, icon);

        if(!nl) break;
        line = nl + 1;
    }

    /* The wrapped total is only knowable once the canvas has measured it, so
     * the scrollbar and the input clamp both use what this pass counted. */
    m->lines = pass.produced;
    m->rows = pass.rows;

    if(pass.produced > pass.rows) {
        elements_scrollbar_pos(
            canvas,
            FLIPSO_TEXT_SCREEN_W,
            (uint16_t)pass.top,
            (uint16_t)(FLIPSO_TEXT_SCREEN_H - pass.top),
            m->scroll,
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
            /* Nothing to scroll until a draw has counted the lines. */
            uint16_t rows = model->rows ? model->rows : flipso_text_rows();
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

/** Turn to the page @p delta away, at the top; nothing past either end. */
static void flipso_text_turn(FlipsoTextView* instance, int16_t delta) {
    with_view_model(
        instance->view,
        FlipsoTextModel * model,
        {
            int32_t next = (int32_t)model->page + delta;
            if(next >= 0 && next < model->pages) {
                model->page = (uint16_t)next;
                model->scroll = 0;
                model->lines = 0;
            }
        },
        true);
}

static bool flipso_text_view_input(InputEvent* event, void* context) {
    FlipsoTextView* instance = context;

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    switch(event->key) {
    case InputKeyUp:
        flipso_text_step(instance, -1);
        return true;
    case InputKeyDown:
        flipso_text_step(instance, 1);
        return true;
    case InputKeyLeft:
        flipso_text_turn(instance, -1);
        return true;
    case InputKeyRight:
        flipso_text_turn(instance, 1);
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
        instance->view,
        FlipsoTextModel * model,
        {
            model->text = furi_string_alloc();
            model->pages = 1;
        },
        false);
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

void flipso_text_view_take_text(FlipsoTextView* instance, FuriString* text) {
    furi_assert(instance);
    furi_assert(text);
    with_view_model(
        instance->view,
        FlipsoTextModel * model,
        {
            /* Moved rather than copied, and the old text freed rather than
             * emptied, which would keep its buffer at the longest screen's
             * size until the app exits. */
            furi_string_move(model->text, text);
            model->page = 0;
            model->pages = 1;
            for(const char* c = furi_string_get_cstr(model->text);
                (c = strchr(c, FLIPSO_TEXT_PAGE)) != NULL;
                c++) {
                model->pages++;
            }
            model->scroll = 0;
            model->lines = 0;
            model->rows = 0;
        },
        true);
}

void flipso_text_view_set_text(FlipsoTextView* instance, const char* text) {
    furi_assert(text);
    flipso_text_view_take_text(instance, furi_string_alloc_set_str(text));
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
