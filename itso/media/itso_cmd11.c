/**
 * @file itso_cmd11.c
 * @brief CMD11's read commands and its OID list: see itso_cmd11.h.
 *
 * Offsets cite ITSO TS 1000 clause numbers so they can be checked against the
 * published specification. Every accessor is bounds checked: card data is
 * attacker-controlled as far as this app is concerned.
 */
#include "../itso_i.h"

/** InstanceID and seal, which follow every IPE and value group (TS 1000-2). */
#define ITSO_CMD11_TRAILER (ITSO_INSTANCE_ID_LEN + ITSO_SEAL_LEN)

/** The length of a data group whose first six bits count its four-byte blocks
 *  - IPELength or VGLength - with its InstanceID and seal; 0 if too short. */
static size_t itso_cmd11_group_len(const uint8_t* data, size_t len) {
    if(len < 2) return 0;
    size_t dataset_len = (size_t)itso_bits(data, 0, 6) * ITSO_IPE_BLOCK_LEN;
    return dataset_len < 4 ? 0 : dataset_len + ITSO_CMD11_TRAILER;
}

size_t itso_cmd11_ipe_group(const uint8_t* response, size_t len, const uint8_t** group) {
    if(len < ITSO_DIR_ENTRY_LEN + 2) return 0;
    const uint8_t* ipe = response + ITSO_DIR_ENTRY_LEN;
    size_t available = len - ITSO_DIR_ENTRY_LEN;

    /* Partial reads are not supported (clause 12.4.8), so the response should
     * be exactly the group. A shorter one is decoded for what it holds, as a
     * sector chain that ended early would be. */
    size_t whole = itso_cmd11_group_len(ipe, available);
    *group = ipe;
    return whole && whole < available ? whole : available;
}

size_t itso_cmd11_value_group(const uint8_t* response, size_t len, const uint8_t** group) {
    size_t bare = itso_cmd11_group_len(response, len);
    if(bare && bare == len) {
        *group = response;
        return len;
    }
    if(len > ITSO_DIR_ENTRY_LEN) {
        size_t after_entry =
            itso_cmd11_group_len(response + ITSO_DIR_ENTRY_LEN, len - ITSO_DIR_ENTRY_LEN);
        if(after_entry && after_entry == len - ITSO_DIR_ENTRY_LEN) {
            *group = response + ITSO_DIR_ENTRY_LEN;
            return after_entry;
        }
    }
    /* Neither fits exactly: read it as bare, which is what the figure for the
     * command shows ("the full contents of the VRG with its seal"). */
    if(len < 2) return 0;
    *group = response;
    return bare && bare < len ? bare : len;
}

void itso_parse_oid_list(ItsoCard* card, const uint8_t* list, size_t len) {
    for(uint8_t i = 0; i < card->product_count; i++) {
        ItsoProduct* product = &card->products[i];
        size_t at = (size_t)(product->dir_index - 1) * 2;
        if(product->dir_index == 0 || at + 2 > len) continue;

        uint16_t oid = (uint16_t)((list[at] << 8) | list[at + 1]);
        /* TS 1000-2 table B4: the bits above the thirteen are 000 without the
         * extension flag, and 001, 011 or 111 with it. */
        uint8_t prefix = (uint8_t)(oid >> 13);
        bool agrees = product->oid_extended ? (prefix == 1 || prefix == 3 || prefix == 7) :
                                              prefix == 0;
        if(agrees && (oid & 0x1FFF) == (product->oid & 0x1FFF)) product->oid = oid;
    }
}
