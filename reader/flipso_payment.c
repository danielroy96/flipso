/**
 * @file flipso_payment.c
 * @brief The PPSE answer, read for its scheme and nothing else: see flipso_payment.h.
 *
 * Card data is attacker-controlled as far as this app is concerned - a phone
 * can answer anything - so every length is checked against what is left
 * before it is believed, and templates nest only as deep as the PPSE's do.
 */
#include "flipso_payment.h"

#include <stdbool.h>

const uint8_t flipso_payment_ppse_select[FLIPSO_PAYMENT_PPSE_SELECT_LEN] = {
    0x00, 0xA4, 0x04, 0x00, 0x0E, '2', 'P', 'A', 'Y', '.',
    'S',  'Y',  'S',  '.',  'D',  'D', 'F', '0', '1', 0x00};

/* The objects of the PPSE's FCI (EMV Contactless Book B, Entry Point): the
 * template, its proprietary template and issuer discretionary data, one
 * directory entry per application, and in each entry the two objects read. */
#define FLIPSO_TAG_FCI   0x6Fu
#define FLIPSO_TAG_AID   0x4Fu
#define FLIPSO_TAG_LABEL 0x50u

/* 6F > A5 > BF0C > 61 > 4F is four templates deep. */
#define FLIPSO_PAYMENT_MAX_DEPTH 4

/* Registered application provider identifiers (ISO 7816-5), the first five
 * bytes of every AID the scheme issues. */
typedef struct {
    uint8_t rid[5];
    uint8_t scheme; /**< FlipsoPaymentScheme, as a byte: these tables reach RAM. */
} FlipsoPaymentRid;

static const FlipsoPaymentRid flipso_payment_rids[] = {
    {{0xA0, 0x00, 0x00, 0x00, 0x03}, FlipsoPaymentVisa},
    {{0xA0, 0x00, 0x00, 0x00, 0x04}, FlipsoPaymentMastercard},
    {{0xA0, 0x00, 0x00, 0x00, 0x25}, FlipsoPaymentAmex},
    {{0xA0, 0x00, 0x00, 0x00, 0x65}, FlipsoPaymentJcb},
    {{0xA0, 0x00, 0x00, 0x01, 0x52}, FlipsoPaymentDiscover},
    {{0xA0, 0x00, 0x00, 0x03, 0x33}, FlipsoPaymentUnionPay},
};

/* What an application label says when the AID is one this table lacks, as a
 * scheme's own variants are ("VISA DEBIT", "Debit Mastercard"). Matched
 * anywhere in the label, ignoring case. */
typedef struct {
    const char* word;
    uint8_t scheme;
} FlipsoPaymentWord;

static const FlipsoPaymentWord flipso_payment_words[] = {
    {"visa", FlipsoPaymentVisa},
    {"mastercard", FlipsoPaymentMastercard},
    {"maestro", FlipsoPaymentMastercard},
    {"american express", FlipsoPaymentAmex},
    {"amex", FlipsoPaymentAmex},
    {"jcb", FlipsoPaymentJcb},
    {"discover", FlipsoPaymentDiscover},
    {"diners", FlipsoPaymentDiscover},
    {"unionpay", FlipsoPaymentUnionPay},
    {"union pay", FlipsoPaymentUnionPay},
};

#define FLIPSO_COUNT(a) (sizeof(a) / sizeof((a)[0]))

/** One BER-TLV object's header (ISO 7816-4 clause 5.2.2). */
typedef struct {
    uint32_t tag; /**< All of its bytes, first byte highest. */
    bool constructed;
    size_t value; /**< Offset of the value. */
    size_t len;
} FlipsoTlv;

/** @return false when the header, or the value it claims, runs past @p len. */
static bool flipso_payment_tlv(const uint8_t* data, size_t len, size_t offset, FlipsoTlv* tlv) {
    if(offset >= len) return false;
    size_t pos = offset;
    const uint8_t first = data[pos++];
    tlv->tag = first;
    tlv->constructed = (first & 0x20) != 0;
    if((first & 0x1F) == 0x1F) {
        /* Subsequent bytes carry bit 8 while more follow. Three bytes is as
         * long as a tag EMV uses gets. */
        uint8_t byte;
        do {
            if(pos >= len || pos - offset >= 3) return false;
            byte = data[pos++];
            tlv->tag = (tlv->tag << 8) | byte;
        } while(byte & 0x80);
    }
    if(pos >= len) return false;

    size_t length = data[pos++];
    if(length & 0x80) {
        const size_t bytes = length & 0x7F;
        if(bytes == 0 || bytes > 2 || bytes > len - pos) return false;
        length = 0;
        for(size_t i = 0; i < bytes; i++)
            length = (length << 8) | data[pos++];
    }
    if(length > len - pos) return false;

    tlv->value = pos;
    tlv->len = length;
    return true;
}

static FlipsoPaymentScheme flipso_payment_from_aid(const uint8_t* aid, size_t len) {
    if(len < 5) return FlipsoPaymentNone;
    for(size_t i = 0; i < FLIPSO_COUNT(flipso_payment_rids); i++) {
        const uint8_t* rid = flipso_payment_rids[i].rid;
        size_t j = 0;
        while(j < 5 && aid[j] == rid[j])
            j++;
        if(j == 5) return (FlipsoPaymentScheme)flipso_payment_rids[i].scheme;
    }
    return FlipsoPaymentNone;
}

static char flipso_payment_lower(uint8_t c) {
    return (char)((c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c);
}

/** True when @p word appears in @p label, ignoring case. */
static bool flipso_payment_label_has(const uint8_t* label, size_t len, const char* word) {
    size_t word_len = 0;
    while(word[word_len])
        word_len++;
    for(size_t start = 0; start + word_len <= len; start++) {
        size_t i = 0;
        while(i < word_len && flipso_payment_lower(label[start + i]) == word[i])
            i++;
        if(i == word_len) return true;
    }
    return false;
}

static FlipsoPaymentScheme flipso_payment_from_label(const uint8_t* label, size_t len) {
    for(size_t i = 0; i < FLIPSO_COUNT(flipso_payment_words); i++) {
        if(flipso_payment_label_has(label, len, flipso_payment_words[i].word)) {
            return (FlipsoPaymentScheme)flipso_payment_words[i].scheme;
        }
    }
    return FlipsoPaymentNone;
}

/**
 * Walk one template's objects, and the templates inside it, for the first AID
 * whose RID is known; the first label naming a scheme goes to @p by_label, as
 * the fallback.
 */
static FlipsoPaymentScheme flipso_payment_walk(
    const uint8_t* data,
    size_t len,
    unsigned depth,
    FlipsoPaymentScheme* by_label) {
    size_t offset = 0;
    FlipsoTlv tlv;
    while(flipso_payment_tlv(data, len, offset, &tlv)) {
        const uint8_t* value = data + tlv.value;
        if(tlv.tag == FLIPSO_TAG_AID) {
            FlipsoPaymentScheme scheme = flipso_payment_from_aid(value, tlv.len);
            if(scheme != FlipsoPaymentNone) return scheme;
        } else if(tlv.tag == FLIPSO_TAG_LABEL) {
            if(*by_label == FlipsoPaymentNone) {
                *by_label = flipso_payment_from_label(value, tlv.len);
            }
        } else if(tlv.constructed && depth < FLIPSO_PAYMENT_MAX_DEPTH) {
            FlipsoPaymentScheme scheme = flipso_payment_walk(value, tlv.len, depth + 1, by_label);
            if(scheme != FlipsoPaymentNone) return scheme;
        }
        offset = tlv.value + tlv.len;
    }
    return FlipsoPaymentNone;
}

FlipsoPaymentScheme flipso_payment_scheme_from_ppse(const uint8_t* fci, size_t len) {
    FlipsoTlv tlv;
    if(!fci || !flipso_payment_tlv(fci, len, 0, &tlv) || tlv.tag != FLIPSO_TAG_FCI) {
        return FlipsoPaymentNone;
    }

    FlipsoPaymentScheme by_label = FlipsoPaymentNone;
    FlipsoPaymentScheme scheme = flipso_payment_walk(fci + tlv.value, tlv.len, 1, &by_label);
    if(scheme != FlipsoPaymentNone) return scheme;
    if(by_label != FlipsoPaymentNone) return by_label;
    /* It answered the payment directory with an FCI: a payment card, even if
     * not one of the schemes above. */
    return FlipsoPaymentUnknown;
}

const char* flipso_payment_scheme_name(FlipsoPaymentScheme scheme) {
    switch(scheme) {
    case FlipsoPaymentVisa:
        return "Visa";
    case FlipsoPaymentMastercard:
        return "Mastercard";
    case FlipsoPaymentAmex:
        return "Amex";
    case FlipsoPaymentJcb:
        return "JCB";
    case FlipsoPaymentDiscover:
        return "Discover";
    case FlipsoPaymentUnionPay:
        return "UnionPay";
    default:
        return NULL;
    }
}
