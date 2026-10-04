/**
 * @file itso_log.h
 * @brief The cyclic log: one Transient Ticket Record per tap (TS 1000-5 clause 3).
 */
#pragma once

#include "itso_location.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One Transient Ticket Record from the cyclic log: a single tap. */
typedef struct {
    uint8_t format_rev;
    uint8_t transaction_type; /**< EN1545 EventTypeCode. */
    ItsoDts dts; /**< When the tap was. */

    /* AMT group (TS 1000-5 table 59). */
    ItsoMoney amount;
    bool has_mop;
    uint8_t mop; /**< EN1545 PaymentMeansCode: how the fare was paid. */
    bool no_fare_charged; /**< Operator let the holder travel without taking the fare. */
    bool has_vat;
    uint16_t vat; /**< VATSalesTax in 0.01% steps. */

    bool has_ipe_pointer;
    uint8_t ipe_pointer; /**< Directory entry of the product used. */
    ItsoLocation origin;
    ItsoLocation destination;
    ItsoLocation route; /**< RC group: the "via" point that determines the fare. */

    /* IIN group: the network the POST that wrote the record is registered with. */
    bool has_iin;
    uint32_t iin;

    /* CIPE group (format revisions 3 and 4): which products the POST considered
     * for this journey, and whether anything was flagged against the holder. */
    bool has_cipe;
    uint8_t cipe[4]; /**< Candidate directory entries, zero where unused. */
    bool invalid_travel; /**< CIPEFlags bit 0. */
    bool inspected; /**< CIPEFlags bit 1: checked by an inspector this journey. */

    /* ENTRY group (format revision 4): where and when this journey checked in,
     * copied from the tap-in record so a tap-out record is self-contained. */
    bool has_entry;
    ItsoDts entry_dts;
    bool has_entry_oid;
    uint16_t entry_oid; /**< Operator whose gate the holder entered through. */
    uint8_t entry_iin_index; /**< ENTRY_IIN_Index: that operator's network. */
    uint32_t entry_isam; /**< ENTRY group: ISAM of the check-in record. */
    uint32_t entry_isam_seq; /**< ...and its sequence number. */

    /* AMT group flags, format revision 2 on (TS 1000-5 table 60). */
    bool companion; /**< CompanionTravelled. */
    bool return_ticket; /**< ReturnTicket: the fare was for a return. */

    /* The record's own InstanceID, after its dataset: the ISAM of the POST
     * that wrote it, which names the operator whose reader took the tap. */
    bool has_writer;
    uint32_t writer_isam;

    /* False for a record that came out of a saved file rather than out of the
     * log the card just offered, as ItsoValueRecord::on_card is. The log keeps
     * four; anything older survives only because a file remembered it. */
    bool on_card;
} ItsoTap;

/** Decode the cyclic log into card->taps. */
void itso_parse_log(ItsoCard* card, const uint8_t* data, size_t len);

/**
 * Decode Transient Ticket Records an earlier read of this card saw.
 *
 * @param data ITSO_TAP_RECORD_LEN records back to back, in any order.
 *
 * Call it after itso_parse_log(), so that the live log fills the array first and
 * a record it holds is the one kept, marked as still on the card.
 */
void itso_parse_log_history(ItsoCard* card, const uint8_t* data, size_t len);

/** True when a log slot holds a Transient Ticket Record at all. */
bool itso_tap_record_present(const uint8_t* record, size_t len);

/** True when tap record @p a was written later than @p b. */
bool itso_tap_record_newer(const uint8_t* a, const uint8_t* b);

#ifdef __cplusplus
}
#endif
