/**
 * @file flipso_format_card.c
 * @brief The card screen: its number, expiry, issuer, chip and layout.
 */
#include "flipso_format_i.h"

/** The ISO/IEC 7816-6 manufacturer of a Type A UID's first byte, for the two
 *  makers of the chips ITSO's Type 2 media name, or NULL. */
static const char* flipso_chip_maker(uint8_t code) {
    switch(code) {
    case 0x04:
        return "NXP";
    case 0x05:
        return "Infineon";
    default:
        return NULL;
    }
}

/** "6-13, 15" for the pages set in @p pages (bit n is page n), or "None". */
static void flipso_cat_pages(FuriString* out, uint16_t pages) {
    if(!pages) {
        furi_string_cat(out, "None");
        return;
    }
    const char* sep = "";
    for(uint8_t page = 0; page < 16; page++) {
        if(!(pages & (1u << page))) continue;
        uint8_t last = page;
        while(last + 1 < 16 && (pages & (1u << (last + 1))))
            last++;
        if(last == page) {
            furi_string_cat_printf(out, "%s%u", sep, page);
        } else {
            furi_string_cat_printf(out, "%s%u-%u", sep, page, last);
        }
        sep = ", ";
        page = last;
    }
}

static void flipso_cat_isrn(FuriString* out, const ItsoCard* card) {
    static const uint8_t groups[] = {6, 4, 4, 4};
    size_t pos = 0;
    for(size_t g = 0; g < COUNT_OF(groups); g++) {
        if(g) furi_string_push_back(out, ' ');
        for(uint8_t i = 0; i < groups[g] && card->isrn[pos]; i++) {
            furi_string_push_back(out, card->isrn[pos++]);
        }
    }
}

/** What the card's Technical page says: its media, layout, keys and directory. */
static void
    flipso_cat_card_technical(FuriString* out, const FlipsoFormat* f, const ItsoCard* card) {
    const uint16_t issuer = itso_card_issuer_oid(card);
    /* FVC is the number of the customer media definition the shell follows. */
    switch(card->fvc) {
    case 2:
        furi_string_cat(out, "Card type: Smartcard (CMD2)\n");
        break;
    case 4:
        furi_string_cat(out, "Card type: Ultralight (CMD4)\n");
        break;
    case 7:
        furi_string_cat(out, "Card type: DESFire (CMD7)\n");
        break;
    case 9:
        furi_string_cat(out, "Card type: NTAG (CMD9)\n");
        break;
    case 10:
        furi_string_cat(out, "Card type: Ultralight EV1 (CMD10)\n");
        break;
    case 12:
        furi_string_cat(out, "Card type: DESFire (CMD12)\n");
        break;
    default:
        furi_string_cat_printf(out, "Card type: CMD%u\n", card->fvc);
        break;
    }
    furi_string_cat_printf(out, "Operator number: %u\n", issuer);
    /* Every ITSO shell carries ITSO's own issuer number, so this only earns a
     * line when it is something else. */
    if(!itso_iin_name(card->iin)) {
        furi_string_cat_printf(out, "Issuer network: %06lu\n", (unsigned long)card->iin);
    }
    furi_string_cat_printf(out, "Layout version: %u\n", card->format_rev);
    /* The shell's own checksum, which is the only thing on the card Flipso can
     * actually verify: the data groups are sealed with keys it does not have,
     * so everything else on these screens is reported on the card's word. Both
     * outcomes are stated, and a mismatch shows its numbers, because the useful
     * thing to do with one is to report the card. TS 1000-2 clause 4.1.15. */
    if(card->secrc_checked) {
        if(card->secrc_valid) {
            furi_string_cat(out, "Checksum: Correct\n");
        } else {
            furi_string_cat_printf(
                out,
                "Checksum: Wrong\n  Stored: %04X\n  Worked out: %04X\n",
                card->secrc_stored,
                card->secrc_computed);
        }
    }
    /* A compact shell stores none of the key set, geometry or directory group:
     * the CMD fixes them (TS 1000-10 table 42), so they are not the card's to
     * report, and it has no directory sequence to count. */
    if(card->shell_compact) {
        furi_string_cat(out, "Layout: Compact shell\n");
        /* A compact shell stores no number: the one it has is implied by the
         * CMD (TS 1000-10 table 42), so every paper ticket of its kind shows
         * the same one, and it is not the ticket's identity - the chip's UID
         * is. Its OID is the generic one every compact shell carries, which is
         * why the operator above, taken from the ticket's product, has a
         * different number from the one in the card number. */
        furi_string_cat(out, "  Implied card number: ");
        flipso_cat_isrn(out, card);
        furi_string_push_back(out, '\n');
        furi_string_cat_printf(out, "  Shell operator number: %u\n", card->oid);
    } else {
        furi_string_cat_printf(out, "Key set: %u, version %u\n", card->ksc, card->kvc);
        furi_string_cat_printf(
            out,
            "Layout: %u sector%s of %u bytes\n",
            card->sector_count,
            card->sector_count == 1 ? "" : "s",
            card->sector_size);
        furi_string_cat_printf(
            out, "Directory: %u slot%s\n", card->dir_entries, card->dir_entries == 1 ? "" : "s");
    }
    /* The directory sequence number counts every change made to the shell,
     * modulo 256; it is also how a CMD2 card's two directory copies are told
     * apart. */
    if(card->dir_valid && !card->shell_compact) {
        furi_string_cat_printf(out, "Update count: %u\n", card->dir_sequence);
        if(card->dir_instance_valid && card->shell_iteration) {
            /* INS#: bumped to bring a stopped card back into use. */
            furi_string_cat_printf(out, "Times reinstated: %u\n", card->shell_iteration);
        }
    }
    /* The directory is rewritten by every transaction, so the machine that
     * last sealed it is the last one to change anything on the card
     * (TS 1000-2 table 8, annex B). */
    if(card->dir_instance_valid) {
        flipso_cat_machine(out, f, "", "Last updated by machine", card->dir_isam);
        /* KID: the version of the key the directory's seal is made with. */
        furi_string_cat_printf(out, "Directory seal key version: %u\n", card->dir_kid);
    }
}

/**
 * A card's first page: whether the issuer still honours it, its number, its
 * expiry and who issued it. Not a paper ticket's, whose shell stores no
 * number or expiry and whose state is its one product's.
 */
static void flipso_cat_card_page(FuriString* out, const FlipsoFormat* f, const ItsoCard* card) {
    /* The blocking indicator is a property of the whole shell, so it comes
     * before anything else on the screen: once it is set a machine rejects the
     * card, however valid the products further down still look.
     * TS 1000-2 clause 5.1.2. */
    flipso_cat_page(out, card->shell_blocked ? FlipsoIconWarning : FlipsoIconCard, "Card");
    if(card->shell_blocked) {
        furi_string_cat(
            out,
            "This card has been stopped by its issuer. Readers will reject it, "
            "even where the products on it are still in date.\n");
    }

    /* The number stands alone under the page's title: with a label in front,
     * its eighteen digits break across two rows. */
    flipso_cat_isrn(out, card);
    furi_string_push_back(out, '\n');
    if(!card->isrn_check_ok) furi_string_cat(out, "  Check digit: Does not match\n");

    /* The good case is stated rather than left to silence, because nothing else
     * on the screen separates a card the issuer is happy with from one whose
     * directory was never read. */
    if(card->shell_blocked) {
        furi_string_cat(out, "Status: Blocked\n");
    } else if(itso_card_retired(card)) {
        furi_string_cat(out, "Status: Retired\n");
    } else if(!itso_date_open(card->expiry) && itso_date_expired(card->expiry, f->now)) {
        furi_string_cat(out, "Status: Expired\n");
    } else if(card->dir_valid) {
        furi_string_cat(out, "Status: Active\n");
    }
    flipso_cat_expiry(out, "", "Expires", "Expired", card->expiry, f->now);
    /* A blocked or retired card has no time left to use, whatever its date. */
    if(!card->shell_blocked && !itso_card_retired(card)) {
        flipso_cat_time_left(out, "  ", card->expiry, f->now);
    }

    /* The shell owner is the operator that issued the card and so the one that
     * brands it. An unnamed one shows its number in the "Unknown (1234)" that
     * stands in for the name, because the number is what a user needs to add
     * their card to the operators file; a named one's is under Technical. */
    flipso_cat_operator(out, f, "", "Operator", itso_card_issuer_oid(card));
    if(card->mcrn_present && card->mcrn[0]) {
        furi_string_cat_printf(out, "Card reference: %s\n", card->mcrn);
    }
}

void flipso_format_card(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const char* saved_name,
    bool demo,
    ItsoUnixTime read_at) {
    /* A paper ticket is its one product: its state and operator are that
     * product's, which the ticket's own screen leads with, so this screen is
     * its chip and the codes behind it, and opens on the chip. */
    if(!card->shell_compact) flipso_cat_card_page(out, f, card);

    /* What the chip says about itself, which only a live DESFire read asks. */
    if(f->media && f->media->valid) {
        flipso_cat_page(out, FlipsoIconChip, "Chip");
        flipso_cat_chip_summary(out, f->media);
    } else if(card->chip_uid_valid) {
        /* A Type 2 tag's serial is in its own page memory, saved with the rest,
         * and is the only thing that tells one paper ticket from another. */
        flipso_cat_page(out, FlipsoIconChip, "Chip");
        /* A full-shell tag's chip is named by its media definition. */
        const char* chip = itso_type2_chip_name(card);
        if(chip) furi_string_cat_printf(out, "Chip: %s\n", chip);

        /* A CMD9's one-way count of transactions (TS 1000-10 table 107). Each
         * value record written must be numbered at least this high, which is
         * what stops an old copy of the card being written back; the count
         * starts at 1 and 16 retires the card, so a product on it can be used
         * fifteen times less what has been counted. Next to the chip, because
         * it is the chip's life that runs out. */
        if(card->chip_abacus_valid) {
            if(itso_card_retired(card)) {
                furi_string_cat(out, "Uses left: None, retired\n");
            } else {
                furi_string_cat_printf(
                    out, "Uses left: %u\n", card->chip_abacus < 15 ? 15 - card->chip_abacus : 0);
            }
            furi_string_cat_printf(out, "  Abacus: %u of 16\n", card->chip_abacus);
        }

        furi_string_cat(out, "UID: ");
        flipso_cat_hex(out, card->chip_uid, sizeof(card->chip_uid));
        furi_string_push_back(out, '\n');
        /* The first byte of a 7-byte UID is the maker's ISO/IEC 7816-6 code. */
        const char* maker = flipso_chip_maker(card->chip_uid[0]);
        if(maker) {
            furi_string_cat_printf(out, "Manufacturer: %s\n", maker);
        } else {
            furi_string_cat_printf(out, "Manufacturer: Unknown (%02X)\n", card->chip_uid[0]);
        }
        furi_string_cat_printf(out, "Memory: %u bytes\n", card->chip_memory_len);

        /* The lock bits: which pages the issuer made read-only. ITSO says which
         * a CMD4 must lock once it is issued (TS 1000-10 clause 5.10.2), so a
         * ticket whose data could still be rewritten says so. */
        uint16_t locked = itso_type2_locked_pages(card->chip_lock);
        furi_string_cat(out, "Locked pages: ");
        flipso_cat_pages(out, locked);
        furi_string_push_back(out, '\n');
        if(card->shell_compact) {
            uint16_t unlocked = ITSO_CMD4_LOCKED_PAGES & ~locked;
            flipso_cat_flag(out, "  ", "As ITSO requires", !unlocked);
            if(unlocked) {
                furi_string_cat(out, "  Still writable: ");
                flipso_cat_pages(out, unlocked);
                furi_string_push_back(out, '\n');
            }
        } else if(chip) {
            /* CMD9 and CMD10 only recommend it (TS 1000-10 clause 10.23.1): the
             * shell never changes, so its pages are locked, and the directory
             * that starts on page 12 is left writable. */
            uint16_t unlocked = ITSO_TYPE2_FULL_LOCKED_PAGES & ~locked;
            flipso_cat_flag(out, "  ", "Shell locked", !unlocked);
        }
        /* The block-lock bits, which fix the lock bits themselves. */
        furi_string_cat(out, "Lock bits frozen: ");
        flipso_cat_pages(out, itso_type2_frozen_pages(card->chip_lock));
        furi_string_push_back(out, '\n');
    }

    /* Where this came from, for a card opened off the SD card. The read time
     * matters more than it looks: a balance is only true as of the tap that
     * wrote it, and a saved card carries no hint of its own age otherwise -
     * so this comes before the codes, not after them. */
    if(saved_name) {
        /* A demo card is a saved-card file too, but calling it saved would
         * say the user kept it. */
        flipso_cat_page(
            out, demo ? FlipsoIconCard : FlipsoIconSave, demo ? "Demo card" : "Saved card");
        furi_string_cat_printf(out, "Name: %s\n", saved_name);
        if(read_at) {
            furi_string_cat(out, "Read: ");
            flipso_cat_time(out, read_at);
            furi_string_push_back(out, '\n');
        }
    }

    flipso_cat_page(out, FlipsoIconCode, "Technical");
    flipso_cat_card_technical(out, f, card);
}
