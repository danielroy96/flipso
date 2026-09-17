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
    {143, "Southern", NULL}, /* RSPS3002 appendix D.3, example POST SET address */
    /* Shell owner of an SPT Subway card, read 2026-09-17. The only CMD2 card in
     * the table: the Subway is the one ITSO scheme still on ISO 7816 media. */
    {196, "SPT (Strathclyde)", "SPT Subway"},
    /* Shell owner of a Freedom Pass, read 2026-09-17. The scheme is run by
     * London Councils for the London boroughs; the name is from that, the OID
     * and the brand are from the card. */
    {226, "London Councils", "Freedom Pass"},
    /* Provenance recorded only as "read from a South West Trains smartcard",
     * which is OID 109 above. That comment therefore describes a different
     * entry, leaving this one with no provenance of its own: no brand until a
     * card confirms what it is. */
    {246, "South Western Railway", NULL},
    {247, "c2c", NULL}, /* RSPS3002 appendix D.2, "a C2C live Shell ISRN" */
    {1136, "SEFT", NULL}, /* RSPS3002 appendix D.1, South East Flexible Ticketing */
    {8000, "ITSO STR (National Rail)", NULL},
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
