/**
 * @file itso_fci.c
 * @brief The FCI an ITSO application's SELECT returns: see itso_fci.h.
 *
 * Card data is attacker-controlled as far as this app is concerned: every
 * length is checked against what is left before it is believed.
 */
#include "../itso_i.h"

/** The two templates the objects ITSO defines sit in (TS 1000-10 figure 9). */
#define ITSO_FCI_TEMPLATE    0x6F
#define ITSO_FCI_PROPRIETARY 0xA5

/**
 * Read one object's header at @p offset: its tag, if a single byte, and the
 * extent of its value.
 *
 * @return false when the header or the value runs past @p len.
 */
static bool itso_fci_header(
    const uint8_t* data,
    size_t len,
    size_t offset,
    int* tag,
    size_t* value,
    size_t* value_len) {
    if(offset >= len) return false;

    /* A tag whose low five bits are all set continues in the bytes after it,
     * each with bit 8 set but the last. ITSO uses none, but one skipped
     * properly is one that does not end the search. */
    size_t pos = offset;
    *tag = data[pos++];
    if((*tag & 0x1F) == 0x1F) {
        *tag = -1;
        while(pos < len && (data[pos] & 0x80))
            pos++;
        pos++;
    }
    if(pos >= len) return false;

    size_t length = data[pos++];
    if(length & 0x80) {
        size_t bytes = length & 0x7F;
        if(bytes == 0 || bytes > 2 || pos + bytes > len) return false;
        length = 0;
        for(size_t i = 0; i < bytes; i++) {
            length = (length << 8) | data[pos++];
        }
    }
    if(length > len - pos) return false;

    *value = pos;
    *value_len = length;
    return true;
}

const uint8_t* itso_fci_find(const uint8_t* data, size_t len, uint8_t tag, size_t* out_len) {
    size_t offset = 0;
    int current;
    size_t value, value_len;
    while(itso_fci_header(data, len, offset, &current, &value, &value_len)) {
        if(current == tag) {
            *out_len = value_len;
            return data + value;
        }
        if(current == ITSO_FCI_TEMPLATE || current == ITSO_FCI_PROPRIETARY) {
            const uint8_t* found = itso_fci_find(data + value, value_len, tag, out_len);
            if(found) return found;
        }
        offset = value + value_len;
    }
    return NULL;
}
