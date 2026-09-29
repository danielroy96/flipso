#!/usr/bin/env python3
"""Build demo ITSO cards as saved-card files, ready to copy to the Flipper.

A saved card is the raw blocks a read produced, so a file written here decodes
on the device exactly as a card would if it were tapped - through the same
parsers, on whatever build is running. That makes a synthetic card the only way
to see most of Flipso without owning the card that carries the feature: nobody
has a wallet with a loyalty IPE, a charge-to-account product, a blocked shell
and a revision 1 period ticket in it.

So these cards are built to cover the app rather than to be plausible wallets.
Between them they reach every screen, the CMD7, CMD2 and Type 2 (CMD4, CMD9,
CMD10) geometries, every IPE type the decoder names, every value record tail it
decodes, nearly every location renderer, and the parts of the model only a
saved card can hold - journeys and transactions that have rolled off the card,
and products the card no longer lists.

    tools/demo/build_demo_cards.py <outdir>
    tools/flipper/flipctl push <outdir>/<name>.flipso \\
        /ext/apps_data/flipso/cards/<name>.flipso

Nothing here is a real card: the ISRNs are in ITSO's registered issuer range
but the serials are invented, the holders are invented, and every seal is
filler. The operator numbers are real, because the branding and the product
lines are what is being demonstrated.
"""
import calendar
import datetime
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "test"))
from itso_build import (  # noqa: E402
    Bits, bcd, charge_tail, count_tail, date_stamp, dir_entry, directory, dts, isam,
    instance_and_seal, isrn, journey_tail, loc1, loc2, log_entry, loyalty_tail,
    naptan, pad_sector, period_tail, purse_tail, shell_dataset, sncode, sncode2,
    tt_record, tt_record_rev4, type2_full_page_memory, type2_page_memory, typ27_dataset,
    typ29_dataset, value_group, value_record, voucher_tail)

IIN = "633597"


def unix(y, mo, d, h=12, mi=0):
    return calendar.timegm(datetime.datetime(y, mo, d, h, mi).timetuple())


def group(ipe, sector_size, values=None, instance=None):
    """One product's sector chain, concatenated the way a read assembles it.

    The Value Record Data Group starts in the sector after the one the IPE data
    group ends in (clause 5.1.5.3 rule 2), so the padding here is not cosmetic:
    it is what puts the records where the decoder looks for them.
    """
    body = bytes(ipe.buf) + (instance if instance is not None else instance_and_seal())
    out = pad_sector(body, sector_size)
    if values is not None:
        out += pad_sector(values + instance_and_seal(), sector_size)
    return out


def bus_stage(oid, service, stage):
    """A LocDefType 209 body: an operator, an SNCODE service number packed into
    the twenty bits after it, and a fare stage (TS 1000-1 table 38).

    The SNCODE alphabet has no I, J, O, Q or U in it, so a service number that
    uses one cannot be written here - which is the point of packing it through
    the same table the decoder unpacks.
    """
    body = Bits(6)
    body.put(0, 16, oid)
    body.put(16, 20, sncode(service))
    body.buf[5] = stage
    return bytes(body.buf)


def history_block(last_seen, entry_index, entry, ipe_group):
    """A product the card has dropped, behind the header the file gives it.

    Four bytes of read time, the directory entry it held, and the five bytes
    that described it - because the directory in this file does not describe it
    any more, and a product the card has forgotten cannot say when it was last
    true.
    """
    return last_seen.to_bytes(4, "big") + bytes([entry_index]) + entry + ipe_group


def chip_block(uid, free_bytes):
    """What a DESFire EV1 with 4K of storage answers to GetVersion, then
    GetFreeMemory: the Chip block a live read saves (flipso_media.h)."""
    hardware = bytes([0x04, 0x01, 0x01, 0x01, 0x00, 0x18, 0x05])
    software = bytes([0x04, 0x01, 0x01, 0x01, 0x04, 0x18, 0x05])
    batch = bytes([0xBA, 0x44, 0x9C, 0x30, 0x10])
    week, year = 0x37, 0x19  # BCD: week 37 of 2019
    return (hardware + software + uid + batch + bytes([week, year]) +
            free_bytes.to_bytes(3, "little"))


def write_card(path, read_at, blocks):
    lines = ["Filetype: Flipso card", "Version: 1", f"Read at: {read_at}"]
    for key, data in blocks:
        lines.append(f"{key}: " + " ".join(f"{b:02X}" for b in data))
    with open(path, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    return path


# ====================================================================
# Card 1 - a Kent commuter's Southeastern "The Key"
#
# Eleven products, which is more than a real card would hold, chosen so that
# every full-shell IPE type is on one card: a purse with a journey in progress,
# an ID, a season ticket and a book of journeys, loyalty points, a charge-to-
# account, a voucher, a railcard, and three products in the states the list
# flags - blocked, expired and never read. The paper-only types (TYP 27-29) are
# on the paper tickets instead. The shell carries an MCRN, which the other
# smartcards leave out.
#
# The holder lives in Tunbridge Wells and commutes to London Bridge via
# Sevenoaks; the rest of what the card has done is short hops around Tonbridge
# on pay as you go, and a book of Highspeed journeys from Ashford International.
# ====================================================================
def card_the_key():
    B, S, E, SCTL = 64, 32, 12, 20
    ACTIVE, BLOCKED = S - 1, S - 2
    OID = "0289"                                 # Southeastern, brand "The Key"
    EXP = date_stamp(2031, 8, 31)

    shell = shell_dataset(IIN, OID, "0100001", fvc=7, ksc=4, kvc=1, expiry=EXP,
                          b=B, s=S, e=E, sctl=SCTL, mcrn="4920038815270264")

    # ---- E1: pay as you go, TS 1000-5 table 2
    purse = Bits(24)
    purse.put(0, 6, 6)                           # IPELength = 6 blocks
    purse.put(6, 6, 0)                           # IPEBitMap: no optional IIN
    purse.put(12, 4, 1)                          # IPEFormatRevision
    purse.buf[2] = 255                           # RemoveDate: owner only
    purse.putb(3, (8000).to_bytes(2, "big"))     # sold by ITSO STR, not by the owner
    purse.buf[5] = 0b01100000                    # TYP2Flags: print ticket and receipt
    purse.putb(6, (1000).to_bytes(2, "big"))     # Threshold: top up below GBP 10
    purse.putb(8, (2000).to_bytes(2, "big"))     # TopUpAmount: GBP 20
    purse.putb(10, (15000).to_bytes(2, "big"))   # MaxValue2: GBP 150
    purse.putb(12, (200).to_bytes(2, "big"))     # MaximumNegativeAmount: GBP 2
    purse.putb(14, (500).to_bytes(2, "big"))     # DepositAmount: GBP 5
    purse.put(128, 14, date_stamp(2025, 4, 6))   # StartDateAutoTopUp
    purse.put(156, 4, 3)                         # DepositMethodOfPayment: card
    purse.put(160, 4, 0)                         # DepositCurrencyCode: sterling
    # Three records, all written: the live one has a journey in progress, and
    # the two behind it are what the Earlier on card section is made of.
    purse_values = value_group([
        value_record(4, 340, dts(2026, 8, 12, 18, 5), purse_tail(3120)),
        value_record(7, 341, dts(2026, 9, 18, 8, 12), purse_tail(2765)),
        value_record(14, 342, dts(2026, 9, 19, 12, 31),
                     purse_tail(2415, legs=2, cumulative=350, flags=0b011)),
    ], format_rev=1)

    # ---- E2: ITSO ID, revision 2 (TS 1000-5 table 22a)
    fore, sur = b"JAMIE", b"OKONKWO-LEE"
    ident = Bits(52)
    ident.put(0, 6, 13)
    ident.put(6, 6, 0b000100)                    # IPEBitMap: forename and surname
    ident.put(12, 4, 2)
    ident.buf[2] = 255
    ident.buf[5] = 0b00010011                    # IDFlags: photo, male, companion
    ident.put(50, 6, 15)                         # PassbackTime: 15 minutes
    ident.putb(7, bcd("19780514"))               # DateOfBirth: a Datef, not a DATE
    ident.put(130, 14, date_stamp(2025, 4, 6))   # EntitlementStartDate
    ident.put(144, 14, EXP)                      # EntitlementExpiryDate
    ident.buf[29] = 13                           # EntitlementCode: capped fare
    ident.buf[30] = 16                           # ConcessionaryClass: commuter
    ident.buf[31] = len(fore)
    ident.putb(32, fore)
    ident.buf[32 + len(fore)] = len(sur)
    ident.putb(33 + len(fore), sur)

    # ---- E3: period ticket, revision 3 (TS 1000-5 clause 2.9.3): monthly
    # passes between Tunbridge Wells and London Bridge, bought as a stock.
    period = Bits(48)
    period.put(0, 6, 12)
    period.put(6, 6, 0b000110)                   # identity document, route, locations
    # No CPICC (bit 4), so RouteCode follows the 29 fixed bytes directly and the
    # locations start at 29 + 5 = 34 (TS 1000-5 table 3.27).
    period.put(12, 4, 3)
    period.buf[2] = 255
    period.putb(3, (289).to_bytes(2, "big"))
    # TYP22Flags: print a ticket, and keep expired passes at a top-up
    # (TreatmentOfExpiredSP, revision 3 only).
    period.put(40, 16, (1 << 5) | (1 << 7))
    period.put(64, 14, date_stamp(2026, 8, 28))  # IssueDate
    period.put(96, 3, 2)                         # Class: standard
    period.put(106, 14, date_stamp(2026, 9, 1))  # ValidityStartDate
    period.buf[19] = 1                           # PartySizeAdult
    period.putb(23, (56620).to_bytes(4, "big"))  # AmountPaid: GBP 566.20 a month
    period.put(27 * 8, 4, 3)                     # by card
    period.putb(34, loc1(203, b"5230"))          # Tunbridge Wells
    period.putb(40, loc1(203, b"5148"))          # London Bridge
    # IdentityDocumentID after the locations: type 3, a pointer to directory
    # entry 2 - the holder must carry the ITSO ID the season is priced against.
    period.put(46 * 8, 3, 3)
    period.put(46 * 8 + 3, 5, 1)
    period.buf[47] = 2
    period_values = value_group([
        value_record(1, 21, dts(2026, 9, 1, 7, 40),
                     period_tail(6, 0b01, date_stamp(2027, 1, 31), date_stamp(2026, 9, 30))),
        value_record(13, 22, dts(2026, 9, 21, 7, 38),
                     period_tail(5, 0b01, date_stamp(2027, 1, 31), date_stamp(2026, 10, 20))),
    ], format_rev=3)

    # ---- E4: journey ticket, revision 2 (TS 1000-5 table 31a): a book of ten
    # Highspeed journeys between Ashford International and St Pancras.
    journey = Bits(52)
    journey.put(0, 6, 13)
    journey.put(6, 6, 0b001010)                  # mode group, route and locations
    journey.put(12, 4, 2)
    journey.buf[2] = 30                          # RemoveDate: 30 days after expiry
    journey.putb(3, (289).to_bytes(2, "big"))
    journey.put(58, 14, date_stamp(2026, 9, 5))  # IssueDate
    journey.put(93, 3, 2)                        # Class: standard
    journey.buf[12] = 1                          # PartySizeAdult
    journey.putb(16, (32850).to_bytes(4, "big")) # AmountPaid: GBP 328.50
    journey.put(20 * 8, 4, 3)                    # by card
    journey.putb(35, b"00000")                   # RouteCode
    journey.putb(40, loc1(203, b"5004"))         # Ashford International
    journey.putb(46, loc1(203, b"1555"))         # London St Pancras International
    journey_values = value_group([
        value_record(1, 5, dts(2026, 9, 5, 9, 12), journey_tail(10, 0, 0)),
        value_record(7, 6, dts(2026, 9, 20, 8, 3), journey_tail(8, 1, 0b10)),
    ], format_rev=2)

    # ---- E5: loyalty, whose value record counts points rather than money
    loyalty = Bits(8)
    loyalty.put(0, 6, 2)
    loyalty.put(6, 6, 0)
    loyalty.put(12, 4, 1)
    loyalty.buf[2] = 30
    loyalty.putb(3, (289).to_bytes(2, "big"))
    loyalty_values = value_group([
        value_record(1, 44, dts(2026, 7, 2, 10, 15), loyalty_tail(4250)),
        value_record(9, 45, dts(2026, 9, 14, 19, 2), loyalty_tail(5100, user=321)),
    ], format_rev=1)

    # ---- E6: charge to account, TYP 5 (TS 1000-5 table 15)
    charge = Bits(24)
    charge.put(0, 6, 6)
    charge.put(6, 6, 0)
    charge.put(12, 4, 1)
    charge.buf[2] = 255
    charge.putb(3, (289).to_bytes(2, "big"))
    charge.buf[6] = 4                            # WeeksPerChargePeriod
    charge.buf[7] = 60                           # MaxTransactionsPerPeriod
    charge.putb(8, (25000).to_bytes(2, "big"))   # MaxValue5: GBP 250 a period
    charge.putb(10, (1000).to_bytes(2, "big"))   # DepositAmount: GBP 10
    charge.put(96, 14, date_stamp(2026, 4, 6))   # StartDateCTA
    charge.put(110, 14, date_stamp(2027, 4, 5))  # EndDate, 3.75 bytes in
    charge.buf[15] |= 6                          # DepositMethodOfPayment: direct debit
    charge.put(128, 4, 0)                        # DepositCurrencyCode
    charge.put(132, 12, 2000)                    # DepositVATSalesTax: 20.00%
    charge.buf[5] = 0b01000000                   # TYP5Flags: print a receipt
    charge_values = value_group([
        value_record(15, 9, dts(2026, 8, 6, 9, 0), charge_tail(0, date_stamp(2026, 8, 6))),
        value_record(7, 10, dts(2026, 9, 19, 18, 22),
                     # TYP5ValueFlags bit 1: spent before any other product
                     charge_tail(23, date_stamp(2026, 9, 6), legs=1, flags=0b0010)),
    ], format_rev=1)

    # ---- E7: a voucher, which the decoder reports from its directory entry and
    # its value record alone - there is no TYP 25 dataset parser.
    voucher = Bits(16)
    voucher.put(0, 6, 4)
    voucher.put(6, 6, 0)
    voucher.put(12, 4, 1)
    voucher.buf[2] = 0                           # RemoveDate: removable at expiry
    voucher.putb(3, (289).to_bytes(2, "big"))
    voucher_values = value_group([
        value_record(1, 2, dts(2026, 6, 1, 11, 0), voucher_tail(4, auto_renew=True)),
        value_record(7, 3, dts(2026, 9, 11, 8, 44), voucher_tail(3, auto_renew=True)),
    ], format_rev=1)

    # ---- E8: entitlement, TYP 14 revision 2: a Disabled Persons Railcard -
    # a third off, for the holder and a companion, with the discounted fare
    # rounded down to 5p as railcard fares are.
    ent = Bits(32)
    ent.put(0, 6, 8)
    ent.put(6, 6, 0)                             # nothing optional
    ent.put(12, 4, 2)
    ent.buf[2] = 255
    ent.buf[5] = 0b00110011                      # IDFlags: photo, male, companion, print
    ent.put(48, 1, 1)                            # RoundingFlagsEnable
    ent.put(50, 6, 0)                            # PassbackTime: the reader's own
    ent.putb(7, (8841372).to_bytes(4, "big"))    # HolderID: the railcard number
    ent.put(89, 1, 1)                            # RoundingValueFlag: down, to 5p
    ent.put(90, 14, date_stamp(2026, 4, 1))      # EntitlementStartDate
    ent.put(104, 14, date_stamp(2027, 3, 31))    # EntitlementExpiryDate
    ent.buf[20] = 3                              # EntitlementCode: proportional fare
    ent.buf[21] = 5                              # ConcessionaryClass: disabled

    # ---- E9: loyalty points with a partner outside transport - an owner in
    # the extended OID range - which the partner has stopped; and E10, a seat
    # reservation that has expired. Neither type has a dataset parser, so their
    # directory entries and value records are what the screens show.
    partner = Bits(16)
    partner.put(0, 6, 4)
    partner.put(12, 4, 1)
    partner.buf[2] = 255
    partner.putb(3, (289).to_bytes(2, "big"))

    reserved = Bits(16)
    reserved.put(0, 6, 4)
    reserved.put(12, 4, 1)
    reserved.buf[2] = 255
    reserved.putb(3, (289).to_bytes(2, "big"))
    reserved_values = value_group([
        value_record(1, 1, dts(2026, 2, 20, 16, 0), count_tail(1)),
        value_record(2, 2, dts(2026, 3, 1, 7, 12), count_tail(0)),
    ], format_rev=1)

    entries = [
        dir_entry(289, 2, 0, True, EXP),                             # E1 purse
        dir_entry(246, 16, 1, False, EXP),                           # E2 ITSO ID
        dir_entry(289, 22, 2, True, date_stamp(2027, 3, 31)),        # E3 period
        # IINL set: the owner belongs to another network, which the product
        # screen's Technical section says under its operator number.
        dir_entry(289, 23, 4, True, date_stamp(2026, 12, 31), foreign=True),
        dir_entry(289, 3, 0, True, 0),                               # E5 loyalty, no expiry
        dir_entry(289, 5, 0, True, date_stamp(2028, 6, 30)),         # E6 charge to account
        dir_entry(289, 25, 2, True, date_stamp(2026, 10, 31)),       # E7 voucher
        dir_entry(246, 14, 0, False, date_stamp(2027, 3, 31)),       # E8 railcard
        # E9 uses the extended IPE-owner range: raw 5678 with the flag set is 13870.
        dir_entry(5678, 17, 0, False, date_stamp(2027, 5, 31), extended=True),
        dir_entry(289, 24, 0, True, date_stamp(2026, 3, 31)),        # E10 expired
        # E11: a toll pass for the Dartford Crossing, which a Kent driver might
        # carry - hypothetical, as no toll is paid by ITSO card today - from an
        # operator the table does not know. It has no block at all: an entry the
        # card lists and Flipso could not read, which is what "Details: Not
        # decoded" is for.
        dir_entry(4410, 26, 0, False, date_stamp(2029, 1, 31)),
        log_entry(ptr=3, eei=1, when=dts(2026, 9, 21, 17, 46), record_offset=0,
                  passback=20),
    ]
    chain = {1: 13, 13: ACTIVE, 2: ACTIVE, 3: 15, 15: ACTIVE, 4: 16, 16: ACTIVE,
             5: 17, 17: ACTIVE, 6: 18, 18: ACTIVE, 7: 19, 19: ACTIVE, 8: ACTIVE,
             9: 20, 20: BLOCKED, 10: 21, 21: ACTIVE, 11: 11}

    # The cyclic log: four slots, and Record Offset names the next one to be
    # written, so slot 3 holds the newest record and the log entry above agrees
    # with it. The two revision 4 records carry no origin: with the entry they
    # close, the products the gate considered and a routing point in them, a
    # 48-byte slot has no room left for one.
    log = b"".join([
        # Pay as you go from Tonbridge to Paddock Wood, off the season's line:
        # the gate weighed the season, flagged it as not valid there, and
        # charged the purse instead.
        tt_record_rev4(12, dts(2026, 9, 18, 8, 12), 355,
                       via=None,
                       dest=loc2(203, b"5224"),           # Paddock Wood
                       ipe_ptr=1, entry_when=dts(2026, 9, 18, 8, 4), entry_oid=289,
                       entry_iin_index=0, candidates=[1, 3, 0, 0], mop=8, vat=0,
                       cipe_flags=0b01),
        tt_record(12, dts(2026, 9, 19, 12, 31), 350,
                  origin=loc2(203, b"5124"),              # Sevenoaks
                  dest=loc2(203, b"5071"), ipe_ptr=1),    # Otford
        # Monday's commute on the season, via Sevenoaks rather than Redhill,
        # checked by a guard on the way.
        tt_record_rev4(12, dts(2026, 9, 21, 8, 3), 0,
                       via=loc2(203, b"5124"),            # via Sevenoaks
                       dest=loc2(203, b"5148"),           # London Bridge
                       ipe_ptr=3, entry_when=dts(2026, 9, 21, 7, 12), entry_oid=289,
                       entry_iin_index=0, candidates=[3, 1, 4, 0], no_fare=True,
                       cipe_flags=0b10, vat=0),
        tt_record(11, dts(2026, 9, 21, 17, 46), 0,
                  origin=loc2(203, b"5148"), dest=None, ipe_ptr=3),
    ])

    return "Demo 01 The Key Kent", unix(2026, 9, 21, 19, 12), [
        ("Shell", bytes(shell.buf)),
        # What the chip said about itself, so the Card screen's Chip section
        # has something to show for a saved card too.
        ("Chip", chip_block(bytes.fromhex("04512A3AB25E80"), 1824)),
        ("Directory", directory(entries, chain, S, E, SCTL, 0x5B)),
        # The purse gets an InstanceID of its own: it is the only element in a
        # shell that names one particular product rather than a kind of one, so
        # leaving every product on the same ISAM would hide what it is for.
        ("Product 1", group(purse, B, purse_values,
                            instance=instance_and_seal(kid=4, inp=0,
                                                       isam_id=0x60090128,
                                                       isam_seq=88123))),
        ("Product 2", group(ident, B)),
        ("Product 3", group(period, B, period_values)),
        ("Product 4", group(journey, B, journey_values,
                            instance=instance_and_seal(kid=2, inp=3,
                                                       isam_id=0x6009006D,
                                                       isam_seq=14902))),
        ("Product 5", group(loyalty, B, loyalty_values)),
        ("Product 6", group(charge, B, charge_values)),
        ("Product 7", group(voucher, B, voucher_values)),
        ("Product 8", group(ent, B)),
        ("Product 9", group(partner, B)),
        ("Product 10", group(reserved, B, reserved_values)),
        ("Log", log),
    ]


# ====================================================================
# Card 2 - a Freedom Pass the issuer has stopped
#
# A London pensioner's pass, reported lost and blocked by London Councils. The
# blocking indicator is the headline fact about a card, and it changes four
# things at once: the menu title and its icon, the banner on the Card screen,
# the missing Active line, and the tone the scan ends on. The products under it
# are what a real Freedom Pass carries - an ITSO ID, the Greater London
# entitlement and a purse that has never been topped up - in the revision 1
# layouts of the ID and the entitlement, which the other cards do not exercise.
# ====================================================================
def card_blocked():
    B, S, E, SCTL = 64, 16, 8, 7
    ACTIVE, BLOCKED = S - 1, S - 2
    OID = "0226"                                 # London Councils, brand "Freedom Pass"
    EXP = date_stamp(2027, 3, 31)

    shell = shell_dataset(IIN, OID, "0200002", fvc=7, ksc=4, kvc=2, expiry=EXP,
                          b=B, s=S, e=E, sctl=SCTL)

    # ---- E1: ITSO ID, revision 1 (TS 1000-5 table 22): no start date, and the
    # entitlement sits two bytes earlier than revision 2 puts it.
    fore, sur = b"MARGARET", b"OKAFOR"
    ident = Bits(48)
    ident.put(0, 6, 12)
    ident.put(6, 6, 0b000100)
    ident.put(12, 4, 1)
    ident.buf[2] = 255
    # IDFlags: photo, female, companion, and URI - the holder's details are in
    # another application on the card, so the Language below is not used.
    ident.buf[5] = 0b00011101
    ident.put(50, 6, 45)                         # PassbackTime
    ident.putb(7, bcd("19480922"))
    ident.buf[11] = 44                           # Language: English
    ident.put(130, 14, EXP)                      # EntitlementExpiryDate
    ident.buf[27] = 14                           # EntitlementCode: free travel
    ident.buf[28] = 4                            # ConcessionaryClass: pensioner
    ident.buf[29] = len(fore)
    ident.putb(30, fore)
    ident.buf[30 + len(fore)] = len(sur)
    ident.putb(31 + len(fore), sur)

    # ---- E2: entitlement, revision 1 (table 20)
    ent = Bits(24)
    ent.put(0, 6, 6)
    ent.put(6, 6, 0)
    ent.put(12, 4, 1)
    ent.buf[2] = 255
    ent.putb(3, (0x00E2).to_bytes(2, "big"))     # CPICC
    ent.buf[5] = 0b00010101
    ent.put(48, 1, 1)                            # RoundingFlagsEnable
    ent.put(50, 6, 45)
    ent.putb(7, (81142).to_bytes(4, "big"))      # HolderID, straight after passback
    ent.put(89, 1, 1)                            # RoundingValueFlag: down, to 5p
    ent.put(90, 14, EXP)                         # EntitlementExpiryDate
    ent.put(108, 4, 0)                           # DepositCurrencyCode, ahead of...
    ent.put(112, 4, 3)                           # ...DepositMethodOfPayment: card
    ent.putb(16, (1000).to_bytes(2, "big"))      # DepositAmount: GBP 10.00
    ent.buf[18] = 2                              # EntitlementCode: limited free ride
    ent.buf[19] = 4                              # ConcessionaryClass: pensioner

    # ---- E3: the purse a Freedom Pass is issued with and nobody tops up: one
    # record, written when the pass was made, with nothing in it.
    purse = Bits(24)
    purse.put(0, 6, 6)
    purse.put(6, 6, 0)
    purse.put(12, 4, 1)
    purse.buf[2] = 255
    purse.putb(3, (226).to_bytes(2, "big"))
    purse.putb(10, (5000).to_bytes(2, "big"))    # MaxValue2: GBP 50
    purse_values = value_group([
        value_record(1, 1, dts(2024, 3, 18, 10, 41), purse_tail(0)),
    ], format_rev=1)

    entries = [
        dir_entry(226, 16, 0, False, EXP),                          # E1 ITSO ID
        dir_entry(96, 14, 0, False, EXP),                           # E2 entitlement
        dir_entry(226, 2, 0, True, EXP),                            # E3 purse
        bytes(5), bytes(5), bytes(5), bytes(5),                     # E4-E7 unused
        # Basic mode: the POST updates the log entry and writes no journey
        # record, so the card has a last tap and no log to show for it - which
        # is what a real Freedom Pass read on 2026-09-25 showed, down to the
        # two-minute passback and the ID as the product used.
        log_entry(ptr=1, eei=0, when=dts(2026, 9, 2, 10, 19), record_offset=0,
                  passback=2, normal_mode=False),
    ]
    chain = {1: ACTIVE, 2: ACTIVE, 3: 4, 4: ACTIVE}

    return "Demo 02 Freedom Pass", unix(2026, 9, 21, 19, 20), [
        ("Shell", bytes(shell.buf)),
        ("Directory", directory(entries, chain, S, E, SCTL, 0x11, blocked=True)),
        ("Product 1", group(ident, B)),
        ("Product 2", group(ent, B)),
        ("Product 3", group(purse, B, purse_values)),
    ]


# ====================================================================
# Card 3 - a Glasgow Subway smartcard, the other customer media
#
# CMD2 is ITSO on ISO 7816 rather than DESFire, and the Subway's reusable
# smartcard is the one scheme still issuing it. What shows on screen is the
# geometry: 80-byte sectors, 64 of them and 16 directory entries, against the
# 64/16/8 the DESFire cards here use. The purse has no expiry date and a value
# record that has never been written, which are both things the decoder has to
# handle rather than display.
#
# The Subway is one flat fare around a single loop, so its season has no
# locations - it is good anywhere the Subway goes - and its journeys are
# between stations, which NaPTAN codes as stops like any other.
# ====================================================================
def card_cmd2():
    B, S, E, SCTL = 80, 64, 16, 46
    ACTIVE = S - 1
    OID = "0196"                                 # SPT, brand "SPT Subway"
    EXP = date_stamp(2040, 4, 15)

    shell = shell_dataset(IIN, OID, "0300003", fvc=2, ksc=3, kvc=1, expiry=EXP,
                          b=B, s=S, e=E, sctl=SCTL)

    purse = Bits(24)
    purse.put(0, 6, 6)
    purse.put(6, 6, 0)
    purse.put(12, 4, 1)
    purse.buf[2] = 255
    purse.putb(3, (196).to_bytes(2, "big"))
    purse.putb(6, (500).to_bytes(2, "big"))      # Threshold: GBP 5
    purse.putb(8, (1500).to_bytes(2, "big"))     # TopUpAmount: GBP 15
    purse.putb(10, (4000).to_bytes(2, "big"))    # MaxValue2: GBP 40
    purse.putb(14, (300).to_bytes(2, "big"))     # DepositAmount: GBP 3
    purse.put(156, 4, 1)                         # DepositMethodOfPayment: cash
    # The third record has never been written. A blank record reads as a DTS of
    # zero, whose epoch is in 2028 - later than any real timestamp - so it has
    # to be skipped rather than taken as the newest.
    purse_values = value_group([
        value_record(4, 12, dts(2026, 8, 2, 9, 41), purse_tail(1800)),
        value_record(7, 13, dts(2026, 9, 17, 8, 2), purse_tail(1630)),
        bytes(15),
    ], format_rev=1)

    fore, sur = b"JO", b"CLYDE"
    ident = Bits(48)
    ident.put(0, 6, 12)
    ident.put(6, 6, 0b000100)
    ident.put(12, 4, 2)
    ident.buf[2] = 255
    ident.buf[5] = 0b00000001                    # IDFlags: photo, gender not stated
    ident.put(50, 6, 10)
    ident.putb(7, bcd("19910227"))
    ident.put(130, 14, date_stamp(2025, 4, 16))
    ident.put(144, 14, EXP)
    ident.buf[29] = 5                            # EntitlementCode: flat fare
    ident.buf[30] = 1                            # ConcessionaryClass: adult
    ident.buf[31] = len(fore)
    ident.putb(32, fore)
    ident.buf[32 + len(fore)] = len(sur)
    ident.putb(33 + len(fore), sur)

    # Period ticket, revision 2: four-week Subway passes, bought as a stock and
    # renewed automatically. No locations - the whole Subway - and so no
    # RouteCode either.
    period = Bits(32)
    period.put(0, 6, 8)
    period.put(6, 6, 0)
    period.put(12, 4, 2)
    period.buf[2] = 255
    period.putb(3, (196).to_bytes(2, "big"))
    period.put(40, 16, 0xFE00)                   # TYP22Flags: every part of every day
    period.put(64, 14, date_stamp(2026, 9, 1))   # IssueDate
    period.put(96, 3, 2)                         # Class: standard
    period.buf[17] = 0xFF                        # ValidOnDayCode: every day
    period.buf[18] = 1                           # PartySizeAdult
    period.putb(22, (2600).to_bytes(4, "big"))   # AmountPaid: GBP 26.00
    period.put(26 * 8, 4, 3)                     # by card
    period_values = value_group([
        value_record(1, 30, dts(2026, 9, 1, 8, 0),
                     period_tail(1, 0b01, date_stamp(2027, 2, 28), date_stamp(2026, 9, 30))),
        value_record(13, 31, dts(2026, 9, 19, 7, 55),
                     period_tail(0, 0b01, date_stamp(2027, 2, 28), date_stamp(2026, 10, 19))),
    ], format_rev=2)

    entries = [
        dir_entry(196, 2, 0, True, 0),                              # E1 purse, no expiry
        dir_entry(196, 16, 0, False, EXP),                          # E2 ITSO ID
        dir_entry(196, 22, 2, True, date_stamp(2027, 2, 28)),       # E3 period
    ] + [bytes(5)] * (E - 4) + [
        log_entry(ptr=3, eei=0, when=dts(2026, 9, 19, 18, 12), record_offset=2,
                  passback=8),
    ]
    chain = {1: 18, 18: ACTIVE, 2: ACTIVE, 3: 20, 20: ACTIVE}

    # Two journeys between Subway stations, by their NaptanCodes: a pay as
    # you go single from Kelvinbridge into town, and home to Hillhead on the
    # season. The first names its origin as a list of NaptanCodes (212), the
    # form a rail card never writes.
    log = b"".join([
        tt_record(12, dts(2026, 9, 17, 8, 2), 170,
                  origin=loc2(212, naptan("gladama")),     # Kelvinbridge
                  dest=loc2(206, naptan("gladatd")),       # St Enoch
                  ipe_ptr=1, mop=8),
        tt_record(12, dts(2026, 9, 19, 18, 12), 0,
                  origin=loc2(206, naptan("gladadm")),     # Buchanan Street
                  dest=loc2(206, naptan("gladajd")), ipe_ptr=3),  # Hillhead
    ])

    return "Demo 03 Subway card", unix(2026, 9, 21, 19, 26), [
        ("Shell", bytes(shell.buf)),
        ("Directory", directory(entries, chain, S, E, SCTL, 0x07)),
        ("Product 1", group(purse, B, purse_values)),
        ("Product 2", group(ident, B)),
        ("Product 3", group(period, B, period_values)),
        ("Log", log),
    ]


# ====================================================================
# Card 4 - what only a saved card knows: a Surrey commuter's SWR Touch
#
# Everything a card says about its own past is a rolling window: four slots in
# the log, two value records per product, and a directory entry that is freed
# the moment a ticket is removed. A file written while those were still there
# is the only place they survive, so this card carries the three kinds of
# history - journeys, transactions and whole products the card has dropped -
# and is the only one of the four that can.
# ====================================================================
def card_history():
    B, S, E, SCTL = 64, 32, 8, 20
    ACTIVE = S - 1
    OID = "0109"                                 # South Western Railway, "SWR Touch"
    EXP = date_stamp(2029, 11, 30)

    shell = shell_dataset(IIN, OID, "0400004", fvc=7, ksc=4, kvc=1, expiry=EXP,
                          b=B, s=S, e=E, sctl=SCTL)

    purse = Bits(24)
    purse.put(0, 6, 6)
    purse.put(6, 6, 0)
    purse.put(12, 4, 1)
    purse.buf[2] = 255
    purse.putb(3, (109).to_bytes(2, "big"))
    purse.putb(6, (800).to_bytes(2, "big"))
    purse.putb(8, (1500).to_bytes(2, "big"))
    purse.putb(10, (12000).to_bytes(2, "big"))
    purse.putb(14, (500).to_bytes(2, "big"))
    purse.put(128, 14, date_stamp(2025, 6, 1))
    purse.put(156, 4, 6)                         # DepositMethodOfPayment: direct debit
    # What the card holds now. Everything older than these two is in the value
    # history below, because the card has written over it.
    purse_values = value_group([
        value_record(7, 210, dts(2026, 9, 16, 7, 58), purse_tail(1145)),
        value_record(5, 211, dts(2026, 9, 20, 18, 31),
                     purse_tail(2645, flags=0b001)),
    ], format_rev=1)
    purse_history = b"".join([
        value_record(4, 205, dts(2026, 6, 2, 12, 15), purse_tail(2200)),
        value_record(7, 206, dts(2026, 7, 6, 8, 2), purse_tail(1845)),
        value_record(7, 207, dts(2026, 7, 31, 17, 40), purse_tail(1490)),
        value_record(10, 208, dts(2026, 8, 14, 13, 5), purse_tail(1740)),
        value_record(7, 209, dts(2026, 9, 1, 8, 1), purse_tail(1385)),
    ])

    # A book of six journeys between Woking and London Waterloo, one of them
    # used since.
    journey = Bits(52)
    journey.put(0, 6, 13)
    journey.put(6, 6, 0b001010)
    journey.put(12, 4, 2)
    journey.buf[2] = 255
    journey.putb(3, (109).to_bytes(2, "big"))
    journey.put(58, 14, date_stamp(2026, 9, 7))  # IssueDate
    journey.put(93, 3, 2)                        # Class: standard
    journey.buf[12] = 1                          # PartySizeAdult
    journey.putb(16, (9720).to_bytes(4, "big"))  # AmountPaid: GBP 97.20
    journey.put(20 * 8, 4, 3)                    # by card
    journey.putb(35, b"00000")
    journey.putb(40, loc1(203, b"5685"))         # Woking
    journey.putb(46, loc1(203, b"5598"))         # London Waterloo
    journey_values = value_group([
        value_record(1, 14, dts(2026, 9, 7, 10, 0), journey_tail(6, 0, 0)),
        value_record(7, 15, dts(2026, 9, 18, 7, 44), journey_tail(5, 0, 0)),
    ], format_rev=2)

    fore, sur = b"PRIYA", b"RAMANATHAN"
    ident = Bits(52)
    ident.put(0, 6, 13)
    ident.put(6, 6, 0b000100)
    ident.put(12, 4, 2)
    ident.buf[2] = 255
    ident.buf[5] = 0b00000101                    # IDFlags: photo, female
    ident.put(50, 6, 20)
    ident.putb(7, bcd("19990801"))
    ident.put(130, 14, date_stamp(2025, 6, 1))
    ident.put(144, 14, EXP)
    ident.buf[29] = 4                            # EntitlementCode: flat fare discount
    ident.buf[30] = 3                            # ConcessionaryClass: student
    ident.buf[31] = len(fore)
    ident.putb(32, fore)
    ident.buf[32 + len(fore)] = len(sur)
    ident.putb(33 + len(fore), sur)

    entries = [
        dir_entry(109, 2, 0, True, EXP),                            # E1 purse
        dir_entry(109, 23, 2, True, date_stamp(2027, 6, 30)),       # E2 journey ticket
        dir_entry(109, 16, 1, False, EXP),                          # E3 ITSO ID
        bytes(5), bytes(5), bytes(5), bytes(5),                     # E4-E7 unused
        log_entry(ptr=1, eei=0, when=dts(2026, 9, 20, 18, 31), record_offset=1,
                  passback=12),
    ]
    chain = {1: 9, 9: ACTIVE, 2: 10, 10: ACTIVE, 3: ACTIVE}

    # Four slots on the card, and Record Offset 1 makes slot 0 the newest.
    log = b"".join([
        tt_record(12, dts(2026, 9, 20, 18, 31), 610,
                  origin=loc2(203, b"5578"),               # Wimbledon
                  dest=loc2(203, b"5685"), ipe_ptr=1, mop=8),
        tt_record(11, dts(2026, 9, 18, 7, 44), 0,
                  origin=loc2(203, b"5685"), dest=None, ipe_ptr=2),
        tt_record(12, dts(2026, 9, 18, 17, 51), 610,
                  origin=loc2(203, b"5685"), dest=loc2(203, b"5578"),
                  ipe_ptr=1, mop=8),
        # A journey out of the country's own numbering: LocDefType 208 puts a
        # UIC country code in front of the NLC.
        tt_record(12, dts(2026, 9, 16, 7, 58), 355,
                  origin=loc2(208, bcd("0070") + b"5571"),  # Surbiton, UK
                  dest=loc2(208, bcd("0070") + b"5578"), ipe_ptr=1, mop=8),
    ])

    # Journeys earlier reads of this card saw and the card has written over.
    # Eight of them, which with the four above is exactly what the decoder has
    # room to show.
    older = []
    for day, fare, origin, dest in [
        (14, 355, b"5571", b"5578"), (11, 610, b"5685", b"5578"),
        (9, 240, b"5578", b"5570"), (7, 610, b"5578", b"5685"),
        (4, 355, b"5578", b"5571"), (2, 240, b"5570", b"5578"),
        (1, 610, b"5685", b"5578"),
    ]:
        older.append(tt_record(12, dts(2026, 9, day, 8, 12), fare,
                               origin=loc2(203, origin), dest=loc2(203, dest),
                               ipe_ptr=1, mop=8))
    # And one on the season that has since left the card.
    older.append(tt_record(12, dts(2026, 3, 10, 8, 5), 0,
                           origin=loc2(203, b"5685"),
                           dest=loc2(203, b"5598"), ipe_ptr=4, mop=8))

    # ---- The products the card has dropped since this file was first written.
    # Each is the IPE group as that read assembled it, behind the read time and
    # the directory entry that then described it.
    gone_period = Bits(48)
    gone_period.put(0, 6, 12)
    gone_period.put(6, 6, 0b000010)
    gone_period.put(12, 4, 3)
    gone_period.buf[2] = 255
    gone_period.putb(3, (109).to_bytes(2, "big"))
    gone_period.put(106, 14, date_stamp(2025, 9, 1))
    gone_period.putb(34, loc1(203, b"5685"))     # Woking
    gone_period.putb(40, loc1(203, b"5598"))     # London Waterloo
    gone_period_values = value_group([
        value_record(1, 60, dts(2026, 1, 5, 7, 30),
                     period_tail(3, 0b01, date_stamp(2026, 3, 31), date_stamp(2026, 2, 4))),
        value_record(13, 61, dts(2026, 2, 5, 7, 32),
                     period_tail(2, 0b01, date_stamp(2026, 3, 31), date_stamp(2026, 3, 7))),
    ], format_rev=3)
    gone_period_history = b"".join([
        value_record(1, 58, dts(2025, 11, 4, 7, 28),
                     period_tail(5, 0b01, date_stamp(2026, 3, 31), date_stamp(2025, 12, 3))),
        value_record(13, 59, dts(2025, 12, 4, 7, 26),
                     period_tail(4, 0b01, date_stamp(2026, 3, 31), date_stamp(2026, 1, 3))),
    ])

    gone_voucher = Bits(16)
    gone_voucher.put(0, 6, 4)
    gone_voucher.put(12, 4, 1)
    gone_voucher.buf[2] = 0
    gone_voucher.putb(3, (109).to_bytes(2, "big"))
    gone_voucher_values = value_group([
        value_record(1, 4, dts(2025, 12, 20, 14, 2), voucher_tail(2)),
        value_record(7, 5, dts(2025, 12, 24, 9, 18), voucher_tail(1)),
    ], format_rev=1)

    gone_purse = Bits(24)
    gone_purse.put(0, 6, 6)
    gone_purse.put(6, 6, 0)
    gone_purse.put(12, 4, 1)
    gone_purse.buf[2] = 255
    gone_purse.putb(3, (8000).to_bytes(2, "big"))
    gone_purse.putb(10, (9000).to_bytes(2, "big"))
    gone_purse_values = value_group([
        value_record(7, 90, dts(2025, 7, 14, 8, 9), purse_tail(430)),
        value_record(7, 91, dts(2025, 8, 1, 8, 11), purse_tail(75)),
    ], format_rev=1)

    gone_ent = Bits(32)
    gone_ent.put(0, 6, 8)
    gone_ent.put(6, 6, 0)
    gone_ent.put(12, 4, 2)
    gone_ent.buf[2] = 255
    gone_ent.buf[5] = 0b00000101
    gone_ent.put(50, 6, 30)
    gone_ent.put(90, 14, date_stamp(2024, 9, 1))
    gone_ent.put(104, 14, date_stamp(2025, 6, 30))
    gone_ent.buf[20] = 3                         # EntitlementCode: proportional fare
    gone_ent.buf[21] = 3                         # ConcessionaryClass: student

    return "Demo 04 SWR Touch", unix(2026, 9, 21, 19, 33), [
        ("Shell", bytes(shell.buf)),
        ("Directory", directory(entries, chain, S, E, SCTL, 0x7E)),
        ("Product 1", group(purse, B, purse_values)),
        ("Product 2", group(journey, B, journey_values)),
        ("Product 3", group(ident, B)),
        ("Log", log),
        ("Log history", b"".join(older)),
        ("Value history 1", purse_history),
        # Slots rather than entry numbers: the entry a gone product held may
        # belong to something else by now.
        ("Product history 100", history_block(
            unix(2026, 3, 12, 8, 2), 4,
            dir_entry(109, 22, 2, True, date_stamp(2026, 3, 31)),
            group(gone_period, B, gone_period_values))),
        ("Value history 100", gone_period_history),
        ("Product history 101", history_block(
            unix(2025, 12, 24, 10, 6), 5,
            dir_entry(109, 25, 0, True, date_stamp(2025, 12, 31)),
            group(gone_voucher, B, gone_voucher_values))),
        ("Product history 102", history_block(
            unix(2025, 8, 1, 9, 30), 2,
            dir_entry(8000, 2, 0, True, date_stamp(2025, 8, 31)),
            group(gone_purse, B, gone_purse_values))),
        ("Product history 103", history_block(
            unix(2025, 6, 30, 18, 45), 6,
            dir_entry(246, 14, 0, False, date_stamp(2025, 6, 30)),
            group(gone_ent, B))),
    ]


# ====================================================================
# Card 5 - a Glasgow Subway paper ticket
#
# The other four cards are ISO 14443-4 media - DESFire or ISO 7816. This one is
# an NFC Type 2 tag (TS 1000-10 CMD4): a MIFARE Ultralight / Infineon my-d, the
# family SPT's single-use paper tickets use. It has no full shell and no
# directory - the whole card is one flat run of 4-byte pages, and its data groups
# sit at fixed page offsets. So the saved card is a single "Type 2" block of the
# raw page memory, which itso_parse_type2() decodes on its own: the Compact
# Shell, the single IPE Directory Entry, the InstanceID and the TYP 27 dataset,
# whose offsets were confirmed against a real day ticket.
# ====================================================================
def card_subway_paper():
    # A day ticket in the shape of a real one read on 2026-09-27: a Period ticket
    # (TYP 27) owned by SPT's product OID 8323 (extended range, so raw 131 with the
    # flag set), an adult all-day ticket valid across the whole network, bought
    # and last tapped on the one day it is good for. The chip serial is invented;
    # it is the card's identity, since a compact shell's number is the same on
    # every ticket, so Flipso keys a saved Type 2 card on it (flipso_capture.c).
    day = date_stamp(2026, 9, 21)
    pages = type2_page_memory(
        bytes([0x04, 0xA2, 0xB3, 0xC4, 0xD5, 0xE6, 0xF7]),
        dir_entry(131, 27, 0, False, day, extended=True),
        typ27_dataset(
            issue_date=day, amount=445, passback=7,
            flags=0b1000,  # ExpiryTimeFlag: the operator's own end-of-service time
            event2=12, last_use=dts(2026, 9, 21, 17, 47)))

    return "Demo 05 Subway day", unix(2026, 9, 21, 19, 40), [
        ("Type 2", pages),
    ]


# ====================================================================
# Card 6 - a Glasgow Subway paper return, half used
#
# Rebuilt from Ryan Murphy's published dump of 21 SPT Subway tickets
# (blog.ry4n.org, "Reverse engineering Glasgow's subway tickets", 2022): the
# builder reproduces his bytes exactly, so this is the shape of a real return
# rather than one read off the spec. A Multi-Use ticket (TYP 29 revision 1) of
# two rides, bought for GBP 3.30, with one ride left. Its last use was getting
# off at fare stage 4 through gate 5F2800 - which on an SPT ticket is Hillhead.
# Its one-time-programmable backup has 31 of 32 bits set, the 7F FF FF FF his
# half-used returns show. The serial is invented and the seal filler.
# ====================================================================
def card_subway_return():
    day = date_stamp(2026, 9, 21)
    pages = type2_page_memory(
        bytes([0x04, 0x5B, 0x61, 0x7C, 0x2A, 0x90, 0x3D]),
        dir_entry(131, 29, 2, False, day, extended=True),
        typ29_dataset(
            issue_date=day, rides_left=1, amount=330, mop=3,
            flags=0b1000,  # ExpiryTimeFlag: the operator's own end-of-service time
            usage_code=0b101,  # UsageRec is an alighting point, LocDefType 202
            usage=bytes.fromhex("5F280004")))
    return "Demo 06 Subway return", unix(2026, 9, 21, 19, 45), [
        ("Type 2", pages),
    ]


# ====================================================================
# Card 10 - a zonal book of coupons on paper
#
# A Multi-Use ticket (TYP 29 revision 1) in the coupon form: QtyRemaining
# counts coupons, several of which a journey may take. Two things no Subway
# ticket has: its AreaValidity is a location rather than a fare code - a LOC3
# zone map, zones 1 to 3 - and its ScalingFactor is 4, so each of the 32
# one-time-programmable backup bits stands for four coupons and the backup can
# only say "up to" (TS 1000-5 tables 57 and 58b). Hypothetical: SPT's product
# OID for the branding, but a scheme's shape rather than a ticket's.
# ====================================================================
def card_zonal_coupons():
    day = date_stamp(2026, 9, 1)
    pages = type2_page_memory(
        bytes([0x04, 0x3C, 0x71, 0x0E, 0x92, 0x5D, 0xA8]),
        dir_entry(131, 29, 5, False, date_stamp(2026, 12, 31), extended=True),
        typ29_dataset(
            # Thirty-eight coupons, not yet taken through a gate.
            issue_date=day, rides_left=38, amount=2000, mop=3, coupons=True,
            scaling=4, area_type=4,                   # LocDefType 204, zone map
            area_slots=bytes([0b00000111, 0, 0, 0])))  # zones 1, 2 and 3
    return "Demo 10 SPT coupons", unix(2026, 9, 21, 19, 50), [
        ("Type 2", pages),
    ]


# ====================================================================
# Card 7 - a rail season ticket card: GWR Touch
#
# Built from the shape of a real GWR Touch card read on 2026-09-28, which
# carried encodings none of the cards above had. The values are invented and so
# are the stations - Oxford rather than the real card's - but every structure is
# the real card's:
#
#   - DESFire sectors of 160 bytes, so each product is one sector with room to
#     spare (the other cards use 64 and 80)
#   - an ITSO ID at revision 1 with an empty bitmap: no name, no birth date,
#     just the language, a concession class and an entitlement expiry of
#     0x3FFF, "never" - and unused, its sector chained to itself
#   - period tickets at revision 2 with no Value Record Data Group, so their
#     expiry is the directory entry's alone - one with CPICC (bitmap 0x12) and
#     one without (0x02), a ValidityCode, an ExpiryTime past midnight, and a
#     ProductRetailer in a TS 1000-2 table B2 gap, which is what the real card's
#     retailers held
#   - revision 4 journey records in the two shapes a rail gate writes: a
#     check-out with no amount and no entry group, and a check-in carrying the
#     entry operator (ENTRY_OID) without the ENTRY group, each with the reader's
#     InstanceID after it
#   - a Directory InstanceID naming an ISAM whose OID uses the 16-bit extended
#     encoding (0x6009, the operator that runs the gate)
# ====================================================================
def card_gwr_touch():
    B, S, E, SCTL = 160, 16, 8, 7
    ACTIVE = S - 1
    OID = "0287"                                 # Great Western Railway, "GWR Touch"
    SEFT = 246                                   # owns rail season tickets
    RETAILER = 0x9000                            # 36864: in the B2 gap, as real ones were
    EXP = date_stamp(2035, 3, 14)

    shell = shell_dataset(IIN, OID, "0700007", fvc=7, ksc=4, kvc=1, expiry=EXP,
                          b=B, s=S, e=E, sctl=SCTL)

    # ---- E1: ITSO ID, revision 1, bitmap empty (TS 1000-5 table 22)
    ident = Bits(32)
    ident.put(0, 6, 8)                           # IPELength: 8 blocks
    ident.put(6, 6, 0)                           # IPEBitMap: nothing optional
    ident.put(12, 4, 1)
    ident.buf[2] = 255
    ident.buf[11] = 44                           # LanguageCode: English
    ident.put(130, 14, 0x3FFF)                   # EntitlementExpiryDate: never
    ident.buf[28] = 1                            # ConcessionaryClass: adult

    # ---- E2, E3: period tickets, revision 2 (table 27a). The mandatory part
    # is 28 bytes; CPICC (bit 4) comes next, then RouteCode and the locations.
    def season(cpicc, issued, start, paid, isam_id, isam_seq):
        t = Bits(48)
        t.put(0, 6, 12)
        t.put(6, 6, 0b010010 if cpicc is not None else 0b000010)
        t.put(12, 4, 2)
        t.buf[2] = 1                             # RemoveDate: a day after expiry
        t.putb(3, RETAILER.to_bytes(2, "big"))   # ProductRetailer
        t.put(40, 16, 0xFE00)                    # TYP22Flags: every part of every day
        t.put(64, 14, issued)                    # IssueDate
        t.put(78, 11, 1440 + 270)                # ExpiryTime: 04:30 the next day
        t.put(96, 3, 2)                          # Class: standard
        t.put(99, 5, 17)                         # ValidityCode
        t.put(104, 24, start)                    # ValidityStartDTS
        t.buf[17] = 0xFF                         # ValidOnDayCode: every day
        t.buf[18] = 1                            # PartySizeAdult
        t.putb(22, paid.to_bytes(4, "big"))      # AmountPaid, pence
        t.put(26 * 8, 4, 3)                      # MOP: card
        pos = 28
        if cpicc is not None:
            t.putb(pos, cpicc.to_bytes(2, "big"))
            pos += 2
        t.putb(pos, b"00000")                    # RouteCode: any permitted
        t.putb(pos + 5, loc1(203, b"3115"))      # Oxford
        t.putb(pos + 11, loc1(203, b"0035"))     # London Zone R1256
        return group(t, B, instance=instance_and_seal(kid=0, inp=0, isam_id=isam_id,
                                                      isam_seq=isam_seq))

    # Weekly GBP 128.00: an annual is 40 weeks of it, a month 3.84.
    annual = season(0, date_stamp(2025, 9, 30), dts(2025, 10, 1, 0, 0), 512000,
                    0x07B0A611, 1841)
    monthly = season(None, date_stamp(2026, 9, 4), dts(2026, 9, 5, 0, 0), 49152,
                     0x07B18901, 203377)

    entries = [
        dir_entry(287, 16, 1, False, 0),                             # E1 ID
        dir_entry(SEFT, 22, 19, False, date_stamp(2026, 9, 30)),     # E2 annual
        dir_entry(SEFT, 22, 19, False, date_stamp(2026, 10, 4)),     # E3 monthly
        b"\x00" * 5, b"\x00" * 5, b"\x00" * 5, b"\x00" * 5,
        log_entry(ptr=3, eei=0, when=dts(2026, 9, 18, 18, 49), record_offset=2,
                  passback=3),
    ]
    # The ID's sector points at itself, "never written" (TS 1000-2 5.1.5.2), as
    # the real card's did: the scheme issues it and nothing ever uses it.
    chain = {1: 1, 2: ACTIVE, 3: ACTIVE}

    # Tapped in at a gate run by 0x6009 at Paddington, out at Oxford. Readers in
    # 8160's range (FF00xxxx) wrote both, as on the real card.
    paddington = loc2(208, bcd("0070") + b"3087")
    oxford = loc2(208, bcd("0070") + b"3115")
    log = b"".join([
        tt_record_rev4(11, dts(2026, 9, 18, 17, 52), None, None, None, 3,
                       None, 0x6009, [3, 0, 0, 0], origin=paddington, iin=None,
                       cipe_flags=0, writer=0xFF00B51A),
        tt_record_rev4(12, dts(2026, 9, 18, 18, 49), None, None, oxford, 3,
                       None, None, [3, 0, 0, 0], origin=paddington, iin=None,
                       cipe_flags=0, writer=0xFF00A3C7),
        bytes(48), bytes(48),
    ])

    return "Demo 07 GWR Touch", unix(2026, 9, 21, 20, 5), [
        ("Shell", bytes(shell.buf)),
        ("Directory", directory(entries, chain, S, E, SCTL, 0x08,
                                instance=bytes([0x00]) + (0x004E30F3).to_bytes(4, "big"))),
        ("Product 1", group(ident, B, instance=instance_and_seal(
            kid=0, inp=0, isam_id=0x08F80411, isam_seq=30112))),
        ("Product 2", annual),
        ("Product 3", monthly),
        ("Log", log),
    ]


# ====================================================================
# Cards 8 and 9 - a full ITSO shell on an NFC Type 2 tag
#
# CMD9 (an NTAG215 or NTAG216) and CMD10 (a MIFARE Ultralight EV1) carry the
# same shell, directory and IPEs a smartcard does, over logical sectors at fixed
# pages (TS 1000-10 sections 10 and 11): nine sectors, a single IPE and the log,
# and TS 1000-10 annex A's software anti-tear - two copies of the directory and
# of each value record group, the records alternating between them. A read
# saves such a card as the blocks a smartcard is saved as, plus a "Tag" block of
# its chip pages, which is what these are.
# ====================================================================
def tag_block(uid, locks, abacus=None):
    """Pages 0-3 of a full-shell tag, as a read saves them."""
    return type2_full_page_memory(uid, bytes(32), b"", b"", {}, 64, locks=locks,
                                  abacus=abacus)[:16]


def full_type2_directory(entries, chain, sequence, isam_id):
    """A CMD9/CMD10 directory copy, padded to the ten pages it occupies."""
    d = directory(entries, chain, 9, 2, 3, sequence,
                  instance=bytes([0x10]) + isam_id.to_bytes(4, "big"))
    return d + bytes(40 - len(d))


def card_ntag():
    B = 64
    OID = "0163"                                 # Reading Buses
    EXP = date_stamp(2030, 8, 31)
    shell = shell_dataset(IIN, OID, "0800008", fvc=9, ksc=1, kvc=1, expiry=EXP,
                          b=B, s=9, e=2, sctl=3)

    # ---- E1: a carnet of ten bus rides, a TYP 23 journey ticket whose mode
    # group counts stored journeys (TS 1000-5 table 31a).
    carnet = Bits(36)
    carnet.put(0, 6, 9)
    carnet.put(6, 6, 0b001000)                   # the mode group only
    carnet.put(12, 4, 2)
    carnet.buf[2] = 30                           # RemoveDate: 30 days after expiry
    carnet.putb(3, (163).to_bytes(2, "big"))
    carnet.put(58, 14, date_stamp(2026, 9, 1))   # IssueDate
    carnet.put(93, 3, 2)                         # Class: standard
    carnet.buf[12] = 1                           # PartySizeAdult
    carnet.putb(16, (1800).to_bytes(4, "big"))   # AmountPaid: GBP 18
    carnet.put(20 * 8, 4, 3)                     # by card
    carnet.put(29 * 8 + 4, 4, 1)                 # TYP23Mode: stored journeys
    carnet.buf[30] = 1                           # MaxTransfers
    carnet.buf[31] = 120                         # TimeLimit: an hour
    carnet.putb(32, (180).to_bytes(2, "big"))    # ValueOfRideJourney: GBP 1.80

    # Four rides taken. Records alternate between the copies, odd TS# in B and
    # even in A (annex A.3.2.3), and the last, TS#4, went into A, which the
    # chain now names first.
    copy_a = value_group([
        value_record(7, 2, dts(2026, 9, 8, 8, 12), journey_tail(8, 0, 0b10)),
        value_record(7, 4, dts(2026, 9, 21, 17, 40), journey_tail(6, 0, 0b10)),
    ], format_rev=2)
    copy_b = value_group([
        value_record(1, 1, dts(2026, 9, 1, 9, 30), journey_tail(10, 0, 0)),
        value_record(7, 3, dts(2026, 9, 15, 8, 5), journey_tail(7, 1, 0b10)),
    ], format_rev=2)

    entries = [
        dir_entry(163, 23, 3, True, date_stamp(2027, 2, 28)),
        log_entry(ptr=1, eei=0, when=dts(2026, 9, 21, 17, 40), record_offset=0,
                  passback=5),
    ]
    # IPE in sector 1, then copy A (sector 5), copy B (sector 6), then S-1. The
    # log is Log File A in sector 2 linking to B in sector 3; Record Offset 0
    # says T0 is next to be written, so T1 is the newer.
    chain = {1: 5, 5: 6, 6: 8, 2: 3}
    log = b"".join([
        tt_record(12, dts(2026, 9, 15, 8, 5), 180,
                  origin=loc2(209, bus_stage(163, "17", 2)),
                  dest=loc2(209, bus_stage(163, "17", 9)), ipe_ptr=1, mop=8),
        tt_record(12, dts(2026, 9, 21, 17, 40), 180,
                  origin=loc2(209, bus_stage(163, "17", 9)),
                  dest=loc2(209, bus_stage(163, "17", 2)), ipe_ptr=1, mop=8),
    ])

    return "Demo 08 Reading Buses", unix(2026, 9, 21, 20, 15), [
        ("Shell", bytes(shell.buf)),
        # The Abacus has counted the four value records, plus the one it
        # starts at: state 5, of the 16 that retire the card.
        ("Tag", tag_block(bytes([0x04, 0x63, 0x08, 0x2E, 0x51, 0x9A, 0x40]),
                          bytes([0xF7, 0x0F]), abacus=5)),
        ("Directory", full_type2_directory(entries, chain, 0x09, isam(163, 0x0A21))),
        ("Product 1", group(carnet, B, copy_a) + pad_sector(copy_b + instance_and_seal(), B)),
        ("Log", log),
    ]


def card_ultralight_ev1():
    B = 128
    OID = "0162"                                 # Xplore Dundee
    EXP = date_stamp(2029, 12, 31)
    # With every optional element present, as TS 1000-10 clause 11.10.1 allows:
    # the MCRN takes the shell to eight blocks, filling the eight pages it is
    # stored in, and puts 0x20 rather than 0x18 in the byte the rotation moves.
    shell = shell_dataset(IIN, OID, "0900009", fvc=10, ksc=1, kvc=1, expiry=EXP,
                          b=B, s=9, e=2, sctl=3, mcrn="4917250331")

    # ---- E1: a four-week pass, TYP 22 revision 3, anywhere in the operator's
    # area: no locations, so the area is its to define.
    period = Bits(32)
    period.put(0, 6, 8)
    period.put(6, 6, 0)
    period.put(12, 4, 3)
    period.buf[2] = 7                            # RemoveDate: a week after expiry
    period.putb(3, (162).to_bytes(2, "big"))
    period.put(40, 16, 0xFE00)                   # TYP22Flags: every part of every day
    period.put(64, 14, date_stamp(2026, 9, 7))   # IssueDate
    period.put(78, 11, 1440 + 60)                # ExpiryTime: 01:00 the next day
    period.put(96, 3, 2)                         # Class: standard
    period.put(106, 14, date_stamp(2026, 9, 7))  # ValidityStartDate
    period.buf[18] = 0xFF                        # ValidOnDayCode: every day
    period.buf[19] = 1                           # PartySizeAdult
    period.putb(23, (5600).to_bytes(4, "big"))   # AmountPaid: GBP 56
    period.put(27 * 8, 4, 3)                     # by card

    current = value_group([
        value_record(1, 2, dts(2026, 9, 7, 7, 55),
                     period_tail(0, 0b01, date_stamp(2026, 10, 4), date_stamp(2026, 10, 4))),
        bytes(15),
    ], format_rev=3)
    previous = value_group([
        value_record(1, 1, dts(2026, 9, 6, 16, 20),
                     period_tail(1, 0b01, date_stamp(2026, 10, 4), date_stamp(2026, 9, 6))),
        bytes(15),
    ], format_rev=3)

    entries = [
        dir_entry(162, 22, 1, True, date_stamp(2026, 10, 4)),
        log_entry(ptr=1, eei=0, when=dts(2026, 9, 19, 8, 2), record_offset=1,
                  passback=10),
    ]
    chain = {1: 5, 5: 6, 6: 8, 2: 3}
    # Record Offset 1: T0 was written last, and T1 is the older journey.
    log = b"".join([
        tt_record(12, dts(2026, 9, 19, 8, 2), 0,
                  origin=loc2(209, bus_stage(162, "22", 5)), dest=None, ipe_ptr=1,
                  mop=8),
        tt_record(12, dts(2026, 9, 18, 17, 31), 0,
                  origin=loc2(209, bus_stage(162, "22", 11)),
                  dest=loc2(209, bus_stage(162, "22", 5)), ipe_ptr=1, mop=8),
    ])

    return "Demo 09 MyXplore", unix(2026, 9, 21, 20, 25), [
        ("Shell", bytes(shell.buf)),
        # Locked as TS 1000-10 clause 10.23.1 recommends. CMD10 keeps its
        # transaction count in a one-way counter, not page 3.
        ("Tag", tag_block(bytes([0x04, 0x62, 0x10, 0x3C, 0x7D, 0x81, 0x55]),
                          bytes([0xF7, 0x0F]))),
        ("Directory", full_type2_directory(entries, chain, 0x03, isam(162, 0x0311))),
        ("Product 1", group(period, B, current) + pad_sector(previous + instance_and_seal(), B)),
        ("Log", log),
    ]


# ====================================================================
# Card 11 - a Brighton commuter's Southern "The Key"
#
# The Key is Go-Ahead's scheme, and Southern issues it under an OID of its own
# (143) where Southeastern's is 289. This one carries the older layouts: a
# revision 1 season ticket - the only revision that flags its two locations
# independently rather than behind RouteCode - which Southern has stopped
# after a refund, a revision 3 Gatwick Express return, a purse run into its
# overdraft, and a business travel account (TYP 4, charge to account by value),
# which no other card has.
# ====================================================================
def card_key_sussex():
    B, S, E, SCTL = 64, 16, 8, 7
    ACTIVE, BLOCKED = S - 1, S - 2
    OID = "0143"                                 # Southern, brand "The Key"
    EXP = date_stamp(2031, 5, 31)

    shell = shell_dataset(IIN, OID, "1100011", fvc=7, ksc=4, kvc=1, expiry=EXP,
                          b=B, s=S, e=E, sctl=SCTL)

    # ---- E1: pay as you go, spent past zero - which is what the overdraft in
    # the dataset is for.
    purse = Bits(24)
    purse.put(0, 6, 6)
    purse.put(6, 6, 0)
    purse.put(12, 4, 1)
    purse.buf[2] = 255
    purse.putb(3, (143).to_bytes(2, "big"))
    purse.putb(10, (5000).to_bytes(2, "big"))    # MaxValue2: GBP 50
    purse.putb(12, (200).to_bytes(2, "big"))     # MaximumNegativeAmount: GBP 2
    purse_values = value_group([
        value_record(7, 88, dts(2026, 8, 30, 8, 20), purse_tail(65)),
        value_record(7, 89, dts(2026, 9, 2, 8, 19), purse_tail(-120 & 0xFFFF)),
    ], format_rev=1)

    # ---- E2: period ticket, revision 1 - the only revision that flags its two
    # locations independently rather than behind RouteCode. Brighton to London
    # Victoria, stopped after the holder asked for a refund.
    period = Bits(40)
    period.put(0, 6, 10)
    period.put(6, 6, 0b000110)                   # bit 2 origin, bit 1 destination
    period.put(12, 4, 1)
    period.buf[2] = 255
    period.putb(3, (143).to_bytes(2, "big"))     # sold by Southern
    period.putb(26, loc1(203, b"5268"))          # Brighton
    period.putb(32, loc1(203, b"5426"))          # London Victoria
    period_values = value_group([
        value_record(1, 3, dts(2026, 4, 2, 9, 0),
                     period_tail(2, 0b00, date_stamp(2026, 11, 30), date_stamp(2026, 5, 1))),
        value_record(13, 4, dts(2026, 5, 2, 8, 55),
                     period_tail(1, 0b00, date_stamp(2026, 11, 30), date_stamp(2026, 6, 1))),
    ], format_rev=1)

    # ---- E3: journey ticket, revision 3 (table 31b): a Gatwick Express return,
    # mode 3, whose legs may be joined by one change within 45 minutes 30
    # seconds. Sixty bytes of dataset, so it runs into a second sector.
    ret = Bits(60)
    ret.put(0, 6, 15)
    ret.put(6, 6, 0b001010)                      # mode group, route and locations
    ret.put(12, 4, 3)
    ret.buf[2] = 7
    ret.putb(3, (143).to_bytes(2, "big"))
    ret.buf[5] = 0b01100000                      # TYP23Flags: print ticket and receipt
    ret.put(58, 14, date_stamp(2026, 9, 5))      # IssueDate
    ret.put(72, 24, dts(2026, 9, 6, 6, 0))       # ValidityStartDTS
    ret.put(101, 11, 1440 + 180)                 # ExpiryTime: 03:00 the day after
    ret.put(117, 3, 2)                           # Class: standard
    ret.buf[15] = 1                              # PartySizeAdult
    ret.putb(19, (4090).to_bytes(4, "big"))      # AmountPaid: GBP 40.90
    ret.put(23 * 8, 4, 1)                        # AmountPaidMethodOfPayment: cash
    ret.put(33 * 8 + 4, 4, 3)                    # TYP23Mode: return
    ret.buf[34] = 1                              # MaxTransfers
    ret.buf[35] = 91                             # TimeLimit: 91 x 30 s
    ret.putb(36, (2045).to_bytes(4, "big"))      # ValueOfRideJourney: GBP 20.45
    ret.putb(41, b"00000")                       # RouteCode: any permitted
    ret.putb(46, loc1(203, b"5416"))             # Gatwick Airport
    ret.putb(52, loc1(203, b"5426"))             # London Victoria

    # ---- E4: charge to account by value, TYP 4 (TS 1000-5 table 11): an
    # employer's travel account, spent against a monthly limit and billed.
    account = Bits(20)
    account.put(0, 6, 5)
    account.put(6, 6, 0)
    account.put(12, 4, 1)
    account.buf[2] = 255
    account.putb(3, (143).to_bytes(2, "big"))
    account.buf[5] = 0b01000000                  # TYP4Flags: print a receipt
    account.putb(6, (40000).to_bytes(2, "big"))  # MaxValue4: GBP 400 a month
    account.put(80, 14, date_stamp(2026, 4, 1))  # StartDateCTA
    account.put(94, 14, date_stamp(2027, 3, 31)) # EndDate
    account_values = value_group([
        value_record(7, 17, dts(2026, 9, 1, 7, 58), purse_tail(2310)),
        value_record(7, 18, dts(2026, 9, 3, 18, 2), purse_tail(4620)),
    ], format_rev=1)

    entries = [
        dir_entry(143, 2, 0, True, EXP),                            # E1 purse
        dir_entry(143, 22, 1, True, date_stamp(2026, 11, 30)),      # E2 blocked
        dir_entry(143, 23, 7, False, date_stamp(2026, 9, 30)),      # E3 return
        dir_entry(143, 4, 0, True, date_stamp(2027, 3, 31)),        # E4 account
        bytes(5), bytes(5), bytes(5),                               # E5-E7 unused
        log_entry(ptr=3, eei=0, when=dts(2026, 9, 6, 14, 38), record_offset=3,
                  passback=5),
    ]
    chain = {1: 9, 9: ACTIVE, 2: 10, 10: BLOCKED, 3: 5, 5: ACTIVE, 4: 12, 12: ACTIVE}

    # The season's last journey before it was stopped, then the outward half
    # of the return: in at Gatwick Airport, out at Victoria half an hour later.
    log = b"".join([
        tt_record(12, dts(2026, 5, 1, 8, 51), 0,
                  origin=loc2(203, b"5268"), dest=loc2(203, b"5426"), ipe_ptr=2),
        tt_record(11, dts(2026, 9, 6, 14, 5), 0,
                  origin=loc2(203, b"5416"), dest=None, ipe_ptr=3),
        tt_record(12, dts(2026, 9, 6, 14, 38), 0,
                  origin=loc2(203, b"5416"), dest=loc2(203, b"5426"), ipe_ptr=3),
        bytes(48),
    ])

    return "Demo 11 The Key Sussex", unix(2026, 9, 21, 20, 30), [
        ("Shell", bytes(shell.buf)),
        ("Directory", directory(entries, chain, S, E, SCTL, 0x2C)),
        ("Product 1", group(purse, B, purse_values)),
        ("Product 2", group(period, B, period_values)),
        ("Product 3", group(ret, B)),
        ("Product 4", group(account, B, account_values)),
        ("Log", log),
    ]


CARDS = [card_the_key, card_blocked, card_cmd2, card_history, card_subway_paper,
         card_subway_return, card_gwr_touch, card_ntag, card_ultralight_ev1,
         card_zonal_coupons, card_key_sussex]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "demo-cards"
    os.makedirs(out, exist_ok=True)
    for build in CARDS:
        name, read_at, blocks = build()
        path = write_card(os.path.join(out, name + ".flipso"), read_at, blocks)
        print(f"{path}: {len(blocks)} blocks, "
              f"{sum(len(b) for _, b in blocks)} bytes of card")


if __name__ == "__main__":
    main()
