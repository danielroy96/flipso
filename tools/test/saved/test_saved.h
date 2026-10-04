/**
 * @file test_saved.h
 * @brief What the saved-card test files share: the stubs' controls, the
 * helpers, and each file's tests for test_saved.c to run in order.
 */
#pragma once

#include "../test.h"
#include "cards/flipso_saved.h"
#include "../card_data.h"

#include <dialogs/dialogs.h>
#include <gui/canvas.h>
#include <toolbox/path.h>
#include <toolbox/stream/file_stream.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* --- test_saved_stubs.c --- */

/** Bytes a write may still put down before the "SD card" is full; negative for
 *  a card with room. How a test makes a save fail part way through. */
extern long stub_write_budget;
/** Set to make every rename fail, as an SD card error would. */
extern bool stub_rename_fails;
/** The header of the last message the dialogs service was asked to show. */
extern char last_alert[128];

/* --- test_saved_util.c --- */

void write_text(const char* path, const char* text);
/** Empty and remove the saved-cards folder, however the last run left it. */
void clean(void);
/** A capture holding one card's shell and directory, stamped with a time. */
FlipsoCapture* make(
    const uint8_t* shell,
    size_t shell_len,
    const uint8_t* dir,
    size_t dir_len,
    ItsoUnixTime when);
/** Files in the saved-cards folder. */
size_t count_files(void);
/** True when the saved-cards folder holds @p name. */
bool exists(const char* name);
/** The read time of the card saved as @p name, or 0 when it will not load. */
ItsoUnixTime read_time(const char* name);
/** Write a card as @p name with @p suffix after its extension, as a save would. */
void leave(const char* name, const char* suffix, ItsoUnixTime read_at);

/* --- The tests, in the order test_saved.c runs them --- */

void names(void); /* test_names.c */
void round_trip(void); /* test_saved_files.c */
void bad_files(void);
void finding(void); /* test_finding.c */
void renaming(void); /* test_renaming.c */
void power_cuts(void); /* test_power_cuts.c */
void demos(void); /* test_demos.c */
