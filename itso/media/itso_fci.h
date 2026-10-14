/**
 * @file itso_fci.h
 * @brief The File Control Information the ITSO application answers SELECT with.
 */
#pragma once

#include "../itso_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Find one data object in a SELECT response's FCI.
 *
 * The FCI is BER-TLV (ISO/IEC 7816-4): template 6F wraps the DF name (84) and
 * the proprietary template A5, which holds ITSO's own objects - the shell (C0)
 * on every ITSO microprocessor card, the Parameter EF (E0) on CMD2, and the
 * directory, OID list and the rest on CMD11 (TS 1000-10 figures 9-11). Only
 * those two templates are descended into: a CMD2 card's E0 holds tags of its own
 * (C3 is a short file id there and the OID list on CMD11), and finding one of
 * them in place of the other would read one as the other.
 *
 * Lengths may take the long form (81 xx, 82 xx xx): a CMD11 FCI passes 127
 * bytes from its Normal geometry up (table 114). Anything malformed ends the
 * search rather than reading past the end.
 *
 * @return the object's value, or NULL; @p out_len receives its length.
 */
const uint8_t* itso_fci_find(const uint8_t* data, size_t len, uint8_t tag, size_t* out_len);

#ifdef __cplusplus
}
#endif
