/**
 * @file replay.c
 * @brief Run the ITSO decoder over bytes captured from a real card.
 *
 * The data comes from replay_data.h, which tools/test/replay.py generates from
 * a dump pulled off the device. Nothing here touches the Flipper: this is the
 * fast loop for decoder bugs, because a hypothesis can be tested in a second
 * without reflashing and re-tapping the card.
 */
#include "itso.h"
#include "itso_i.h"
#include "replay_data.h"

#include <stdio.h>
#include <string.h>

static void show_location(const char* label, const ItsoLocation* loc) {
    if(loc->valid) printf("      %-12s %s (LocDefType %u)\n", label, loc->text, loc->def_type);
}

static void show_money(const char* label, const ItsoMoney* money) {
    if(!money->valid) return;
    char buf[32];
    itso_format_money(money, buf, sizeof(buf));
    printf("      %-12s %s\n", label, buf);
}

int main(void) {
    ItsoCard card;
    itso_card_reset(&card);

    printf("Shell (%zu bytes)\n", replay_shell_len);
    if(!replay_shell_len) {
        printf("  no SHELL block in the dump - nothing to decode\n");
        return 1;
    }
    if(!itso_parse_shell(&card, replay_shell, replay_shell_len)) {
        printf("  REJECTED by itso_parse_shell\n");
        printf("  looks_like_shell: %s\n",
               itso_looks_like_shell(replay_shell, replay_shell_len) ? "yes" : "no");
        return 1;
    }
    printf("  card number   %s (check digit %s)\n", card.isrn,
           card.isrn_check_ok ? "ok" : "BAD");
    printf("  IIN %u, OID %u, FVC %u, format rev %u\n", card.iin, card.oid, card.fvc,
           card.format_rev);
    printf("  geometry      %u sectors of %u bytes, %u directory entries, SCTL %u\n",
           card.sector_count, card.sector_size, card.dir_entries, card.sct_len);

    printf("\nDirectory (%zu bytes)\n", replay_dir_len);
    if(replay_dir_len && !itso_parse_directory(&card, replay_dir, replay_dir_len)) {
        printf("  REJECTED by itso_parse_directory\n");
        return 1;
    }
    printf("  %u product(s), shell %s\n", card.product_count,
           card.shell_blocked ? "BLOCKED" : "not blocked");
    if(card.log_entry_valid)
        printf("  log entry at E%u, product E%u, %s\n", card.log_dir_index, card.log_ptr,
               card.log_eei ? "inside a closed system" : "outside a closed system");

    /* The dump writes one GROUP block per directory entry, in directory order,
     * which is the order the reader itself walks them in. */
    printf("\nProducts\n");
    for(size_t i = 0; i < replay_group_count; i++) {
        const ReplayBlock* block = &replay_groups[i];
        ItsoProduct* product = NULL;
        for(uint8_t p = 0; p < card.product_count; p++) {
            if(card.products[p].dir_index == block->index) {
                product = &card.products[p];
                break;
            }
        }
        if(!product) {
            printf("  E%u: %zu bytes, but the directory has no such entry\n", block->index,
                   block->len);
            continue;
        }

        itso_parse_ipe(product, block->data, block->len, card.sector_size);
        printf("  E%u  %s (TYP %u.%u), %zu bytes, rev %u, bitmap 0x%02X, %s\n",
               product->dir_index, itso_typ_name(product->typ), product->typ, product->ptyp,
               block->len, product->format_rev, product->bitmap,
               product->body_parsed ? "parsed" : "NOT PARSED");
        printf("      %-12s %u%s\n", "owner OID", product->oid,
               product->oid_extended ? " (extended)" : "");
        printf("      %-12s %s\n", "status", itso_status_name(product->status));
        show_money("balance", &product->balance);
        show_location("from", &product->from);
        show_location("to", &product->to);
        if(product->value_group && !product->body_parsed)
            printf("      has a value record that could not be read\n");
    }

    if(replay_log_len) {
        printf("\nJourney log (%zu bytes)\n", replay_log_len);
        itso_parse_log(&card, replay_log, replay_log_len);
        printf("  %u tap(s)\n", card.tap_count);
        for(uint8_t i = 0; i < card.tap_count; i++) {
            const ItsoTap* tap = &card.taps[i];
            printf("  tap %u%s: %s, DTS %u\n", i, tap->latest ? " (latest)" : "",
                   itso_transaction_name(tap->transaction_type), tap->dts);
            show_money("fare", &tap->amount);
            show_location("origin", &tap->origin);
            show_location("destination", &tap->destination);
            show_location("route", &tap->route);
        }
    }

    printf("\nDecoded without crashing. Compare the fields above against what the "
           "card should say.\n");
    return 0;
}
