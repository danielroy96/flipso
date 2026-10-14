/**
 * @file itso_cmd11.h
 * @brief CMD11, ITSO's Programmable Intelligent Media (TS 1000-10 clause 12):
 * the parts of its SELECT answer and its read commands the decoder needs.
 *
 * A CMD11 card is an applet with a command set of its own rather than a file
 * system. SELECT answers with the shell, the directory and a list of 16-bit
 * OIDs in its FCI; ReadIPE, ReadVRG and ReadLog return the data groups whole,
 * before any authentication, with the InstanceIDs and seals blanked to 0xFF
 * (clauses 12.4.8-12.4.10). There are no sectors (B is zero, table 114), so a
 * product is saved as its IPE group with its value group straight after it,
 * which is where itso_value_records() looks when the sector size is zero.
 */
#pragma once

#include "../itso_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The IPE Data Group in a ReadIPE response.
 *
 * The card returns the IPE's five-byte directory entry, then the IPE and its
 * InstanceID and seal (clause 12.4.8). This is the part after the entry, cut to
 * the length the IPE's own length element gives it, plus the sixteen bytes of
 * InstanceID and seal.
 *
 * @param[out] group where the IPE group starts within @p response.
 * @return its length, or 0 when the response is too short to hold one.
 */
size_t itso_cmd11_ipe_group(const uint8_t* response, size_t len, const uint8_t** group);

/**
 * The Value Record Data Group in a ReadVRG response.
 *
 * Clause 12.4.9 says ReadVRG behaves as ReadIPE does, without saying whether
 * that includes the directory entry in front, and no CMD11 card has been seen
 * to settle it. So both are taken: the response is read as a bare group when
 * its own length element says it is exactly that long, and as an entry and a
 * group when the length element after five bytes does.
 *
 * @param[out] group where the value group starts within @p response.
 * @return its length, or 0 when the response is too short to hold one.
 */
size_t itso_cmd11_value_group(const uint8_t* response, size_t len, const uint8_t** group);

/**
 * Give each product the full OID the FCI's OID list (tag C3) has for it.
 *
 * The list holds one OID16 per directory entry, in entry order (table 113). A
 * directory entry holds thirteen bits and the extension flag, which is all an
 * IPE owner needs (TS 1000-2 table B2); the list is the one place a CMD11 card
 * says more, because the InstanceID that says it elsewhere is redacted. An OID
 * is taken only when it agrees with its entry - the same thirteen bits, and
 * the prefix table B4 allows for the entry's flag - so a list that does not
 * line up with the directory changes nothing.
 *
 * Call after itso_parse_directory().
 */
void itso_parse_oid_list(ItsoCard* card, const uint8_t* list, size_t len);

#ifdef __cplusplus
}
#endif
