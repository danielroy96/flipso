#!/usr/bin/env python3
"""Build demo ITSO cards as saved-card files, ready to copy to the Flipper.

A saved card is the raw blocks a read produced, so a file written here decodes
on the device exactly as a card would if it were tapped - through the same
parsers, on whatever build is running. That makes a synthetic card the only way
to see most of Flipso without owning the card that carries the feature: nobody
has a wallet with a loyalty IPE, a charge-to-account product, a blocked shell
and a revision 1 period ticket in it.

So these four cards are built to cover the app rather than to be plausible
wallets. Between them they reach every screen, both the CMD7 and CMD2
geometries, every IPE type the decoder names, every value record tail it
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
    Bits, bcd, charge_tail, count_tail, date_stamp, dir_entry, dts,
    instance_and_seal, isrn, journey_tail, loc1, loc2, log_entry, loyalty_tail,
    naptan, pad_sector, period_tail, purse_tail, shell_dataset, sncode, sncode2,
    tt_record, tt_record_rev4, value_group, value_record, voucher_tail)

IIN = "633597"


def unix(y, mo, d, h=12, mi=0):
    return calendar.timegm(datetime.datetime(y, mo, d, h, mi).timetuple())


def sct_bits(sector_count):
    """psi: the smallest number of bits with S <= 2^psi (TS 1000-2 5.1.5.1)."""
    psi = 1
    while (1 << psi) < sector_count and psi < 8:
        psi += 1
    return psi


def directory(entries, chain, sector_count, dir_entries, sct_len, sequence,
              blocked=False):
    """A Directory Data Group: the entries, the Sector Chain Table, DIRS#.

    @p chain maps a sector to the sector that follows it. A chain ending at S-1
    is a product in use, one ending at S-2 is blocked, and one pointing at
    itself has never been written (TS 1000-2 clause 5.1.5.2).
    """
    base = 2 + 5 * dir_entries
    d = Bits(base + sct_len + 1)
    d.put(0, 6, 0)                              # DIRLength: RFU
    # DIRBitMap: bit 0 stops the whole shell, bits 2:1 say the last entry is a
    # log entry. The two sit next to each other, which is why the blocking bit
    # is worth writing deliberately rather than by offset from the top.
    d.put(6, 6, 0b000010 | (1 if blocked else 0))
    d.put(12, 4, 1)                             # DIRFormatRevision
    for i, entry in enumerate(entries):
        d.putb(2 + i * 5, entry)

    psi = sct_bits(sector_count)
    for sector in range(1, sector_count):
        bit = base * 8 + (sector - 1) * psi
        if bit + psi > (base + sct_len) * 8:
            break
        d.put(bit, psi, chain.get(sector, 0))
    d.buf[base + sct_len] = sequence
    return bytes(d.buf)


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


def write_card(path, read_at, blocks):
    lines = ["Filetype: Flipso card", "Version: 1", f"Read at: {read_at}"]
    for key, data in blocks:
        lines.append(f"{key}: " + " ".join(f"{b:02X}" for b in data))
    with open(path, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    return path


# ====================================================================
# Card 1 - a rail smartcard carrying one of everything
#
# Eleven products, which is more than any real card would hold, chosen so that
# every IPE type the decoder has a case for is on one card: a purse with a
# journey in progress, an ID, both the period and journey tickets, loyalty
# points, a charge-to-account, a voucher, an entitlement, and three products in
# the states the list flags - blocked, expired and never used. The shell
# carries an MCRN, which is the one shell element the other three leave out.
# ====================================================================
def card_the_key():
    B, S, E, SCTL = 64, 32, 12, 20
    ACTIVE, BLOCKED = S - 1, S - 2
    OID = "0289"                                 # Southeastern, brand "The Key"
    EXP = date_stamp(2031, 8, 31)

    shell = shell_dataset(IIN, OID, "0100001", fvc=7, ksc=4, kvc=1, expiry=EXP,
                          b=B, s=S, e=E, sctl=SCTL, mcrn="1234567890123456")

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
        value_record(14, 342, dts(2026, 9, 21, 17, 46),
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

    # ---- E3: period ticket, revision 3 (TS 1000-5 clause 2.9.3)
    period = Bits(48)
    period.put(0, 6, 12)
    period.put(6, 6, 0b000010)                   # RouteCode and both locations
    # No CPICC (bit 4), so RouteCode follows the 29 fixed bytes directly and the
    # locations start at 29 + 5 = 34 (TS 1000-5 table 3.27).
    period.put(12, 4, 3)
    period.buf[2] = 255
    period.putb(3, (289).to_bytes(2, "big"))
    period.put(106, 14, date_stamp(2026, 9, 1))  # ValidityStartDate
    period.putb(34, loc1(203, b"5148"))          # London Bridge
    period.putb(40, loc1(203, b"5018"))          # Margate
    period_values = value_group([
        value_record(1, 21, dts(2026, 9, 1, 7, 40),
                     period_tail(6, 0b01, date_stamp(2027, 1, 31), date_stamp(2026, 9, 30))),
        value_record(13, 22, dts(2026, 9, 21, 7, 38),
                     period_tail(5, 0b01, date_stamp(2027, 1, 31), date_stamp(2026, 10, 20))),
    ], format_rev=3)

    # ---- E4: journey ticket, revision 2 (TS 1000-5 table 31a)
    journey = Bits(52)
    journey.put(0, 6, 13)
    journey.put(6, 6, 0b001010)                  # mode group, route and locations
    journey.put(12, 4, 2)
    journey.buf[2] = 30                          # RemoveDate: 30 days after expiry
    journey.putb(3, (109).to_bytes(2, "big"))
    journey.putb(35, b"00000")                   # RouteCode
    journey.putb(40, loc1(203, b"5631"))         # Guildford
    journey.putb(46, loc1(203, b"5685"))         # Woking
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
    loyalty.putb(3, (96).to_bytes(2, "big"))
    loyalty_values = value_group([
        value_record(1, 44, dts(2026, 7, 2, 10, 15), loyalty_tail(4250)),
        value_record(9, 45, dts(2026, 9, 14, 19, 2), loyalty_tail(5100)),
    ], format_rev=1)

    # ---- E6: charge to account, TYP 5 (TS 1000-5 table 15)
    charge = Bits(24)
    charge.put(0, 6, 6)
    charge.put(6, 6, 0)
    charge.put(12, 4, 1)
    charge.buf[2] = 255
    charge.putb(3, (8000).to_bytes(2, "big"))
    charge.buf[6] = 4                            # WeeksPerChargePeriod
    charge.buf[7] = 60                           # MaxTransactionsPerPeriod
    charge.putb(8, (25000).to_bytes(2, "big"))   # MaxValue5: GBP 250 a period
    charge.putb(10, (1000).to_bytes(2, "big"))   # DepositAmount: GBP 10
    charge.put(96, 14, date_stamp(2026, 4, 6))   # StartDateCTA
    charge.put(110, 14, date_stamp(2027, 4, 5))  # EndDate, 3.75 bytes in
    charge.buf[15] |= 6                          # DepositMethodOfPayment: direct debit
    charge.put(128, 4, 0)                        # DepositCurrencyCode
    charge_values = value_group([
        value_record(15, 9, dts(2026, 8, 6, 9, 0), charge_tail(0, date_stamp(2026, 8, 6))),
        value_record(7, 10, dts(2026, 9, 19, 18, 22),
                     charge_tail(23, date_stamp(2026, 9, 6), legs=1)),
    ], format_rev=1)

    # ---- E7: a voucher, which the decoder reports from its directory entry and
    # its value record alone - there is no TYP 25 dataset parser.
    voucher = Bits(16)
    voucher.put(0, 6, 4)
    voucher.put(6, 6, 0)
    voucher.put(12, 4, 1)
    voucher.buf[2] = 0                           # RemoveDate: removable at expiry
    voucher.putb(3, (247).to_bytes(2, "big"))    # sold by c2c
    voucher_values = value_group([
        value_record(1, 2, dts(2026, 6, 1, 11, 0), voucher_tail(4, auto_renew=True)),
        value_record(7, 3, dts(2026, 9, 11, 8, 44), voucher_tail(3, auto_renew=True)),
    ], format_rev=1)

    # ---- E8: entitlement, TYP 14 revision 2, carrying a zonal validity
    ent = Bits(32)
    ent.put(0, 6, 8)
    ent.put(6, 6, 0b000100)                      # bit 2: ValidAtOrFrom present
    ent.put(12, 4, 2)
    ent.buf[2] = 255
    ent.buf[5] = 0b00010101                      # IDFlags: photo, female, companion
    ent.put(50, 6, 60)                           # PassbackTime
    ent.put(90, 14, date_stamp(2026, 4, 1))      # EntitlementStartDate
    ent.put(104, 14, date_stamp(2027, 3, 31))    # EntitlementExpiryDate
    ent.buf[20] = 14                             # EntitlementCode: free travel
    ent.buf[21] = 5                              # ConcessionaryClass: disabled
    ent.putb(24, loc1(204, bytes([0b00000111, 0, 0])))  # zones 1, 2 and 3

    # ---- E9 and E10: a blocked product and an expired one, each with a value
    # group whose tail the decoder does not claim to understand.
    carnet = Bits(16)
    carnet.put(0, 6, 4)
    carnet.put(12, 4, 1)
    carnet.buf[2] = 255
    carnet.putb(3, (289).to_bytes(2, "big"))
    carnet_values = value_group([
        value_record(1, 1, dts(2026, 2, 3, 9, 30), count_tail(10)),
        value_record(2, 2, dts(2026, 5, 9, 9, 31), count_tail(6)),
    ], format_rev=1)

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
        # screen says in as many words.
        dir_entry(109, 23, 4, True, date_stamp(2026, 12, 31), foreign=True),
        dir_entry(96, 3, 0, True, 0),                                # E5 loyalty, no expiry
        dir_entry(8000, 5, 0, True, date_stamp(2028, 6, 30)),        # E6 charge to account
        dir_entry(247, 25, 2, True, date_stamp(2026, 10, 31)),       # E7 voucher
        dir_entry(165, 14, 0, False, date_stamp(2027, 3, 31)),       # E8 entitlement
        # E9 uses the extended IPE-owner range: raw 5678 with the flag set is 13870.
        dir_entry(5678, 28, 0, True, date_stamp(2027, 5, 31), extended=True),
        dir_entry(289, 24, 0, True, date_stamp(2026, 3, 31)),        # E10 expired
        dir_entry(289, 29, 0, False, date_stamp(2029, 1, 31)),       # E11 never used
        log_entry(ptr=1, eei=1, when=dts(2026, 9, 21, 17, 46), record_offset=0,
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
        tt_record_rev4(12, dts(2026, 9, 18, 8, 12), 460,
                       via=loc2(203, b"5571"),           # via Surbiton
                       dest=loc2(203, b"5685"),          # Woking
                       ipe_ptr=1, entry_when=dts(2026, 9, 18, 7, 41), entry_oid=109,
                       candidates=[1, 4, 0, 0], mop=8, vat=0),
        tt_record(12, dts(2026, 9, 19, 12, 31), 165,
                  origin=loc2(206, naptan("cumfatda")),
                  dest=loc2(206, naptan("manwpwjm")), ipe_ptr=1),
        tt_record_rev4(12, dts(2026, 9, 20, 8, 3), 0,
                       via=loc2(203, b"5004"),           # via Ashford International
                       dest=loc2(203, b"5018"),          # Margate
                       ipe_ptr=4, entry_when=dts(2026, 9, 20, 7, 2), entry_oid=289,
                       candidates=[4, 1, 3, 0], no_fare=True, cipe_flags=0b11, vat=0),
        tt_record(11, dts(2026, 9, 21, 17, 46), 0,
                  origin=loc2(203, b"5148"), dest=None, ipe_ptr=1),
    ])

    return "Demo 1 The Key", unix(2026, 9, 21, 19, 12), [
        ("Shell", bytes(shell.buf)),
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
        ("Product 9", group(carnet, B, carnet_values)),
        ("Product 10", group(reserved, B, reserved_values)),
        # E11 has no block at all: an entry the card lists and Flipso could not
        # read, which is what "Detail not decoded" is for.
        ("Log", log),
    ]


# ====================================================================
# Card 2 - a concessionary pass the issuer has stopped
#
# The blocking indicator is the headline fact about a card, and it changes four
# things at once: the menu title and its icon, the banner on the Card screen,
# the missing Active line, and the tone the scan ends on. The products under it
# are the revision 1 layouts - an ID and an entitlement written to the older
# tables, which the other cards do not exercise - plus a purse in overdraft.
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
    ident.buf[5] = 0b00010101                    # IDFlags: photo, female, companion
    ident.put(50, 6, 45)                         # PassbackTime
    ident.putb(7, bcd("19480922"))
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
    ent.buf[5] = 0b00010101
    ent.put(50, 6, 45)
    ent.put(90, 14, EXP)                         # EntitlementExpiryDate
    ent.buf[18] = 2                              # EntitlementCode: limited free ride
    ent.buf[19] = 4                              # ConcessionaryClass: pensioner

    # ---- E3: a purse that has been spent past zero, which is what the
    # overdraft in the dataset is for.
    purse = Bits(24)
    purse.put(0, 6, 6)
    purse.put(6, 6, 0)
    purse.put(12, 4, 1)
    purse.buf[2] = 255
    purse.putb(3, (226).to_bytes(2, "big"))
    purse.putb(10, (5000).to_bytes(2, "big"))    # MaxValue2: GBP 50
    purse.putb(12, (200).to_bytes(2, "big"))     # MaximumNegativeAmount: GBP 2
    purse_values = value_group([
        value_record(7, 88, dts(2026, 8, 30, 8, 20), purse_tail(65)),
        value_record(7, 89, dts(2026, 9, 2, 8, 19), purse_tail(-120 & 0xFFFF)),
    ], format_rev=1)

    # ---- E4: a compact period pass, expired
    compact = Bits(16)
    compact.put(0, 6, 4)
    compact.put(12, 4, 1)
    compact.buf[2] = 255
    compact.putb(3, (226).to_bytes(2, "big"))

    # ---- E5: period ticket, revision 1 - the only revision that flags its two
    # locations independently rather than behind RouteCode.
    period = Bits(40)
    period.put(0, 6, 10)
    period.put(6, 6, 0b000110)                   # bit 2 origin, bit 1 destination
    period.put(12, 4, 1)
    period.buf[2] = 255
    period.putb(3, (143).to_bytes(2, "big"))     # sold by Southern
    period.putb(26, loc1(203, b"5416"))          # Gatwick Airport
    period.putb(32, loc1(203, b"5148"))          # London Bridge
    period_values = value_group([
        value_record(1, 3, dts(2026, 4, 2, 9, 0),
                     period_tail(2, 0b00, date_stamp(2026, 11, 30), date_stamp(2026, 5, 1))),
        value_record(13, 4, dts(2026, 5, 2, 8, 55),
                     period_tail(1, 0b00, date_stamp(2026, 11, 30), date_stamp(2026, 6, 1))),
    ], format_rev=1)

    entries = [
        dir_entry(226, 16, 0, False, EXP),                          # E1 ITSO ID
        dir_entry(96, 14, 0, False, EXP),                           # E2 entitlement
        dir_entry(226, 2, 0, True, EXP),                            # E3 purse
        dir_entry(226, 27, 0, False, date_stamp(2026, 6, 30)),      # E4 expired
        dir_entry(143, 22, 1, True, date_stamp(2026, 11, 30)),      # E5 blocked
        bytes(5), bytes(5),                                         # E6-E7 unused
        # Basic mode: the POST updates the log entry and writes no journey
        # record, so the card has a last tap and no log to show for it.
        log_entry(ptr=3, eei=0, when=dts(2026, 9, 2, 8, 19), record_offset=0,
                  passback=0, normal_mode=False),
    ]
    chain = {1: ACTIVE, 2: ACTIVE, 3: 9, 9: ACTIVE, 4: ACTIVE, 5: 11, 11: BLOCKED}

    return "Demo 2 blocked pass", unix(2026, 9, 21, 19, 20), [
        ("Shell", bytes(shell.buf)),
        ("Directory", directory(entries, chain, S, E, SCTL, 0x11, blocked=True)),
        ("Product 1", group(ident, B)),
        ("Product 2", group(ent, B)),
        ("Product 3", group(purse, B, purse_values)),
        ("Product 4", group(compact, B)),
        ("Product 5", group(period, B, period_values)),
    ]


# ====================================================================
# Card 3 - the other customer media
#
# CMD2 is ITSO on ISO 7816 rather than DESFire, and what shows on screen is the
# geometry: 80-byte sectors, 64 of them and 16 directory entries, against the
# 64/16/8 the DESFire cards here use. The purse has no expiry date and a value
# record that has never been written, which are both things the decoder has to
# handle rather than display.
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

    # Period ticket, revision 2 - the middle layout, where CPICC and the pass
    # duration shift the locations rather than the bitmap gating them.
    period = Bits(48)
    period.put(0, 6, 12)
    period.put(6, 6, 0b000010)                   # RouteCode and both locations
    # No CPICC (bit 4), so RouteCode follows the 29 fixed bytes directly and the
    # locations start at 29 + 5 = 34 (TS 1000-5 table 3.27).
    period.put(12, 4, 2)
    period.buf[2] = 255
    period.putb(3, (196).to_bytes(2, "big"))
    period.putb(33, loc1(207, (1).to_bytes(4, "big")))    # Zone 1
    period.putb(39, loc1(207, (2).to_bytes(4, "big")))    # Zone 2
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
        log_entry(ptr=1, eei=0, when=dts(2026, 9, 19, 18, 12), record_offset=2,
                  passback=8),
    ]
    chain = {1: 18, 18: ACTIVE, 2: ACTIVE, 3: 20, 20: ACTIVE}

    # Two records, and two location renderings a rail card never produces: a
    # list of NaptanCodes, and a bus fare stage keyed by service number.
    log = b"".join([
        tt_record(12, dts(2026, 9, 17, 8, 2), 170,
                  origin=loc2(212, naptan("buchanan")),
                  dest=loc2(209, bus_stage(196, "SPT1", 4)),
                  ipe_ptr=1, mop=8),
        tt_record(12, dts(2026, 9, 19, 18, 12), 0,
                  origin=loc2(207, (1).to_bytes(4, "big")),
                  dest=loc2(207, (2).to_bytes(4, "big")), ipe_ptr=3),
    ])

    return "Demo 3 Subway CMD2", unix(2026, 9, 21, 19, 26), [
        ("Shell", bytes(shell.buf)),
        ("Directory", directory(entries, chain, S, E, SCTL, 0x07)),
        ("Product 1", group(purse, B, purse_values)),
        ("Product 2", group(ident, B)),
        ("Product 3", group(period, B, period_values)),
        ("Log", log),
    ]


# ====================================================================
# Card 4 - what only a saved card knows
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

    # A journey ticket whose destination is a bus stop reached by a named
    # service: LocDefType 216 carries both, and only an IPE can hold one - the
    # six-byte body a log record uses is too short for it.
    # Fifteen blocks rather than thirteen: a 216 location is eleven bytes with
    # its tag and length, and a dataset that ends before it does would run the
    # location into the InstanceID behind it.
    journey = Bits(60)
    journey.put(0, 6, 15)
    journey.put(6, 6, 0b001010)
    journey.put(12, 4, 2)
    journey.buf[2] = 255
    journey.putb(3, (109).to_bytes(2, "big"))
    journey.putb(35, b"00000")
    journey.putb(40, loc1(203, b"5685"))         # Woking
    journey.putb(46, loc1(216, (109).to_bytes(2, "big") + sncode2("X15") +
                          naptan("wokgrand")))
    journey_values = value_group([
        value_record(1, 14, dts(2026, 9, 7, 10, 0), journey_tail(6, 0, 0b01)),
        value_record(7, 15, dts(2026, 9, 20, 18, 31), journey_tail(5, 2, 0b01)),
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
                  origin=loc2(203, b"5685"), dest=None, ipe_ptr=1),
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
    # And one taken on a bus, on the ticket that has since left the card.
    older.append(tt_record(12, dts(2026, 8, 28, 16, 20), 210,
                           origin=loc2(206, naptan("wokgrand")),
                           dest=loc2(206, naptan("guilbdst")), ipe_ptr=4, mop=8))

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
    gone_period.putb(40, loc1(203, b"1575"))     # Waterloo International
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
    gone_voucher.putb(3, (8000).to_bytes(2, "big"))
    gone_voucher_values = value_group([
        value_record(1, 4, dts(2025, 12, 20, 14, 2), voucher_tail(2)),
        value_record(7, 5, dts(2025, 12, 24, 9, 18), voucher_tail(1)),
    ], format_rev=1)

    gone_purse = Bits(24)
    gone_purse.put(0, 6, 6)
    gone_purse.put(6, 6, 0)
    gone_purse.put(12, 4, 1)
    gone_purse.buf[2] = 255
    gone_purse.putb(3, (246).to_bytes(2, "big"))
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
    gone_ent.buf[20] = 15                        # EntitlementCode: half fare
    gone_ent.buf[21] = 3                         # ConcessionaryClass: student

    return "Demo 4 past reads", unix(2026, 9, 21, 19, 33), [
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
            dir_entry(8000, 25, 0, True, date_stamp(2025, 12, 31)),
            group(gone_voucher, B, gone_voucher_values))),
        ("Product history 102", history_block(
            unix(2025, 8, 1, 9, 30), 2,
            dir_entry(246, 2, 0, True, date_stamp(2025, 8, 31)),
            group(gone_purse, B, gone_purse_values))),
        ("Product history 103", history_block(
            unix(2025, 6, 30, 18, 45), 6,
            dir_entry(165, 14, 0, False, date_stamp(2025, 6, 30)),
            group(gone_ent, B))),
    ]


CARDS = [card_the_key, card_blocked, card_cmd2, card_history]


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
