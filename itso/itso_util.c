/**
 * @file itso_util.c
 * @brief Bit, BCD, ISAM, date and money primitives for the ITSO decoder.
 */
#include "itso_i.h"

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

uint32_t itso_bcd_number(const uint8_t* data, uint32_t bit_offset, uint8_t digits) {
    uint32_t value = 0;
    for(uint8_t i = 0; i < digits && i < 9; i++) {
        uint8_t nibble = itso_bits(data, bit_offset + i * 4, 4);
        if(nibble > 9) return UINT32_MAX;
        value = value * 10 + nibble;
    }
    return value;
}

uint16_t itso_isam_oid(uint32_t isam) {
    uint16_t top = (uint16_t)(isam >> 19); /* The 13 bits every OID has. */
    if(!(isam & (1UL << 18))) return top;
    if(!(isam & (1UL << 17))) return (uint16_t)(0x2000 | top);
    return (uint16_t)(((isam & (1UL << 16)) ? 0xE000 : 0x6000) | top);
}

bool itso_is_blank(const uint8_t* data, size_t len) {
    for(size_t i = 0; i < len; i++) {
        if(data[i] != 0) return false;
    }
    return true;
}

uint16_t itso_crc_b(const uint8_t* data, size_t len) {
    /* The ISO 14443-3 type B CRC, which ITSO TS 1000-2 Annex A reproduces along
     * with the reference implementation this follows: the ITU-T X.25 polynomial
     * reflected, seeded with 0xFFFF and complemented at the end. Annex A also
     * gives three test vectors, which tools/test/parse/test_shell.c checks. */
    uint16_t crc = 0xFFFF;
    for(size_t i = 0; i < len; i++) {
        uint8_t ch = (uint8_t)(data[i] ^ (crc & 0x00FF));
        ch = (uint8_t)(ch ^ (ch << 4));
        crc = (uint16_t)((crc >> 8) ^ ((uint16_t)ch << 8) ^ ((uint16_t)ch << 3) ^
                         ((uint16_t)ch >> 4));
    }
    return (uint16_t)~crc;
}

ItsoUnixTime itso_date_to_unix(ItsoDate date) {
    uint32_t days = (date == 0) ? ITSO_DATE_MAX : date;
    return ITSO_DATE_EPOCH + days * 86400UL;
}

ItsoUnixTime itso_dts_to_unix(ItsoDts dts) {
    /* Sign-extend the 24-bit two's complement value before offsetting the epoch. */
    int32_t minutes = (int32_t)(dts & 0xFFFFFF);
    if(minutes & 0x800000) minutes -= 0x1000000;
    int64_t unix_time = (int64_t)ITSO_DTS_EPOCH + (int64_t)minutes * 60;
    if(unix_time < 0) return 0;
    return (ItsoUnixTime)unix_time;
}

bool itso_date_expired(ItsoDate date, ItsoUnixTime now) {
    /* A DATE is the last day of validity, so the product survives until midnight. */
    return now >= itso_date_to_unix(date) + 86400UL;
}

bool itso_date_open(ItsoDate date) {
    return date == 0 || date == 0x3FFF;
}

void itso_decode_money(int32_t raw, uint8_t valc, ItsoMoney* out) {
    static const int32_t scale[4] = {1, 10, 100, 1000};
    out->valid = true;
    out->currency = valc & 0x03;
    /* A four-byte amount (TYP 22 AmountPaid) times a scale of 1000 can pass
     * 32 bits, so saturate rather than wrap to a plausible-looking number. */
    int64_t value = (int64_t)raw * scale[(valc >> 2) & 0x03];
    if(value > INT32_MAX) value = INT32_MAX;
    if(value < INT32_MIN) value = INT32_MIN;
    out->value = (int32_t)value;
}

void itso_format_money(const ItsoMoney* money, char* out, size_t len) {
    if(!money->valid) {
        snprintf(out, len, "-");
        return;
    }

    if(money->currency > 1) {
        /* Tokens have no minor unit and no symbol we can rely on. */
        snprintf(out, len, "%ld token%s", (long)money->value, money->value == 1 ? "" : "s");
        return;
    }

    /* VALC's currency bits name the scheme's local currency (0) or its global
     * one (1), TS 1000-5 annex A.21. For a UK scheme those are sterling and the
     * euro, and it is the card that decides which rather than the Flipper's
     * locale: a balance is in whatever money the card holds it in. Both have a
     * base unit of 0.01. The symbols are UTF-8; the view draws them by hand,
     * because the Flipper's fonts do not have them. */
    const char* symbol = (money->currency == 0) ? "\xC2\xA3" : "\xE2\x82\xAC";
    int32_t value = money->value;
    const char* sign = "";
    if(value < 0) {
        sign = "-";
        value = -value;
    }
    snprintf(out, len, "%s%s%ld.%02ld", sign, symbol, (long)(value / 100), (long)(value % 100));
}

uint8_t itso_ticket_days(uint8_t valid_on_day, uint16_t flags) {
    uint8_t days = valid_on_day ? valid_on_day : 0xFF;
    if(!(flags & ITSO_T22_DAY_MASK)) return days;

    if(!(flags & (ITSO_T22_WEEKDAY_AM | ITSO_T22_WEEKDAY_PM))) days &= ~ITSO_DOW_WEEKDAYS;
    if(!(flags & (ITSO_T22_SATURDAY_AM | ITSO_T22_SATURDAY_PM))) days &= ~ITSO_DOW_SATURDAY;
    if(!(flags & (ITSO_T22_SUNDAY_AM | ITSO_T22_SUNDAY_PM))) days &= ~ITSO_DOW_SUNDAY;
    if(!(flags & ITSO_T22_PUBLIC_HOLIDAY)) days &= ~ITSO_DOW_SPECIAL;
    return days;
}

void itso_format_days(uint8_t days, char* out, size_t len) {
    static const char* const names[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    if(!len) return;
    out[0] = '\0';

    days &= ITSO_DOW_ALL_DAYS;
    if(days == ITSO_DOW_ALL_DAYS) {
        snprintf(out, len, "Every day");
        return;
    }
    if(!days) {
        snprintf(out, len, "None");
        return;
    }

    /* A single run of three or more days reads better as a range. */
    int first = -1, last = -1, count = 0;
    for(int i = 0; i < 7; i++) {
        if(days & (ITSO_DOW_MONDAY >> i)) {
            if(first < 0) first = i;
            last = i;
            count++;
        }
    }
    if(count >= 3 && count == last - first + 1) {
        snprintf(out, len, "%s-%s", names[first], names[last]);
        return;
    }

    size_t pos = 0;
    for(int i = 0; i < 7 && pos + 1 < len; i++) {
        if(!(days & (ITSO_DOW_MONDAY >> i))) continue;
        int n = snprintf(out + pos, len - pos, "%s%s", pos ? " " : "", names[i]);
        if(n < 0 || (size_t)n >= len - pos) break; /* Truncated: stop, still terminated. */
        pos += (size_t)n;
    }
}

void itso_format_part_days(uint8_t days, uint16_t flags, char* out, size_t len) {
    static const struct {
        uint8_t days;
        uint16_t am;
        uint16_t pm;
        const char* name;
    } groups[] = {
        {ITSO_DOW_WEEKDAYS, ITSO_T22_WEEKDAY_AM, ITSO_T22_WEEKDAY_PM, "Weekdays"},
        {ITSO_DOW_SATURDAY, ITSO_T22_SATURDAY_AM, ITSO_T22_SATURDAY_PM, "Sat"},
        {ITSO_DOW_SUNDAY, ITSO_T22_SUNDAY_AM, ITSO_T22_SUNDAY_PM, "Sun"},
    };
    if(!len) return;
    out[0] = '\0';
    if(!(flags & ITSO_T22_DAY_MASK)) return;

    size_t pos = 0;
    for(size_t i = 0; i < sizeof(groups) / sizeof(groups[0]); i++) {
        if(!(days & groups[i].days)) continue;
        bool am = (flags & groups[i].am) != 0;
        bool pm = (flags & groups[i].pm) != 0;
        if(am == pm) continue;
        int n = snprintf(
            out + pos,
            len - pos,
            "%s%s %s only",
            pos ? ", " : "",
            groups[i].name,
            am ? "AM" : "PM");
        if(n < 0 || (size_t)n >= len - pos) break;
        pos += (size_t)n;
    }
}

uint8_t itso_half_days_mask(uint16_t half_days) {
    /* Annex A.10 packs a pair of period bits per day, Monday first, special
     * days last - the same order as a ValidOnDayCode's bits. */
    uint8_t mask = 0;
    for(int day = 0; day < 8; day++) {
        if((half_days >> (14 - 2 * day)) & 0x03) mask |= (uint8_t)(ITSO_DOW_MONDAY >> day);
    }
    return mask;
}

/** Read a signed 16-bit big-endian value: a VALS, which only a balance is. */
int32_t itso_int16(const uint8_t* data) {
    return (int16_t)((data[0] << 8) | data[1]);
}

/**
 * Read an unsigned 16-bit big-endian value: a VALI, which every limit, deposit
 * and price is (TS 1000-1 table 5). Read as signed, anything past GBP 327.67
 * came out negative.
 */
int32_t itso_uint16(const uint8_t* data) {
    return (int32_t)((data[0] << 8) | data[1]);
}

/** Copy a fixed-length ASCII name field, trimming trailing spaces. */
size_t itso_copy_name(const uint8_t* src, size_t n, char* dst, size_t dst_len) {
    size_t pos = 0;
    for(size_t i = 0; i < n && pos + 1 < dst_len; i++) {
        char c = (char)src[i];
        if(c < 0x20 || c > 0x7E) break;
        dst[pos++] = c;
    }
    while(pos > 0 && dst[pos - 1] == ' ') {
        pos--;
    }
    dst[pos] = '\0';
    return pos;
}

/**
 * Copy a fixed-length ASCII field as itso_copy_name() does, less its leading
 * spaces too: rail left-pads a coach to two characters and a seat to three,
 * " A" and " 33" (RSPS3002 section 3.8.6), and the padding is not part of it.
 */
void itso_copy_trimmed(const uint8_t* src, size_t n, char* dst, size_t dst_len) {
    while(n > 0 && *src == ' ') {
        src++;
        n--;
    }
    itso_copy_name(src, n, dst, dst_len);
}
