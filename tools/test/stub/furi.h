/*
 * Host stand-in for the furi core, shared by every test in this directory.
 *
 * Only what the code under test actually uses: assertions that abort so the
 * sanitisers catch a broken precondition, the logging macros compiled away,
 * and the record names the storage stub keys on.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define furi_assert(x)                                    \
    do {                                                  \
        if(!(x)) {                                        \
            fprintf(                                      \
                stderr,                                   \
                "furi_assert failed: %s at %s:%d\n",      \
                #x,                                       \
                __FILE__,                                 \
                __LINE__);                                \
            abort();                                      \
        }                                                 \
    } while(0)
#define furi_check(x) furi_assert(x)

#define UNUSED(x)   (void)(x)
#define COUNT_OF(x) (sizeof(x) / sizeof((x)[0]))

/* Discarded rather than printed: the device's uint32_t is a long and the host's
 * is not, so the format strings in the app would warn if they were compiled. */
#define FURI_LOG_E(tag, ...) ((void)(tag))
#define FURI_LOG_W(tag, ...) ((void)(tag))
#define FURI_LOG_I(tag, ...) ((void)(tag))
#define FURI_LOG_D(tag, ...) ((void)(tag))
#define FURI_LOG_T(tag, ...) ((void)(tag))

#define RECORD_STORAGE "storage"

/* Provided by whichever test needs them. */
void* furi_record_open(const char* name);
void furi_record_close(const char* name);

/*
 * FuriString: a growable string. The app builds every screen with one, so a
 * stand-in is what lets the screen-building code be tested at all. Only the
 * operations the code under test uses are here.
 */
typedef struct FuriString {
    char* data;
    size_t len;
    size_t cap;
} FuriString;

static inline FuriString* furi_string_alloc(void) {
    FuriString* s = malloc(sizeof(FuriString));
    s->cap = 64;
    s->len = 0;
    s->data = malloc(s->cap);
    s->data[0] = '\0';
    return s;
}

static inline void furi_string_free(FuriString* s) {
    free(s->data);
    free(s);
}

static inline void furi_string_grow(FuriString* s, size_t extra) {
    if(s->len + extra + 1 <= s->cap) return;
    while(s->len + extra + 1 > s->cap) s->cap *= 2;
    s->data = realloc(s->data, s->cap);
}

static inline void furi_string_reset(FuriString* s) {
    s->len = 0;
    s->data[0] = '\0';
}

static inline const char* furi_string_get_cstr(const FuriString* s) {
    return s->data;
}

static inline size_t furi_string_size(const FuriString* s) {
    return s->len;
}

static inline void furi_string_push_back(FuriString* s, char c) {
    furi_string_grow(s, 1);
    s->data[s->len++] = c;
    s->data[s->len] = '\0';
}

static inline void furi_string_cat_str(FuriString* s, const char* str) {
    size_t n = strlen(str);
    furi_string_grow(s, n);
    memcpy(s->data + s->len, str, n + 1);
    s->len += n;
}

static inline void furi_string_set_str(FuriString* s, const char* str) {
    furi_string_reset(s);
    furi_string_cat_str(s, str);
}

static inline bool furi_string_empty(const FuriString* s) {
    return s->len == 0;
}

static inline FuriString* furi_string_alloc_set_str(const char* str) {
    FuriString* s = furi_string_alloc();
    furi_string_set_str(s, str);
    return s;
}

static inline void furi_string_cat_string(FuriString* s, const FuriString* src) {
    furi_string_cat_str(s, src->data);
}

static inline void furi_string_set_string(FuriString* s, const FuriString* src) {
    furi_string_set_str(s, src->data);
}

static inline FuriString* furi_string_alloc_set_string(const FuriString* src) {
    return furi_string_alloc_set_str(src->data);
}

/*
 * The firmware's versions take either a FuriString or a plain string and pick
 * the right one, so the stub has to as well: a stub that only took plain
 * strings would reject code the device compiles, which is a test failure that
 * says nothing about the code under test.
 */
#define FURI_STRING_SELECT(string_fn, str_fn, arg)     \
    _Generic(                                          \
        (arg),                                         \
        FuriString *: string_fn,                       \
        const FuriString *: string_fn,                 \
        default: str_fn)

#define furi_string_cat(s, str) \
    FURI_STRING_SELECT(furi_string_cat_string, furi_string_cat_str, str)((s), (str))
#define furi_string_set(s, str) \
    FURI_STRING_SELECT(furi_string_set_string, furi_string_set_str, str)((s), (str))
#define furi_string_alloc_set(str) \
    FURI_STRING_SELECT(furi_string_alloc_set_string, furi_string_alloc_set_str, str)(str)

__attribute__((format(printf, 2, 3))) static inline void
    furi_string_printf(FuriString* s, const char* format, ...);

__attribute__((format(printf, 2, 3))) static inline void
    furi_string_cat_printf(FuriString* s, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int n = vsnprintf(NULL, 0, format, args);
    va_end(args);
    furi_assert(n >= 0);

    furi_string_grow(s, (size_t)n);
    va_start(args, format);
    vsnprintf(s->data + s->len, (size_t)n + 1, format, args);
    va_end(args);
    s->len += (size_t)n;
}

__attribute__((format(printf, 2, 3))) static inline void
    furi_string_printf(FuriString* s, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int n = vsnprintf(NULL, 0, format, args);
    va_end(args);
    furi_assert(n >= 0);

    furi_string_reset(s);
    furi_string_grow(s, (size_t)n);
    va_start(args, format);
    vsnprintf(s->data, (size_t)n + 1, format, args);
    va_end(args);
    s->len = (size_t)n;
}
