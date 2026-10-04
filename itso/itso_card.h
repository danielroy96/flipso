/**
 * @file itso_card.h
 * @brief Everything Flipso knows about one card, and the lifecycle of what it owns.
 */
#pragma once

#include "itso_log.h"
#include "ipe/itso_product.h"
#include "ipe/itso_space_saving.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Everything Flipso knows about one card. */
struct ItsoCard {
    /* --- ITSO Shell Environment Data Group (TS 1000-2 clause 4) --- */
    bool shell_valid;
    ItsoShellVerdict shell_reject; /**< Accepted, or which test rejected the shell. */
    char isrn[ITSO_ISRN_DIGITS + 1]; /**< 18-digit card number, IIN+OID+ISSN+check. */
    bool isrn_check_ok; /**< Luhn check digit verifies. */
    uint32_t iin; /**< Issuer Identification Number as a decimal value. */
    uint16_t oid; /**< Shell owner. */
    ItsoDate expiry; /**< EXP, the shell's expiry. */
    uint8_t format_rev; /**< ShellFormatRevision. */
    /** Format Version Code, the media definition: 2 = ISO 7816 CMD2, 4 = Ultralight
     *  CMD4, 7 = DESFire CMD7, 9 = NTAG CMD9, 10 = Ultralight EV1 CMD10, 12 = CMD12. */
    uint8_t fvc;
    /**
     * A Compact ITSO Shell (TS 1000-2 clause 4.2): the tiny page-based media that
     * cannot hold a full shell - a MIFARE Ultralight / Infineon my-d (CMD4), as
     * SPT's Glasgow Subway paper tickets use. Only ShellLength, ShellBitMap,
     * ShellFormatRevision and FVC are stored; the rest of the identity and the
     * geometry are implied by the CMD (TS 1000-10 table 42) and filled in here.
     *
     * The implied identity is fixed for every card of the CMD - IIN 633597, OID
     * 8189, ISSN 0 - so @c isrn is the media type's number, not a per-card one.
     * The card's real serial is its chip UID; a caller that needs to tell two of
     * these apart must use that rather than the ISRN.
     */
    bool shell_compact;
    /** A Type 2 tag's 7-byte chip serial (pages 0-1, less BCC0): the identity a
     *  compact shell lacks. Set by itso_parse_type2() and itso_parse_type2_tag(). */
    bool chip_uid_valid;
    uint8_t chip_uid[7];
    /** The tag's two static lock bytes (page 2, bytes 2-3); see
     *  itso_type2_locked_pages() and itso_type2_frozen_pages(). */
    uint8_t chip_lock[2];
    uint16_t chip_memory_len; /**< Bytes of page memory the tag gave up. */
    /**
     * A CMD9 card's Abacus (TS 1000-10 clause 10.24.4, table 107): the bits
     * set across the one-time-programmable bytes 1 and 3 of page 3. It counts
     * value-record transactions and can only go up, so it is what stops an old
     * copy of the card being written back; 16 means the card is retired. Set
     * by itso_parse_type2_tag().
     */
    bool chip_abacus_valid;
    uint8_t chip_abacus;
    /** KSC, Key Strategy Code (TS 1000-2 clause 4.1.6): which security algorithm
     *  guards the data groups on a card of this FVC. */
    uint8_t ksc;
    /** KVC, Key-set Version Code (clause 4.1.7): which version of that strategy's
     *  key set the card was issued with. */
    uint8_t kvc;
    uint8_t shell_len; /**< ShellLength, in blocks of ITSO_SHELL_BLOCK_LEN. */

    /* ITSO Shell Environment Checksum (TS 1000-2 clause 4.1.15).
     *
     * This is the only integrity check on the shell that can be made without
     * keys. The data groups are sealed rather than encrypted, and a seal is a
     * MAC over a key Flipso does not have, so Flipso can report what a card
     * says but never whether it has been tampered with. A CRC cannot tell you
     * that either - anyone rewriting a shell would recompute it - but it does
     * catch the thing that actually goes wrong here, which is a misread. */
    bool secrc_checked; /**< The shell was long enough to hold its checksum. */
    bool secrc_valid;
    uint16_t secrc_stored;
    uint16_t secrc_computed;
    uint8_t sector_size; /**< B */
    uint8_t sector_count; /**< S */
    uint8_t dir_entries; /**< e# */
    uint8_t sct_len; /**< SCTL */
    /** ShellBitMap bit 1: the shell carries an MCRN (TS 1000-2 table 3). */
    bool mcrn_present;
    /** MCRN, the Multi-application Card Reference Number (clause 4.1.13): a copy
     *  of the number the issuer of a multi-application card gave the card the
     *  shell is installed on. The digits, up to the F that ends them. */
    char mcrn[ITSO_MCRN_DIGITS + 1];

    /* --- Directory Data Group (TS 1000-2 clause 5) --- */
    bool dir_valid;
    bool shell_blocked;
    uint8_t dir_sequence;
    /* Directory InstanceID (TS 1000-2 table 8): the last ISAM to rewrite the
     * directory is the last device that changed anything on the card. */
    bool dir_instance_valid;
    uint8_t dir_kid;
    uint8_t shell_iteration; /**< INS#: hotlists name a shell by ISRN and this. */
    uint32_t dir_isam;

    /* --- Log Directory Entry (TS 1000-2 clause 8) --- */
    bool log_entry_valid;
    uint8_t log_dir_index; /**< Directory entry holding the log entry, 0 if none. */
    bool log_normal_mode; /**< LPF: normal mode references a transient ticket record. */
    uint8_t log_ptr; /**< Directory entry of the product used on the last tap. */
    uint8_t log_eei; /**< Entry/exit indicator: 0 = outside a closed system. */
    ItsoDts log_dts;
    uint8_t log_record_offset; /**< RO: next record to be written. */
    uint8_t log_passback; /**< PTLBM, minutes. */

    /* The directory's products first, in entry order, then any the card has
     * dropped since a file was written - so a screen that walks the array in
     * order shows the card before it shows the card's past.
     *
     * Allocated to fit: 268 bytes a product on the device, and the cap is
     * ITSO_MAX_CARD_PRODUCTS while a real card carries five or six, so a fixed
     * array spent most of the card's memory on slots nothing filled. Each
     * product's value records are allocated to fit behind it in turn. The card
     * owns both - see itso_card_init() - and every slot up to
     * @c product_capacity is either zeroed or a product whose history it owns,
     * which is what lets a reset free them all. */
    ItsoProduct* products;
    uint8_t product_count;
    uint8_t product_capacity; /**< Slots allocated behind @c products. */

    /** The Space Saving IPE's own elements, for the product whose @c space_saving
     *  is set; NULL on any other card. Only a paper ticket has one, so it is
     *  allocated when one decodes rather than carried by every card. */
    ItsoSpaceSaving* space;

    /* Newest first. Allocated to fit, like @c products: ITSO_MAX_TAPS slots of
     * 128 bytes is the cap, and a card straight off the reader has four at most. */
    ItsoTap* taps;
    uint8_t tap_count;
    uint8_t tap_capacity; /**< Slots allocated behind @c taps. */
};

/**
 * Make @p card an empty card that owns nothing, for storage that is not zeroed
 * already. A card in zeroed storage - static, calloc'd, or `= {0}` - starts that
 * way without this.
 *
 * The products and taps arrays, each product's value history, and a paper
 * ticket's @c space are the things an ItsoCard owns, which is what makes the
 * distinction matter: every other entry point may free them.
 */
void itso_card_init(ItsoCard* card);

/**
 * Empty @p card for the next decode, releasing its products, their value
 * histories, its taps and its space.
 *
 * @p card must be initialised - see itso_card_init(). A card copied by struct
 * assignment shares all of them with the original, so reset only one of them.
 */
void itso_card_reset(ItsoCard* card);

/** Release what @p card owns when it is finished with; the same as a reset. */
void itso_card_free(ItsoCard* card);

/**
 * True when two cards decoded to the same thing: every field, and the products,
 * their value records, the taps and space themselves rather than where they
 * happen to be allocated.
 */
bool itso_card_equal(const ItsoCard* a, const ItsoCard* b);

/**
 * Append a product the directory does not list, from the entry bytes that did.
 *
 * Only a saved card has these: the file kept the directory entry and the IPE
 * group of a product from a read that still found it, and the entry it sat in
 * has since been freed or taken by something else. The entry is decoded exactly
 * as itso_parse_directory() decodes a live one - it is the same five bytes - so
 * what follows can parse the group into it in the usual way.
 *
 * The array grows to take it, so the pointer returned - and any other pointer
 * into @c products - is good only until the next product is added.
 *
 * @param entry ITSO_DIR_ENTRY_LEN bytes of IPE Directory Entry.
 * @param index the 1-based directory position it occupied, for display.
 * @return the product, or NULL when there is no room for another. Status is
 *         left unknown: the chain terminator that says used or blocked lives in
 *         a Sector Chain Table that describes the card as it is now.
 */
ItsoProduct* itso_card_add_product(ItsoCard* card, const uint8_t* entry, uint8_t index);

/**
 * The operator that issued the card, and so the one whose branding titles it.
 *
 * Ordinarily the shell owner. A compact shell (CMD4, a paper ticket) is the
 * exception: its shell OID is the generic 8189 shared by every compact shell, so
 * the operator is named by the owner of the one product the ticket carries.
 */
uint16_t itso_card_issuer_oid(const ItsoCard* card);

/**
 * True for a CMD9 card whose Abacus has reached 16, "Retired" in TS 1000-10
 * table 107: a POST reads the Abacus to learn whether the card is retired, and
 * rejects one that is (clause 10.24.2).
 */
bool itso_card_retired(const ItsoCard* card);

#ifdef __cplusplus
}
#endif
