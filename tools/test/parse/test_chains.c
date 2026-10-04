/**
 * @file test_chains.c
 * @brief Sector chains, and the log a software anti-tear card keeps in them.
 */
#include "test_parse.h"

/* A fake card for itso_read_chain(): sector n reads as sector_size bytes of n,
 * unless it is the one told to fail. */
typedef struct {
    uint8_t sector_size;
    uint8_t fail_at;
    uint8_t reads;
} ChainSource;

static size_t chain_read(void* context, uint8_t sector, uint8_t* out, size_t capacity) {
    ChainSource* source = context;
    source->reads++;
    if(sector == source->fail_at) return 0;
    size_t len = source->sector_size < capacity ? source->sector_size : capacity;
    memset(out, sector, len);
    return len;
}

/* Write SCT element @p sector (1-based) as @p value, psi bits wide. */
static void chain_set(uint8_t* dir, const ItsoCard* card, uint8_t sector, uint8_t value) {
    uint8_t psi = itso_sct_bits(card->sector_count);
    uint32_t bit =
        (uint32_t)(2 + ITSO_DIR_ENTRY_LEN * card->dir_entries) * 8 + (uint32_t)(sector - 1) * psi;
    for(uint8_t i = 0; i < psi; i++) {
        uint32_t at = bit + i;
        uint8_t mask = (uint8_t)(0x80 >> (at % 8));
        if(value & (1u << (psi - 1 - i))) {
            dir[at / 8] |= mask;
        } else {
            dir[at / 8] &= (uint8_t)~mask;
        }
    }
}

void sector_chains(void) {
    static ItsoCard card;
    itso_card_reset(&card);
    card.sector_count = 16;
    card.sector_size = 32;
    card.dir_entries = 8;
    card.sct_len = 8; /* 16 four-bit elements. */
    uint8_t dir[2 + 8 * 5 + 8 + 1];
    memset(dir, 0, sizeof(dir));
    uint8_t out[ITSO_MAX_GROUP_LEN];

    /* 3 -> 5 -> 6, then S-1: an in-use product over three sectors. */
    chain_set(dir, &card, 3, 5);
    chain_set(dir, &card, 5, 6);
    chain_set(dir, &card, 6, 15);
    ChainSource source = {32, 0, 0};
    size_t len =
        itso_read_chain(&card, dir, sizeof(dir), 3, chain_read, &source, out, sizeof(out));
    check("a chain is read to its terminator", len == 96 && source.reads == 3);
    check("in chain order", out[0] == 3 && out[32] == 5 && out[64] == 6);

    /* A sector that will not read ends the chain with what came before it. */
    ChainSource failing = {32, 5, 0};
    len = itso_read_chain(&card, dir, sizeof(dir), 3, chain_read, &failing, out, sizeof(out));
    check("a sector that will not read ends the chain", len == 32);

    /* An unused product points at itself. */
    chain_set(dir, &card, 2, 2);
    source.reads = 0;
    len = itso_read_chain(&card, dir, sizeof(dir), 2, chain_read, &source, out, sizeof(out));
    check("a self-terminated chain is one sector", len == 32 && source.reads == 1);

    /* A loop is cut off by the hop cap rather than spinning. */
    chain_set(dir, &card, 7, 8);
    chain_set(dir, &card, 8, 7);
    source.reads = 0;
    len = itso_read_chain(&card, dir, sizeof(dir), 7, chain_read, &source, out, sizeof(out));
    check("a looping chain stops at the hop cap", source.reads == ITSO_MAX_CHAIN_HOPS);

    /* And by the buffer: the next sector would not fit. */
    source.reads = 0;
    len = itso_read_chain(&card, dir, sizeof(dir), 3, chain_read, &source, out, 70);
    check("a chain stops before overrunning the buffer", len == 64);

    /* Sector zero is the shell and never part of a chain. */
    check(
        "sector zero is not a chain",
        itso_read_chain(&card, dir, sizeof(dir), 0, chain_read, &source, out, sizeof(out)) == 0);
}

/*
 * A cyclic log on a software anti-tear card keeps a record per sector. On a CMD2
 * card with 80-byte sectors, concatenating the chain would put T1 at byte 80,
 * where a 48-byte stride looks for the second record at 48 and a third at 96.
 */
void log_sectors(void) {
    static ItsoCard card;
    itso_card_reset(&card);
    itso_parse_shell(&card, cmd2_shell, sizeof(cmd2_shell));
    itso_parse_directory(&card, cmd2_dir, sizeof(cmd2_dir));

    uint8_t dir[sizeof(cmd2_dir)];
    memcpy(dir, cmd2_dir, sizeof(dir));
    chain_set(dir, &card, 16, 17); /* T0 in sector 16, T1 in 17. */
    chain_set(dir, &card, 17, 0);

    ChainSource source = {80, 0, 0};
    uint8_t out[ITSO_MAX_GROUP_LEN];
    size_t len =
        itso_read_log_sectors(&card, dir, sizeof(dir), chain_read, &source, out, sizeof(out));
    check(
        "a CMD2 log is one record from each of its two sectors",
        len == 2 * ITSO_TAP_RECORD_LEN && source.reads == 2);
    check("each record starts its sector", out[0] == 16 && out[ITSO_TAP_RECORD_LEN] == 17);

    /* A chain back to its start, as a corrupt SCT might make, stops. */
    chain_set(dir, &card, 17, 16);
    source.reads = 0;
    len = itso_read_log_sectors(&card, dir, sizeof(dir), chain_read, &source, out, sizeof(out));
    check("a log chain that loops stops", len == 2 * ITSO_TAP_RECORD_LEN);

    /* A card with no log entry has no log to gather. */
    card.log_dir_index = 0;
    check(
        "no log entry, no log",
        itso_read_log_sectors(&card, dir, sizeof(dir), chain_read, &source, out, sizeof(out)) ==
            0);
}
