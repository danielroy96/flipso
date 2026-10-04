/**
 * @file test_parse_util.c
 * @brief Helpers the decoder tests share.
 */
#include "test_parse.h"

const char* fmt_unix(ItsoUnixTime t) {
    static char buf[32];
    time_t tt = (time_t)t;
    struct tm tm;
    gmtime_r(&tt, &tm);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

/* A few buffers in turn, so that one printf can show several locations. */
#define LOC_BUFFERS 4

const char* loc_text(const ItsoLocation* loc) {
    static char text[LOC_BUFFERS][ITSO_LOC_LEN];
    static unsigned next;
    char* out = text[next++ % LOC_BUFFERS];
    itso_location_text(loc, out, ITSO_LOC_LEN);
    return out;
}

const char* loc_code(const ItsoLocation* loc) {
    static char code[LOC_BUFFERS][ITSO_LOC_CODE_LEN];
    static unsigned next;
    char* out = code[next++ % LOC_BUFFERS];
    itso_location_code(loc, out, ITSO_LOC_CODE_LEN);
    return out;
}

ItsoLocCodeKind loc_kind(const ItsoLocation* loc) {
    char code[ITSO_LOC_CODE_LEN];
    return itso_location_code(loc, code, sizeof(code));
}

void dump_location(const char* label, const ItsoLocation* loc) {
    if(loc->valid) printf("      %s: %s (type %u)\n", label, loc_text(loc), loc->def_type);
}

/** Decode one IPE group of type @p typ from an exact-length heap copy. */
void parse_group(ItsoProduct* p, uint8_t typ, bool vgp, const uint8_t* src, size_t len) {
    itso_product_free(p);
    memset(p, 0, sizeof(*p));
    p->typ = typ;
    p->value_group = vgp;
    uint8_t* exact = malloc(len);
    memcpy(exact, src, len);
    itso_parse_ipe(p, exact, len, 64);
    free(exact);
}
