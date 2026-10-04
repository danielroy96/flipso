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
#include <time.h>

/** A DTS as a readable UTC timestamp, which is what a history is read by. */
static const char* fmt_dts(ItsoDts dts) {
    static char buf[24];
    time_t when = (time_t)itso_dts_to_unix(dts);
    struct tm tm;
    gmtime_r(&when, &tm);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

/** A DATE as a readable UTC day. */
static const char* fmt_date(ItsoDate date) {
    static char buf[16];
    time_t when = (time_t)itso_date_to_unix(date);
    struct tm tm;
    gmtime_r(&when, &tm);
    strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

/** A location's text, rendered into a buffer that lasts until the next call. */
static const char* location_text(const ItsoLocation* loc) {
    static char text[ITSO_LOC_LEN];
    itso_location_text(loc, text, sizeof(text));
    return text;
}

static void show_location(const char* label, const ItsoLocation* loc) {
    if(loc->valid)
        printf("      %-12s %s (LocDefType %u)\n", label, location_text(loc), loc->def_type);
}

static void show_money(const char* label, const ItsoMoney* money) {
    if(!money->valid) return;
    char buf[32];
    itso_format_money(money, buf, sizeof(buf));
    printf("      %-12s %s\n", label, buf);
}

/* Everything the decoder reads beyond the basics above, one line per element,
 * so a real card can be checked against the spec without a device. */
static void show_extras(const ItsoProduct* p, const uint8_t* group, size_t len, uint8_t sector) {
    if(p->instance_valid)
        printf(
            "      %-12s ISAM %08lX (operator %u) #%lu\n",
            "created by",
            (unsigned long)p->isam_id,
            itso_isam_oid(p->isam_id),
            (unsigned long)p->isam_seq);
    if(p->value_parsed)
        printf(
            "      %-12s ISAM %08lX (operator %u)\n",
            "last POST",
            (unsigned long)p->value_isam,
            itso_isam_oid(p->value_isam));
    if(p->has_cpicc) printf("      %-12s %u (0x%04X)\n", "CPICC", p->cpicc, p->cpicc);
    if(itso_product_id(p)->has_holder_id)
        printf("      %-12s %lu\n", "holder ID", (unsigned long)itso_product_id(p)->holder_id);
    if(itso_product_id(p)->has_secondary_holder)
        printf(
            "      %-12s %lu\n",
            "2nd holder",
            (unsigned long)itso_product_id(p)->secondary_holder_id);
    if(itso_product_id(p)->language) {
        char code[3];
        printf(
            "      %-12s %u = %s\n",
            "language",
            itso_product_id(p)->language,
            itso_language_code(itso_product_id(p)->language, code) ? code : "?");
    }
    if(itso_product_id(p)->rounding)
        printf("      %-12s 0x%X\n", "rounding", itso_product_id(p)->rounding);
    if(itso_product_id(p)->has_half_days)
        printf("      %-12s 0x%04X\n", "half days", itso_product_id(p)->half_days);
    if(p->has_deposit) show_money("deposit", &p->deposit);
    if(itso_product_id(p)->has_shell_deposit)
        show_money("shell dep.", &itso_product_id(p)->shell_deposit);
    if(p->has_passback && p->passback) printf("      %-12s %u min\n", "passback", p->passback);

    const ItsoTicketTerms* t = itso_product_ticket(p);
    if(t->valid) {
        printf(
            "      %-12s flags 0x%04X, days 0x%02X, class %u, validity %u, promo %u\n",
            "terms",
            t->flags,
            t->valid_days,
            t->travel_class,
            t->validity_code,
            t->promotion_code);
        printf(
            "      %-12s issued %u, ends %u min, from DTS %s",
            "",
            t->issue_date,
            t->expiry_time,
            t->valid_from_dts ? fmt_dts(t->valid_from_dts) : "-");
        printf(", party %u/%u/%u\n", t->adults, t->children, t->concessions);
        if(t->amount_paid.valid) show_money("paid", &t->amount_paid);
        if(t->paid_mop || t->vat) printf("      %-12s MOP %u, VAT %u\n", "", t->paid_mop, t->vat);
        if(t->has_pass_duration)
            printf(
                "      %-12s %u (unit %u)\n", "pass length", t->pass_duration, t->duration_unit);
        if(t->photocard) printf("      %-12s %lu\n", "photocard", (unsigned long)t->photocard);
        if(t->has_mode_group)
            printf(
                "      %-12s mode %u, transfers %u, limit %u\n",
                "mode",
                t->mode,
                t->max_transfers,
                t->time_limit);
    }

    if(p->vgx_ref) {
        printf("      %-12s VGXRef %u\n", "extension", p->vgx_ref);
        ItsoCapping cap;
        if(itso_parse_capping(group, len, sector, itso_product_purse(p)->balance.currency, &cap)) {
            printf("      %-12s strategy %u\n", "capping", cap.strategy);
            for(int a = 0; a < ITSO_CAP_ACCUMULATORS; a++) {
                const ItsoCapAccumulator* c = &cap.acc[a];
                printf(
                    "        set %d: rule %u, day %ld, multi %ld, uncapped %ld, days %u%s%s\n",
                    a + 1,
                    c->rule,
                    (long)c->day.value,
                    (long)c->multiday.value,
                    (long)c->uncapped.value,
                    c->day_count,
                    c->location.valid ? ", at " : "",
                    c->location.valid ? location_text(&c->location) : "");
            }
        }
    }
}

/** Print the shell identity and geometry a card decoded to. */
static void show_shell(const ItsoCard* card) {
    printf(
        "  card number   %s (check digit %s)\n", card->isrn, card->isrn_check_ok ? "ok" : "BAD");
    printf(
        "  IIN %u, OID %u, FVC %u, format rev %u%s\n",
        card->iin,
        card->oid,
        card->fvc,
        card->format_rev,
        card->shell_compact ? " (compact)" : "");
    printf(
        "  geometry      %u sectors of %u bytes, %u directory entries, SCTL %u\n",
        card->sector_count,
        card->sector_size,
        card->dir_entries,
        card->sct_len);
}

int main(void) {
    static ItsoCard card;
    itso_card_reset(&card);

    /* A Type 2 tag is one flat block of pages, not a shell/directory/product
     * read, so it decodes on its own and there is nothing else in the file. */
    if(replay_type2_len) {
        printf("Type 2 tag (%zu bytes of page memory)\n", replay_type2_len);
        static const char* const kinds[] = {
            [ItsoType2Incomplete] = "incomplete read",
            [ItsoType2NotItso] = "no ITSO shell",
            [ItsoType2FullShell] = "full shell (CMD9/CMD10)",
            [ItsoType2OtherShell] = "full shell of an unknown media definition",
            [ItsoType2Compact] = "compact shell (CMD4)",
        };
        ItsoType2Kind kind = itso_type2_kind(replay_type2, replay_type2_len);
        printf("  %s\n", kinds[kind]);
        if(kind == ItsoType2FullShell) {
            /* A read saves a CMD9 or CMD10 as the blocks a smartcard is saved
             * as, so it is that file which replays - page memory from a debug
             * dump would need the transport's own walk. */
            printf("  save the card in the app and replay the .flipso instead\n");
            return 1;
        }
        if(!itso_parse_type2(&card, replay_type2, replay_type2_len)) {
            printf("  REJECTED by itso_parse_type2 - not a whole CMD4 ticket\n");
            return 1;
        }
        show_shell(&card);
        printf("  chip UID     ");
        for(size_t i = 0; i < sizeof(card.chip_uid); i++)
            printf("%02X", card.chip_uid[i]);
        printf("\n");
        printf(
            "  lock bytes   %02X %02X: locked pages %04X (ITSO wants %04X), frozen %04X\n",
            card.chip_lock[0],
            card.chip_lock[1],
            itso_type2_locked_pages(card.chip_lock),
            ITSO_CMD4_LOCKED_PAGES,
            itso_type2_frozen_pages(card.chip_lock));
        printf("  memory       %u bytes\n", card.chip_memory_len);
        printf("\nProducts\n  %u product(s)\n", card.product_count);
        const ItsoSpaceSaving* ss = card.space;
        for(uint8_t p = 0; p < card.product_count; p++) {
            const ItsoProduct* product = &card.products[p];
            const ItsoTicketTerms* t = itso_product_ticket(product);
            printf(
                "  E%u  %s (TYP %u.%u), owner OID %u%s, %s\n",
                product->dir_index,
                itso_typ_name(product->typ),
                product->typ,
                product->ptyp,
                product->oid,
                product->oid_extended ? " (extended)" : "",
                itso_status_name(product->status));
            printf(
                "      %-12s %s (DATE %u)\n",
                "expires",
                fmt_date(product->expiry),
                product->expiry);
            if(product->instance_valid)
                printf(
                    "      %-12s ISAM %08lX (OID %u), seq %lu, key %u\n",
                    "created by",
                    (unsigned long)product->isam_id,
                    itso_isam_oid(product->isam_id),
                    (unsigned long)product->isam_seq,
                    product->key_id);
            if(!product->space_saving || !ss) continue;

            if(t->issue_date) printf("      %-12s %s\n", "issued", fmt_date(t->issue_date));
            show_money("price paid", &t->amount_paid);
            if(t->adults || t->children)
                printf("      %-12s %s\n", "traveller", t->children ? "child" : "adult");
            printf("      %-12s %s\n", "class", t->travel_class == 1 ? "first" : "standard");
            static const char* const area[] = {"fare code", "fare value", "location type"};
            printf(
                "      %-12s %s %lu\n",
                "area",
                area[ss->area_kind % 3],
                (unsigned long)ss->area_value);
            printf(
                "      %-12s off-peak=%d weekday=%d first=%d expiry-time=%d\n",
                "flags",
                !!(ss->flags & ITSO_SS_OFF_PEAK),
                !!(ss->flags & ITSO_SS_WEEKDAY),
                !!(ss->flags & ITSO_SS_FIRST_CLASS),
                !!(ss->flags & ITSO_SS_EXPIRY_TIME));
            if(product->has_passback)
                printf("      %-12s %u min\n", "passback", product->passback);
            if(ss->has_last_use)
                printf(
                    "      %-12s %s\n",
                    "last used",
                    ss->last_use_dts ? fmt_dts(ss->last_use_dts) : "never");
            if(ss->has_events)
                printf(
                    "      %-12s %s, %s\n",
                    "events",
                    itso_transaction_name(ss->event1),
                    itso_transaction_name(ss->event2));
            if(t->photocard) printf("      %-12s %lu\n", "photocard", (unsigned long)t->photocard);
            if(itso_count_name(product->count_kind))
                printf(
                    "      %-12s %lu\n",
                    itso_count_name(product->count_kind),
                    (unsigned long)product->count);
            if(product->from.valid)
                printf(
                    "      %-12s %s (LocDefType %u)\n",
                    ss->usage_alighted ? "last off at" : "last on at",
                    location_text(&product->from),
                    product->from.def_type);
            if(product->typ == ItsoTypCarnet) {
                printf(
                    "      %-12s %u %u %u %u %u %u, issue day %d, expiry day %d\n",
                    "day ticks",
                    ss->carnet_ticks[0],
                    ss->carnet_ticks[1],
                    ss->carnet_ticks[2],
                    ss->carnet_ticks[3],
                    ss->carnet_ticks[4],
                    ss->carnet_ticks[5],
                    ss->carnet_issue_day,
                    ss->carnet_expiry_day);
            }
            if(product->typ == ItsoTypMultiUse && product->format_rev == 2) {
                printf(
                    "      %-12s %s\n",
                    "journey began",
                    ss->journey_start_dts ? fmt_dts(ss->journey_start_dts) : "not yet");
                printf(
                    "      %-12s %u that day (limit %u), %u changes (limit %u)\n",
                    "journeys",
                    ss->daily_journeys,
                    ss->max_daily_journeys,
                    ss->transfers,
                    t->max_transfers);
            }
        }
        printf("\nDecoded without crashing. Compare the fields above against the ticket.\n");
        return 0;
    }

    printf("Shell (%zu bytes)\n", replay_shell_len);
    if(!replay_shell_len) {
        printf("  no SHELL block in the dump - nothing to decode\n");
        return 1;
    }
    if(!itso_parse_shell(&card, replay_shell, replay_shell_len)) {
        printf("  REJECTED by itso_parse_shell\n");
        printf(
            "  looks_like_shell: %s\n",
            itso_looks_like_shell(replay_shell, replay_shell_len) ? "yes" : "no");
        return 1;
    }
    printf("  card number   %s (check digit %s)\n", card.isrn, card.isrn_check_ok ? "ok" : "BAD");
    printf(
        "  IIN %u, OID %u, FVC %u, format rev %u\n", card.iin, card.oid, card.fvc, card.format_rev);
    /* A CMD9 or CMD10's chip pages, saved beside its shell. */
    if(replay_tag_len) {
        itso_parse_type2_tag(&card, replay_tag, replay_tag_len);
        printf(
            "  chip          %s, UID ",
            itso_type2_chip_name(&card) ? itso_type2_chip_name(&card) : "unknown");
        for(size_t i = 0; i < sizeof(card.chip_uid); i++)
            printf("%02X", card.chip_uid[i]);
        printf(", locked pages %04X", itso_type2_locked_pages(card.chip_lock));
        if(card.chip_abacus_valid) printf(", Abacus %u of 16", card.chip_abacus);
        printf("\n");
    }
    printf(
        "  geometry      %u sectors of %u bytes, %u directory entries, SCTL %u\n",
        card.sector_count,
        card.sector_size,
        card.dir_entries,
        card.sct_len);
    printf(
        "  checksum      %s\n",
        !card.secrc_checked ? "not checked (shell shorter than it claims)" :
        card.secrc_valid    ? "verified" :
                              "MISMATCH - these bytes are not what the card wrote");

    printf("\nDirectory (%zu bytes)\n", replay_dir_len);
    if(replay_dir_len && !itso_parse_directory(&card, replay_dir, replay_dir_len)) {
        printf("  REJECTED by itso_parse_directory\n");
        return 1;
    }
    printf(
        "  %u product(s), shell %s\n",
        card.product_count,
        card.shell_blocked ? "BLOCKED" : "not blocked");
    if(card.log_entry_valid)
        printf(
            "  log entry at E%u, product E%u, %s\n",
            card.log_dir_index,
            card.log_ptr,
            card.log_eei ? "inside a closed system" : "outside a closed system");
    if(card.dir_instance_valid)
        printf(
            "  last written  ISAM %08lX (operator %u), KID %u, shell iteration %u\n",
            (unsigned long)card.dir_isam,
            itso_isam_oid(card.dir_isam),
            card.dir_kid,
            card.shell_iteration);

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
            printf(
                "  E%u: %zu bytes, but the directory has no such entry\n",
                block->index,
                block->len);
            continue;
        }

        itso_parse_ipe(product, block->data, block->len, card.sector_size);
        printf(
            "  E%u  %s (TYP %u.%u), %zu bytes, rev %u, bitmap 0x%02X, %s\n",
            product->dir_index,
            itso_typ_name(product->typ),
            product->typ,
            product->ptyp,
            block->len,
            product->format_rev,
            product->bitmap,
            product->body_parsed ? "parsed" : "NOT PARSED");
        printf(
            "      %-12s %u%s\n",
            "owner OID",
            product->oid,
            product->oid_extended ? " (extended)" : "");
        printf("      %-12s %s\n", "status", itso_status_name(product->status));
        show_money("balance", &itso_product_purse(product)->balance);
        show_location("from", &product->from);
        show_location("to", &product->to);
        /* Every value record, not only the live one: the others are what the
         * product looked like before the last few transactions, and on a saved
         * card they reach further back than the card itself keeps. */
        for(uint8_t v = 0; v < product->value_history_count; v++) {
            const ItsoValueRecord* record = &product->value_history[v];
            printf(
                "      %-12s TS#%-4u %-16s %s",
                v ? "" : "records",
                record->ts,
                itso_transaction_name(record->txn),
                fmt_dts(record->dts));
            /* has_count first: the counter and the balance share their room. */
            if(record->has_count) {
                printf(
                    "  %s %lu",
                    itso_count_name(product->count_kind) ?: "count",
                    (unsigned long)record->count);
            } else if(record->amount.valid) {
                char money[24];
                itso_format_money(&record->amount, money, sizeof(money));
                printf("  %s", money);
            }
            printf("%s\n", v ? "" : "  <- live");
        }
        if(product->value_group && !product->body_parsed)
            printf("      has a value record that could not be read\n");
        show_extras(product, block->data, block->len, card.sector_size);
    }

    if(replay_log_len) {
        printf("\nJourney log (%zu bytes)\n", replay_log_len);
        itso_parse_log(&card, replay_log, replay_log_len);
        printf("  %u tap(s)\n", card.tap_count);
        for(uint8_t i = 0; i < card.tap_count; i++) {
            const ItsoTap* tap = &card.taps[i];
            printf(
                "  tap %u: %s, DTS %u\n",
                i,
                itso_transaction_name(tap->transaction_type),
                tap->dts);
            show_money("fare", &tap->amount);
            show_location("origin", &tap->origin);
            show_location("destination", &tap->destination);
            show_location("route", &tap->route);
            if(tap->has_writer)
                printf(
                    "      %-12s ISAM %08lX (operator %u)\n",
                    "written by",
                    (unsigned long)tap->writer_isam,
                    itso_isam_oid(tap->writer_isam));
            if(tap->has_entry_oid)
                printf(
                    "      %-12s operator %u, IIN index %u\n",
                    "entered at",
                    tap->entry_oid,
                    tap->entry_iin_index);
        }
    }

    printf("\nDecoded without crashing. Compare the fields above against what the "
           "card should say.\n");
    return 0;
}
