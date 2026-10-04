/**
 * @file itso_location.c
 * @brief Location records (TS 1000-1 clause 4.2.4): kept as the card stored
 * them, rendered as text and a lookup code when a screen shows one, and the
 * station a rail retailer code names.
 */
#include "itso_i.h"

#include <string.h>
#include <stdio.h>

/** SNCODE: four 5-bit characters, right justified and padded with 0x1F spaces. */
static void itso_decode_service(uint32_t packed, char* out, size_t len) {
    static const char map[32] = {
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
        'G', 'H', 'K', 'L', 'M', 'N', 'P', 'R', 'S', 'T', 'V', 'W', 'X', 'Y', 'Z', ' ',
    };
    size_t pos = 0;
    for(int8_t i = 3; i >= 0 && pos + 1 < len; i--) {
        char c = map[(packed >> (i * 5)) & 0x1F];
        if(c == ' ' && pos == 0) continue; /* Skip leading padding. */
        out[pos++] = c;
    }
    out[pos] = '\0';
}

/** SNCODE2: six bits per character, 0x3F is the pad. */
static void itso_decode_service2(const uint8_t* data, char* out, size_t len) {
    size_t pos = 0;
    for(uint8_t i = 0; i < 4 && pos + 1 < len; i++) {
        uint8_t code = itso_bits(data, i * 6, 6);
        char c;
        if(code <= 9) {
            c = '0' + code;
        } else if(code >= 0x0A && code <= 0x23) {
            c = 'A' + (code - 0x0A);
        } else {
            c = ' ';
        }
        if(c == ' ' && pos == 0) continue;
        out[pos++] = c;
    }
    out[pos] = '\0';
}

/** Render the set bits of a 3- or 4-byte zonal bit map as "1,3,5". */
static void itso_decode_zones(const uint8_t* data, uint8_t bytes, char* out, size_t len) {
    size_t pos = 0;
    uint8_t printed = 0;
    for(uint8_t byte = 0; byte < bytes; byte++) {
        for(uint8_t bit = 0; bit < 8; bit++) {
            if(!(data[byte] & (1 << bit))) continue;
            uint8_t zone = byte * 8 + bit + 1;
            int written =
                snprintf(out + pos, len - pos, "%s%u", printed ? "," : "", (unsigned)zone);
            if(written <= 0 || (size_t)written >= len - pos) {
                /* Ran out of room: leave what fits. */
                return;
            }
            pos += written;
            printed++;
        }
    }
    if(!printed) snprintf(out, len, "none");
}

/** Copy printable ASCII, stopping at the first byte that is not. */
static void itso_copy_ascii(const uint8_t* data, size_t n, char* out, size_t len) {
    size_t pos = 0;
    for(size_t i = 0; i < n && pos + 1 < len; i++) {
        char c = (char)data[i];
        if(c < 0x20 || c > 0x7E) break;
        out[pos++] = c;
    }
    out[pos] = '\0';
}

/** True when @p text is a non-empty run of decimal digits. */
static bool itso_all_digits(const char* text) {
    if(*text == '\0') return false;
    for(; *text; text++) {
        if(*text < '0' || *text > '9') return false;
    }
    return true;
}

/**
 * Record a code a national register could name, along with which register.
 *
 * Anything longer than ITSO_LOC_CODE_LEN allows is dropped rather than
 * truncated: half an AtcoCode would find the wrong stop, whereas no code at all
 * falls back to the rendered text, which is merely less helpful.
 *
 * @return @p of, or ItsoLocCodeNone when the code was dropped.
 */
static ItsoLocCodeKind
    itso_note_code(char* code, size_t code_len, ItsoLocCodeKind of, const char* value) {
    size_t length = strlen(value);
    if(length == 0 || length >= ITSO_LOC_CODE_LEN || length >= code_len) return ItsoLocCodeNone;
    memcpy(code, value, length + 1);
    return of;
}

/**
 * Render a location as text, and as the code a register could name it by.
 *
 * Both come out of one pass because they come out of the same fields; each
 * caller wants one, and gives the other a buffer on its stack.
 *
 * @param code @p code_len bytes; left empty when there is no code, or none
 *             that fits.
 * @return     which register @p code is a key into.
 */
static ItsoLocCodeKind itso_render_location(
    const ItsoLocation* location,
    char* out,
    size_t len,
    char* code,
    size_t code_len) {
    const uint8_t def_type = location->def_type;
    const uint8_t* body = location->body;
    /* The bytes of body there are to read. A LOC1 may claim more than are kept,
     * but no LocDefType reads past ITSO_LOC_BODY_LEN, so only a 212's count of
     * further stops needs the length itself. */
    const size_t n = location->length < ITSO_LOC_BODY_LEN ? location->length : ITSO_LOC_BODY_LEN;
    ItsoLocCodeKind kind = ItsoLocCodeNone;
    /* Sized so the compiler can prove every snprintf below fits in ITSO_LOC_LEN. */
    char scratch[16]; /* NLC, zone list, stop code */
    char service[6]; /* four SNCODE characters plus terminator */
    out[0] = '\0';
    code[0] = '\0';

    switch(def_type) {
    case 202: /* Bus fare stage type 1: 3-byte machine number, 1-byte stage. */
        if(n >= 4) {
            const char* station = location->subway_station ? itso_spt_subway_station(body[3]) :
                                                             NULL;
            if(station) {
                snprintf(out, len, "%s", station);
            } else {
                snprintf(
                    out,
                    len,
                    "Fare stage %u (%lu)",
                    body[3],
                    (unsigned long)itso_bits(body, 0, 24));
            }
        }
        break;

    case 203: /* Short rail NLC: four ASCII characters. */
        if(n >= 4) {
            itso_copy_ascii(body, 4, scratch, sizeof(scratch));
            snprintf(out, len, "Station %s", scratch);
            kind = itso_note_code(code, code_len, ItsoLocCodeNlc, scratch);
        }
        break;

    case 204: /* Zonal bit map, valid within zone. */
    case 205: /* Zonal bit map, valid zone to zone. */
        if(n >= 3) {
            /* Three bytes in a LOC1 or LOC2, four in each slot of a LOC3 or
             * LOC4 (TS 1000-1 tables 20-23). A LOC2 pads its fourth with zero,
             * which names no zone. */
            itso_decode_zones(body, n >= 4 ? 4 : 3, scratch, sizeof(scratch));
            if(strcmp(scratch, "none") == 0) {
                snprintf(out, len, "No zones");
            } else {
                snprintf(out, len, "Zones %s", scratch);
            }
        }
        break;

    case 206: /* NaptanCode bus stop, 8 BCD digits. */
        if(n >= 4) {
            /* These digits are not the NaptanCode itself. A NaptanCode is eight
             * characters, and TS 1000-1 table 28 folds its letters onto a
             * telephone keypad (ABC->2 ... WXYZ->9) so that it fits four bytes
             * of BCD. The folding is lossy, so the letters cannot be recovered
             * here - only a table built by folding the register the same way
             * can turn these digits back into a stop name. */
            itso_bcd(body, 0, 8, scratch);
            snprintf(out, len, "Stop %s", scratch);
            if(itso_all_digits(scratch)) {
                kind = itso_note_code(code, code_len, ItsoLocCodeNaptan, scratch);
            }
        }
        break;

    case 207: /* Zone number. */
        if(n >= 4) snprintf(out, len, "Zone %lu", (unsigned long)itso_bits(body, 0, 32));
        break;

    case 208: /* UIC country code plus a four character rail NLC. */
        if(n >= 6) {
            itso_copy_ascii(body + 2, 4, scratch, sizeof(scratch));
            uint32_t country = itso_bits(body, 4, 12);
            char country_digits[4];
            itso_bcd(body, 4, 3, country_digits);
            if(country == 0x070) {
                snprintf(out, len, "Station %s", scratch);
                /* Only UK codes index the station table. */
                kind = itso_note_code(code, code_len, ItsoLocCodeNlc, scratch);
            } else {
                snprintf(out, len, "Station %.4s (country %.3s)", scratch, country_digits);
            }
        }
        break;

    case 209: /* Bus fare stage type 2: OID, SNCODE service, stage. */
        if(n >= 6) {
            itso_decode_service(itso_bits(body, 16, 20), service, sizeof(service));
            snprintf(out, len, "Route %s, stage %u", service, body[5]);
        }
        break;

    case 210: /* One or more SNCODE service numbers. */
        if(n >= 3) {
            itso_decode_service(itso_bits(body, 0, 20), service, sizeof(service));
            snprintf(out, len, "Route %s", service);
        }
        break;

    case 211: /* AtcoCode bus stop: up to twelve ASCII characters (TS 1000-1
               * table 40). Stored whole, so unlike a NaptanCode it needs no
               * unfolding to be looked up. */
        itso_copy_ascii(body, n, scratch, sizeof(scratch));
        if(scratch[0]) {
            snprintf(out, len, "Stop %s", scratch);
            kind = itso_note_code(code, code_len, ItsoLocCodeAtco, scratch);
        }
        break;

    case 212: /* One or more NaptanCodes, four bytes each; show the first and
               * count the rest (TS 1000-1 clause 4.2.4.3.13). */
        if(n >= 4) {
            itso_bcd(body, 0, 8, scratch);
            uint8_t others = itso_location_more(location);
            if(others > 0) {
                /* Bounded for the compiler as the data bounds it: eight digits,
                 * and at most 62 others in a 255-byte body. */
                snprintf(out, len, "Stop %.8s and %u more", scratch, (unsigned)(others % 100));
            } else {
                snprintf(out, len, "Stop %s", scratch);
            }
            if(itso_all_digits(scratch)) {
                kind = itso_note_code(code, code_len, ItsoLocCodeNaptan, scratch);
            }
        }
        break;

    case 216: /* OID (2) + SNCODE2 service (3) + NaptanCode (4), so the stop
               * starts at bit 40 of the body (TS 1000-1 table 42c). */
        if(n >= 9) {
            itso_decode_service2(body + 2, service, sizeof(service));
            char stop[9];
            itso_bcd(body, 40, 8, stop);
            /* The '@' is what flipso_format_lines.c splits on to keep the route
             * number in front of a stop name the table resolves, so everything
             * before it has to read correctly on its own. */
            snprintf(out, len, "Route %s@%s", service, stop);
            if(itso_all_digits(stop)) {
                kind = itso_note_code(code, code_len, ItsoLocCodeNaptan, stop);
            }
        }
        break;

    case 217: /* Bus fare stage type 3. */
        if(n >= 6) {
            itso_decode_service2(body + 2, service, sizeof(service));
            snprintf(out, len, "Route %s, stage %u", service, body[5]);
        }
        break;

    case 218: /* Extended service numbers. */
        if(n >= 3) {
            itso_decode_service2(body, service, sizeof(service));
            snprintf(out, len, "Route %s", service);
        }
        break;

    case 255: /* Null location. */
        snprintf(out, len, "Not recorded");
        break;

    default:
        break;
    }

    if(out[0] == '\0') snprintf(out, len, "Unknown location (type %u)", def_type);
    return kind;
}

void itso_location_text(const ItsoLocation* location, char* out, size_t len) {
    if(len == 0) return;
    char code[ITSO_LOC_CODE_LEN];
    itso_render_location(location, out, len, code, sizeof(code));
}

ItsoLocCodeKind itso_location_code(const ItsoLocation* location, char* out, size_t len) {
    if(len == 0) return ItsoLocCodeNone;
    char text[ITSO_LOC_LEN];
    return itso_render_location(location, text, sizeof(text), out, len);
}

size_t itso_parse_location(
    const uint8_t* data,
    size_t avail,
    ItsoLocStruct variant,
    ItsoLocation* out) {
    memset(out, 0, sizeof(ItsoLocation));
    if(avail < 1) return 0;

    uint8_t def_type = data[0];
    const uint8_t* body;
    size_t body_len;
    size_t consumed;

    if(variant == ItsoLocStructLoc2) {
        /* Fixed 7-byte record: tag then body, zero padded. */
        if(avail < 7) return 0;
        body = data + 1;
        body_len = 6;
        consumed = 7;
    } else {
        /* LOC1 is tag, length, body. A null location may omit the body entirely. */
        if(avail < 2) return 0;
        body_len = data[1];
        if(2 + body_len > avail) return 0;
        body = data + 2;
        consumed = 2 + body_len;
    }

    out->def_type = def_type;
    /* Type 255 is the documented "no location here" marker; treat it as absent
     * so the UI can skip the row rather than printing a placeholder. */
    out->valid = (def_type != 255);
    /* A LOC1's length is one byte and a LOC2's body six, so either fits. */
    out->length = (uint8_t)body_len;
    memcpy(out->body, body, body_len < ITSO_LOC_BODY_LEN ? body_len : ITSO_LOC_BODY_LEN);
    return consumed;
}

void itso_parse_loc_fixed(uint8_t def_type, const uint8_t* data, uint8_t slots, ItsoLocation* out) {
    for(uint8_t i = 0; i < slots; i++) {
        const uint8_t* slot = data + (size_t)i * 4;
        /* Each slot is the LOCE a LOC2 would carry, so it is decoded as one. A
         * fare stage is the exception (TS 1000-1 tables 14 and 15): the
         * destination and via are bare stage numbers on the origin's machine. */
        uint8_t loc2[7] = {def_type, slot[0], slot[1], slot[2], slot[3], 0, 0};
        bool blank = itso_is_blank(slot, 4);
        if(def_type == 202 && i > 0) {
            loc2[1] = data[0];
            loc2[2] = data[1];
            loc2[3] = data[2];
            loc2[4] = slot[0];
            blank = slot[0] == 0;
        }
        itso_parse_location(loc2, sizeof(loc2), ItsoLocStructLoc2, &out[i]);
        if(blank) out[i].valid = false;
    }
}

bool itso_retailer_location(uint16_t retailer, ItsoLocation* out) {
    memset(out, 0, sizeof(*out));
    if(!(retailer & 0x8000)) return false;
    const uint8_t first = (retailer >> 10) & 0x1F;
    const uint16_t rest = retailer & 0x03FF;
    if(rest > 999) return false;
    /* Rebuilt as the LOC1 an NLC would be, so it renders and resolves to a
     * station name the way every other rail location does. */
    char digits[4];
    snprintf(digits, sizeof(digits), "%03u", rest);
    const uint8_t loc1[6] = {
        203,
        4,
        (uint8_t)(first < 10 ? '0' + first : 'A' + first - 10),
        digits[0],
        digits[1],
        digits[2]};
    return itso_parse_location(loc1, sizeof(loc1), ItsoLocStructLoc1, out) == sizeof(loc1);
}
