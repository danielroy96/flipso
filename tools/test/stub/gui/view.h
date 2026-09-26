/* Host stub: a View that owns a model, with the with_view_model macro. */
#pragma once
#include <furi.h>
#include <gui/canvas.h>

typedef enum {
    InputTypePress,
    InputTypeRelease,
    InputTypeShort,
    InputTypeLong,
    InputTypeRepeat
} InputType;
typedef enum {
    InputKeyUp,
    InputKeyDown,
    InputKeyRight,
    InputKeyLeft,
    InputKeyOk,
    InputKeyBack
} InputKey;

typedef struct {
    InputKey key;
    InputType type;
} InputEvent;

typedef enum {
    ViewModelTypeLocking,
    ViewModelTypeLockFree
} ViewModelType;

typedef void (*ViewDrawCallback)(Canvas* canvas, void* model);
typedef bool (*ViewInputCallback)(InputEvent* event, void* context);

typedef struct {
    void* model;
    void* context;
    ViewDrawCallback draw;
    ViewInputCallback input;
    /* Counted so the tests can check a mutation actually asked for a redraw. */
    unsigned commits;
} View;

static inline View* view_alloc(void) {
    View* v = malloc(sizeof(View));
    memset(v, 0, sizeof(View));
    return v;
}
static inline void view_free(View* v) {
    free(v->model);
    free(v);
}
static inline void view_allocate_model(View* v, ViewModelType type, size_t size) {
    UNUSED(type);
    v->model = malloc(size);
    memset(v->model, 0, size);
}
static inline void view_set_context(View* v, void* context) {
    v->context = context;
}
static inline void view_set_draw_callback(View* v, ViewDrawCallback cb) {
    v->draw = cb;
}
static inline void view_set_input_callback(View* v, ViewInputCallback cb) {
    v->input = cb;
}

#define with_view_model(view_ptr, model_decl, code, update) \
    do {                                                    \
        View* _v = (view_ptr);                              \
        model_decl = _v->model;                             \
        { code }                                            \
        if(update) _v->commits++;                           \
    } while(0)
