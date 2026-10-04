/**
 * @file test_format.h
 * @brief What the screen-text test files share: reading a rendered screen, the
 * house style, the synthetic card, and each file's tests for test_format.c.
 */
#pragma once

#include "../test.h"
#include "format/flipso_format.h"
#include "views/flipso_text_view.h"
#include "itso/itso_operators.h"
#include "../card_data.h"
#include "itso_i.h"

#include <stdio.h>
#include <string.h>

/* A fixed "now", so what has expired does not depend on the day the test
 * runs: 2026-09-20. */
#define FLIPSO_TEST_NOW 1758326400u

/* 2060-01-01: past the expiry of every card and product the tests build. */
#define FLIPSO_TEST_LATER 2840140800u

/* --- test_format_fakes.c --- */

/** The synthetic card from build_card.py, as the blocks a read captures. */
FlipsoCapture* synthetic_capture(void);

/** Read a saved card file into @p capture, as the app does. */
bool load(FlipsoCapture* capture, const char* path);

/* --- test_format_pages.c: reading a rendered screen --- */

/** True when @p needle is anywhere in the screen. */
bool shows(const FuriString* text, const char* needle);
/** Where the page titled @p title starts, at its "\e#", or NULL. */
const char* find_page(const FuriString* text, const char* title);
/** The page titled @p title, title line and all, or NULL. */
const char* page_of(const FuriString* text, const char* title);
/** True when the screen's page titles are @p want, joined by '|'. */
bool titles_are(const FuriString* text, const char* want);
/** True when @p needle is on the page titled @p title. */
bool on_page(const FuriString* text, const char* title, const char* needle);
/** True when the page titled @p title opens with @p needle under its title. */
bool page_starts(const FuriString* text, const char* title, const char* needle);
/** How many pages carry @p icon in their title. */
int pages_with_icon(const FuriString* text, FlipsoIcon icon);
/** True when @p needle is on the screen before its Technical page. */
bool before_technical(const FuriString* text, const char* needle);
/** True when @p needle is on the screen's Technical page. */
bool technical(const FuriString* text, const char* needle);
/** How many times @p needle appears in the screen. */
int occurrences_of(const FuriString* text, const char* needle);

/* --- test_house_style.c --- */

/** Hold every line of a screen to the house style flipso_format.h sets out. */
void house_style(const char* where, const FuriString* text);
/** Every screen of @p card, each held to the house style and to its page rules. */
void every_screen(const char* name, const FlipsoFormat* f, const ItsoCard* card);

/* --- The screens, in the order test_format.c runs them --- */

void synthetic_screens(const FlipsoFormat* f, const ItsoCard* card, FuriString* text);
void spec_review(const FlipsoFormat* f, const ItsoCard* card); /* test_spec_screens.c */
/** Decode @p group into @p p - zeroed, or what an earlier call left, whose value
 *  history this releases - and render it as its product screen. */
void product_screen(
    FuriString* text,
    const FlipsoFormat* f,
    const ItsoCard* card,
    ItsoProduct* p,
    uint8_t typ,
    bool vgp,
    const uint8_t* group,
    size_t len);
void reservation_screen(const FlipsoFormat* f, const ItsoCard* card);
void paper_ticket_screens(const FlipsoFormat* f, FuriString* text);
void product_lines(const FlipsoFormat* f, const ItsoCard* card, FuriString* text);
void about_screens(FuriString* text); /* test_about_media.c */
void media_screens(FuriString* text);
void demo_cards(const char* directory, FlipsoFormat f); /* test_demo_cards.c */

/* --- test_demo_screens.c: what particular demo cards have to say --- */

void demo_one(const FlipsoFormat* f, const ItsoCard* card);
void demo_four(const FlipsoFormat* f, const ItsoCard* card);
void demo_seven(const FlipsoFormat* f, const ItsoCard* card);
void demo_fourteen(const FlipsoFormat* f, const ItsoCard* card);
void demo_type2_full(const FlipsoFormat* f, const ItsoCard* card, bool ntag);
