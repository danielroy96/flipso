/**
 * @file itso_space_saving.c
 * @brief TYP 27, 28 and 29: the Space Saving IPEs a CMD4 paper ticket carries (TS 1000-5 clauses 2.14-2.16).
 *
 * Offsets cite ITSO TS 1000 clause numbers so they can be checked against the
 * published specification. Every accessor is bounds checked: card data is
 * attacker-controlled as far as this app is concerned, and a malformed card
 * must produce an empty result rather than a crash.
 */
#include "itso_ipe_i.h"

#include <string.h>
#include <stdlib.h>

/**
 * Decode a Space Saving IPE's area element: TYP 27's 100-bit GeoValidity (table
 * 50) or TYP 28/29's 68-bit AreaValidity (tables 53 and 57). Both start at bit
 * 60 of the dataset and share one coding - a top nibble that, when non-zero, is
 * a LocDefType-200; when zero, the bit below it choosing a reference fare code or
 * an actual fare value, held in the rest of the element.
 */
static void itso_space_area(ItsoSpaceSaving* ss, const uint8_t* ds, uint8_t bits) {
    uint8_t locdef = (uint8_t)itso_bits(ds, 60, 4);
    if(locdef == 0) {
        ss->area_kind = itso_bits(ds, 64, 1) ? ItsoAreaFareValue : ItsoAreaFareCode;
        /* The code or value fills 63 or 95 bits; the low 32 are kept, which any
         * fare and any fare table an operator could print will fit. */
        ss->area_value = itso_bits(ds, 60 + bits - 32, 32);
    } else {
        /* The element is then a LOC4 (TYP 27's 100 bits) or a LOC3 (TYP 28 and
         * 29's 68) of LocDefType 200+n: the nibble, then four-byte slots for
         * an origin, a destination and in a LOC4 a via (TS 1000-1 clauses
         * 4.2.4.2.3-4), which start on a byte here. */
        ss->area_kind = ItsoAreaLocation;
        ss->area_value = (uint32_t)locdef + 200;
        itso_parse_loc_fixed((uint8_t)ss->area_value, ds + 8, bits == 100 ? 3 : 2, ss->area);
    }
}

/**
 * TYP 29's ScaledQtyBackup (tables 58a and 58b): each bit set stands for m
 * rides or coupons used, so the ones left unset say how many remain - to
 * within m - should QtyRemaining be lost to a torn write.
 */
static void
    itso_space_backup(ItsoSpaceSaving* ss, const uint8_t* ds, uint8_t bitmap, uint8_t code) {
    static const uint16_t step[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 20, 32, 64, 128, 256};
    /* Bit 3 says the backup is in use; a ScalingFactor of 0 says it is not. */
    if(!(bitmap & (1 << 3)) || code == 0) return;
    uint32_t bits = itso_bits(ds, 192, 32);
    uint8_t unset = 32;
    for(; bits; bits &= bits - 1) {
        unset--;
    }
    ss->has_backup = true;
    ss->backup_step = step[code & 0x0F];
    ss->backup_count = (uint16_t)(ss->backup_step * unset);
}

/** LastUseDTS, where the type has one. Zero is "never used" at creation (and
 *  a DTS of zero decodes to 2028, so it must not be shown as a time). */
static void itso_space_last_use(ItsoSpaceSaving* ss, const uint8_t* ds) {
    ss->has_last_use = true;
    ss->last_use_dts = itso_bits(ds, 168, 24);
}

/**
 * Where a TYP 29 was last used: a 4-byte LOCE whose LocDefType (200-203) the
 * TYP29UsageRecCode gives (table 58). Held in @c from, rendered through the same
 * location decoder a full ticket's are.
 *
 * SPT's Subway gates record a bus fare stage (202) whose stage number is the
 * station, 1-15, and whose machine number is the gate - as Ryan Murphy found,
 * with Partick at 2 and Hillhead at 4. On an SPT ticket the stage is shown as the
 * station it names; anywhere else it stays a fare stage. The location is
 * rendered later, without the product, so it is marked here as the OID says.
 */
static void itso_space_usage_place(ItsoProduct* product, uint8_t def_type, const uint8_t* loce) {
    const uint8_t loc2[7] = {def_type, loce[0], loce[1], loce[2], loce[3], 0, 0};
    itso_parse_location(loc2, sizeof(loc2), ItsoLocStructLoc2, &product->from);
    if(itso_is_blank(loce, 4)) product->from.valid = false; /* Never used. */
    product->from.subway_station = def_type == 202 && product->oid == ITSO_OID_SPT_SUBWAY_TICKET;
}

/*
 * Each dataset is a fixed sequence of fields at known bit offsets, unlike a full
 * IPE where a bitmap says which optional elements are present. A CMD4 splits it
 * across three page regions, which itso_cmd4.c reassembles before it gets here,
 * so the offsets below are into the one contiguous dataset. All four layouts -
 * TYP 27, 28, and TYP 29 at revisions 1 and 2 - fill 16 static and 12 dynamic
 * bytes exactly, and agree on the first 31 bits and on where the pass flags and
 * area sit.
 *
 * TYP 27 was confirmed field for field against a real SPT Subway day ticket read
 * 2026-09-27, and TYP 29 revision 1 against Ryan Murphy's published dump of 21
 * Subway singles and returns. TYP 28 and TYP 29 revision 2 follow the spec alone.
 * The elements a full ticket also has go into @c product->ticket so they render
 * through the shared path.
 */
void itso_parse_space_saving(ItsoCard* card, ItsoProduct* product, const uint8_t* ds) {
    /* A CMD4 carries only these three (TS 1000-10 clause 5.6). Any other type in
     * the directory entry is left as the directory entry describes it, rather
     * than read through a layout that is not its own. */
    if(product->typ != ItsoTypPeriodCompact && product->typ != ItsoTypCarnet &&
       product->typ != ItsoTypMultiUse) {
        return;
    }

    /* TYP 27 and 28 define revision 1 only, TYP 29 revisions 1 and 2. A card
     * claiming another is left as a product decoded from its directory entry. */
    uint8_t rev = (uint8_t)itso_bits(ds, 12, 4);
    bool multi_leg = product->typ == ItsoTypMultiUse && rev == 2;
    if(rev != 1 && !multi_leg) return;

    /* Allocated here, the one place a card comes to need it: only a CMD4 carries
     * a Space Saving IPE. Without the room the product stays as its directory
     * entry describes it. */
    if(!card->space) card->space = malloc(sizeof(ItsoSpaceSaving));
    if(!card->space) return;
    ItsoSpaceSaving* ss = card->space;
    memset(ss, 0, sizeof(ItsoSpaceSaving));
    product->space_saving = true;
    product->body_parsed = true;
    product->format_rev = rev;
    product->bitmap = (uint8_t)itso_bits(ds, 6, 6);

    ItsoTicketTerms* t = &product->terms.ticket;
    t->valid = true;
    t->issue_date = (ItsoDate)itso_bits(ds, 16, 14); /* IssueDate. */
    ss->euro = itso_bits(ds, 30, 1);

    /* Bits 32-39 are PassbackTime and the payment method on TYP 27 and 28; TYP 29
     * spends the same eight bits differently at each revision. PassbackTime is
     * kept even at zero, which means the reader's own rule applies. */
    if(multi_leg) {
        product->has_passback = true;
        product->passback = (uint8_t)itso_bits(ds, 32, 4);
        ss->max_daily_journeys = (uint8_t)itso_bits(ds, 36, 4);
        t->max_transfers = (uint8_t)itso_bits(ds, 40, 4);
    } else {
        if(product->typ != ItsoTypMultiUse) {
            product->has_passback = true;
            product->passback = (uint8_t)itso_bits(ds, 32, 4);
        }
        t->paid_mop = (uint8_t)itso_bits(ds, 36, 4);
        t->amount_paid.value = (int32_t)itso_bits(ds, 40, 16); /* AmountPaid, VALI, pence. */
        t->amount_paid.currency = ss->euro ? 1 : 0;
        t->amount_paid.valid = true;
    }

    /* TYP27/28/29PassFlags share one definition (tables 49, 52 and 56). */
    ss->flags = (uint8_t)itso_bits(ds, 56, 4);
    t->travel_class = (ss->flags & ITSO_SS_FIRST_CLASS) ? 1 : 2;

    switch(product->typ) {
    case ItsoTypPeriodCompact: {
        /* Table 48. Bit 31 is the child flag; GeoValidity runs on into dynamic
         * memory, 100 bits in all. */
        if(itso_bits(ds, 31, 1)) {
            t->children = 1;
        } else {
            t->adults = 1;
        }
        itso_space_area(ss, ds, 100);
        /* Event1 and Event2: two EventTypeCodes, which the spec does not order
         * or otherwise explain, so both are kept and shown as they stand. */
        ss->has_events = true;
        ss->event1 = (uint8_t)itso_bits(ds, 160, 4);
        ss->event2 = (uint8_t)itso_bits(ds, 164, 4);
        itso_space_last_use(ss, ds);
        t->photocard = itso_bits(ds, 192, 24); /* PhotocardNumber, 0 if none. */
        /* TYP27ExpiryDate: days to subtract from the directory expiry. Zero leaves
         * the directory's own date standing. */
        uint8_t expiry_offset = (uint8_t)itso_bits(ds, 216, 8);
        if(expiry_offset && product->expiry > expiry_offset) {
            product->expiry = (ItsoDate)(product->expiry - expiry_offset);
        }
        break;
    }

    case ItsoTypCarnet: {
        /* Table 51: a carnet of up to eight day passes (clause 2.15.2). Six
         * one-time-programmable ticks record the days used; issue and expiry day
         * each have a flag instead of a tick. */
        itso_space_area(ss, ds, 68);
        itso_space_last_use(ss, ds);
        uint8_t unused = 0;
        for(uint8_t i = 0; i < sizeof(ss->carnet_ticks); i++) {
            ss->carnet_ticks[i] = (uint8_t)itso_bits(ds, 192 + i * 5, 5);
            if(ss->carnet_ticks[i] == 0) unused++;
        }
        ss->carnet_issue_day = itso_bits(ds, 222, 1);
        ss->carnet_expiry_day = itso_bits(ds, 223, 1);
        /* The expiry-day pass spends no tick, so it is one more left until the
         * day it is for - and on that day the product's own expiry takes over.
         * The issue-day pass is not counted: it is for the day the carnet was
         * bought, which has gone by the time anyone is counting what is left. */
        if(ss->carnet_expiry_day) unused++;
        product->count_kind = ItsoCountTickets;
        product->count = unused;
        break;
    }

    case ItsoTypMultiUse:
        itso_space_area(ss, ds, 68);
        if(!multi_leg) {
            /* Table 55: a carnet of single tickets, or coupons. QtyRemaining counts
             * up from 8191 minus the number bought, so what is left is the
             * difference. */
            product->count_kind = itso_bits(ds, 31, 1) ? ItsoCountCoupons : ItsoCountRides;
            product->count = 8191 - itso_bits(ds, 147, 13);
            itso_space_backup(ss, ds, product->bitmap, (uint8_t)itso_bits(ds, 32, 4));
            uint8_t code = (uint8_t)itso_bits(ds, 144, 3); /* TYP29UsageRecCode. */
            ss->usage_alighted = code & 0x01;
            itso_space_usage_place(product, (uint8_t)(200 + ((code >> 1) & 0x03)), ds + 20);
        } else {
            /* Table 55a: multi-leg journeys. QtyRemaining counts up from 255. */
            ss->journey_start_dts = itso_bits(ds, 128, 24);
            product->count_kind = ItsoCountRides;
            product->count = 255 - itso_bits(ds, 152, 8);
            itso_space_backup(ss, ds, product->bitmap, (uint8_t)itso_bits(ds, 44, 4));
            ss->transfers = (uint8_t)itso_bits(ds, 160, 4);
            ss->daily_journeys = (uint8_t)itso_bits(ds, 164, 4);
            itso_space_last_use(ss, ds);
        }
        break;

    default:
        break;
    }
}
