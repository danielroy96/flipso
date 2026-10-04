/**
 * @file flipso_format_lines.c
 * @brief The line builders every screen shares, each in the house style of flipso_format.h.
 */
#include "flipso_format_i.h"

void flipso_cat_datetime_struct(FuriString* out, const DateTime* dt, bool with_time) {
    FuriString* formatted = furi_string_alloc();
    locale_format_date(formatted, dt, locale_get_date_format(), "/");
    furi_string_cat(out, formatted);

    if(with_time) {
        furi_string_reset(formatted);
        locale_format_time(formatted, dt, locale_get_time_format(), false);
        furi_string_cat_printf(out, " %s", furi_string_get_cstr(formatted));
    }

    furi_string_free(formatted);
}

/** Split a Unix timestamp and append it in the user's configured date format. */
void flipso_cat_timestamp(FuriString* out, ItsoUnixTime timestamp, bool with_time) {
    DateTime dt;
    datetime_timestamp_to_datetime(timestamp, &dt);
    flipso_cat_datetime_struct(out, &dt, with_time);
}

void flipso_cat_date(FuriString* out, ItsoDate date) {
    flipso_cat_timestamp(out, itso_date_to_unix(date), false);
}

void flipso_cat_short_date(FuriString* out, ItsoDate date) {
    FuriString* full = furi_string_alloc();
    flipso_cat_date(full, date);
    const char* text = furi_string_get_cstr(full);
    size_t len = strlen(text);
    /* The locale writes the year as four digits, first or last; the century
     * is the pair that goes. */
    if(len == 10 && locale_get_date_format() == LocaleDateFormatYMD) {
        furi_string_cat(out, text + 2);
    } else if(len == 10) {
        furi_string_cat_printf(out, "%.6s%s", text, text + 8);
    } else {
        furi_string_cat(out, text);
    }
    furi_string_free(full);
}

void flipso_cat_time(FuriString* out, ItsoUnixTime timestamp) {
    flipso_cat_timestamp(out, timestamp, true);
}

/** Append "dd/mm/yyyy hh:mm" for an ITSO DTS. */
static void flipso_cat_datetime(FuriString* out, ItsoDts dts) {
    flipso_cat_timestamp(out, itso_dts_to_unix(dts), true);
}

void flipso_cat_heading(FuriString* out, FlipsoIcon icon, const char* title) {
    if(icon > FlipsoIconNone && icon < FlipsoIconCount) {
        furi_string_cat_printf(out, "\e#%c%s\n", (char)(FLIPSO_TEXT_ICON_BASE + icon), title);
    } else {
        furi_string_cat_printf(out, "\e#%s\n", title);
    }
}

void flipso_cat_page(FuriString* out, FlipsoIcon icon, const char* title) {
    if(!furi_string_empty(out)) furi_string_push_back(out, FLIPSO_TEXT_PAGE);
    flipso_cat_heading(out, icon, title);
}

void flipso_cat_page_from(FuriString* out, FlipsoIcon icon, const char* title, FuriString* body) {
    if(furi_string_empty(body)) return;
    flipso_cat_page(out, icon, title);
    furi_string_cat(out, body);
    furi_string_reset(body);
}

/** "Label: Yes" or "Label: No". */
void flipso_cat_flag(FuriString* out, const char* indent, const char* label, bool value) {
    furi_string_cat_printf(out, "%s%s: %s\n", indent, label, value ? "Yes" : "No");
}

/** "Label: £1.23", or nothing when the amount was not decoded. */
void flipso_cat_money(
    FuriString* out,
    const char* indent,
    const char* label,
    const ItsoMoney* money) {
    if(!money->valid) return;
    char text[FLIPSO_MONEY_LEN];
    itso_format_money(money, text, sizeof(text));
    furi_string_cat_printf(out, "%s%s: %s\n", indent, label, text);
}

/** "Label: +£1.23" or "Label: -£1.23": a change, which says which way it went. */
void flipso_cat_money_change(
    FuriString* out,
    const char* indent,
    const char* label,
    const ItsoMoney* money) {
    if(!money->valid) return;
    char text[FLIPSO_MONEY_LEN];
    itso_format_money(money, text, sizeof(text));
    /* itso_format_money() already writes a minus; zero has no sign to show. */
    furi_string_cat_printf(out, "%s%s: %s%s\n", indent, label, money->value > 0 ? "+" : "", text);
}

/** "Label: dd/mm/yyyy". */
void flipso_cat_date_line(FuriString* out, const char* indent, const char* label, ItsoDate date) {
    furi_string_cat_printf(out, "%s%s: ", indent, label);
    flipso_cat_date(out, date);
    furi_string_push_back(out, '\n');
}

/** "Label: dd/mm/yyyy hh:mm" for a DTS. */
void flipso_cat_datetime_line(FuriString* out, const char* indent, const char* label, ItsoDts dts) {
    furi_string_cat_printf(out, "%s%s: ", indent, label);
    flipso_cat_datetime(out, dts);
    furi_string_push_back(out, '\n');
}

/**
 * An expiry date, whose label changes once it has passed.
 *
 * Changing the label rather than appending a marker keeps the line to what fits
 * across the screen. The dates that mean "no expiry" (itso_date_open()) say so
 * rather than printing a day in 2041.
 */
void flipso_cat_expiry(
    FuriString* out,
    const char* indent,
    const char* label,
    const char* past_label,
    ItsoDate date,
    ItsoUnixTime now) {
    if(itso_date_open(date)) {
        furi_string_cat_printf(out, "%s%s: No expiry\n", indent, label);
        return;
    }
    flipso_cat_date_line(out, indent, itso_date_expired(date, now) ? past_label : label, date);
}

/** "VAT: 20.00%", from a rate in 0.01% steps. Nothing for a rate of zero. */
void flipso_cat_vat(FuriString* out, const char* indent, uint16_t vat) {
    if(vat) furi_string_cat_printf(out, "%sVAT: %u.%02u%%\n", indent, vat / 100, vat % 100);
}

/**
 * "Label: Southeastern", or the operator's number where it has no name.
 *
 * ITSO's operator register is not public, so an unknown number is expected
 * rather than exceptional; the number is what lets the user add it to their
 * operators file.
 */
void flipso_cat_operator(
    FuriString* out,
    const FlipsoFormat* f,
    const char* indent,
    const char* label,
    uint16_t oid) {
    const char* name = flipso_operators_name(f->operators, oid);
    if(name) {
        furi_string_cat_printf(out, "%s%s: %s\n", indent, label, name);
    } else {
        furi_string_cat_printf(out, "%s%s: Unknown (%u)\n", indent, label, oid);
    }
}

/**
 * The operator a machine's ISAM is registered to, then the machine itself.
 *
 * TS 1000-2 annex B builds an ISAM ID from the OID of the operator it belongs
 * to, so every ISAM on a card names an operator. Zero is what a record holds
 * until a machine first writes it (TS 1000-2 clause 7.2.4.4), not operator
 * zero, so it gets nothing.
 */
void flipso_cat_machine(
    FuriString* out,
    const FlipsoFormat* f,
    const char* indent,
    const char* label,
    uint32_t isam) {
    if(!isam) return;
    furi_string_cat_printf(out, "%s%s: %08lX\n", indent, label, (unsigned long)isam);
    /* The operator is a detail of the machine, so one step further in. */
    char under[16];
    snprintf(under, sizeof(under), "%s  ", indent);
    flipso_cat_operator(out, f, under, "Operator", itso_isam_oid(isam));
}

/**
 * "Label: place", or nothing when the location is absent.
 *
 * Rail codes are resolved to station names and bus stop codes to stop names,
 * where the tables that hold them are on the SD card; anything else falls back
 * to the text the decoder renders from the code itself.
 */
void flipso_cat_location(
    FuriString* out,
    const FlipsoFormat* f,
    const char* indent,
    const char* label,
    const ItsoLocation* location) {
    if(!location->valid) return;

    /* A location holds the card's bytes, not its text: the code and the text
     * are rendered here, for the one line, on the stack. */
    char code[ITSO_LOC_CODE_LEN];
    const char* place = NULL;
    switch(itso_location_code(location, code, sizeof(code))) {
    case ItsoLocCodeNlc:
        place = flipso_stations_name(f->stations, code);
        break;
    case ItsoLocCodeNaptan:
        place = flipso_naptan_stop(f->naptan, code);
        break;
    case ItsoLocCodeAtco:
        place = flipso_naptan_atco(f->naptan, code);
        break;
    default:
        break;
    }
    char text[ITSO_LOC_LEN];
    itso_location_text(location, text, sizeof(text));

    /* LocDefType 216 is a route and a stop together (TS 1000-1 table 42c),
     * which itso_location_text() joins with an '@'. The route half is kept
     * either way; the stop half is named where the table can, and reads as a
     * stop number where it cannot. */
    const char* at = location->def_type == 216 ? strchr(text, '@') : NULL;
    if(at) {
        int route_len = (int)(at - text);
        if(place) {
            furi_string_cat_printf(
                out, "%s%s: %.*s at %s\n", indent, label, route_len, text, place);
        } else {
            furi_string_cat_printf(
                out, "%s%s: %.*s, stop %s\n", indent, label, route_len, text, at + 1);
        }
        return;
    }

    uint8_t more = itso_location_more(location);
    if(place && more) {
        /* Named, the first stop no longer carries the count the decoder's own
         * text gave it, so it is put back. */
        furi_string_cat_printf(out, "%s%s: %s and %u more\n", indent, label, place, more);
        return;
    }
    furi_string_cat_printf(out, "%s%s: %s\n", indent, label, place ? place : text);
}

void flipso_cat_hex(FuriString* out, const uint8_t* data, size_t len) {
    for(size_t i = 0; i < len; i++) {
        furi_string_cat_printf(out, "%02X", data[i]);
    }
}

/**
 * Owner-defined bytes: as text when they are printable ASCII, which a rail
 * RouteCode is, else as hex. Zero padding after text is the spec's, and all
 * zeros is its "not used" (TS 1000-5 tables 27a and 31a).
 */
void flipso_cat_code_bytes(FuriString* out, const uint8_t* data, size_t len) {
    size_t text = 0;
    while(text < len && data[text] >= 0x20 && data[text] <= 0x7E) {
        text++;
    }
    size_t end = text;
    while(end < len && data[end] == 0) {
        end++;
    }
    if(text == 0 && end == len) {
        furi_string_cat(out, "None");
    } else if(text > 0 && end == len) {
        for(size_t i = 0; i < text; i++) {
            furi_string_push_back(out, (char)data[i]);
        }
    } else {
        for(size_t i = 0; i < len; i++) {
            furi_string_cat_printf(out, "%02X", data[i]);
        }
    }
}

/**
 * A user-defined element (TS 1000-1's UD) as it stands: text when it is
 * printable, less trailing spaces and zero padding; otherwise a number when it
 * is short enough to be one, which a ticket number is; otherwise hex. All
 * zeros is "None".
 */
void flipso_cat_ud(FuriString* out, const uint8_t* data, size_t len) {
    size_t text = 0;
    while(text < len && data[text] >= 0x20 && data[text] <= 0x7E) {
        text++;
    }
    size_t end = text;
    while(end < len && data[end] == 0) {
        end++;
    }
    if(text > 0 && end == len) {
        while(text > 0 && data[text - 1] == ' ') {
            text--;
        }
        if(text > 0) {
            furi_string_cat_printf(out, "%.*s", (int)text, (const char*)data);
            return;
        }
    }
    if(len <= 4) {
        uint32_t number = 0;
        for(size_t i = 0; i < len; i++) {
            number = (number << 8) | data[i];
        }
        if(number == 0) {
            furi_string_cat(out, "None");
        } else {
            furi_string_cat_printf(out, "%lu", (unsigned long)number);
        }
        return;
    }
    flipso_cat_code_bytes(out, data, len);
}

/** "Label: <UD element>". */
void flipso_cat_ud_line(
    FuriString* out,
    const char* indent,
    const char* label,
    const uint8_t* data,
    size_t len) {
    furi_string_cat_printf(out, "%s%s: ", indent, label);
    flipso_cat_ud(out, data, len);
    furi_string_push_back(out, '\n');
}
