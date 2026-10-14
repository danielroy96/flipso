/**
 * @file test_cmd11_capture.c
 * @brief A CMD11 card through the file and back, read twice, and the random-MID
 * card that must not be saved at all.
 */
#include "test_capture.h"

/* The FCI object a read keeps, found as the transport finds it. */
static const uint8_t* fci_object(const uint8_t* fci, size_t len, uint8_t tag, size_t* out) {
    const uint8_t* found = itso_fci_find(fci, len, tag, out);
    check("the synthetic FCI holds the object", found != NULL);
    return found;
}

/* The blocks flipso_cmd11_read() keeps for the static-MID card. */
static void capture_cmd11(FlipsoCapture* capture, const uint8_t* product1, size_t product1_len) {
    size_t len = 0;
    const uint8_t* fci = cmd11_fci_static;
    flipso_capture_add(capture, FlipsoBlockShell, 0, cmd11_shell, sizeof(cmd11_shell));
    const uint8_t* oids = fci_object(fci, sizeof(cmd11_fci_static), 0xC3, &len);
    flipso_capture_add(capture, FlipsoBlockOidList, 0, oids, len);
    const uint8_t* dir = fci_object(fci, sizeof(cmd11_fci_static), 0xC2, &len);
    flipso_capture_add(capture, FlipsoBlockDirectory, 0, dir, len);
    flipso_capture_add(capture, FlipsoBlockProduct, 1, product1, product1_len);
    flipso_capture_add(capture, FlipsoBlockProduct, 2, cmd11_product2, sizeof(cmd11_product2));
    flipso_capture_add(capture, FlipsoBlockLog, 0, cmd11_log, sizeof(cmd11_log));
}

/* A capture through its lines and back into a fresh one. */
static FlipsoCapture* reload(const FlipsoCapture* capture) {
    size_t count = 0;
    char** lines = to_lines(capture, &count);
    FlipsoCapture* loaded = flipso_capture_alloc();
    for(size_t i = 0; i < count; i++)
        flipso_capture_parse_line(loaded, lines[i]);
    free_lines(lines, count);
    return loaded;
}

void cmd11_capture(void) {
    FlipsoCapture* capture = flipso_capture_alloc();
    capture_cmd11(capture, cmd11_product1, sizeof(cmd11_product1));

    char number[ITSO_ISRN_DIGITS + 1];
    check(
        "a CMD11 card is known by its card number",
        flipso_capture_card_number(capture, number) && strcmp(number, EXPECT_CMD11_ISRN) == 0);

    static ItsoCard direct, reloaded;
    check("a CMD11 capture decodes", flipso_capture_decode(capture, &direct));
    check(
        "to its products and journeys",
        direct.fvc == ITSO_FVC_PIM && direct.product_count == 3 && direct.tap_count == 5);
    check(
        "with the purse's records found past its IPE",
        direct.products[0].value_parsed && direct.products[0].value_history_count == 2 &&
            itso_product_purse(&direct.products[0])->balance.value == 1230);
    check(
        "and its InstanceID hidden",
        direct.products[0].instance_hidden && !direct.products[0].instance_valid);
    check("and the OID list's owner for E3", direct.products[2].oid == 24867);
    check("not a shared shell", !direct.shell_pseudo);

    FlipsoCapture* loaded = reload(capture);
    check("the loaded CMD11 capture decodes", flipso_capture_decode(loaded, &reloaded));
    check("to the same card, OID list and all", itso_card_equal(&direct, &reloaded));

    flipso_capture_free(loaded);
    flipso_capture_free(capture);
    itso_card_reset(&direct);
    itso_card_reset(&reloaded);
}

void cmd11_merge(void) {
    /* With no sectors, the value group's place in a product block comes from
     * the IPE's length alone - which the merge has to find to keep records. */
    FlipsoCapture* previous = flipso_capture_alloc();
    capture_cmd11(previous, cmd11_product1_before, sizeof(cmd11_product1_before));
    FlipsoCapture* now = flipso_capture_alloc();
    capture_cmd11(now, cmd11_product1, sizeof(cmd11_product1));

    FlipsoCaptureDiff diff;
    memset(&diff, 0, sizeof(diff));
    flipso_capture_merge_history(now, previous, &diff);
    check("a CMD11 read again finds the one new transaction", diff.new_values == 1);
    check("and keeps the one that has rolled off", diff.kept_values == 1);

    static ItsoCard card;
    flipso_capture_decode(now, &card);
    const ItsoProduct* purse = &card.products[0];
    check("the purse has all three records", purse->value_history_count == 3);
    check(
        "the oldest is the one only the file has",
        purse->value_history_count == 3 && purse->value_history[2].ts == 5 &&
            !purse->value_history[2].on_card);

    flipso_capture_free(now);
    flipso_capture_free(previous);
    itso_card_reset(&card);
}

void cmd11_pseudo(void) {
    /* A random-MID card's pseudo-shell is the same on every card of its
     * operator (TS 1000-10 clause 12.4.3): matching saved cards on it would
     * file strangers' cards as one. */
    FlipsoCapture* capture = flipso_capture_alloc();
    size_t len = 0;
    flipso_capture_add(capture, FlipsoBlockShell, 0, cmd11_shell, sizeof(cmd11_shell));
    const uint8_t* mid = fci_object(cmd11_fci_pseudo, sizeof(cmd11_fci_pseudo), 0xC6, &len);
    flipso_capture_add(capture, FlipsoBlockPseudoMid, 0, mid, len);

    char number[ITSO_ISRN_DIGITS + 1] = "";
    check("a pseudo-shell has no card number", !flipso_capture_card_number(capture, number));

    static ItsoCard card, reloaded;
    check("a pseudo-shell still decodes", flipso_capture_decode(capture, &card));
    check("and says it is shared", card.shell_pseudo);

    /* The demo cards are files, so the block has to survive one. */
    FlipsoCapture* loaded = reload(capture);
    flipso_capture_decode(loaded, &reloaded);
    check("a loaded pseudo-shell is still shared", reloaded.shell_pseudo);
    check(
        "and still has no card number to be saved under",
        !flipso_capture_card_number(loaded, number));

    flipso_capture_free(loaded);
    flipso_capture_free(capture);
    itso_card_reset(&card);
    itso_card_reset(&reloaded);
}
