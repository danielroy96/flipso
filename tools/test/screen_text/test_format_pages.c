/**
 * @file test_format_pages.c
 * @brief Reading a rendered screen: its pages, their titles, and what is on each.
 */
#include "test_format.h"

bool shows(const FuriString* text, const char* needle) {
    return strstr(furi_string_get_cstr(text), needle) != NULL;
}

/** True when @p c is a heading's icon number rather than the start of its text. */
static bool is_icon(char c) {
    return (unsigned char)c > FLIPSO_TEXT_ICON_BASE && (unsigned char)c < '0';
}

/** Where the page titled @p title starts, at its "\e#", or NULL. */
const char* find_page(const FuriString* text, const char* title) {
    const size_t want = strlen(title);
    for(const char* p = furi_string_get_cstr(text); p;
        p = strchr(p, '\f') ? strchr(p, '\f') + 1 : NULL) {
        if(p[0] != '\e' || p[1] != '#') continue;
        const char* t = is_icon(p[2]) ? p + 3 : p + 2;
        if(strncmp(t, title, want) == 0 && t[want] == '\n') return p;
    }
    return NULL;
}

/**
 * The lines of the page titled @p title, less its title row, or NULL when the
 * screen has no such page. A heading's icon byte is skipped, so "History"
 * finds the page whatever icon it carries. Four buffers, so a check can
 * compare a few pages at once.
 */
const char* page_of(const FuriString* text, const char* title) {
    static char pages[4][4096];
    static unsigned next;
    const size_t want = strlen(title);
    for(const char* p = furi_string_get_cstr(text); p;
        p = strchr(p, '\f') ? strchr(p, '\f') + 1 : NULL) {
        if(p[0] != '\e' || p[1] != '#') continue;
        const char* t = p + 2;
        if(is_icon(*t)) t++;
        if(strncmp(t, title, want) != 0 || t[want] != '\n') continue;
        const char* body = t + want + 1;
        const char* end = strchr(body, '\f');
        size_t n = end ? (size_t)(end - body) : strlen(body);
        char* buf = pages[next++ % 4];
        if(n >= sizeof(pages[0])) n = sizeof(pages[0]) - 1;
        memcpy(buf, body, n);
        buf[n] = '\0';
        return buf;
    }
    return NULL;
}

/** True when the screen's pages are titled @p want, in order, as "One|Two|Three". */
bool titles_are(const FuriString* text, const char* want) {
    char got[512] = "";
    for(const char* p = furi_string_get_cstr(text); p;
        p = strchr(p, '\f') ? strchr(p, '\f') + 1 : NULL) {
        if(p[0] != '\e' || p[1] != '#') continue;
        const char* t = p + 2;
        if(is_icon(*t)) t++;
        const char* nl = strchr(t, '\n');
        size_t n = nl ? (size_t)(nl - t) : strlen(t);
        if(got[0]) strncat(got, "|", sizeof(got) - strlen(got) - 1);
        strncat(got, t, n < sizeof(got) - strlen(got) - 1 ? n : sizeof(got) - strlen(got) - 1);
    }
    if(strcmp(got, want) != 0) printf("    pages: %s\n    want:  %s\n", got, want);
    return strcmp(got, want) == 0;
}

/** True when @p needle is on the page titled @p title. */
bool on_page(const FuriString* text, const char* title, const char* needle) {
    const char* page = page_of(text, title);
    return page && strstr(page, needle);
}

/** True when the page titled @p title opens with @p needle. */
bool page_starts(const FuriString* text, const char* title, const char* needle) {
    const char* page = page_of(text, title);
    return page && strncmp(page, needle, strlen(needle)) == 0;
}

/** Pages whose title carries @p icon: a journeys screen's tap pages carry the taps icon. */
int pages_with_icon(const FuriString* text, FlipsoIcon icon) {
    char mark[4] = {'\e', '#', (char)(FLIPSO_TEXT_ICON_BASE + icon), '\0'};
    int n = 0;
    for(const char* p = furi_string_get_cstr(text); (p = strstr(p, mark)) != NULL; p++) {
        n++;
    }
    return n;
}

/** True when @p needle appears on a page before Technical. */
bool before_technical(const FuriString* text, const char* needle) {
    const char* found = strstr(furi_string_get_cstr(text), needle);
    const char* heading = find_page(text, "Technical");
    return found && (!heading || found < heading);
}

/** True when @p needle appears, and only under the screen's Technical heading. */
bool technical(const FuriString* text, const char* needle) {
    const char* heading = find_page(text, "Technical");
    const char* found = strstr(furi_string_get_cstr(text), needle);
    return heading && found && found > heading;
}

/** How many times @p needle appears in @p text. */
int occurrences_of(const FuriString* text, const char* needle) {
    int n = 0;
    for(const char* p = furi_string_get_cstr(text); (p = strstr(p, needle)) != NULL; p++) {
        n++;
    }
    return n;
}
