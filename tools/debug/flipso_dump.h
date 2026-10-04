/**
 * @file flipso_dump.h
 * @brief Opt-in diagnostic: append the raw bytes read off a card to the SD card.
 *
 * Not built by default. To turn it on for a debugging session:
 *
 *   1. add "tools/debug/flipso_dump.c" to `sources` in application.fam
 *   2. `#include "tools/debug/flipso_dump.h"` in flipso_desfire.c
 *   3. call flipso_dump_begin() at the top of flipso_desfire_read(), and
 *      flipso_dump_block("SHELL"/"DIR"/"GROUP", buf, len) next to each read
 *   4. deploy, scan the card, then:
 *        tools/flipper/flipctl pull /ext/apps_data/flipso/dump.txt dump.txt
 *        tools/test/replay.py dump.txt
 *
 * Take the wiring back out afterwards, and delete dump.txt from the SD card:
 * it contains the card number and the holder's name.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/** Truncate the dump file and start a new record. */
void flipso_dump_begin(void);

/** Append one labelled block of bytes as "LABEL <len>" then one hex line. */
void flipso_dump_block(const char* label, const uint8_t* data, size_t len);
