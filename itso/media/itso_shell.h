/**
 * @file itso_shell.h
 * @brief The ITSO Shell Environment Data Group (TS 1000-2 clause 4).
 */
#pragma once

#include "../itso_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Parse the 24/32-byte ITSO Shell Environment Data Group. */
bool itso_parse_shell(ItsoCard* card, const uint8_t* data, size_t len);

/**
 * Read just the card number out of a Shell Environment Data Group.
 *
 * The card number is the only unique identity a card has, so this is how one
 * card is told from another without decoding either: a saved card is matched
 * to the card in the reader by comparing these, and decoding a whole card
 * allocates every product and journey for the sake of eighteen digits.
 *
 * @param out at least ITSO_ISRN_DIGITS + 1 bytes.
 * @return false for bytes this decoder would not accept as a shell, in which
 *         case @p out is untouched.
 */
bool itso_shell_card_number(const uint8_t* data, size_t len, char* out);

/** True if @p data looks like an ITSO Shell Environment (IIN 6335 97). */
bool itso_looks_like_shell(const uint8_t* data, size_t len);

/**
 * B, the size of a memory sector, from a Shell Environment Data Group.
 *
 * Enough of the shell to find the value records in a product group, without
 * decoding a whole card to get at one byte. Zero when @p data is not a shell.
 */
uint8_t itso_shell_sector_size(const uint8_t* data, size_t len);

#ifdef __cplusplus
}
#endif
