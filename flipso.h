/**
 * @file flipso.h
 * @brief Application state shared by the Flipso scenes.
 */
#pragma once

#include "flipso_reader.h"
#include "flipso_operators.h"
#include "flipso_stations.h"
#include "itso/itso.h"
#include "views/flipso_menu_view.h"
#include "views/flipso_scan_view.h"
#include "views/flipso_text_view.h"
#include "scenes/flipso_scene.h"

#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/widget.h>
#include <gui/icon.h>
#include <notification/notification_messages.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FlipsoViewScan,
    FlipsoViewMenu,
    FlipsoViewText,
    FlipsoViewWidget,
} FlipsoView;

typedef enum {
    /* Posted from the scan view when the user presses OK on the idle prompt. */
    FlipsoCustomEventStartScan = 100,
    /* Posted from the NFC worker thread when a card has been processed. */
    FlipsoCustomEventReaderDone,
} FlipsoCustomEvent;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    FlipsoMenuView* menu_view;
    Widget* widget;
    FlipsoTextView* text_view;
    FlipsoScanView* scan_view;

    FlipsoReader* reader;
    FlipsoReaderStatus status;

    /** Operator names: the user's file layered over the built-in table. */
    FlipsoOperators* operators;

    /** Rail station names, read on demand from the SD card. */
    FlipsoStations* stations;

    ItsoCard card;

    /** What a card that is not an ITSO one said about itself. */
    FlipsoMedia media;

    /** Index into card.products chosen on the product list scene. */
    uint8_t selected_product;

    /** Reads lost to the card leaving the field during the current scan. */
    uint8_t card_error_retries;
} Flipso;

/* ------------------------------------------------------------------ */
/* Formatting helpers shared by the detail scenes                      */
/* ------------------------------------------------------------------ */

/** Append "dd/mm/yyyy" for an ITSO DATE. */
void flipso_cat_date(FuriString* out, uint16_t date);

/**
 * Append a complete expiry line, including the trailing newline.
 *
 * The label switches to @p past_label once the date has gone by. Appending a
 * marker instead would overflow the 128px screen and wrap mid-word.
 */
void flipso_cat_expiry(
    FuriString* out,
    const char* label,
    const char* past_label,
    uint16_t date,
    uint32_t now);

/** Append "dd/mm/yyyy hh:mm" for an ITSO DTS. */
void flipso_cat_datetime(FuriString* out, uint32_t dts);

/** Current time as a Unix timestamp, from the Flipper's RTC. */
uint32_t flipso_now(void);

/**
 * Append "<label>: <operator>", naming the operator where we can and falling
 * back to its ITSO operator number where we cannot.
 */
void flipso_cat_operator(FuriString* out, const Flipso* app, const char* label, uint16_t oid);

/**
 * Append a location line "Label: place", or nothing when the location is absent.
 * Rail location codes are resolved to station names where the station table has
 * them; anything else falls back to the code the card carries.
 */
void flipso_cat_location(
    FuriString* out,
    Flipso* app,
    const char* label,
    const ItsoLocation* location);

/** First product of the given IPE type, or NULL when the card carries none. */
const ItsoProduct* flipso_find_product(const Flipso* app, uint8_t typ);

/** Human label for a product, e.g. "Pay as you go". */
void flipso_product_title(const ItsoProduct* product, char* out, size_t len);

/**
 * Row icon for a product, chosen from its IPE type so that a list of products
 * can be read at a glance. Never NULL: unrecognised types get a generic tag.
 */
const Icon* flipso_product_icon(const ItsoProduct* product);

/** Append the 18-digit card number grouped as 6-4-4-4. */
void flipso_cat_card_number(FuriString* out, const char* isrn);

/** Append "<label>: GBP 1.23", or nothing when the amount was not decoded. */
void flipso_cat_money(FuriString* out, const char* label, const ItsoMoney* money);

/** Append the shared detail block for one product (operator, dates, locations). */
void flipso_cat_product(FuriString* out, Flipso* app, const ItsoProduct* product, uint32_t now);

/**
 * Append the commercial terms of a purse or charge-to-account product: its
 * ceiling, any overdraft, the auto-top-up rule and the deposit paid for it.
 */
void flipso_cat_purse_terms(FuriString* out, const ItsoProduct* product);

/** Append what the product's newest value record says the last transaction was. */
void flipso_cat_last_transaction(FuriString* out, const ItsoProduct* product);

#ifdef __cplusplus
}
#endif
