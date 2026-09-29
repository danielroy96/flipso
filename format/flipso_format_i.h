/**
 * @file flipso_format_i.h
 * @brief What the flipso_format*.c files share with each other and nobody else.
 *
 * The screens are built across several files, one per screen or group of
 * screens, and these are the line builders more than one of them uses. Every
 * one follows the house style flipso_format.h sets out.
 */
#pragma once

#include "flipso_format.h"
#include "../itso/itso_operators.h"
#include "../views/flipso_text_view.h"

#include <datetime/datetime.h>
#include <locale/locale.h>
#include <string.h>

/* Room for any amount itso_format_money() writes. */
#define FLIPSO_MONEY_LEN 24

/** Append a date, and optionally its time, in the user's configured formats. */
void flipso_cat_datetime_struct(FuriString* out, const DateTime* dt, bool with_time);

/** "Label: Yes" or "Label: No". */
void flipso_cat_flag(FuriString* out, const char* indent, const char* label, bool value);

/** "Label: £1.23", or nothing when the amount was not decoded. */
void flipso_cat_money(
    FuriString* out,
    const char* indent,
    const char* label,
    const ItsoMoney* money);

/** "Label: dd/mm/yyyy". */
void flipso_cat_date_line(FuriString* out, const char* indent, const char* label, uint16_t date);

/** "Label: dd/mm/yyyy hh:mm" for a DTS. */
void flipso_cat_datetime_line(FuriString* out, const char* indent, const char* label, uint32_t dts);

/** An expiry date, labelled @p past_label once it has passed, or "No expiry". */
void flipso_cat_expiry(
    FuriString* out,
    const char* indent,
    const char* label,
    const char* past_label,
    uint16_t date,
    uint32_t now);

/** "VAT: 20.00%", from a rate in 0.01% steps. Nothing for a rate of zero. */
void flipso_cat_vat(FuriString* out, const char* indent, uint16_t vat);

/** "Label: Southeastern", or "Label: Unknown (1234)" where it has no name. */
void flipso_cat_operator(
    FuriString* out,
    const FlipsoFormat* f,
    const char* indent,
    const char* label,
    uint16_t oid);

/** A machine an ISAM ID names, and the operator it is registered to. */
void flipso_cat_machine(
    FuriString* out,
    const FlipsoFormat* f,
    const char* indent,
    const char* label,
    uint32_t isam);

/** "Label: place", named from the SD card tables where they can; nothing when absent. */
void flipso_cat_location(
    FuriString* out,
    const FlipsoFormat* f,
    const char* indent,
    const char* label,
    const ItsoLocation* location);

/** "Label: Pay as you go", naming the product in directory entry @p dir_index. */
void flipso_cat_product_ref(
    FuriString* out,
    const ItsoCard* card,
    const char* indent,
    const char* label,
    uint8_t dir_index);

/** The name of the product in directory entry @p dir_index, or "Directory slot 3". */
void flipso_cat_product_name(FuriString* out, const ItsoCard* card, uint8_t dir_index);

/**
 * When or where a Space Saving ticket was last used, or "Last used: Never";
 * nothing for any other product. A TYP 29 revision 1 records a place, labelled
 * @p place_label, or by default by whether the holder got on or off there.
 */
void flipso_cat_last_use(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product,
    const char* place_label);

/** "Label: Active" for a compact-shell ticket, from its one product. */
void flipso_cat_ticket_state(
    FuriString* out,
    const char* label,
    const ItsoCard* card,
    uint32_t now);
