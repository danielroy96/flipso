/**
 * @file flipso_scan_view.c
 * @brief Draws the idle prompt and the card-detection screen.
 *
 * Everything is drawn with canvas primitives rather than bitmap assets: it keeps
 * the .fap small and avoids shipping icons for a single screen.
 */
#include "flipso_scan_view.h"

#include <furi.h>
#include <gui/elements.h>

/** Frames per wave step. The scene ticks at 100 ms, so a wave every ~300 ms. */
#define FLIPSO_WAVE_PERIOD 3
#define FLIPSO_WAVE_COUNT  3

/* Unit circle from 140 to 220 degrees, scaled by 64. Drawing an arc from a table
 * avoids linking libm for three decorative curves. */
static const int8_t flipso_arc[][2] = {
    {-49, 41},  {-52, 37},  {-55, 32},  {-58, 27},  {-60, 22},
    {-62, 17},  {-63, 11},  {-64, 6},   {-64, 0},   {-64, -6},
    {-63, -11}, {-62, -17}, {-60, -22}, {-58, -27}, {-55, -32},
    {-52, -37}, {-49, -41},
};

typedef struct {
    uint8_t frame;
    bool scanning;
} FlipsoScanModel;

struct FlipsoScanView {
    View* view;
    FlipsoScanViewCallback callback;
    void* context;
};

static void flipso_draw_arc(Canvas* canvas, int16_t cx, int16_t cy, int16_t radius) {
    for(size_t i = 0; i < COUNT_OF(flipso_arc); i++) {
        int16_t x = cx + (flipso_arc[i][0] * radius) / 64;
        int16_t y = cy + (flipso_arc[i][1] * radius) / 64;
        if(x >= 0 && x < 128 && y >= 0 && y < 64) canvas_draw_dot(canvas, x, y);
    }
}

static void flipso_scan_view_draw(Canvas* canvas, void* model) {
    const FlipsoScanModel* m = model;

    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Flipso");
    canvas_draw_line(canvas, 0, 13, 127, 13);

    /* Graphic band: a smartcard with waves radiating towards it from the left.
     * The group is centred so the widest wave stays clear of the text below.
     * Idle sits a little higher to leave room for the Scan button. */
    const int16_t card_x = 54, card_y = m->scanning ? 17 : 15, card_w = 42, card_h = 24;
    canvas_draw_rframe(canvas, card_x, card_y, card_w, card_h, 3);
    canvas_draw_box(canvas, card_x + 5, card_y + 5, 9, 7); /* contact pad */
    canvas_draw_line(canvas, card_x + 19, card_y + 6, card_x + 36, card_y + 6);
    canvas_draw_line(canvas, card_x + 19, card_y + 10, card_x + 36, card_y + 10);
    canvas_draw_line(canvas, card_x + 5, card_y + 17, card_x + 30, card_y + 17);

    canvas_set_font(canvas, FontSecondary);

    if(m->scanning) {
        /* Waves grow outwards one at a time so the screen reads as "listening". */
        uint8_t active = (m->frame / FLIPSO_WAVE_PERIOD) % (FLIPSO_WAVE_COUNT + 1);
        for(uint8_t i = 0; i < FLIPSO_WAVE_COUNT; i++) {
            if(i < active) flipso_draw_arc(canvas, card_x, card_y + card_h / 2, 8 + i * 7);
        }

        canvas_draw_str_aligned(canvas, 64, 47, AlignCenter, AlignTop, "Hold an ITSO smartcard");
        canvas_draw_str_aligned(canvas, 64, 56, AlignCenter, AlignTop, "against the back");
    } else {
        /* Idle: the reader is off until the user asks for it. */
        canvas_draw_str_aligned(canvas, 64, 42, AlignCenter, AlignTop, "Ready to read a card");
        elements_button_center(canvas, "Scan"); /* occupies the bottom 12 rows */
    }
}

static bool flipso_scan_view_input(InputEvent* event, void* context) {
    FlipsoScanView* instance = context;

    if(event->type != InputTypeShort || event->key != InputKeyOk) return false;

    bool scanning = false;
    with_view_model(
        instance->view, FlipsoScanModel * model, { scanning = model->scanning; }, false);

    /* OK only means anything on the idle prompt; while scanning it is ignored so
     * a stray press cannot restart the reader mid-read. */
    if(scanning || instance->callback == NULL) return false;

    instance->callback(instance->context);
    return true;
}

FlipsoScanView* flipso_scan_view_alloc(void) {
    FlipsoScanView* instance = malloc(sizeof(FlipsoScanView));
    memset(instance, 0, sizeof(FlipsoScanView));

    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(FlipsoScanModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, flipso_scan_view_draw);
    view_set_input_callback(instance->view, flipso_scan_view_input);
    return instance;
}

void flipso_scan_view_free(FlipsoScanView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* flipso_scan_view_get_view(FlipsoScanView* instance) {
    furi_assert(instance);
    return instance->view;
}

void flipso_scan_view_set_callback(
    FlipsoScanView* instance,
    FlipsoScanViewCallback callback,
    void* context) {
    furi_assert(instance);
    instance->callback = callback;
    instance->context = context;
}

void flipso_scan_view_set_scanning(FlipsoScanView* instance, bool scanning) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        FlipsoScanModel * model,
        {
            model->scanning = scanning;
            model->frame = 0;
        },
        true);
}

void flipso_scan_view_tick(FlipsoScanView* instance) {
    furi_assert(instance);
    with_view_model(
        instance->view, FlipsoScanModel * model, { model->frame++; }, true);
}
