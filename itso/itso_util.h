/**
 * @file itso_util.h
 * @brief Field access, dates, money and day masks: the primitives every decoder uses.
 */
#pragma once

#include "itso_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Read up to 32 bits, MSB first, from an arbitrary bit offset. */
uint32_t itso_bits(const uint8_t* data, uint32_t bit_offset, uint8_t bit_len);

/** Read @p digits BCD nibbles into @p out, which needs digits+1 bytes. */
void itso_bcd(const uint8_t* data, uint32_t bit_offset, uint8_t digits, char* out);

/**
 * Read @p digits BCD digits (at most 9) as a number: an IIN, which TS 1000-1
 * defines as six BCD digits in three bytes. A nibble above 9 is not a digit,
 * and makes the whole value UINT32_MAX rather than a plausible wrong number.
 */
uint32_t itso_bcd_number(const uint8_t* data, uint32_t bit_offset, uint8_t digits);

/**
 * The operator an ISAM is registered to (TS 1000-2 annex B, tables B3 and B4).
 *
 * The top 13 bits of an ISAM ID are the OID; bits 18, 17 and 16 extend it into
 * the 8192, 24576 and 57344 ranges at the expense of the serial number. Every
 * ISAM ID on a card - who created a product, who last wrote a value record or
 * the directory, whose reader took a tap - can be named this way.
 */
uint16_t itso_isam_oid(uint32_t isam);

/** True if every byte in the range is zero. */
bool itso_is_blank(const uint8_t* data, size_t len);

/**
 * Convert a 14-bit EN1545 DateStamp to a Unix timestamp.
 * Days since 1997-01-01; a stored zero means the maximum date (2041-11-10).
 */
uint32_t itso_date_to_unix(uint16_t date);

/**
 * Convert a 24-bit DTS to a Unix timestamp.
 * DTS is a two's complement count of minutes from the epoch 2028-11-24 20:16.
 */
uint32_t itso_dts_to_unix(uint32_t dts);

/** True once the DATE has passed relative to @p now (a Unix timestamp). */
bool itso_date_expired(uint16_t date, uint32_t now);

/**
 * True for an expiry DATE that means "does not expire": zero, the EN1545 maximum
 * date schemes use for it, and 0x3FFF, the last date the 14 bits can hold, which
 * is what TS 1000-10 table 42 gives a compact shell ("does not expire for the
 * foreseeable future"). Either would otherwise print as a day in 2041.
 */
bool itso_date_open(uint16_t date);

/** HalfDayOfWeek as a ValidOnDayCode-style day mask: a day counts if either period does. */
uint8_t itso_half_days_mask(uint16_t half_days);

/**
 * The days a TYP 22 ticket may be used, as a ValidOnDayCode-style mask.
 *
 * Rule 7 of TS 1000-5 clause 2.9.1.4 requires ValidOnDayCode and the day's
 * TYP22Flags to both allow it, so a day either one excludes is dropped. A filter
 * that is entirely zero is taken as not in use rather than as "never valid": a
 * ticket nobody could travel on is not a product anyone sells.
 */
uint8_t itso_ticket_days(uint8_t valid_on_day, uint16_t flags);

/**
 * Render a day mask as briefly as it will go: "every day", "Mon-Fri",
 * "Sat Sun", "Mon Wed Fri". The special-days bit is not rendered; callers say
 * what they mean by it.
 */
void itso_format_days(uint8_t days, char* out, size_t len);

/**
 * The days in @p days that TYP22Flags allows for only half of, e.g. "Sat PM
 * only". Empty when there are none, including when the flags do not restrict by
 * day at all.
 */
void itso_format_part_days(uint8_t days, uint16_t flags, char* out, size_t len);

/**
 * Render an amount, e.g. "£12.34", with the symbol in UTF-8. Writes at most
 * @p len bytes; 16 holds any amount.
 */
void itso_format_money(const ItsoMoney* money, char* out, size_t len);

#ifdef __cplusplus
}
#endif
