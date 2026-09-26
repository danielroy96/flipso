/* Host stub: the firmware's DateTime and its conversion from Unix time. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint8_t weekday;
} DateTime;

/* Howard Hinnant's civil_from_days, which is what the firmware's own loop over
 * years and months comes to for any date a card can hold. */
static inline void datetime_timestamp_to_datetime(uint32_t timestamp, DateTime* dt) {
    int64_t days = timestamp / 86400;
    uint32_t rest = timestamp % 86400;
    dt->hour = (uint8_t)(rest / 3600);
    dt->minute = (uint8_t)(rest / 60 % 60);
    dt->second = (uint8_t)(rest % 60);
    days += 719468;
    int64_t era = days / 146097;
    int64_t doe = days - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    dt->day = (uint8_t)(doy - (153 * mp + 2) / 5 + 1);
    dt->month = (uint8_t)(mp < 10 ? mp + 3 : mp - 9);
    dt->year = (uint16_t)(yoe + era * 400 + (dt->month <= 2));
    dt->weekday = (uint8_t)((timestamp / 86400 + 3) % 7 + 1);
}
