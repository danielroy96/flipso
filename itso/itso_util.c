/**
 * @file itso_util.c
 * @brief Bit, BCD, date and money primitives for the ITSO decoder.
 */
#include "itso_i.h"

#include <string.h>
#include <stdio.h>

/* 1997-01-01T00:00:00Z: day zero of the EN1545 DateStamp. */
#define ITSO_DATE_EPOCH 852076800UL
/* 2028-11-24T20:16:00Z: minute zero of the ITSO DateTimeStamp (TS 1000-1 clause 6). */
#define ITSO_DTS_EPOCH  1858709760UL
/* A DateStamp of zero means the top of the 14-bit range, not 1997-01-01. */
#define ITSO_DATE_MAX   16384UL

uint32_t itso_bits(const uint8_t* data, uint32_t bit_offset, uint8_t bit_len) {
    uint32_t result = 0;
    for(uint8_t i = 0; i < bit_len; i++) {
        uint32_t bit = bit_offset + i;
        uint8_t value = (data[bit / 8] >> (7 - (bit % 8))) & 1;
        result = (result << 1) | value;
    }
    return result;
}

void itso_bcd(const uint8_t* data, uint32_t bit_offset, uint8_t digits, char* out) {
    for(uint8_t i = 0; i < digits; i++) {
        uint8_t nibble = itso_bits(data, bit_offset + i * 4, 4);
        out[i] = (nibble <= 9) ? ('0' + nibble) : 'F';
    }
    out[digits] = '\0';
}

bool itso_is_blank(const uint8_t* data, size_t len) {
    for(size_t i = 0; i < len; i++) {
        if(data[i] != 0) return false;
    }
    return true;
}

uint32_t itso_date_to_unix(uint16_t date) {
    uint32_t days = (date == 0) ? ITSO_DATE_MAX : date;
    return ITSO_DATE_EPOCH + days * 86400UL;
}

uint32_t itso_dts_to_unix(uint32_t dts) {
    /* Sign-extend the 24-bit two's complement value before offsetting the epoch. */
    int32_t minutes = (int32_t)(dts & 0xFFFFFF);
    if(minutes & 0x800000) minutes -= 0x1000000;
    int64_t unix_time = (int64_t)ITSO_DTS_EPOCH + (int64_t)minutes * 60;
    if(unix_time < 0) return 0;
    return (uint32_t)unix_time;
}

bool itso_date_expired(uint16_t date, uint32_t now) {
    /* A DATE is the last day of validity, so the product survives until midnight. */
    return now >= itso_date_to_unix(date) + 86400UL;
}

void itso_decode_money(int32_t raw, uint8_t valc, ItsoMoney* out) {
    static const int32_t scale[4] = {1, 10, 100, 1000};
    out->valid = true;
    out->currency = valc & 0x03;
    out->value = raw * scale[(valc >> 2) & 0x03];
}

void itso_format_money(const ItsoMoney* money, char* out, size_t len) {
    if(!money->valid) {
        snprintf(out, len, "-");
        return;
    }

    if(money->currency > 1) {
        /* Tokens have no minor unit and no symbol we can rely on. */
        snprintf(out, len, "%ld tokens", (long)money->value);
        return;
    }

    /* Local (GBP) and global (EUR) currencies both use a base unit of 0.01. */
    const char* symbol = (money->currency == 0) ? "GBP" : "EUR";
    int32_t value = money->value;
    const char* sign = "";
    if(value < 0) {
        sign = "-";
        value = -value;
    }
    snprintf(out, len, "%s%s %ld.%02ld", sign, symbol, (long)(value / 100), (long)(value % 100));
}

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
            int written = snprintf(
                out + pos, len - pos, "%s%u", printed ? "," : "", (unsigned)zone);
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

/**
 * Render a location body (everything after the tag and optional length byte).
 * @param body  first byte of the location element.
 * @param n     bytes of body available.
 */
static void itso_render_location(
    uint8_t def_type,
    const uint8_t* body,
    size_t n,
    char* out,
    size_t len,
    char* code) {
    /* Sized so the compiler can prove every snprintf below fits in ITSO_LOC_LEN. */
    char scratch[16]; /* NLC, zone list, stop code */
    char service[6]; /* four SNCODE characters plus terminator */
    out[0] = '\0';

    switch(def_type) {
    case 202: /* Bus fare stage type 1: 3-byte machine number, 1-byte stage. */
        if(n >= 4) snprintf(out, len, "Stage %u/%lu", body[3], (unsigned long)itso_bits(body, 0, 24));
        break;

    case 203: /* Short rail NLC: four ASCII characters. */
        if(n >= 4) {
            itso_copy_ascii(body, 4, scratch, sizeof(scratch));
            snprintf(out, len, "NLC %s", scratch);
            snprintf(code, ITSO_LOC_CODE_LEN, "%.4s", scratch); /* NLC is four characters */
        }
        break;

    case 204: /* Zonal bit map, valid within zone. */
    case 205: /* Zonal bit map, valid zone to zone. */
        if(n >= 3) {
            itso_decode_zones(body, 3, scratch, sizeof(scratch));
            snprintf(out, len, "Zones %s", scratch);
        }
        break;

    case 206: /* NaptanCode bus stop, 8 BCD digits. */
        if(n >= 4) {
            itso_bcd(body, 0, 8, scratch);
            snprintf(out, len, "Stop %s", scratch);
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
                snprintf(out, len, "NLC %s", scratch);
                /* Only UK codes index the station table. */
                snprintf(code, ITSO_LOC_CODE_LEN, "%.4s", scratch); /* NLC is four characters */
            } else {
                snprintf(out, len, "%s %s", country_digits, scratch);
            }
        }
        break;

    case 209: /* Bus fare stage type 2: OID, SNCODE service, stage. */
        if(n >= 6) {
            itso_decode_service(itso_bits(body, 16, 20), service, sizeof(service));
            snprintf(out, len, "Svc %s stg %u", service, body[5]);
        }
        break;

    case 210: /* One or more SNCODE service numbers. */
        if(n >= 3) {
            itso_decode_service(itso_bits(body, 0, 20), service, sizeof(service));
            snprintf(out, len, "Svc %s", service);
        }
        break;

    case 211: /* AtcoCode bus stop, ASCII. */
        itso_copy_ascii(body, n, scratch, sizeof(scratch));
        if(scratch[0]) snprintf(out, len, "Stop %s", scratch);
        break;

    case 212: /* One or more NaptanCodes; show the first. */
        if(n >= 4) {
            itso_bcd(body, 0, 8, scratch);
            snprintf(out, len, "Stop %s%s", scratch, n >= 8 ? "+" : "");
        }
        break;

    case 216: /* Extended service number plus NaptanCode stop. */
        if(n >= 9) {
            itso_decode_service2(body + 2, service, sizeof(service));
            char stop[9];
            itso_bcd(body, 40, 8, stop);
            snprintf(out, len, "%s@%s", service, stop);
        }
        break;

    case 217: /* Bus fare stage type 3. */
        if(n >= 6) {
            itso_decode_service2(body + 2, service, sizeof(service));
            snprintf(out, len, "Svc %s stg %u", service, body[5]);
        }
        break;

    case 218: /* Extended service numbers. */
        if(n >= 3) {
            itso_decode_service2(body, service, sizeof(service));
            snprintf(out, len, "Svc %s", service);
        }
        break;

    case 255: /* Null location. */
        snprintf(out, len, "Not recorded");
        break;

    default:
        break;
    }

    if(out[0] == '\0') snprintf(out, len, "Loc type %u", def_type);
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
    itso_render_location(def_type, body, body_len, out->text, sizeof(out->text), out->code);
    return consumed;
}
