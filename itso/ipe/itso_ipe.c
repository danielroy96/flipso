/**
 * @file itso_ipe.c
 * @brief The IPE Data Group (TS 1000-2 clause 6): the elements every type shares, and which type decodes the rest.
 *
 * Offsets cite ITSO TS 1000 clause numbers so they can be checked against the
 * published specification. Every accessor is bounds checked: card data is
 * attacker-controlled as far as this app is concerned, and a malformed card
 * must produce an empty result rather than a crash.
 */
#include "itso_ipe_i.h"

#include <string.h>

/**
 * Elements every IPE dataset puts in the same place (TS 1000-5 clause 2).
 *
 * RemoveDate sits at byte 2 for every type. ProductRetailer follows it at byte
 * 3 for every type except the two identity ones, which put an accounting
 * reference there instead - so reading it unconditionally would report a
 * scheme's internal account code as the shop that sold the card.
 *
 * The optional IIN is the odd one out: TS 1000-2 clause 6.2.6 appends it after
 * the dataset padding rather than inline, so it is the last three bytes of the
 * dataset whenever IPEBitMap bit 0 is set.
 */
static void itso_parse_ipe_common(
    ItsoProduct* product,
    const uint8_t* data,
    size_t dataset_len,
    uint8_t bitmap) {
    if(dataset_len < 3) return;

    product->remove_date = data[2];
    product->has_remove_date = true;

    bool has_retailer = product->typ != ItsoTypId && product->typ != ItsoTypEntitlement;
    if(has_retailer && dataset_len >= 5) {
        product->retailer = (uint16_t)((data[3] << 8) | data[4]);
        /* Zero is "not recorded" rather than operator zero, which is RFU. */
        product->has_retailer = product->retailer != 0;
    }

    /* Six BCD digits (TS 1000-1 clause 4.2), like the shell's own. */
    if((bitmap & 0x01) && dataset_len >= 3) {
        product->iin = itso_bcd_number(data, (uint32_t)(dataset_len - 3) * 8, 6);
        product->has_iin = true;
    }
}

/**
 * The IPE InstanceID that follows the dataset (TS 1000-2 table 11).
 *
 * This is the only unique identity a product carries: the shell has an ISRN and
 * the directory entry has an owner and a type, but nothing names one particular
 * season ticket except the ISAM that created it and that ISAM's sequence
 * number.
 */
void itso_parse_instance_id(
    ItsoProduct* product,
    const uint8_t* group,
    size_t len,
    size_t dataset_len) {
    if(dataset_len + ITSO_INSTANCE_ID_LEN > len) return;

    const uint8_t* id = group + dataset_len;
    product->key_id = (id[0] >> 4) & 0x0F;
    product->iteration = id[0] & 0x0F;
    product->isam_id = itso_bits(id, 8, 32);
    product->isam_seq = itso_bits(id, 40, 24);
    product->instance_valid = true;
}

/**
 * RouteCode, which revisions 2 and 3 of TYP 22 and TYP 23 put in front of the
 * two locations under the same bitmap bit (tables 27a, 3.27, 31a and 31b).
 *
 * @return the offset after it, or 0 when the dataset ends first.
 */
size_t itso_parse_route(ItsoTicketTerms* t, const uint8_t* data, size_t len, size_t pos) {
    if(pos + sizeof(t->route_code) > len) return 0;
    memcpy(t->route_code, data + pos, sizeof(t->route_code));
    t->has_route_code = true;
    return pos + sizeof(t->route_code);
}

/**
 * A ticket's two LOC1 locations, into @c from and @c to.
 *
 * @return the offset after the second, or 0 when either does not fit - which
 *         leaves nothing after them that could be found.
 */
size_t itso_parse_journey_ends(ItsoProduct* product, const uint8_t* data, size_t len, size_t pos) {
    if(pos >= len) return 0;
    size_t used = itso_parse_location(data + pos, len - pos, ItsoLocStructLoc1, &product->from);
    if(!used) return 0;
    pos += used;
    if(pos >= len) return 0;
    used = itso_parse_location(data + pos, len - pos, ItsoLocStructLoc1, &product->to);
    return used ? pos + used : 0;
}

/*
 * The IPE types Flipso decodes beyond their directory entry, which part of
 * each - the dataset, the tail of the live value record, or both (TS 1000-5
 * clause 2) - and the family of ItsoTerms they fill. A type that is not here is
 * still listed, from its directory entry and its value record's common header.
 * Each type's decoders are in its own itso_ipe_*.c; the Space Saving types,
 * which only a CMD4 carries, are decoded by itso_parse_space_saving() and are
 * here for their family.
 */
static const ItsoIpeType itso_ipe_types[] = {
    {ItsoTypStoredTravelRights,
     ItsoFamilyPurse,
     itso_ipe_purse_dataset,
     itso_ipe_stored_travel_rights_value},
    {ItsoTypLoyalty1, ItsoFamilyOther, NULL, itso_ipe_loyalty_value},
    {ItsoTypChargeToAccount1,
     ItsoFamilyPurse,
     itso_ipe_purse_dataset,
     itso_ipe_charge_to_account1_value},
    {ItsoTypChargeToAccount2,
     ItsoFamilyPurse,
     itso_ipe_purse_dataset,
     itso_ipe_charge_to_account2_value},
    {ItsoTypEntitlement, ItsoFamilyId, itso_ipe_id_dataset, NULL},
    {ItsoTypId, ItsoFamilyId, itso_ipe_id_dataset, NULL},
    {ItsoTypPeriodTicket, ItsoFamilyTicket, itso_ipe_period_dataset, itso_ipe_period_value},
    {ItsoTypJourneyTicket, ItsoFamilyTicket, itso_ipe_journey_dataset, itso_ipe_journey_value},
    {ItsoTypReservationTicket,
     ItsoFamilyTicket,
     itso_ipe_reservation_dataset,
     itso_ipe_reservation_value},
    {ItsoTypVoucher, ItsoFamilyOther, NULL, itso_ipe_voucher_value},
    {ItsoTypTolling, ItsoFamilyOther, NULL, itso_ipe_voucher_value},
    {ItsoTypPeriodCompact, ItsoFamilyTicket, NULL, NULL},
    {ItsoTypCarnet, ItsoFamilyTicket, NULL, NULL},
    {ItsoTypMultiUse, ItsoFamilyTicket, NULL, NULL},
};

const ItsoIpeType* itso_ipe_type(uint8_t typ) {
    for(size_t i = 0; i < sizeof(itso_ipe_types) / sizeof(itso_ipe_types[0]); i++) {
        if(itso_ipe_types[i].typ == typ) return &itso_ipe_types[i];
    }
    return NULL;
}

ItsoFamily itso_product_family(uint8_t typ) {
    const ItsoIpeType* type = itso_ipe_type(typ);
    return type ? type->family : ItsoFamilyOther;
}

/* What a product of another family reads as its terms: nothing set. */
static const ItsoTerms itso_no_terms;

const ItsoPurseTerms* itso_product_purse(const ItsoProduct* product) {
    return itso_product_family(product->typ) == ItsoFamilyPurse ? &product->terms.purse :
                                                                  &itso_no_terms.purse;
}

const ItsoIdTerms* itso_product_id(const ItsoProduct* product) {
    return itso_product_family(product->typ) == ItsoFamilyId ? &product->terms.id :
                                                               &itso_no_terms.id;
}

const ItsoTicketTerms* itso_product_ticket(const ItsoProduct* product) {
    return itso_product_family(product->typ) == ItsoFamilyTicket ? &product->terms.ticket :
                                                                   &itso_no_terms.ticket;
}

void itso_parse_ipe(ItsoProduct* product, const uint8_t* group, size_t len, uint8_t sector_size) {
    if(len < 2) return;

    uint8_t ipe_length = itso_bits(group, 0, 6);
    uint8_t bitmap = itso_bits(group, 6, 6);
    product->bitmap = bitmap;
    product->format_rev = itso_bits(group, 12, 4);

    size_t dataset_len = (size_t)ipe_length * ITSO_IPE_BLOCK_LEN;
    if(dataset_len < 4 || dataset_len > len) return;

    product->body_parsed = true;

    itso_parse_ipe_common(product, group, dataset_len, bitmap);
    itso_parse_instance_id(product, group, len, dataset_len);

    /* The Value Record Data Group belongs to the IPE rather than to its type, so
     * decode it wherever VGP says one is there. It is decoded before the dataset
     * because the dataset's amounts take their currency from the value record. */
    if(product->value_group) {
        itso_parse_value_records(product, group, len, sector_size);
    }

    /* Other product types are reported from their directory entry and, where
     * they carry one, their value record. */
    const ItsoIpeType* type = itso_ipe_type(product->typ);
    if(type && type->dataset) type->dataset(product, group, dataset_len);
}

bool itso_product_sold_at(const ItsoProduct* product, ItsoLocation* out) {
    memset(out, 0, sizeof(*out));
    if(!product->has_retailer) return false;
    if(product->typ != ItsoTypPeriodTicket && product->typ != ItsoTypJourneyTicket &&
       product->typ != ItsoTypReservationTicket) {
        return false;
    }
    /* TS 1000-2 table B2: the gap between 32767 and 57344 "shall not be used". */
    if(product->retailer < 32768 || product->retailer >= 57344) return false;
    return itso_retailer_location(product->retailer, out);
}

void itso_product_off_card(ItsoProduct* product, uint32_t last_seen) {
    product->on_card = false;
    product->last_seen = last_seen;
    for(uint8_t i = 0; i < product->value_history_count; i++) {
        product->value_history[i].on_card = false;
    }
}
