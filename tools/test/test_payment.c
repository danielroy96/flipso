/*
 * Host-side test for the PPSE answer: which scheme a bank card or phone
 * belongs to, read from the one response Flipso asks a payment card for.
 *
 * Each response is laid out as EMV Contactless Book B's PPSE FCI is - the
 * template, its DF name, and a directory entry per application under the
 * proprietary template's BF0C - and each array is exactly as long as the data
 * it holds, so that a read past a length lands outside it and the sanitiser
 * sees it.
 */
#include "test.h"
#include "reader/flipso_payment.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCHEME(bytes) flipso_payment_scheme_from_ppse(bytes, sizeof(bytes))

/* A Visa debit card: one entry, its AID and a priority, no label. */
static const uint8_t visa[] = {
    0x6F, 0x23, /* FCI template */
    0x84, 0x0E, '2',  'P',  'A',  'Y',  '.',  'S',  'Y',
    'S',  '.',  'D',  'D',  'F',  '0',  '1',  0xA5, 0x11, /* proprietary template */
    0xBF, 0x0C, 0x0E, /* issuer discretionary data */
    0x61, 0x0C, /* directory entry */
    0x4F, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x03, 0x10, 0x10, /* AID: Visa credit or debit */
    0x87, 0x01, 0x01, /* priority */
};

/* A Mastercard, labelled. */
static const uint8_t mastercard[] = {
    0x6F, 0x2F, /* FCI template */
    0x84, 0x0E, '2',  'P',  'A',  'Y',  '.',  'S',  'Y',  'S', '.', 'D',
    'D',  'F',  '0',  '1',  0xA5, 0x1D, /* proprietary template */
    0xBF, 0x0C, 0x1A, /* issuer discretionary data */
    0x61, 0x18, /* directory entry */
    0x4F, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x04, 0x10, 0x10, /* AID: Mastercard */
    0x50, 0x0A, 'M',  'a',  's',  't',  'e',  'r',  'C',  'a', 'r', 'd', /* label */
    0x87, 0x01, 0x01, /* priority */
};

/* A phone wallet's Amex: the entry carries a kernel identifier (9F2A), a
 * two-byte tag, ahead of the AID. */
static const uint8_t amex_wallet[] = {
    0x6F, 0x26, /* FCI template */
    0x84, 0x0E, '2',  'P',  'A',  'Y',  '.',  'S',  'Y',
    'S',  '.',  'D',  'D',  'F',  '0',  '1',  0xA5, 0x14, /* proprietary template */
    0xBF, 0x0C, 0x11, /* issuer discretionary data */
    0x61, 0x0F, /* directory entry */
    0x9F, 0x2A, 0x01, 0x04, /* kernel identifier */
    0x4F, 0x06, 0xA0, 0x00, 0x00, 0x00, 0x25, 0x01, /* AID: American Express */
    0x87, 0x01, 0x01, /* priority */
};

/* An AID from a RID the table lacks, whose label still names the scheme. */
static const uint8_t label_only[] = {
    0x6F, 0x2A, /* FCI template */
    0x84, 0x0E, '2',  'P',  'A',  'Y',  '.',  'S', 'Y', 'S', '.', 'D',
    'D',  'F',  '0',  '1',  0xA5, 0x18, /* proprietary template */
    0xBF, 0x0C, 0x15, /* issuer discretionary data */
    0x61, 0x13, /* directory entry */
    0x4F, 0x05, 0xA0, 0x00, 0x00, 0x09, 0x99, /* AID: an unlisted RID */
    0x50, 0x0A, 'V',  'I',  'S',  'A',  ' ',  'D', 'E', 'B', 'I', 'T', /* label */
};

/* A payment directory with an entry nothing here names. */
static const uint8_t unknown[] = {
    0x6F, 0x1F, /* FCI template */
    0x84, 0x0E, '2',  'P',  'A',  'Y',  '.',  'S',  'Y',
    'S',  '.',  'D',  'D',  'F',  '0',  '1',  0xA5, 0x0D, /* proprietary template */
    0xBF, 0x0C, 0x0A, /* issuer discretionary data */
    0x61, 0x08, /* directory entry */
    0x4F, 0x06, 0xA0, 0x00, 0x00, 0x09, 0x99, 0x01, /* AID: an unlisted RID */
};

/* A template that claims more than the response holds. */
static const uint8_t overlong[] = {0x6F, 0x30, 0x84, 0x0E, '2', 'P', 'A', 'Y'};

/* A directory entry that claims more than its template holds: the walk stops
 * at it, but the FCI around it is still a payment card's. */
static const uint8_t truncated_entry[] = {
    0x6F,
    0x09, /* FCI template */
    0xA5,
    0x07, /* proprietary template */
    0xBF,
    0x0C,
    0x04, /* issuer discretionary data */
    0x61,
    0x10, /* directory entry, claiming sixteen bytes of two */
    0x4F,
    0x07,
};

/* A tag that never ends: every byte after the first has bit 8 set. */
static const uint8_t endless_tag[] = {0x6F, 0x05, 0xBF, 0x8C, 0x8C, 0x8C, 0x8C};

/* Not an FCI at all: a record template, as READ RECORD answers. */
static const uint8_t not_fci[] = {0x70, 0x03, 0x5A, 0x01, 0x00};

int main(void) {
    printf("Payment cards\n");

    check("the PPSE select is 20 bytes", sizeof(flipso_payment_ppse_select) == 20);
    check(
        "and names the payment directory",
        flipso_payment_ppse_select[4] == 14 && flipso_payment_ppse_select[5] == '2' &&
            flipso_payment_ppse_select[18] == '1' && flipso_payment_ppse_select[19] == 0x00);

    check("a Visa card is Visa", SCHEME(visa) == FlipsoPaymentVisa);
    check("a Mastercard is Mastercard", SCHEME(mastercard) == FlipsoPaymentMastercard);
    check(
        "a two-byte tag before the AID is stepped over", SCHEME(amex_wallet) == FlipsoPaymentAmex);
    check("an unlisted AID falls back on its label", SCHEME(label_only) == FlipsoPaymentVisa);
    check("an FCI nothing names is still a payment card", SCHEME(unknown) == FlipsoPaymentUnknown);
    check(
        "an entry cut short is still a payment card",
        SCHEME(truncated_entry) == FlipsoPaymentUnknown);
    check(
        "a tag that never ends is still read within its template",
        SCHEME(endless_tag) == FlipsoPaymentUnknown);

    check("a template longer than the response is not one", SCHEME(overlong) == FlipsoPaymentNone);
    check("a record template is not an FCI", SCHEME(not_fci) == FlipsoPaymentNone);
    check("nothing is not an FCI", flipso_payment_scheme_from_ppse(visa, 0) == FlipsoPaymentNone);
    check(
        "no buffer is not an FCI", flipso_payment_scheme_from_ppse(NULL, 4) == FlipsoPaymentNone);

    /* Every prefix of a real response: none may read past its end. */
    bool prefixes_ok = true;
    for(size_t len = 0; len < sizeof(mastercard); len++) {
        uint8_t* copy = malloc(len ? len : 1);
        memcpy(copy, mastercard, len);
        if(flipso_payment_scheme_from_ppse(copy, len) == FlipsoPaymentMastercard) {
            prefixes_ok = false;
        }
        free(copy);
    }
    check("no prefix of a response names its scheme", prefixes_ok);

    check("Visa is named", strcmp(flipso_payment_scheme_name(FlipsoPaymentVisa), "Visa") == 0);
    check(
        "Mastercard is named",
        strcmp(flipso_payment_scheme_name(FlipsoPaymentMastercard), "Mastercard") == 0);
    check(
        "an unknown scheme has no name", flipso_payment_scheme_name(FlipsoPaymentUnknown) == NULL);
    check("nor does no scheme", flipso_payment_scheme_name(FlipsoPaymentNone) == NULL);

    printf("\n%s\n", failures ? "FAILURES" : "All payment card tests passed");
    return failures ? 1 : 0;
}
