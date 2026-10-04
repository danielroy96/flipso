/**
 * @file test_parse_util.c
 * @brief Helpers the decoder tests share.
 */
#include "test_parse.h"

const char* fmt_unix(uint32_t t) {
    static char buf[32];
    time_t tt = (time_t)t;
    struct tm tm;
    gmtime_r(&tt, &tm);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

void dump_location(const char* label, const ItsoLocation* loc) {
    if(loc->valid) printf("      %s: %s (type %u)\n", label, loc->text, loc->def_type);
}

/** Decode one IPE group of type @p typ from an exact-length heap copy. */
void parse_group(ItsoProduct* p, uint8_t typ, bool vgp, const uint8_t* src, size_t len) {
    memset(p, 0, sizeof(*p));
    p->present = true;
    p->typ = typ;
    p->value_group = vgp;
    uint8_t* exact = malloc(len);
    memcpy(exact, src, len);
    itso_parse_ipe(p, exact, len, 64);
    free(exact);
}
