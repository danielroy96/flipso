/**
 * @file itso_type2.h
 * @brief Type 2 tag page media: CMD4 paper tickets, and the full-shell CMD9 and CMD10
 * cards (TS 1000-10 sections 5, 10 and 11).
 */
#pragma once

#include "../itso_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Decode a Compact-Shell Type 2 tag from its raw page memory (TS 1000-10 section
 * 5, CMD4): the page-based media that SPT's Glasgow Subway paper tickets use.
 *
 * Unlike the other media there is no separate shell/directory/product read - the
 * whole tag is one flat block, its data groups at fixed page offsets. This finds
 * the Compact Shell, the single IPE Directory Entry, and the Space Saving IPE
 * (TYP 27, 28 or 29) spread across the static, dynamic and OTP pages, with its
 * InstanceID and - through the Seal - whether it has been blocked.
 *
 * @param pages the tag's page memory, page 0 first.
 * @return false unless itso_type2_kind() calls @p pages ItsoType2Compact.
 */
bool itso_parse_type2(ItsoCard* card, const uint8_t* pages, size_t len);

/** A CMD4 tag's whole page memory: sixteen 4-byte pages (TS 1000-10 clause 5.2.2). */
#define ITSO_CMD4_MEMORY_LEN 64

/** Where a Type 2 tag's shell starts, compact or full: page 6 (TS 1000-10
 *  clauses 5.13, 10.20 and 11.22). */
#define ITSO_TYPE2_SHELL_OFFSET 24

/** The pages TS 1000-10 clause 5.10.2 requires a CMD4 to make read-only once
 *  issued - the shell and directory entry, InstanceID and IPE static data,
 *  pages 6 to 13 - as a mask of bit n for page n. */
#define ITSO_CMD4_LOCKED_PAGES 0x3FC0

/**
 * The pages a Type 2 tag's static lock bits have made read-only, bit n for page
 * n. The layout is the MIFARE Ultralight one that CMD4 is built on: lock byte 0
 * bits 3-7 lock pages 3-7, and lock byte 1 bits 0-7 lock pages 8-15. Locking is
 * one-way (TS 1000-10 clause 5.10). Pages 0 and 1, and the first half of page 2,
 * are read-only from the factory and have no lock bit.
 */
uint16_t itso_type2_locked_pages(const uint8_t lock[2]);

/**
 * The pages whose lock bits are themselves frozen, bit n for page n: lock byte 0
 * bits 0-2 are the block-lock bits for page 3, pages 4-9 and pages 10-15, and
 * once one is set the lock bits it covers can no longer be changed.
 */
uint16_t itso_type2_frozen_pages(const uint8_t lock[2]);

/** What a Type 2 tag's page memory is, as far as ITSO goes. */
typedef enum {
    /** Shorter than any Type 2 tag - the smallest, an Ultralight, has 64 bytes -
     *  so the read stopped early and says nothing about the card. */
    ItsoType2Incomplete,
    ItsoType2NotItso, /**< No ITSO shell at page 6. */
    /** A full shell, stored rotated from page 4 so that its FVC lands on page 6
     *  byte 2 where a compact shell's does: a CMD9 (NTAG215/216) or CMD10
     *  (Ultralight EV1) card. Read with itso_type2_full_shell() and the sector
     *  map below; itso_parse_type2() does not take it. */
    ItsoType2FullShell,
    /** A full shell laid out the same way whose FVC is not 9 or 10: an ITSO card
     *  on some Type 2 media definition this build does not know. */
    ItsoType2OtherShell,
    ItsoType2Compact, /**< A CMD4 compact shell, which itso_parse_type2() decodes. */
} ItsoType2Kind;

/**
 * Classify a Type 2 tag's page memory by what sits at ITSO_TYPE2_SHELL_OFFSET.
 */
ItsoType2Kind itso_type2_kind(const uint8_t* pages, size_t len);

/* ------------------------------------------------------------------ */
/* Full-shell Type 2 media: CMD9 (NTAG215/216), CMD10 (Ultralight EV1) */
/* ------------------------------------------------------------------ */

/*
 * TS 1000-10 clauses 10.10-10.11 (CMD9) and 11.13-11.14 (CMD10) share one
 * layout, figures 4.1, 4.2 and 7 in page numbers:
 *
 *   0x00-0x03  chip serial, lock bytes, OTP (the CMD9 Abacus lives in 0x03)
 *   0x04-0x0B  the Shell Environment, rotated by one byte (below)
 *   0x0C-0x15  Directory copy A, logical sector S-2
 *   0x16-0x1F  Directory copy B, logical sector S-1
 *   0x20-      logical sectors 1 to 6, B bytes each, in order
 *
 * The geometry is fixed - no override is allowed (clauses 10.11.5, 11.14.5):
 * S = 9, E# = 2, SCTL = 3, and B = 64 on an NTAG215, 128 on an NTAG216 or an
 * Ultralight EV1. So there is one IPE (E1), the Log Directory Entry (E2), and
 * the software anti-tear of TS 1000-10 annex A: two directory copies, two
 * copies of every value record group, and a two-record log.
 */
#define ITSO_FVC_NTAG           9
#define ITSO_FVC_ULTRALIGHT_EV1 10

/** Pages 4-11: where the rotated shell is stored. */
#define ITSO_TYPE2_FULL_SHELL_OFFSET 16
#define ITSO_TYPE2_FULL_SHELL_LEN    32
/** A directory copy: pages 0x0C-0x15 or 0x16-0x1F. */
#define ITSO_TYPE2_DIR_A_OFFSET      48
#define ITSO_TYPE2_DIR_B_OFFSET      88
#define ITSO_TYPE2_DIR_LEN           40
/** Page 0x20, where logical sector 1 starts. Everything before it - the chip
 *  pages, the shell and both directories - is what clause 10.24.2's single
 *  FAST_READ of pages 4 to 0x1F brings back. */
#define ITSO_TYPE2_SECTORS_OFFSET    128
/** The data sectors, 1 to S-3. */
#define ITSO_TYPE2_DATA_SECTORS      6
/** The most page memory a full-shell card's ITSO data reaches: the end of
 *  sector 6 at B = 128, page 0xE0. */
#define ITSO_TYPE2_FULL_MAX_LEN      (ITSO_TYPE2_SECTORS_OFFSET + ITSO_TYPE2_DATA_SECTORS * 128)
/** Pages 0-3, which the saved card keeps as its own block: the chip serial,
 *  the lock bytes and the one-time-programmable page. */
#define ITSO_TYPE2_TAG_LEN           16
/** Pages 4-11, the shell, which TS 1000-10 clause 10.23.1 recommends a CMD9 or
 *  CMD10 locks once issued, as a mask of bit n for page n. */
#define ITSO_TYPE2_FULL_LOCKED_PAGES 0x0FF0

/**
 * The Shell Environment Data Group of a CMD9 or CMD10 card, in TS 1000-2 order.
 *
 * The shell is stored rotated (clauses 10.11.3, 11.14.3): its first byte -
 * ShellLength and the two top bits of the bitmap - is moved from the front to
 * the last byte of the 32-byte block, so that every other element moves one
 * byte earlier and the FVC falls on page 6 byte 2, where CMD4's does. This puts
 * that byte back.
 *
 * @param out ITSO_TYPE2_FULL_SHELL_LEN bytes.
 * @return false when @p len does not reach the end of the shell.
 */
bool itso_type2_full_shell(const uint8_t* pages, size_t len, uint8_t* out);

/**
 * Where logical sector @p sector of a CMD9 or CMD10 card lies in its page
 * memory. Sectors 1 to 6 are B bytes each; S-2 and S-1 are the two directory
 * copies, which are ITSO_TYPE2_DIR_LEN bytes whatever B is.
 *
 * @param card with the shell decoded into it.
 * @return false for a sector the media has no room for, or for a shell whose
 *         geometry is not the one the CMD fixes.
 */
bool itso_type2_sector(const ItsoCard* card, uint8_t sector, size_t* offset, size_t* len);

/** A CMD9 or CMD10 card's page memory, as the context of itso_type2_read_sector(). */
typedef struct {
    const ItsoCard* card; /**< With the shell decoded into it, for the sector map. */
    const uint8_t* pages;
    size_t len; /**< Bytes of @c pages actually read. */
} ItsoType2Pages;

/**
 * An ItsoSectorRead over a CMD9 or CMD10 card's page memory, for
 * itso_read_chain() and itso_read_log_sectors(): the sector the map puts there,
 * or 0 when it is not in the bytes read or does not fit.
 *
 * @param context an ItsoType2Pages.
 */
size_t itso_type2_read_sector(void* context, uint8_t sector, uint8_t* out, size_t capacity);

/**
 * How many bytes of page memory a CMD9 or CMD10 card's ITSO data occupies, from
 * page 0 to the end of sector 6: 512 at B = 64, 896 at B = 128.
 *
 * @return 0 when the shell's geometry is not one of those CMDs'.
 */
size_t itso_type2_full_len(const ItsoCard* card);

/**
 * The live one of a CMD9 or CMD10 card's two Directory copies (TS 1000-10 annex
 * A.3.1.3): the one with the newer DIRS#, counting FF to 00 as a step forward
 * (TS 1000-2 clause 5.1.6). A copy that is all zeros - never written, or torn
 * - loses to one that is not. The Seals that would settle a tie are not
 * checkable without keys.
 *
 * @param card with the shell decoded into it.
 * @return the chosen copy's ITSO_TYPE2_DIR_LEN bytes within @p pages, or NULL
 *         when @p len does not hold both.
 */
const uint8_t* itso_type2_directory(const ItsoCard* card, const uint8_t* pages, size_t len);

/**
 * Take what a CMD9 or CMD10 card holds outside its ITSO sectors from pages 0-3:
 * the chip serial, the static lock bytes, the memory the chip has, and for
 * CMD9 the Abacus.
 *
 * Call after itso_parse_shell(): which chip this is, and so how much memory it
 * has, is known from the FVC and B the shell states (TS 1000-10 table 104).
 *
 * @param pages at least ITSO_TYPE2_TAG_LEN bytes from page 0.
 */
void itso_parse_type2_tag(ItsoCard* card, const uint8_t* pages, size_t len);

/**
 * The chip a CMD9 or CMD10 card is on, from its FVC and B (TS 1000-10 tables
 * 104 and 109): "NTAG215", "NTAG216" or "Ultralight EV1". NULL for any other
 * card.
 */
const char* itso_type2_chip_name(const ItsoCard* card);

#ifdef __cplusplus
}
#endif
