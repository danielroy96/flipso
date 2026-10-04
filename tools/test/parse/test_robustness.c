/**
 * @file test_robustness.c
 * @brief Degenerate and hostile buffers: under ASan any over-read here aborts the run.
 */
#include "test_parse.h"

/* Feed the decoder degenerate and hostile buffers; under ASan/UBSan any
 * out-of-bounds read or overflow here aborts the run. */
void robustness(void) {
    static uint8_t junk[256];
    static ItsoCard c;

    /* All zeros must be rejected everywhere. */
    memset(junk, 0, sizeof(junk));
    itso_card_reset(&c);
    check("all-zero shell rejected", !itso_parse_shell(&c, junk, sizeof(junk)));
    check("directory without shell rejected", !itso_parse_directory(&c, junk, sizeof(junk)));

    /* Truncations of a valid shell and directory must not read past the end. */
    for(size_t len = 0; len <= sizeof(card_shell); len++) {
        itso_card_reset(&c);
        itso_parse_shell(&c, card_shell, len);
    }
    check("truncated shells survive", 1);

    for(size_t len = 0; len <= sizeof(card_dir); len++) {
        itso_card_reset(&c);
        itso_parse_shell(&c, card_shell, sizeof(card_shell));
        itso_parse_directory(&c, card_dir, len);
    }
    check("truncated directories survive", 1);

    /* A deterministic pseudo-random sweep over every entry point. */
    uint32_t seed = 0x1234567u;
    for(int round = 0; round < 4000; round++) {
        for(size_t i = 0; i < sizeof(junk); i++) {
            seed = seed * 1103515245u + 12345u;
            junk[i] = (uint8_t)(seed >> 16);
        }
        /* Keep the ITSO issuer marker so the shell parser accepts the garbage. */
        junk[2] = 0x63;
        junk[3] = 0x35;
        junk[4] = 0x97;
        junk[1] |= 0x10; /* Force the "full shell" bitmap bit. */

        itso_card_reset(&c);
        size_t len = 24 + (seed % (sizeof(junk) - 24));
        if(itso_parse_shell(&c, junk, len)) {
            itso_parse_directory(&c, junk, len);
            for(uint8_t p = 0; p < c.product_count; p++) {
                itso_parse_ipe(&c.products[p], junk, len, c.sector_size);
            }
            itso_parse_log(&c, junk, len);
        }
    }
    check("4000 random buffers survive", 1);

    /* Every IPE type, format revision and bitmap against truncated buffers.
     * The random sweep above reaches these paths only by chance, and the
     * per-type decoders are exactly where a wrong offset reads off the end. */
    for(uint8_t typ = 0; typ < 32; typ++) {
        for(uint8_t rev = 0; rev < 16; rev++) {
            for(uint8_t bitmap = 0; bitmap < 64; bitmap++) {
                for(size_t avail = 0; avail <= 96; avail++) {
                    /* Exactly the size the decoder is told it has. A generous
                     * stack buffer would leave a read past the dataset landing
                     * on valid memory, which is how a bounds bug in one of the
                     * per-type decoders hid from this test. */
                    uint8_t* body = malloc(avail ? avail : 1);
                    for(size_t i = 0; i < avail; i++) {
                        seed = seed * 1103515245u + 12345u;
                        body[i] = (uint8_t)(seed >> 16);
                    }
                    if(avail >= 2) {
                        /* IPELength in blocks, so the dataset claims the whole
                         * buffer, then the six bitmap bits and the revision. */
                        body[0] = (uint8_t)((((avail / 4) << 2) & 0xFC) | ((bitmap >> 4) & 0x03));
                        body[1] = (uint8_t)(((bitmap & 0x0F) << 4) | rev);
                    }
                    ItsoProduct pr;
                    memset(&pr, 0, sizeof(pr));
                    pr.typ = typ;
                    pr.value_group = true;
                    /* Sector size 0 as well: the value group offset divides by it. */
                    itso_parse_ipe(&pr, body, avail, (uint8_t)(avail % 17));
                    itso_product_free(&pr);
                    free(body);
                }
            }
        }
    }
    check("every IPE type survives a truncated dataset", 1);

    /* Every transient ticket revision and optional group combination. */
    for(uint8_t rev = 0; rev < 16; rev++) {
        for(uint16_t bits = 0; bits < 0x1000; bits += 7) {
            uint8_t rec[48];
            for(size_t i = 0; i < sizeof(rec); i++) {
                seed = seed * 1103515245u + 12345u;
                rec[i] = (uint8_t)(seed >> 16);
            }
            rec[1] = (uint8_t)((rec[1] & 0xF0) | rev);
            rec[2] = (uint8_t)(bits >> 4);
            rec[3] = (uint8_t)((bits & 0x0F) << 4 | (rec[3] & 0x0F));
            itso_card_reset(&c);
            itso_parse_log(&c, rec, sizeof(rec));
        }
    }
    check("every tap revision and group set survives", 1);

    /* Every location type against a short buffer, again sized exactly, and
     * rendered as a screen would render it. */
    for(int t = 0; t < 256; t++) {
        for(size_t avail = 0; avail < 24; avail++) {
            ItsoLocation loc;
            uint8_t* body = malloc(avail ? avail : 1);
            memset(body, 0xAA, avail);
            if(avail >= 1) body[0] = (uint8_t)t;
            if(avail >= 2) body[1] = (uint8_t)(avail > 2 ? avail - 2 : 0);
            itso_parse_location(body, avail, ItsoLocStructLoc1, &loc);
            loc_text(&loc);
            loc_code(&loc);
            itso_parse_location(body, avail, ItsoLocStructLoc2, &loc);
            loc_text(&loc);
            loc_code(&loc);
            free(body);
        }
    }
    check("all location types survive short buffers", 1);
}

/*
 * A card allocates only what it holds: its products and journeys used to be
 * fixed arrays of 20 and 12, 15 KB whatever the card, and a real card fills
 * five or six products. Sized to fit, released on reset, and compared by what
 * they hold rather than where they are.
 */
void card_arrays(void) {
    static ItsoCard card;
    itso_card_reset(&card);
    check("an empty card owns nothing", card.products == NULL && card.taps == NULL);

    itso_parse_shell(&card, card_shell, sizeof(card_shell));
    itso_parse_directory(&card, card_dir, sizeof(card_dir));
    check(
        "the directory's five products get exactly five slots",
        card.product_count == 5 && card.product_capacity == 5);

    itso_parse_log(&card, card_log, sizeof(card_log));
    check(
        "the log's journeys get exactly as many slots",
        card.tap_count > 0 && card.tap_capacity == card.tap_count);

    static const uint8_t entry[ITSO_DIR_ENTRY_LEN] = {0x04, 0xD2, 0x10, 0x00, 0x01};
    check(
        "a product added later grows the array by one",
        itso_card_add_product(&card, entry, 3) != NULL);
    check("to six", card.product_count == 6 && card.product_capacity == 6);
    for(uint8_t i = 0; i < ITSO_MAX_CARD_PRODUCTS; i++) {
        itso_card_add_product(&card, entry, 3);
    }
    check(
        "and stops at the cap",
        card.product_count == ITSO_MAX_CARD_PRODUCTS &&
            card.product_capacity == ITSO_MAX_CARD_PRODUCTS &&
            itso_card_add_product(&card, entry, 3) == NULL);

    /* Two decodes of the same bytes are the same card, wherever each put its
     * arrays and however much room it left behind them. */
    static ItsoCard again;
    itso_card_reset(&again);
    itso_parse_shell(&again, card_shell, sizeof(card_shell));
    itso_parse_directory(&again, card_dir, sizeof(card_dir));
    itso_parse_log(&again, card_log, sizeof(card_log));
    itso_card_reset(&card);
    itso_parse_shell(&card, card_shell, sizeof(card_shell));
    itso_parse_directory(&card, card_dir, sizeof(card_dir));
    itso_parse_log(&card, card_log, sizeof(card_log));
    check(
        "the same bytes decode equal",
        itso_card_equal(&card, &again) && card.products != again.products);
    again.products[0].typ ^= 1;
    check("and a product that differs does not", !itso_card_equal(&card, &again));
    again.products[0].typ ^= 1;
    again.taps[0].dts ^= 1;
    check("nor does a journey", !itso_card_equal(&card, &again));

    itso_card_free(&card);
    itso_card_free(&again);
    check(
        "a freed card owns nothing and holds nothing",
        card.products == NULL && card.taps == NULL && card.product_count == 0 &&
            card.tap_count == 0 && card.product_capacity == 0 && card.tap_capacity == 0);
}

void oversized_directory(void) {
    static ItsoCard card;
    itso_card_reset(&card);
    if(!itso_parse_shell(&card, cmd2_shell, sizeof(cmd2_shell))) {
        check("oversized directory needs a shell", 0);
        return;
    }
    card.dir_entries = 31;

    /* 2 header bytes, 31 five-byte entries, then the SCT and its sequence byte. */
    uint8_t dir[2 + 31 * 5 + 46 + 1];
    memset(dir, 0x11, sizeof(dir)); /* Non-blank everywhere: every entry counts. */
    dir[0] = 0x00;
    dir[1] = 0x01; /* DIRBitMap zero: no log entry, so all 31 are products. */

    check("oversized directory parsed", itso_parse_directory(&card, dir, sizeof(dir)));
    check("product count capped", card.product_count == ITSO_MAX_PRODUCTS);
}
