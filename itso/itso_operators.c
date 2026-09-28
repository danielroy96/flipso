/**
 * @file itso_operators.c
 * @brief The built-in ITSO operator name and card branding table.
 *
 * ITSO issues an Operator Identification Number (OID) to each licensed member.
 * The register that maps those numbers to organisations is published by ITSO to
 * its members only - the DfT's ENCTS guidance points operators at a list "available
 * for licensed members on the ITSO website" - so there is no public source to
 * compile a complete table from.
 *
 * This table therefore holds only entries confirmed from public documentation or
 * from cards that have actually been read. Anything missing falls back to the
 * number, and users can add their own entries without rebuilding the app by
 * creating the operators file described in the README.
 *
 * Entries must stay sorted by OID: lookup is a binary search.
 */
#include "itso_operators.h"

#include <stddef.h>

typedef struct {
    uint16_t oid;
    const char* name;
    /* The name the scheme is sold under, where the operator issues a card under
     * one. Distinct from the operator name because the two are rarely the same
     * words: London Councils issues the Freedom Pass, and a card can say
     * "South West Trains Smart" years after the operator was renamed. NULL when
     * the operator owns products on other issuers' cards but brands none of its
     * own, or when we have not seen one of its cards. */
    const char* brand;
} ItsoOperatorEntry;

/*
 * ITSO allocates operator numbers from one namespace, but its members are a mix
 * of very different organisations: local authorities that issue concessionary
 * passes, train operating companies, and industry bodies. There is no public
 * register, so every entry here cites where it came from and can be overridden
 * by the user's own operators file.
 *
 * Provenance matters because a wrong name is worse than no name. An earlier
 * version of this table carried two mappings from a blog post about ITSO card
 * numbering; one of them was contradicted by the card that actually carries that
 * operator's products, so both were dropped. A brand is held to the same bar:
 * it is only filled in for an OID whose card has been read and whose printed
 * branding was checked against it, because the brand is the app's title bar and
 * a card titled with someone else's scheme is worse than one titled "ITSO Card".
 *
 * "RSPS3002" below is Rail Settlement Plan's "ITSO in National Rail"
 * specification, version 02-01, which uses real organisations in its worked
 * examples of ISAM IDs and shell reference numbers.
 *
 * "Watson 2019" is Harley Watson's Abertay dissertation on ITSO data integrity
 * (lobi.to/static/posts/papertickets/dissertation.pdf), whose table 1 lists
 * eighteen cards the author held, each with its issuer, scheme and the OID Smart
 * Ticket Checker read from it. The OIDs there are decimal, as here: its 0109,
 * 0196 and 0247 are this table's 109, 196 and 247. A scheme it names is used as
 * the brand only where it is the card's own name; "Smart", "Smartcard" and
 * "STNR" (Smart Ticketing on National Rail) describe the technology. Where that
 * left a gap, the brand is the one the operator sells the card under on its own
 * website, checked 2026-09-27.
 *
 * Entries must stay sorted by OID: lookup is a binary search.
 */
static const ItsoOperatorEntry itso_operator_table[] = {
    /* RSPS3002 appendix D.1, example ISAM ID. TfL owns products on other
     * issuers' cards; the Oyster card is not an ITSO shell at all. */
    {78, "Transport for London", NULL},
    {96, "Greater London", NULL}, /* read from an ENCTS concessionary pass */
    /* Shell owner of both a "South West Trains Smart" card and an "SWR Touch"
     * card, each read 2026-09-17. South West Trains was the precursor to South
     * Western Railway and the OID carried across the rebrand, so the shell says
     * nothing about which of the two brands is printed on the card in hand.
     * Deliberately branded as the current scheme: it is right for cards being
     * issued now and wrong only for legacy stock. The alternative was no brand
     * at all for either. */
    {109, "South Western Railway", "SWR Touch"},
    /* Watson 2019, a ScotRail card read on both DESFire and CMD2 media. The
     * scheme is recorded only as "Smartcard", so the brand names the issuer
     * too, as c2c's does. */
    {116, "ScotRail", "ScotRail Smartcard"},
    /* Watson 2019, the only card in its table carrying both tickets and an
     * ePurse, then TfGM's "get me there" card. TfGM has since rebranded its
     * network as the Bee Network and the card as the Bee Card. As with OID 109,
     * the OID is assumed to have carried across the rebrand and the brand is the
     * current one, so a legacy get me there card is titled as a Bee Card. */
    {125, "Bee Network", "Bee Card"},
    /* Watson 2019 read this OID from two Dundee cards: a OneScotland National
     * Entitlement Card on MIFARE Classic, which Flipso cannot read, and a
     * saltirecard on CMD2. Two schemes on one OID, so like OID 165 the brand is
     * the issuer, which is true of either card. */
    {130, "Dundee City Council", "Dundee City Council"},
    /* RSPS3002 appendix D.3, example POST SET address. Watson 2019 read it as the
     * shell owner of a "The Key" card issued by Govia, whose Govia Thameslink
     * Railway runs Southern - the same Go-Ahead scheme as OID 289 below. */
    {143, "Southern", "The Key"},
    /* Watson 2019, written there as "m-card"; this is how the scheme styles it.
     * The dissertation names the issuer "West Yorkshire TA"; the combined
     * authority is the body that runs MCard today. */
    {152, "West Yorkshire Combined Authority", "MCard"},
    /* Watson 2019, which calls the scheme "My Xplore" but also quotes an older
     * source calling the card "Discovr". Both are right: Xplore Dundee's own
     * news page says MyXplore replaced Discovr from 2 January 2017, and the
     * brand is the current one, as with OID 109. */
    {162, "Xplore Dundee", "MyXplore"},
    /* Shell owner of a Reading Buses card, read 2026-09-26, which also owns the
     * period ticket on it. Reading Buses is the trading name of Reading Transport
     * Ltd, the municipal operator; the council's own OID is 165 below. */
    {163, "Reading Buses", "Reading Buses"},
    /* Shell owner of an ENCTS concessionary pass, read 2026-09-19, whose issuer
     * the cardholder confirmed. The pass carries the national scheme's artwork
     * rather than the council's, so the brand is the issuer's name: it tells
     * the holder whose pass it is, where "ITSO Card" would tell them nothing. */
    {165, "Reading Borough Council", "Reading Borough Council"},
    /* Shell owner of an SPT Subway card, read 2026-09-17. The Subway's reusable
     * smartcard is the one ITSO scheme still on ISO 7816 media (CMD2).
     *
     * Watson 2019 read the same OID from a Subway card and names the scheme
     * "Bramble". The brand stays as the card in hand has it. Watson also gives
     * 196 for a Subway paper ticket, where the one read here (OID 8323 below)
     * had the generic compact-shell 8189 - an older ticket, or Smart Ticket
     * Checker showing a different field; not settled. */
    {196, "SPT (Strathclyde)", "SPT Subway"},
    /* Shell owner of a Freedom Pass, read 2026-09-17. The scheme is run by
     * London Councils for the London boroughs; the name is from that, the OID
     * and the brand are from the card. */
    {226, "London Councils", "Freedom Pass"},
    /* A shared OID for central products rather than one operator's, identified
     * via Smart Ticket Checker, 2026-09-18. Read as the owner of the ITSO ID
     * product on a Southeastern "The Key" card, which is what a shared south-east
     * body would own: the purse on that same card belongs to 8000, ITSO STR.
     * Related to but distinct from OID 1136 below, which RSPS3002 gives as SEFT
     * itself. No brand: it issues no card of its own.
     *
     * This entry previously read "South Western Railway", on the strength of a
     * comment that actually described OID 109. Hence the provenance above.
     *
     * Watson 2019 read it as the shell owner of a Greater Anglia smartcard.
     * That fits a shared SEFT OID - Greater Anglia is inside the SEFT area - and
     * is exactly why there is no brand: the shell is not one operator's. */
    {246, "SEFT Central Products", NULL},
    /* RSPS3002 appendix D.2, "a C2C live Shell ISRN"; Watson 2019 read it from
     * a c2c Smart card. */
    {247, "c2c", "c2c Smart"},
    /* Watson 2019 records these three National Rail operators' cards only as
     * STNR. Each operator's site sells it as its own "Smartcard"; Chiltern's
     * says "Chiltern Railways Smartcard", shortened so the header fits. */
    {262, "Chiltern Railways", "Chiltern Smartcard"},
    {285, "CrossCountry", "CrossCountry Smartcard"},
    /* Shell owner of a GWR Touch card, read 2026-09-28, which also owns the ITSO
     * ID product on it. The season tickets on the same card belong to OID 246,
     * the shared SEFT one, so this OID names the card and not its tickets. The
     * brand is the one printed on it. */
    {287, "Great Western Railway", "GWR Touch"},
    {288, "TransPennine Express", "TPE Smartcard"},
    /* Shell owner of a Southeastern "The Key" card, read 2026-09-18. The Key is
     * Go-Ahead's scheme rather than one operator's, so other Go-Ahead operators
     * issue Key cards of their own under their own OIDs - unlike OID 109 above,
     * where one OID spans two brands, here one brand spans several OIDs. Naming
     * the operator rather than the group is what makes the product lines read
     * correctly; the brand is what is printed on the card. */
    {289, "Southeastern", "The Key"},
    {303, "McGill's", "Go! Smart"}, /* Watson 2019, a CMD2 card */
    /* Watson 2019, a CMD2 "Tripper" card, which it lists under SPT. SPT's own
     * site says Tripper is not delivered by SPT: it is the Glasgow Bus
     * Alliance's multi-operator bus ticket, issued by Glasgow Smartzone
     * Ticketing Ltd (glasgowtripper.co.uk terms) and sold as Glasgow Tripper.
     * The OID is from the card; the name is from who issues that card. */
    {313, "Glasgow Smartzone Ticketing", "Glasgow Tripper"},
    {1136, "SEFT", NULL}, /* RSPS3002 appendix D.1, South East Flexible Ticketing */
    /* ITSO's own National Rail stored travel rights scheme - STR is the TS 1000
     * name for a purse - named for what a holder would call it. */
    {8000, "National Rail purse", NULL},
    /* No provenance was ever recorded for this entry, and the Freedom Pass it
     * claimed is OID 226 above, read from the card. Unverified rather than
     * deleted, because an OID this far into the range is unlikely to have been
     * invented outright - but no brand, because the brand is the title bar.
     *
     * Not yet distinguishable from OID 96: the ID product on a Freedom Pass
     * reports "Greater London", which is the name both entries carry, and the
     * product screen shows a named operator without its number. Whichever of
     * the two that product is, 96 already has provenance ("read from an ENCTS
     * concessionary pass") and this one still has none. */
    {8288, "Greater London", NULL},
    /* Owner of the Period ticket on an SPT Subway paper day-ticket, read
     * 2026-09-27. The ticket is a compact-shell Type 2 tag (CMD4), whose shell
     * OID is the generic 8189 reserved for compact shells, so this - the product
     * owner, in the extended range - is the only OID on it that names the
     * operator, and it brands the ticket in the shell owner's place (see
     * itso_card_issuer_oid()). The brand is the one the Subway's own smartcard
     * carries. */
    {8323, "SPT (Strathclyde)", "SPT Subway"},
};

/*
 * Note on roles: TS 1000-2 Annex B gives an organisation different numbers for
 * different roles, so Transport for London also appears as 0x6009 (24585) as a
 * service operator. That range only ever occurs in ISAM IDs and POST addresses,
 * never in the shell or a directory entry, so it is not listed here.
 */

static const ItsoOperatorEntry* itso_operator_entry(uint16_t oid) {
    size_t low = 0;
    size_t high = sizeof(itso_operator_table) / sizeof(itso_operator_table[0]);

    while(low < high) {
        size_t mid = low + (high - low) / 2;
        uint16_t candidate = itso_operator_table[mid].oid;
        if(candidate == oid) {
            return &itso_operator_table[mid];
        } else if(candidate < oid) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    return NULL;
}

const char* itso_iin_name(uint32_t iin) {
    /* ITSO holds a single registered six-digit issuer number, used by every
     * ITSO shell (TS 1000-2 clause 4.1.4.1). */
    return (iin == 633597) ? "ITSO" : NULL;
}

const char* itso_operator_name(uint16_t oid) {
    const ItsoOperatorEntry* entry = itso_operator_entry(oid);
    return entry ? entry->name : NULL;
}

const char* itso_operator_brand(uint16_t oid) {
    const ItsoOperatorEntry* entry = itso_operator_entry(oid);
    return entry ? entry->brand : NULL;
}
