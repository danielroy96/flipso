/* Host stub: the locale service, fixed to the UK settings the tests expect. */
#pragma once
#include <furi.h>
#include <datetime/datetime.h>

typedef enum { LocaleTimeFormat24h, LocaleTimeFormat12h } LocaleTimeFormat;
typedef enum { LocaleDateFormatDMY, LocaleDateFormatMDY, LocaleDateFormatYMD } LocaleDateFormat;

static inline LocaleDateFormat locale_get_date_format(void) {
    return LocaleDateFormatDMY;
}
static inline LocaleTimeFormat locale_get_time_format(void) {
    return LocaleTimeFormat24h;
}
static inline void locale_format_date(
    FuriString* out,
    const DateTime* dt,
    LocaleDateFormat format,
    const char* separator) {
    UNUSED(format);
    furi_string_printf(
        out, "%02u%s%02u%s%04u", dt->day, separator, dt->month, separator, dt->year);
}
static inline void
    locale_format_time(FuriString* out, const DateTime* dt, LocaleTimeFormat format, bool seconds) {
    UNUSED(format);
    UNUSED(seconds);
    furi_string_printf(out, "%02u:%02u", dt->hour, dt->minute);
}
