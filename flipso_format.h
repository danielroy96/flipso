/**
 * @file flipso_format.h
 * @brief Everything the detail screens say, as text, built without a screen.
 *
 * Each screen of a card is a scrolling page of "Label: Value" lines under bold
 * headings, and all of them are built here from an ItsoCard and the lookup
 * tables, so that the words the user reads can be tested on the host like the
 * decoder behind them: tools/test/test_format.c renders the synthetic cards and
 * checks what comes out. The scenes only hand the result to the text view.
 *
 * The conventions every line follows, so the screens read as one app:
 *
 *   - "Label: Value", with the value capitalised: "Photo on card: Yes".
 *   - A detail of the line above is indented by two spaces and is itself
 *     "Label: Value"; the text view keeps the indent when it wraps.
 *   - Money is "£12.34" - see itso_format_money().
 *   - Dates and times follow the Flipper's own locale settings.
 *
 * Nothing here includes a firmware header beyond furi's strings and the locale
 * service, both of which the host tests stub.
 */
#pragma once

#include "flipso_capture.h"
#include "flipso_media.h"
#include "flipso_naptan.h"
#include "flipso_operators.h"
#include "flipso_stations.h"
#include "itso/itso.h"

#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The icons a heading or a row can carry.
 *
 * Numbered from 1 because a heading names its icon with one byte after the
 * "\e#" markup (see flipso_text_view.h), and 0 would end the string. The app
 * maps these onto its I_ symbols in flipso.c; the host tests need only the
 * numbers.
 */
typedef enum {
    FlipsoIconNone = 0,
    FlipsoIconCard,
    FlipsoIconPurse,
    FlipsoIconId,
    FlipsoIconTaps,
    FlipsoIconProducts,
    FlipsoIconPass,
    FlipsoIconTicket,
    FlipsoIconStar,
    FlipsoIconTag,
    FlipsoIconPast,
    FlipsoIconWarning,
    FlipsoIconSave,
    FlipsoIconAccount,
    FlipsoIconInfo,
    FlipsoIconCount, /**< One past the last; the table size is this less one. */
} FlipsoIcon;

/** What a screen needs besides the card: the names it looks things up in. */
typedef struct {
    const FlipsoOperators* operators;
    FlipsoStations* stations;
    FlipsoNaptan* naptan;
    /** The raw blocks, for the parts decoded on demand (fare capping). */
    const FlipsoCapture* capture;
    /** What the chip said about itself on a live read; NULL or invalid if nothing. */
    const FlipsoMedia* media;
    /** Unix time, for deciding what has expired. */
    uint32_t now;
} FlipsoFormat;

/* ------------------------------------------------------------------ */
/* Whole screens                                                       */
/* ------------------------------------------------------------------ */

/** The first screen of a card: its state, what it holds, and where it was last used. */
void flipso_format_summary(FuriString* out, const FlipsoFormat* f, const ItsoCard* card);

/**
 * The card itself: number, expiry, issuer, chip and layout.
 *
 * @param saved_name the saved card's name, or NULL for a card just read.
 * @param read_at    when a saved card was read; 0 when not known.
 */
void flipso_format_card(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const char* saved_name,
    uint32_t read_at);

/** Every purse the card holds now. */
void flipso_format_payg(FuriString* out, const FlipsoFormat* f, const ItsoCard* card);

/** Every identity and entitlement product the card holds now. */
void flipso_format_id(FuriString* out, const FlipsoFormat* f, const ItsoCard* card);

/** Where the card stands in a journey, and the journey log. */
void flipso_format_taps(FuriString* out, const FlipsoFormat* f, const ItsoCard* card);

/**
 * Everything a DESFire said about itself without a key: the chip, its
 * applications and the files of the one looked inside. The screen for a card
 * Flipso recognises and cannot decode, such as an Oyster.
 */
void flipso_format_media(FuriString* out, const FlipsoMedia* media);

/** Everything decoded about one product. */
void flipso_format_product(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product);

/**
 * What Flipso is, which build this is, and what it has to work with.
 *
 * The lookup tables are optional and live on the SD card, and nothing else in
 * the app says whether they are there: without the stop table a bus journey
 * reads "Stop 28632832", which looks like a fault rather than a missing file.
 * This is where that is said, along with what to do about it.
 *
 * @param version   the build's version, or NULL when it has none.
 * @param stations  stations the station table names; 0 when it is missing.
 * @param stops     stops the stop table names; 0 when it is missing.
 * @param operators names read from the user's operators file.
 */
void flipso_format_about(
    FuriString* out,
    const char* version,
    uint32_t stations,
    uint32_t stops,
    uint16_t operators);

/* ------------------------------------------------------------------ */
/* Pieces the scenes use directly                                      */
/* ------------------------------------------------------------------ */

/** Append a bold heading, with an icon in front where @p icon is not None. */
void flipso_cat_heading(FuriString* out, FlipsoIcon icon, const char* title);

/** Append "dd/mm/yyyy" for an ITSO DATE, in the user's date format. */
void flipso_cat_date(FuriString* out, uint16_t date);

/**
 * Append a DATE with a two-digit year, "31/03/27", in the user's date order:
 * short enough to sit at the end of a list row.
 */
void flipso_cat_short_date(FuriString* out, uint16_t date);

/** Append "dd/mm/yyyy hh:mm" for a Unix timestamp, in the user's formats. */
void flipso_cat_time(FuriString* out, uint32_t timestamp);

/** Human label for a product, e.g. "Pay as you go". */
const char* flipso_product_title(const ItsoProduct* product);

/** The icon a product's row carries, chosen from its IPE type. */
FlipsoIcon flipso_product_icon(const ItsoProduct* product);

/**
 * The tag at the end of a product's row: "Off card", "Blocked", "Expired" or
 * "Unused", or NULL when the product is none of those.
 */
const char* flipso_product_tag(const ItsoProduct* product, uint32_t now);

/** First product of the given IPE type still on the card, or NULL. */
const ItsoProduct* flipso_find_product(const ItsoCard* card, uint8_t typ);

/**
 * True when the product list has a row for @p product: everything but a purse,
 * ID or entitlement the card still holds, which the card menu reaches directly.
 */
bool flipso_product_listed(const ItsoProduct* product);

#ifdef __cplusplus
}
#endif
