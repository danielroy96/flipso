/**
 * @file flipso_payment.h
 * @brief Which payment scheme a contactless bank card or phone wallet belongs to.
 *
 * UK buses and trains take contactless bank cards, so a debit card or a phone
 * is one of the things people will hold against the reader. Neither has any
 * travel data on it: a pay-as-you-go fare is worked out afterwards by the
 * operator's back office from the taps it saw. What Flipso can usefully do is
 * recognise one and say so.
 *
 * Recognising one takes a single command, the SELECT of the contactless
 * payment directory (PPSE, "2PAY.SYS.DDF01", EMV Contactless Book B), and it
 * is the only payment command Flipso sends - after the ITSO and DESFire
 * selects every card is asked, which a bank card refuses. Its answer lists the payment
 * applications on the card or phone by AID and label, and nothing else: the
 * card number only comes back from GET PROCESSING OPTIONS and READ RECORD,
 * which Flipso never sends. Of that answer only the scheme is kept, as one
 * byte - the label is read to name a scheme the AIDs did not, and dropped.
 *
 * Pure C, no firmware headers: the parser is host-tested by
 * tools/test/test_payment.c.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Kept as a byte wherever it is stored. */
typedef enum {
    FlipsoPaymentNone, /**< Not a payment card: no PPSE, or not asked. */
    FlipsoPaymentUnknown, /**< A payment card, of a scheme not named here. */
    FlipsoPaymentVisa,
    FlipsoPaymentMastercard, /**< Maestro shares its RID. */
    FlipsoPaymentAmex,
    FlipsoPaymentJcb,
    FlipsoPaymentDiscover, /**< Diners Club shares its RID. */
    FlipsoPaymentUnionPay,
} FlipsoPaymentScheme;

/** The PPSE SELECT: 00 A4 04 00, the 14-byte name, and an Le of zero. */
#define FLIPSO_PAYMENT_PPSE_SELECT_LEN 20
extern const uint8_t flipso_payment_ppse_select[FLIPSO_PAYMENT_PPSE_SELECT_LEN];

/**
 * The scheme a PPSE SELECT's answer names.
 *
 * @param fci the response data, without its status word, from a SELECT that
 *            answered 9000.
 * @return FlipsoPaymentNone when it is not an FCI template at all;
 *         FlipsoPaymentUnknown when it is one but neither an AID nor a label
 *         names a scheme Flipso knows.
 */
FlipsoPaymentScheme flipso_payment_scheme_from_ppse(const uint8_t* fci, size_t len);

/** "Visa", short enough for a title; NULL for None and Unknown. */
const char* flipso_payment_scheme_name(FlipsoPaymentScheme scheme);

#ifdef __cplusplus
}
#endif
