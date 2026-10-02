"""Build spec-accurate synthetic ITSO cards and emit them as a C header.

Two cards are generated: a CMD7 (DESFire) one with the default geometry, and a
CMD2 (generic micro-processor) one with the larger geometry that real CMD2 cards
turn out to use - 80-byte sectors, 64 of them, 16 directory entries, and so a
six-bit Sector Chain Table rather than the four-bit one CMD7 needs.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from itso_build import (  # noqa: E402
    Bits, bcd, charge_tail, date_stamp, dir_entry, dts, instance_and_seal, journey_tail,
    capping_vgx, isam, log_entry, loc1, loyalty_tail, loc2, luhn, nlc, pad, period_tail, purse_tail,
    put_secrc, rail_retailer, reservation_tail, reservation_vgx, tt_record, tt_record_rev4, type2_page_memory,
    typ24_dataset, typ27_dataset, typ28_dataset, typ29_dataset, value_group,
    value_record, directory, isrn, shell_dataset, split_sectors, type2_full_page_memory)

# ---------------------------------------------------------------- Shell (FID 15)
IIN, OID, ISSN = "633597", "1234", "0012345"
CHD = luhn(IIN + OID + ISSN)
ISRN = IIN + OID + ISSN + CHD

shell = Bits(32)
shell.put(0, 6, 6)          # ShellLength: 6 blocks of 4 bytes
shell.put(6, 6, 0b000001)   # ShellBitMap: full shell, no MCRN
shell.put(12, 4, 1)         # ShellFormatRevision
shell.putb(2, bcd(IIN))
shell.putb(5, bcd(OID))
shell.putb(7, bcd(ISSN + CHD))
shell.buf[11] = 7           # FVC = 7 -> DESFire CMD7
shell.buf[12] = 4           # KSC
shell.buf[13] = 1           # KVC
shell.put(114, 14, date_stamp(2030, 6, 30))  # EXP
shell.buf[16] = 64          # B
shell.buf[17] = 16          # S
shell.buf[18] = 8           # e#
shell.buf[19] = 7           # SCTL
put_secrc(shell)            # SECRC over everything above

# ---------------------------------------------------------------- Directory (FID 0)
d = Bits(64)
d.put(0, 6, 0)              # DIRLength: RFU
d.put(6, 6, 0b000010)       # DIRBitMap: last entry is a log entry
d.put(12, 4, 1)             # DIRFormatRevision
entries = [
    dir_entry(1234, 2, 0, True, date_stamp(2030, 6, 30)),   # E1 pay as you go
    dir_entry(1234, 16, 1, False, date_stamp(2029, 3, 31)), # E2 ITSO ID
    # E3 uses the extended IPE-owner range: raw 5678 with the flag set is 13870.
    dir_entry(5678, 22, 3, True, date_stamp(2025, 12, 31), extended=True),
    # E4 a journey ticket with a value group, the shape a South Western Railway
    # smartcard uses: format revision 2, so not the revision 3 layout.
    dir_entry(109, 23, 4, True, date_stamp(2026, 9, 30)),
    # E5 loyalty: an IPE whose value record counts points rather than money.
    dir_entry(96, 3, 0, True, 0),
    bytes(5), bytes(5),                                     # E6-E7 unused
    log_entry(ptr=1, eei=1, when=dts(2026, 9, 14, 8, 41), record_offset=2, passback=5),
]
for i, e in enumerate(entries):
    d.putb(2 + i * 5, e)

# Sector Chain Table: 13 nibbles starting at byte 42. SCT(i) describes sector i.
SCT_BASE = 42 * 8
# E1 -> sector 1 then 9 (active); E2 active; E3 blocked; E4 -> sector 4 then its
# value record in sector 10 (active).
sct = {1: 9, 9: 15, 2: 15, 3: 11, 11: 14, 4: 10, 10: 15, 5: 12, 12: 15}
for sector in range(1, 14):
    d.put(SCT_BASE + (sector - 1) * 4, 4, sct.get(sector, 0))
d.buf[49] = 0x2A            # DIRS#
# Directory InstanceID (TS 1000-2 table 8): KID 1, shell iteration 3, and the
# ISAM of the last device to rewrite the directory - South Western Railway's.
d.buf[50] = 0x13
d.putb(51, isam(109, 0x7C77).to_bytes(4, "big"))

# The same directory with the DIRBitMap blocking indicator set. Which of the six
# bitmap bits it is matters: it sits one below the log-configuration pair, so
# reading the wrong one would report a live card as stopped or miss the log
# entry. TS 1000-2 clause 5.1.2.
d_blocked = Bits(len(d.buf))
d_blocked.putb(0, bytes(d.buf))
d_blocked.put(11, 1, 1)

# ---------------------------------------------------------------- IPEs
# E1 sector 1: TYP 2 stored travel rights
ipe2 = Bits(24)
ipe2.put(0, 6, 6)           # IPELength = 6 blocks = 24 bytes
ipe2.put(6, 6, 0)           # IPEBitMap: no optional IIN
ipe2.put(12, 4, 1)          # IPEFormatRevision
ipe2.buf[2] = 255           # RemoveDate
ipe2.putb(3, (247).to_bytes(2, "big"))     # ProductRetailer: sold by c2c
ipe2.buf[5] = 0b01100000    # TYP2Flags: print ticket and receipt
ipe2.putb(6, (500).to_bytes(2, "big"))     # Threshold: top up below GBP 5.00
ipe2.putb(8, (1000).to_bytes(2, "big"))    # TopUpAmount: GBP 10.00
ipe2.putb(10, (9000).to_bytes(2, "big"))   # MaxValue2: GBP 90.00
ipe2.putb(12, (200).to_bytes(2, "big"))    # MaximumNegativeAmount: GBP 2.00
ipe2.putb(14, (500).to_bytes(2, "big"))    # DepositAmount: GBP 5.00
ipe2.put(128, 14, date_stamp(2024, 1, 1))  # StartDateAutoTopUp
ipe2.put(156, 4, 1)         # DepositMethodOfPayment: cash
ipe2.put(160, 4, 0)         # DepositCurrencyCode: sterling
ipe2.put(164, 12, 2000)     # DepositVATSalesTax: 20.00%
sector1 = bytes(ipe2.buf) + instance_and_seal()

# E1 sector 9: the value record data group holding the balance
sector9 = value_group([
    value_record(4, 100, dts(2026, 9, 1, 12, 0), purse_tail(1560, 0)),
    value_record(7, 101, dts(2026, 9, 14, 8, 41),
                 purse_tail(1234, 0, legs=2, cumulative=265, flags=0b101)),
], format_rev=1) + instance_and_seal()

# E2 sector 2: TYP 16 ITSO ID with holder name
name_fore, name_sur = b"ALEX", b"MORGAN"
ipe16 = Bits(48)
ipe16.put(0, 6, 12)         # IPELength = 12 blocks = 48 bytes
ipe16.put(6, 6, 0b000110)   # IPEBitMap: SecondaryHolderID, forename and surname
ipe16.put(12, 4, 2)         # IPEFormatRevision = 2
ipe16.buf[2] = 255          # RemoveDate
# IDFlags (table 24): personalised, female, companion allowed.
ipe16.buf[5] = 0b00010101
ipe16.put(50, 6, 30)        # PassbackTime: 30 minutes
ipe16.putb(7, bcd("19551103"))   # DateOfBirth, a Datef not a DATE
ipe16.putb(3, (0x9100).to_bytes(2, "big"))     # CPICC: the pass issuer
ipe16.put(48, 1, 1)         # RoundingFlagsEnable
ipe16.buf[11] = 182         # Language: Welsh (TS 1000-5 annex A.24)
ipe16.putb(12, (4078).to_bytes(4, "big"))      # HolderID
ipe16.put(128, 1, 1)        # RoundingFlag; RoundingValueFlag left clear
ipe16.put(20 * 8, 4, 1)     # DepositMethodOfPayment: cash
ipe16.put(22 * 8, 4, 3)     # ShellDepositMethodOfPayment: card
ipe16.put(22 * 8 + 4, 12, 2000)                # ShellDepositVATSalesTax: 20.00%
ipe16.putb(25, (500).to_bytes(2, "big"))       # DepositAmount: GBP 5.00
ipe16.putb(27, (300).to_bytes(2, "big"))       # ShellDeposit: GBP 3.00
ipe16.put(130, 14, date_stamp(2024, 4, 1))   # EntitlementStartDate
ipe16.put(144, 14, date_stamp(2029, 3, 31))  # EntitlementExpiryDate
ipe16.buf[29] = 2           # EntitlementCode: limited free ride
ipe16.buf[30] = 4           # ConcessionaryClass: pensioner
ipe16.putb(31, (1234567).to_bytes(4, "big"))  # SecondaryHolderID
ipe16.buf[35] = len(name_fore)
ipe16.putb(36, name_fore)
ipe16.buf[36 + len(name_fore)] = len(name_sur)
ipe16.putb(37 + len(name_fore), name_sur)
sector2 = bytes(ipe16.buf) + instance_and_seal()

# E3 sector 3: TYP 22 period ticket, revision 3, with NLC origin and destination.
# CPICC precedes RouteCode and the locations, so they only land where the
# decoder looks if it honours bitmap bit 4 (table 3.27). The duration group
# would push the dataset past what one sector holds alongside its InstanceID and
# seal, so it gets a product of its own below.
T22_TRANSFERABLE, T22_OFF_PEAK = 1 << 0, 1 << 8
T22_WD_AM, T22_WD_PM, T22_SAT_AM, T22_SAT_PM = 1 << 9, 1 << 10, 1 << 11, 1 << 12
T22_SUN_AM, T22_SUN_PM, T22_HOLIDAY = 1 << 13, 1 << 14, 1 << 15
T22_ALL_DAYS = 0xFE00
ipe22 = Bits(48)
ipe22.put(0, 6, 12)         # IPELength = 12 blocks = 48 bytes
ipe22.put(6, 6, 0b010010)   # IPEBitMap: CPICC, RouteCode + locations
ipe22.put(12, 4, 3)         # IPEFormatRevision = 3
ipe22.buf[2] = 255
# ProductRetailer: an OID with bit 15 set, from the retailer-only range above
# TS 1000-2 table B2's gap. On a TYP 22 it is an operator, not a station.
ipe22.putb(3, (57345).to_bytes(2, "big"))
ipe22.put(40, 16, T22_TRANSFERABLE | T22_OFF_PEAK | T22_WD_AM | T22_WD_PM
          | T22_SAT_PM | T22_SUN_AM | T22_SUN_PM)   # TYP22Flags: Saturday afternoons only
ipe22.put(58, 6, 20)        # PassbackTime: 20 minutes
ipe22.put(64, 14, date_stamp(2024, 12, 20))  # IssueDate
ipe22.put(78, 11, 1440 + 270)                # ExpiryTime: 04:30 the next day
ipe22.put(90, 6, 3)         # AutoRenewQuantity1
ipe22.put(96, 3, 1)         # Class: first
ipe22.put(106, 14, date_stamp(2025, 1, 1))   # ValidityStartDate
ipe22.put(125, 11, 9 * 60 + 30)              # ValidityStartTime: 09:30
ipe22.buf[18] = 0b11111000 | 0b100           # ValidOnDayCode: Monday to Saturday
ipe22.buf[19] = 1           # PartySizeAdult
ipe22.buf[20] = 2           # PartySizeChild
ipe22.putb(23, (12345).to_bytes(4, "big"))   # AmountPaid: GBP 123.45
ipe22.put(27 * 8, 4, 3)     # AmountPaidMethodOfPayment: card
ipe22.put(27 * 8 + 4, 12, 2000)              # AmountPaidVATSalesTax: 20.00%
ipe22.putb(29, (0x0457).to_bytes(2, "big"))  # CPICC
ipe22.putb(31, b"00000")    # RouteCode
ipe22.putb(36, loc1(203, nlc("1072")))
ipe22.putb(42, loc1(203, nlc("1444")))
sector3 = bytes(ipe22.buf)

# E4 sector 4: TYP 23 journey ticket, revision 2. The mandatory part ends at byte
# 29; bit 3 adds six bytes of mode and ride-value elements, then bit 1 brings
# RouteCode at 35 and the two locations from byte 40 (TS 1000-5 table 31a).
ipe23 = Bits(52)
ipe23.put(0, 6, 13)         # IPELength = 13 blocks = 52 bytes
ipe23.put(6, 6, 0b001010)   # IPEBitMap: bit 3 mode group, bit 1 route and locations
ipe23.put(12, 4, 2)         # IPEFormatRevision = 2
ipe23.buf[2] = 255
ipe23.putb(3, rail_retailer("5631").to_bytes(2, "big"))  # ProductRetailer: the station
# The terms, in the shape an SWR single takes (table 31a).
ipe23.put(58, 14, date_stamp(2026, 9, 14))  # IssueDate
ipe23.put(72, 5, 25)        # ValidityCode
ipe23.put(77, 11, 1440 + 270)               # ExpiryTime: 04:30 the next day
ipe23.put(93, 3, 2)         # Class: standard
ipe23.buf[12] = 1           # PartySizeAdult
ipe23.buf[13] = 1           # PartySizeChild
ipe23.putb(16, (580).to_bytes(4, "big"))    # AmountPaid: GBP 5.80
ipe23.put(20 * 8, 4, 3)     # AmountPaidMethodOfPayment: card
ipe23.putb(22, (987654).to_bytes(4, "big")) # PhotocardNumber
ipe23.buf[26] = 7           # PromotionCode
ipe23.putb(27, (0x0012).to_bytes(2, "big")) # CPICC
ipe23.put(29 * 8 + 4, 4, 1) # TYP23Mode: stored journeys
ipe23.buf[30] = 2           # MaxTransfers
ipe23.buf[31] = 120         # TimeLimit: 120 x 30 s = one hour
ipe23.putb(32, (250).to_bytes(2, "big"))    # ValueOfRideJourney: GBP 2.50
ipe23.putb(35, b"00000")    # RouteCode
ipe23.buf[40] = 203         # Origin1: short rail NLC
ipe23.buf[41] = 4
ipe23.putb(42, b"5631")
ipe23.buf[46] = 203         # Destination1
ipe23.buf[47] = 4
ipe23.putb(48, b"5685")
# 52 + 8 + 8 = 68 bytes, so the IPE group runs into a second sector and the value
# record group starts in the third.
sector4 = pad(bytes(ipe23.buf) + instance_and_seal(), 128)

# E4 sector 10: two value records a minute apart in DTS terms - which is to say,
# not apart at all. Only TS# says which of them is live.
USED_TAP = dts(2026, 9, 14, 8, 41)
sector10 = value_group([
    value_record(11, 0x004, USED_TAP, journey_tail(1, 0, 0)),
    value_record(6, 0x005, USED_TAP, journey_tail(0, 0, 0x02)),
], format_rev=2) + instance_and_seal()

# E3 sector 11: a TYP 22 value record. A period ticket keeps a stock of
# unactivated passes and expires them separately from the pass in use
# (TS 1000-5 table 3.29).
sector11 = value_group([
    value_record(1, 7, dts(2025, 1, 1, 9, 0),
                 period_tail(5, 0b01, date_stamp(2025, 12, 31), date_stamp(2025, 1, 31))),
    value_record(13, 8, dts(2025, 2, 1, 9, 0),
                 period_tail(4, 0b01, date_stamp(2025, 12, 31), date_stamp(2025, 2, 28))),
], format_rev=3) + instance_and_seal()

# E5 sector 5: a TYP 3 loyalty IPE. The dataset is only eight bytes: everything
# that changes lives in the value record.
ipe3 = Bits(8)
ipe3.put(0, 6, 2)           # IPELength = 2 blocks = 8 bytes
ipe3.put(6, 6, 0)
ipe3.put(12, 4, 1)
ipe3.buf[2] = 30            # RemoveDate: removable 30 days after expiry
ipe3.putb(3, (96).to_bytes(2, "big"))    # ProductRetailer
sector5 = bytes(ipe3.buf) + instance_and_seal()

# E5 sector 12: LoyaltyPoints is three bytes wide, so a points balance does not
# fit the two-byte slot a purse balance uses (TS 1000-5 table 9).
sector12 = value_group([
    value_record(1, 20, dts(2026, 8, 1, 10, 0), loyalty_tail(1200)),
    value_record(1, 21, dts(2026, 9, 10, 10, 0), loyalty_tail(74500, user=4660)),
], format_rev=1) + instance_and_seal()

# Period tickets at revisions 1 and 2, as whole product groups rather than as
# entries on the card above: they are here for their datasets, and the card's
# directory is already full of assertions about which entry is which.
#
# Revision 1 in the shape a Reading Buses monthly pass takes: no location at
# all, so the ticket is good wherever its owner says that product type is, and
# only PassDuration among the optional elements - which in this revision sits
# after where the locations would be (table 27).
ipe22r1 = Bits(28)
ipe22r1.put(0, 6, 7)        # IPELength = 7 blocks = 28 bytes
ipe22r1.put(6, 6, 0b001000) # IPEBitMap: PassDuration only
ipe22r1.put(12, 4, 1)       # IPEFormatRevision = 1
ipe22r1.putb(3, (163).to_bytes(2, "big"))   # ProductRetailer
ipe22r1.put(40, 16, T22_ALL_DAYS | 0b01100000)  # every day; print ticket and receipt
ipe22r1.put(78, 11, 4 * 60)                 # ExpiryTime: 04:00 on the expiry date
ipe22r1.buf[17] = 0xFF      # ValidOnDayCode: every day and special days
ipe22r1.buf[18] = 1         # PartySizeAdult
ipe22r1.buf[26] = 31        # PassDuration: 31 days
ACTIVATED = dts(2026, 1, 6, 17, 16)
period_rev1_group = (
    pad(bytes(ipe22r1.buf) + instance_and_seal(), 64) +
    pad(value_group([
        value_record(1, 64, ACTIVATED, period_tail(
            1, 0b10, date_stamp(2027, 11, 21), date_stamp(2025, 12, 17))),
        value_record(13, 65, ACTIVATED, period_tail(
            0, 0b10, date_stamp(2027, 11, 21), date_stamp(2026, 2, 5))),
    ], format_rev=1) + instance_and_seal(), 64))

# Revision 2 in the shape of a South Western Railway annual season: a four-byte
# AmountPaid, an ExpiryTime past midnight, and locations behind RouteCode.
ipe22r2 = Bits(48)
ipe22r2.put(0, 6, 12)       # IPELength = 12 blocks = 48 bytes
ipe22r2.put(6, 6, 0b000010) # IPEBitMap: RouteCode + locations
ipe22r2.put(12, 4, 2)       # IPEFormatRevision = 2
ipe22r2.buf[2] = 1
ipe22r2.put(40, 16, T22_ALL_DAYS)
ipe22r2.put(64, 14, date_stamp(2018, 6, 21))    # IssueDate
ipe22r2.put(78, 11, 1440 + 270)                 # ExpiryTime: 04:30 the next day
ipe22r2.put(96, 3, 2)       # Class: standard
ipe22r2.put(99, 5, 17)      # ValidityCode
ipe22r2.put(104, 24, dts(2018, 6, 25, 0, 0))    # ValidityStartDTS
ipe22r2.buf[17] = 0xFF
ipe22r2.buf[18] = 1
ipe22r2.putb(22, (404000).to_bytes(4, "big"))   # AmountPaid: GBP 4040.00
ipe22r2.put(26 * 8, 4, 3)   # AmountPaidMethodOfPayment: card
ipe22r2.putb(28, b"00000")  # RouteCode
ipe22r2.putb(33, loc1(203, nlc("5685")))
ipe22r2.putb(39, loc1(203, nlc("0035")))
period_rev2_group = bytes(ipe22r2.buf) + instance_and_seal()

# Revision 3's duration group: a unit code in front of a 12-bit count, and the
# days the stock of passes is extended by on renewal (table 3.27).
ipe22r3 = Bits(36)
ipe22r3.put(0, 6, 9)        # IPELength = 9 blocks = 36 bytes
ipe22r3.put(6, 6, 0b001000) # IPEBitMap: duration group only
ipe22r3.put(12, 4, 3)       # IPEFormatRevision = 3
ipe22r3.put(40, 16, T22_ALL_DAYS)
ipe22r3.buf[18] = 0xFF
ipe22r3.put(29 * 8, 4, 1)   # PassDurationCode: months
ipe22r3.put(29 * 8 + 4, 12, 1)              # PassDuration: one month
ipe22r3.putb(31, (365).to_bytes(2, "big"))  # ExpiryDateSPDuration
period_rev3_group = bytes(ipe22r3.buf) + instance_and_seal()

# A pay-as-you-go product with a Complex Capping extension, in the shape SPT
# issues its Subway purse (TS 1000-5 clause 4.1): the extension rides on the
# value record group, after both records. The reduced form (VGXRef 1) has one
# location for all four sets; the full form (VGXRef 2) a location and a time for
# each, and the fare last paid.
CAP_SETS = [
    (1, 11, 185, 900, 700, 0, 0),       # day cap: GBP 9.00 of fares, GBP 7.00 counted
    (2, 11, 185, 3000, 0, 2500, 3),     # 7-day cap: three days in
    (0, 0, 0, 0, 0, 0, 0),
    (0, 0, 0, 0, 0, 0, 0),
]
CAP_WHEN = dts(2026, 9, 14, 8, 41)
def capping_group(ref):
    if ref == 1:
        locations = loc1(203, nlc("1072"))
    else:
        locations = [(loc1(203, nlc("1072")), CAP_WHEN), (loc1(255, b""), 0),
                     (loc1(255, b""), 0), (loc1(255, b""), 0)]
    return (pad(bytes(ipe2.buf) + instance_and_seal(), 64) +
            value_group([
                value_record(4, 1, dts(2026, 9, 1, 12, 0), purse_tail(1560, 0)),
                value_record(7, 2, CAP_WHEN, purse_tail(1375, 0)),
            ], format_rev=1, extension=capping_vgx(ref, 7, CAP_SETS, locations)) +
            instance_and_seal())
capping1_group = capping_group(1)
capping2_group = capping_group(2)

# The purse above with a ValueCurrencyCode that scales by ten (TS 1000-5 annex
# A.21.2). The dataset's threshold, top-up, ceiling and overdraft are priced in
# it, so they scale with the balance; the deposit has a currency code of its own.
purse_scaled_group = (
    pad(bytes(ipe2.buf) + instance_and_seal(), 64) +
    value_group([value_record(4, 1, dts(2026, 9, 1, 12, 0), purse_tail(1234, valc=0b0100))],
                format_rev=1) + instance_and_seal())

# Charge to account, mode 1 (TS 1000-5 table 10): a credit limit priced in the
# value record's currency, here scaled by ten, and a deposit with its VAT.
ipe4 = Bits(16)
ipe4.put(0, 6, 4)           # IPELength = 4 blocks = 16 bytes
ipe4.put(12, 4, 1)
ipe4.buf[2] = 255
ipe4.putb(3, (8000).to_bytes(2, "big"))
ipe4.buf[5] = 0b00100000    # TYP4Flags: print ticket, no receipt
ipe4.putb(6, (5000).to_bytes(2, "big"))     # MaxValue4: 5000 units of ten pence
ipe4.putb(8, (1500).to_bytes(2, "big"))     # DepositAmount: GBP 15.00
ipe4.put(80, 14, date_stamp(2026, 1, 1))    # StartDateCTA
ipe4.put(94, 14, date_stamp(2026, 12, 31))  # EndDate
ipe4.put(108, 4, 3)         # DepositMethodOfPayment: card
ipe4.put(112, 4, 0)         # DepositCurrencyCode: sterling
ipe4.put(116, 12, 1750)     # DepositVATSalesTax: 17.50%
charge1_group = (
    pad(bytes(ipe4.buf) + instance_and_seal(), 64) +
    value_group([value_record(7, 3, dts(2026, 9, 2, 9, 0),
                              purse_tail(123, valc=0b0100, legs=1, flags=0b010))],
                format_rev=1) + instance_and_seal())

# Charge to account, mode 2 (TS 1000-5 table 15). Its value record holds a
# count, not money, but a ValueCurrencyCode all the same - euro, scaled by ten
# - and MaxValue5 is priced in it. TYP5ValueFlags marks it to be used first.
ipe5 = Bits(20)
ipe5.put(0, 6, 5)           # IPELength = 5 blocks = 20 bytes
ipe5.put(12, 4, 1)
ipe5.buf[2] = 255
ipe5.putb(3, (8000).to_bytes(2, "big"))
ipe5.buf[5] = 0b01000000    # TYP5Flags: print receipt only
ipe5.buf[6] = 2             # WeeksPerPeriod
ipe5.buf[7] = 40            # QuantityTransactions
ipe5.putb(8, (2500).to_bytes(2, "big"))     # MaxValue5
ipe5.putb(10, (800).to_bytes(2, "big"))     # DepositAmount: GBP 8.00
ipe5.put(96, 14, date_stamp(2026, 1, 5))    # StartDateCTA, a Monday
ipe5.put(110, 14, date_stamp(2026, 12, 27)) # EndDate
ipe5.put(124, 4, 1)         # DepositMethodOfPayment: cash
ipe5.put(128, 4, 0)         # DepositCurrencyCode: sterling
ipe5.put(132, 12, 500)      # DepositVATSalesTax: 5.00%
charge2_group = (
    pad(bytes(ipe5.buf) + instance_and_seal(), 64) +
    value_group([value_record(7, 9, dts(2026, 9, 8, 9, 0),
                              charge_tail(3, date_stamp(2026, 9, 7), legs=1,
                                          valc=0b0101, flags=0b0010))],
                format_rev=1) + instance_and_seal())

# Entitlements (TYP 14) at both revisions. Where an ID puts ProductRetailer's
# neighbours an entitlement has CPICC, HolderID straight after PassbackTime, the
# rounding flags in front of the entitlement dates, and one deposit whose
# currency nibble comes first (TS 1000-5 tables 20 and 20a).
ent1 = Bits(20)
ent1.put(0, 6, 5)           # IPELength = 5 blocks = 20 bytes
ent1.put(12, 4, 1)          # IPEFormatRevision = 1
ent1.buf[2] = 255
ent1.putb(3, (0x0321).to_bytes(2, "big"))   # CPICC
ent1.buf[5] = 0b00100001    # IDFlags: personalised, print ticket
ent1.put(48, 1, 1)          # RoundingFlagsEnable
ent1.put(50, 6, 10)         # PassbackTime
ent1.putb(7, (55501).to_bytes(4, "big"))    # HolderID
ent1.put(88, 1, 1)          # RoundingFlag: up
ent1.put(89, 1, 1)          # RoundingValueFlag: to 5p
ent1.put(90, 14, date_stamp(2027, 8, 31))   # EntitlementExpiryDate
ent1.put(108, 4, 0)         # DepositCurrencyCode: sterling
ent1.put(112, 4, 1)         # DepositMethodOfPayment: cash
ent1.putb(16, (250).to_bytes(2, "big"))     # DepositAmount: GBP 2.50
ent1.buf[18] = 14           # EntitlementCode
ent1.buf[19] = 5            # ConcessionaryClass
entitlement_rev1_group = bytes(ent1.buf) + instance_and_seal()

ent2 = Bits(24)
ent2.put(0, 6, 6)           # IPELength = 6 blocks = 24 bytes
ent2.put(12, 4, 2)          # IPEFormatRevision = 2
ent2.buf[2] = 255
ent2.putb(3, (0x0654).to_bytes(2, "big"))   # CPICC
ent2.buf[5] = 0b01000000    # IDFlags: the deposit is refundable
ent2.put(48, 1, 1)          # RoundingFlagsEnable
ent2.putb(7, (0x00ABCDEF).to_bytes(4, "big"))  # HolderID
ent2.put(89, 1, 1)          # RoundingValueFlag: to 5p, rounding down
ent2.put(90, 14, date_stamp(2026, 4, 1))    # EntitlementStartDate
ent2.put(104, 14, date_stamp(2027, 3, 31))  # EntitlementExpiryDate
ent2.put(124, 4, 0)         # DepositCurrencyCode: sterling
ent2.put(128, 4, 3)         # DepositMethodOfPayment: card
ent2.put(132, 12, 2000)     # DepositVATSalesTax: 20.00%
ent2.putb(18, (1000).to_bytes(2, "big"))    # DepositAmount: GBP 10.00
ent2.buf[20] = 11           # EntitlementCode
ent2.buf[21] = 4            # ConcessionaryClass
entitlement_rev2_group = bytes(ent2.buf) + instance_and_seal()

# A journey ticket at revision 3 (TS 1000-5 table 31b): a return, mode 3,
# which is RFU before this revision. The ride value is four bytes and has a
# currency code of its own - euro, scaled by ten - distinct from the sterling
# the ticket was paid in.
ipe23r3 = Bits(60)
ipe23r3.put(0, 6, 15)       # IPELength = 15 blocks = 60 bytes
ipe23r3.put(6, 6, 0b001010) # IPEBitMap: mode group, route and locations
ipe23r3.put(12, 4, 3)       # IPEFormatRevision = 3
ipe23r3.buf[2] = 255
ipe23r3.buf[5] = 0b01000000 # TYP23Flags: print receipt
ipe23r3.put(50, 6, 5)       # PassbackTime
ipe23r3.put(58, 14, date_stamp(2026, 9, 20))   # IssueDate
ipe23r3.put(72, 24, dts(2026, 9, 21, 6, 0))    # ValidityStartDTS
ipe23r3.put(96, 5, 3)       # ValidityCode
ipe23r3.put(101, 11, 1439)  # ExpiryTime: 23:59
ipe23r3.put(117, 3, 2)      # Class: standard
ipe23r3.buf[15] = 1         # PartySizeAdult
ipe23r3.putb(19, (1200).to_bytes(4, "big"))    # AmountPaid: GBP 12.00
ipe23r3.put(23 * 8, 4, 3)   # AmountPaidMethodOfPayment: card
ipe23r3.buf[32] = 2         # AutoRenewQuantity
ipe23r3.put(33 * 8 + 4, 4, 3)                  # TYP23Mode: return
ipe23r3.buf[34] = 1         # MaxTransfers
ipe23r3.buf[35] = 91        # TimeLimit: 91 x 30 s = 45 min 30 s
ipe23r3.putb(36, (600).to_bytes(4, "big"))     # ValueOfRideJourney
ipe23r3.put(40 * 8 + 4, 4, 0b0101)             # ...in euro, scaled by ten
ipe23r3.putb(41, b"00700")  # RouteCode
ipe23r3.putb(46, loc1(203, nlc("1072")))
ipe23r3.putb(52, loc1(203, nlc("1444")))
journey_rev3_group = bytes(ipe23r3.buf) + instance_and_seal()

# A period ticket at revision 3 carrying the IdentityDocumentID its bitmap bit
# 2 adds (table 3.27): after the route and both locations, a three-bit type, a
# five-bit length, then the document - here as text. TYP22Flags sets
# PrintTicket and TreatmentOfExpiredSP, flags 5 and 7.
ipe22id = Bits(56)
ipe22id.put(0, 6, 14)       # IPELength = 14 blocks = 56 bytes
ipe22id.put(6, 6, 0b000110) # IPEBitMap: identity document, route and locations
ipe22id.put(12, 4, 3)
ipe22id.putb(3, rail_retailer("1072").to_bytes(2, "big"))  # ProductRetailer: the station
ipe22id.put(40, 16, T22_ALL_DAYS | (1 << 5) | (1 << 7))
ipe22id.buf[18] = 0xFF
ipe22id.putb(29, b"00000")  # RouteCode
ipe22id.putb(34, loc1(203, nlc("1072")))
ipe22id.putb(40, loc1(203, nlc("1444")))
ipe22id.put(46 * 8, 3, 2)   # IdentityDocumentIDType: ASCII
ipe22id.put(46 * 8 + 3, 5, 8)
ipe22id.putb(47, b"RC123456")
period_rev3_id_group = bytes(ipe22id.buf) + instance_and_seal()

# The same element with no route or locations in front of it, as a number
# longer than Flipso keeps: twenty bytes, of which sixteen are held.
ipe22long = Bits(52)
ipe22long.put(0, 6, 13)     # IPELength = 13 blocks = 52 bytes
ipe22long.put(6, 6, 0b000100)
ipe22long.put(12, 4, 3)
ipe22long.put(40, 16, T22_ALL_DAYS)
ipe22long.put(29 * 8, 3, 1) # IdentityDocumentIDType: HEX
ipe22long.put(29 * 8 + 3, 5, 20)
ipe22long.putb(30, bytes(range(1, 21)))
period_rev3_long_id_group = bytes(ipe22long.buf) + instance_and_seal()

# A TYP 24 reserved journey (TS 1000-5 clause 2.11), every part of it present:
# the IIN, the passenger, one of each of the eight optional groups, and a value
# group of one record whose VGXRef 3 extension holds two reserved legs. Every
# location is a six-byte NLC LOC1, the length table 136's offsets assume.
T24_DUPLICATE, T24_TEST, T24_PASSENGER, T24_SEAT, T24_AUTO_RENEW = 1 << 1, 1 << 5, 1 << 6, 1 << 7, 1 << 9
RES_FROM = dts(2026, 10, 1, 0, 0)
reservation = typ24_dataset(
    loc1(203, nlc("1072")), loc1(203, nlc("1444")), loc1(203, nlc("5685")),
    alt_origin=loc1(203, nlc("0035")),
    flags=T24_DUPLICATE | T24_TEST | T24_PASSENGER | T24_SEAT | T24_AUTO_RENEW,
    sold_as=1, ticket_number=123456, travel_class=1, renew_days=14, journeys=2,
    out_days=1, rtn_days=30, operator=b"GR", ftot=b"SOR", adults=1, children=1,
    # Rail's IdDocumentReference: ID type 1, the railcard's last four digits.
    id_doc=(14567).to_bytes(4, "big"), route=b"00700", out_from=RES_FROM,
    rtn_from=RES_FROM + 60 * 24 * 2, restriction=b"OP", days=0xF8, restricted_days=0x06,
    mop=3, paid=8950, retailer=rail_retailer("5685"),
    associated=[2],
    # A 16-25 Railcard, a third off: rail rounds the percentage to 33 (RSPS3002).
    discounts=[(b"YNG  ", 0, 33, 1)],
    supplements=[b"SLP"],
    interchanges=[(loc1(203, nlc("5148")), loc1(203, nlc("5143")), 45)],
    transfers=[(2, 511, 0)],                     # break of journey, as many as liked
    bands=[(b"\0\0", loc1(255, b"\0\0\0\0"), 1, 7 * 60, 9 * 60 + 30, False, False)],
    services=[(loc1(203, nlc("1444")), b"GR1234", 18 * 60 + 30, True)],
    routes=[(loc1(203, nlc("1555")), 1)],
    passenger=(b"A N OTHER", 2), iin="633597", reservations=True)
assert len(reservation.buf) == 168, len(reservation.buf)
RES_LEGS = [
    # Coach and seat left-padded with spaces, as rail writes them (RSPS3002).
    (dts(2026, 10, 1, 8, 30), b"GR1234", loc1(203, nlc("1072")), loc1(203, nlc("1444")),
     b" C", b" 42", b"WNDW", 1, 0, 0, False),
    # A berth, and an attribute no table knows.
    (dts(2026, 10, 8, 17, 0), b"GR4321", loc1(203, nlc("1444")), loc1(203, nlc("1072")),
     b"D", b"17A", b"ZQXV", 3, 2, 1, True),
]
reservation_values = value_group([
    value_record(2, 5, dts(2026, 10, 1, 8, 2),
                 reservation_tail(1, transfers=511, part_used=True, reservations=2)),
], format_rev=2, extension=reservation_vgx(
    dts(2026, 10, 1, 8, 2), loc1(203, nlc("1072")), b"ABC12345", RES_LEGS))
reservation_group = (pad(bytes(reservation.buf) + instance_and_seal(), 192) +
                     reservation_values + instance_and_seal())

# The same shape on a bus operator's ticket, whose stops are AtcoCodes
# (LocDefType 211): eleven-byte LOC1s where table 136 assumes six, so nothing
# after Origin is where the table puts it. An interchange and a reserved leg
# carry them too, so every walk is exercised.
ATCO_A, ATCO_B = loc1(211, b"450016879"), loc1(211, b"450030236")
reservation_atco = typ24_dataset(
    ATCO_A, ATCO_B, ATCO_B, journeys=1, out_days=0, out_from=RES_FROM, mop=1, paid=420,
    interchanges=[(ATCO_B, ATCO_A, 0)], routes=[(ATCO_A, 0)], reservations=True)
reservation_atco_group = (
    pad(bytes(reservation_atco.buf) + instance_and_seal(), 192) +
    value_group([value_record(1, 1, RES_FROM, reservation_tail(1, reservations=1))],
                format_rev=2, extension=reservation_vgx(
                    0, loc1(255, b""), b"", [(dts(2026, 10, 1, 9, 5), b"X84", ATCO_A, ATCO_B,
                                               b"", b"12", b"", 0, 0, 3, False)])) +
    instance_and_seal())

# ---------------------------------------------------------------- Cyclic log (FID 1)
log = bytearray(192)
NLC_1072, NLC_1444, NLC_5685 = (loc2(203, n) for n in (b"1072", b"1444", b"5685"))
# A bus stop as the card stores one: "MANAG" and "manwpwjm" folded onto the
# keypad of TS 1000-1 table 28 and packed into four bytes of BCD.
STOP_A, STOP_B = loc2(206, bcd("00062624")), loc2(206, bcd("62697956"))

# Each record's InstanceID names the reader that wrote it: an SWR gate for the
# tap in, and one registered in the extended OID range for the tap out.
log[0:48] = tt_record(11, dts(2026, 9, 13, 17, 22), 0, NLC_1072, None, 1,
                      writer=isam(109, 0x123))                           # tap in
log[48:96] = tt_record(12, dts(2026, 9, 14, 8, 41), 265, NLC_1072, NLC_1444, 1,
                       companion=True, return_ticket=True,
                       writer=isam(9000, 0x42))                          # tap out
# A third record on the newer revision, so the decoder is exercised on both.
log[96:144] = tt_record_rev4(
    12, dts(2026, 9, 12, 18, 5), 480, NLC_1444, NLC_5685, 1,
    entry_when=dts(2026, 9, 12, 8, 12), entry_oid=109, candidates=[1, 4, 0, 0])
# A bus journey, dated before the other three so that adding it leaves their
# positions in the newest-first ordering alone.
log[144:192] = tt_record(12, dts(2026, 9, 10, 7, 55), 210, STOP_A, STOP_B, 1)
assert len(log) == 192, f"log grew to {len(log)} bytes"


# ================================================================== CMD2 card
# A second card on ITSO's generic micro-processor media. The geometry here is the
# one an SPT Glasgow Subway card reports, not the CMD2 defaults: it exercises a
# six-bit SCT, a 16-entry directory and 80-byte sectors.
CMD2_B, CMD2_S, CMD2_E, CMD2_SCTL = 80, 64, 16, 46
CMD2_PSI = 6
CMD2_SCT_BASE = 2 + CMD2_E * 5          # 82
CMD2_DIR_LEN = CMD2_SCT_BASE + CMD2_SCTL + 14

# OID 196 is SPT, the issuer whose cards prompted CMD2 support; the serial
# is made up rather than taken from a real card.
CMD2_IIN, CMD2_OID, CMD2_ISSN = "633597", "0196", "0000123"
CMD2_CHD = luhn(CMD2_IIN + CMD2_OID + CMD2_ISSN)
CMD2_ISRN = CMD2_IIN + CMD2_OID + CMD2_ISSN + CMD2_CHD
CMD2_EXP = date_stamp(2040, 4, 15)

cmd2_shell = Bits(32)
cmd2_shell.put(0, 6, 6)
cmd2_shell.put(6, 6, 0b000001)
cmd2_shell.put(12, 4, 1)
cmd2_shell.putb(2, bcd(CMD2_IIN))
cmd2_shell.putb(5, bcd(CMD2_OID))
cmd2_shell.putb(7, bcd(CMD2_ISSN + CMD2_CHD))
cmd2_shell.buf[11] = 2      # FVC = 2 -> generic micro-processor CMD2
cmd2_shell.buf[12] = 3      # KSC: mutual authentication with two keys
cmd2_shell.buf[13] = 1      # KVC
cmd2_shell.put(114, 14, CMD2_EXP)
cmd2_shell.buf[16] = CMD2_B
cmd2_shell.buf[17] = CMD2_S
cmd2_shell.buf[18] = CMD2_E
cmd2_shell.buf[19] = CMD2_SCTL
put_secrc(cmd2_shell)

cmd2_dir = Bits(CMD2_DIR_LEN)
cmd2_dir.put(0, 6, 0)
cmd2_dir.put(6, 6, 0b000010)            # last entry is a log entry
cmd2_dir.put(12, 4, 1)
cmd2_entries = [
    dir_entry(196, 2, 0, True, 0),                      # E1 purse, no expiry
    dir_entry(196, 16, 0, False, CMD2_EXP),             # E2 ITSO ID
] + [bytes(5)] * (CMD2_E - 3) + [
    log_entry(ptr=0, eei=0, when=0, record_offset=0, passback=0),
]
for i, e in enumerate(cmd2_entries):
    cmd2_dir.putb(2 + i * 5, e)

# E1 chains sector 1 to its value record in sector 18, which terminates at S-1
# (in use). E2 is a single sector, also in use.
cmd2_sct = {1: 18, 18: CMD2_S - 1, 2: CMD2_S - 1}
for sector in range(1, CMD2_S - 2):
    cmd2_dir.put(
        CMD2_SCT_BASE * 8 + (sector - 1) * CMD2_PSI, CMD2_PSI, cmd2_sct.get(sector, 0))
cmd2_dir.buf[CMD2_SCT_BASE + CMD2_SCTL] = 0x01   # DIRS#

# E1 sector 1: the purse IPE itself.
cmd2_ipe2 = Bits(24)
cmd2_ipe2.put(0, 6, 6)
cmd2_ipe2.put(6, 6, 0)
cmd2_ipe2.put(12, 4, 1)
cmd2_ipe2.buf[2] = 255
cmd2_ipe2.putb(3, (196).to_bytes(2, "big"))
cmd2_sector1 = pad(bytes(cmd2_ipe2.buf) + instance_and_seal(), CMD2_B)

# E1 sector 18: two value records, only the first of which has ever been written.
# A blank record reads as a DTS of zero, and that epoch is in 2028 - later than
# any real timestamp - so the decoder has to ignore it rather than treat it as
# the newest.
cmd2_sector18 = pad(
    value_group([
        value_record(4, 1, dts(2025, 4, 16, 14, 31), purse_tail(250, 0)),
        bytes(15),                      # never written
    ], format_rev=1) + instance_and_seal(), CMD2_B)

# E2 sector 2: an ITSO ID carrying a name.
cmd2_name_fore, cmd2_name_sur = b"JO", b"CLYDE"
cmd2_ipe16 = Bits(48)
cmd2_ipe16.put(0, 6, 12)
cmd2_ipe16.put(6, 6, 0b001100)          # names, HalfDayOfWeek and ValidAtOrFrom
cmd2_ipe16.put(12, 4, 2)
cmd2_ipe16.buf[2] = 255
cmd2_ipe16.put(130, 14, date_stamp(2025, 4, 16))
cmd2_ipe16.put(144, 14, CMD2_EXP)
cmd2_ipe16.buf[29] = 2
cmd2_ipe16.buf[30] = 4
cmd2_ipe16.buf[31] = len(cmd2_name_fore)
cmd2_ipe16.putb(32, cmd2_name_fore)
cmd2_ipe16.buf[32 + len(cmd2_name_fore)] = len(cmd2_name_sur)
cmd2_ipe16.putb(33 + len(cmd2_name_fore), cmd2_name_sur)
# Both periods Monday to Friday and Saturday morning (annex A.10), then a place.
cmd2_ipe16.putb(40, (0b1111111111100000).to_bytes(2, "big"))
cmd2_ipe16.putb(42, loc1(203, nlc("5685")))
cmd2_sector2 = pad(bytes(cmd2_ipe16.buf) + instance_and_seal(), CMD2_B)

# ================================================================== CMD4 card
# A Compact ITSO Shell on a MIFARE Ultralight / Infineon my-d (TS 1000-10 section
# 5), the family SPT's Glasgow Subway paper tickets use. Only three bytes are
# stored - ShellLength, ShellBitMap, ShellFormatRevision and FVC (TS 1000-2 table
# 4); the identity (IIN 633597, OID 8189, ISSN 0) and the geometry are implied by
# the CMD (TS 1000-10 table 42) rather than held on the media.
cmd4_shell_bits = Bits(3)
cmd4_shell_bits.put(0, 6, 6)    # ShellLength = 6 blocks
cmd4_shell_bits.put(6, 6, 0)    # ShellBitMap = 0 -> compact
cmd4_shell_bits.put(12, 4, 1)   # ShellFormatRevision = 1
cmd4_shell_bits.buf[2] = 4      # FVC = 4 -> Ultralight CMD4
cmd4_shell = bytes(cmd4_shell_bits.buf)
CMD4_ISRN = "633597" + "8189" + "0000000"
CMD4_ISRN += luhn(CMD4_ISRN)

# The whole 64-byte page memory of a CMD4 tag, as the Type 2 transport reads it:
# the compact shell at page 6, the single IPE Directory Entry at page 6 byte 3,
# and a full TYP 27 dataset spread across the static, dynamic and OTP regions
# (TS 1000-10 table 46). Shaped like an SPT Subway day ticket read 2026-09-27: a
# Period ticket (TYP 27) owned by SPT's extended-range OID 8323, an adult all-day
# ticket at GBP 4.45, issued and last used on one day, valid across the network.
CMD4_EXPIRY = date_stamp(2026, 9, 27)
cmd4_pages = type2_page_memory(
    bytes([0x04, 0xA2, 0xB3, 0xC4, 0xD5, 0xE6, 0xF7]),
    dir_entry(131, 27, 0, False, CMD4_EXPIRY, extended=True),
    typ27_dataset(
        issue_date=CMD4_EXPIRY, amount=445, passback=7,
        flags=0b1000,  # ExpiryTimeFlag: an owner-defined end-of-service time
        event2=12, last_use=dts(2026, 9, 27, 17, 47)))

# The other Space Saving IPEs on the same medium. The return is in the shape of
# one in Ryan Murphy's published dump of SPT Subway tickets (the builder
# reproduces his bytes exactly): a TYP 29 revision 1 carnet of two rides with one
# left, bought for GBP 3.30, last used getting off at fare stage 4 - Hillhead - at
# gate 5F2800. The carnet (TYP 28) and multi-leg ticket (TYP 29 revision 2) follow
# the spec alone: no Subway ticket of either kind has been seen.
T2_SERIAL = bytes([0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66])
cmd4_return = type2_page_memory(
    T2_SERIAL,
    dir_entry(131, 29, 2, False, date_stamp(2026, 9, 26), extended=True),
    typ29_dataset(
        issue_date=date_stamp(2026, 9, 26), rides_left=1, amount=330, mop=3,
        flags=0b1000, usage_code=0b101, usage=bytes.fromhex("5F280004")))
cmd4_carnet = type2_page_memory(
    T2_SERIAL,
    dir_entry(131, 28, 0, False, date_stamp(2026, 10, 10), extended=True),
    typ28_dataset(
        issue_date=date_stamp(2026, 9, 10), amount=2000, passback=5, flags=0b0001,
        last_use=dts(2026, 9, 20, 8, 5),
        # Two passes used, 20 and 10 days before expiry; two left, plus one for
        # the day of expiry itself; two never sold.
        ticks=(20, 10, 0, 0, 31, 31), issue_day=True, expiry_day=True))
cmd4_multileg = type2_page_memory(
    T2_SERIAL,
    dir_entry(131, 29, 0, False, date_stamp(2026, 12, 31), extended=True),
    typ29_dataset(
        issue_date=date_stamp(2026, 9, 1), rides_left=7, rev=2, passback=10,
        max_daily=4, max_transfers=2, journey_start=dts(2026, 9, 21, 7, 58),
        transfers=1, daily=2, last_use=dts(2026, 9, 21, 8, 20)))

# The same day ticket blocked: TS 1000-10 clause 5.16 blocks a CMD4 product by
# zeroing its Seal, since there is no Sector Chain Table to mark.
cmd4_blocked = type2_page_memory(
    bytes([0x04, 0xA2, 0xB3, 0xC4, 0xD5, 0xE6, 0xF7]),
    dir_entry(131, 27, 0, False, CMD4_EXPIRY, extended=True),
    typ27_dataset(issue_date=CMD4_EXPIRY, amount=445),
    seal=bytes(8))
# A directory entry naming a type a CMD4 cannot carry (TYP 22, a full period
# ticket) over a Space Saving layout: left as the entry says, not misread.
cmd4_wrong_type = type2_page_memory(
    T2_SERIAL,
    dir_entry(131, 22, 0, False, CMD4_EXPIRY, extended=True),
    typ27_dataset(issue_date=CMD4_EXPIRY, amount=445))
# A single, bought and never used, on an Infineon chip (maker code 05): no
# usage place, and one ride left. Its issuer locked only pages 6-9, leaving the
# IPE static data in pages 10-13 writable against TS 1000-10 clause 5.10.2.
cmd4_unused = type2_page_memory(
    bytes([0x05, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60]),
    dir_entry(131, 29, 0, False, CMD4_EXPIRY, extended=True),
    typ29_dataset(issue_date=CMD4_EXPIRY, rides_left=1, amount=175, mop=1),
    locks=bytes([0xC0, 0x03]))
# A return with both rides spent: the ticket is used up, though in date.
cmd4_spent = type2_page_memory(
    T2_SERIAL,
    dir_entry(131, 29, 2, False, date_stamp(2040, 1, 1), extended=True),
    typ29_dataset(
        issue_date=CMD4_EXPIRY, rides_left=0, amount=330, mop=3,
        usage_code=0b101, usage=bytes.fromhex("5F280002")))
# The two other ways GeoValidity can be coded (TS 1000-5 table 50): a fare
# value, here GBP 1.75, and a location - a LOC4 of LocDefType 204 whose origin
# slot is a zone map of zones 1 to 3, the way a zonal day ticket would say where
# it is good.
cmd4_fare_value = type2_page_memory(
    T2_SERIAL,
    dir_entry(131, 27, 0, False, CMD4_EXPIRY, extended=True),
    typ27_dataset(issue_date=CMD4_EXPIRY, amount=445, geo=175, fare_value=True,
                  event1=11, event2=12, photocard=424242),
    # All three block-lock bits set as well: the lock bits for pages 3-15 are
    # themselves fixed.
    locks=bytes([0xC7, 0x3F]))
cmd4_location = type2_page_memory(
    T2_SERIAL,
    dir_entry(131, 27, 0, False, CMD4_EXPIRY, extended=True),
    typ27_dataset(issue_date=CMD4_EXPIRY, amount=445, area_type=4,
                  area_slots=bytes([0b00000111, 0, 0, 0])))
# AreaValidity as a LOC3 with both ends (TS 1000-1 table 18): a carnet of day
# passes between two stations.
cmd4_journey_area = type2_page_memory(
    T2_SERIAL,
    dir_entry(131, 28, 0, False, date_stamp(2026, 10, 10), extended=True),
    typ28_dataset(issue_date=date_stamp(2026, 9, 10), amount=2000, area_type=3,
                  area_slots=b"1072" + b"1444"))
# A LOC3 fare stage (TS 1000-1 table 14): the destination is a bare stage
# number on the origin's machine.
cmd4_stage_area = type2_page_memory(
    T2_SERIAL,
    dir_entry(131, 29, 0, False, CMD4_EXPIRY, extended=True),
    typ29_dataset(issue_date=CMD4_EXPIRY, rides_left=1, amount=175, area_type=2,
                  area_slots=bytes([0x5F, 0x28, 0x00, 0x04, 0x09, 0, 0, 0])))
# ScaledQtyBackup (TS 1000-5 table 58b). Ten rides left at ScalingFactor 4, a
# bit per four used, so the backup can say only "up to twelve"; and one ride
# left whose backup still says three, as a write torn between the two would.
cmd4_backup_scaled = type2_page_memory(
    T2_SERIAL,
    dir_entry(131, 29, 0, False, CMD4_EXPIRY, extended=True),
    typ29_dataset(issue_date=CMD4_EXPIRY, rides_left=10, amount=1500, scaling=4))
cmd4_backup_torn = type2_page_memory(
    T2_SERIAL,
    dir_entry(131, 29, 0, False, CMD4_EXPIRY, extended=True),
    typ29_dataset(issue_date=CMD4_EXPIRY, rides_left=1, amount=330, backup_left=3))

# ================================================================== CMD9 card
# A full ITSO shell on an NTAG215 (TS 1000-10 section 10): 64-byte sectors,
# nine of them, two directory entries - one IPE and the log - and TS 1000-10
# annex A's software anti-tear, so two of everything that changes.
CMD9_B = 64
CMD9_EXP = date_stamp(2031, 5, 31)
cmd9_shell = shell_dataset("633597", "1234", "0090009", fvc=9, ksc=1, kvc=1,
                           expiry=CMD9_EXP, b=CMD9_B, s=9, e=2, sctl=3)
CMD9_ISRN = isrn("633597", "1234", "0090009")

# E1: the journey ticket above, whose 68-byte IPE group runs out of sector 1
# into the "IPE optional second sector", sector 4 (figure 4.1). Its value
# record group has two copies, in sectors 5 and 6, holding alternate records
# (annex A.3.2.3): TS#1 and TS#3 in copy B, TS#2 in copy A. The last
# transaction wrote TS#3 into B and relinked the chain to name B first.
cmd9_ipe = split_sectors(bytes(ipe23.buf) + instance_and_seal(), CMD9_B)
cmd9_copy_a = value_group([
    value_record(6, 2, dts(2026, 9, 12, 7, 50), journey_tail(8, 0, 0x02)),
    bytes(15),
], format_rev=2)
cmd9_copy_b = value_group([
    value_record(1, 1, dts(2026, 9, 10, 17, 5), journey_tail(9, 0, 0)),
    value_record(6, 3, dts(2026, 9, 14, 8, 41), journey_tail(7, 1, 0x02)),
], format_rev=2)

# E2: the log, T0 in sector 2 and T1 in sector 3 (Log Files A and B). Record
# Offset 0 says T0 is next to be written, so T1 is the newest.
cmd9_t0 = tt_record(12, dts(2026, 9, 12, 7, 50), 0, NLC_1072, NLC_1444, 1)
cmd9_t1 = tt_record(11, dts(2026, 9, 14, 8, 41), 0, NLC_1072, None, 1)

def cmd9_directory(chain, sequence, record_offset, when):
    return directory(
        [dir_entry(1234, 23, 4, True, CMD9_EXP),
         log_entry(ptr=1, eei=1, when=when, record_offset=record_offset, passback=0)],
        chain, 9, 2, 3, sequence,
        instance=bytes([0x10]) + isam(1234, 0x0909).to_bytes(4, "big"))

# Copy B is the live directory, DIRS# 7; copy A is the one before the last tap,
# DIRS# 6, still naming copy A of the value records first.
cmd9_dir_live = cmd9_directory({1: 4, 4: 6, 6: 5, 5: 8, 2: 3}, 0x07, 0,
                               dts(2026, 9, 14, 8, 41))
cmd9_dir_old = cmd9_directory({1: 4, 4: 5, 5: 6, 6: 8, 2: 3}, 0x06, 1,
                              dts(2026, 9, 12, 7, 50))
CMD9_UID = bytes([0x04, 0x19, 0x09, 0x21, 0x5A, 0x6B, 0x7C])
cmd9_pages = type2_full_page_memory(
    CMD9_UID, cmd9_shell.buf, cmd9_dir_old, cmd9_dir_live,
    {1: cmd9_ipe[0], 4: cmd9_ipe[1],
     5: cmd9_copy_a + instance_and_seal(), 6: cmd9_copy_b + instance_and_seal(),
     2: cmd9_t0, 3: cmd9_t1},
    CMD9_B, abacus=3)
assert len(cmd9_pages) == 512

# The same card torn mid-transaction (annex A.3.2.4.1): TS#4 made it into copy A
# but the directory was never relinked, so B is still current and TS#4 is an
# orphan a POST writes over. It must not become the live record.
cmd9_torn_a = value_group([
    value_record(6, 2, dts(2026, 9, 12, 7, 50), journey_tail(8, 0, 0x02)),
    value_record(6, 4, dts(2026, 9, 15, 9, 0), journey_tail(6, 0, 0x02)),
], format_rev=2)
cmd9_torn = type2_full_page_memory(
    CMD9_UID, cmd9_shell.buf, cmd9_dir_old, cmd9_dir_live,
    {1: cmd9_ipe[0], 4: cmd9_ipe[1],
     5: cmd9_torn_a + instance_and_seal(), 6: cmd9_copy_b + instance_and_seal(),
     2: cmd9_t0, 3: cmd9_t1},
    CMD9_B, abacus=3)

# The same card read before its last tap: TS#2 was the newest, written to copy
# A and linked first; T0 was the only journey. Directory A (DIRS# 6) was live,
# and B held the one before it.
cmd9_before = type2_full_page_memory(
    CMD9_UID, cmd9_shell.buf, cmd9_dir_old,
    cmd9_directory({1: 4, 4: 6, 6: 5, 5: 8, 2: 3}, 0x05, 0, dts(2026, 9, 10, 17, 5)),
    {1: cmd9_ipe[0], 4: cmd9_ipe[1],
     5: cmd9_copy_a + instance_and_seal(),
     6: value_group([cmd9_copy_b[2:17], bytes(15)], format_rev=2) + instance_and_seal(),
     2: cmd9_t0},
    CMD9_B, abacus=2)

# ================================================================= CMD10 card
# A full ITSO shell on an Ultralight EV1 (TS 1000-10 section 11): 128-byte
# sectors, otherwise laid out as CMD9. A purse whose IPE fits one sector, an
# empty log, and no Abacus: CMD10 counts in a one-way counter instead.
CMD10_B = 128
cmd10_shell = shell_dataset("633597", "1234", "0100010", fvc=10, ksc=1, kvc=1,
                            expiry=CMD9_EXP, b=CMD10_B, s=9, e=2, sctl=3)
CMD10_ISRN = isrn("633597", "1234", "0100010")
cmd10_current = value_group([
    value_record(4, 2, dts(2026, 9, 2, 12, 0), purse_tail(1500)),
    bytes(15),
], format_rev=1)
cmd10_previous = value_group([
    value_record(4, 1, dts(2026, 9, 1, 12, 0), purse_tail(1000)),
    bytes(15),
], format_rev=1)
cmd10_dir = directory(
    [dir_entry(1234, 2, 0, True, CMD9_EXP),
     log_entry(ptr=0, eei=0, when=0, record_offset=0, passback=0)],
    {1: 5, 5: 6, 6: 8, 2: 3}, 9, 2, 3, 0x01)
# Copy A never written since the card was made: blank, and so beaten by B.
cmd10_pages = type2_full_page_memory(
    bytes([0x04, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10]), cmd10_shell.buf, bytes(40), cmd10_dir,
    {1: sector1, 5: cmd10_current + instance_and_seal(),
     6: cmd10_previous + instance_and_seal()},
    CMD10_B, locks=bytes([0x00, 0x00]))
assert len(cmd10_pages) == 896

# ---------------------------------------------------------------- emit
def carr(name, data):
    body = ", ".join(f"0x{b:02X}" for b in data)
    return f"static const uint8_t {name}[] = {{{body}}};\n"

with open("card_data.h", "w") as f:
    # Generated, so exempt from ufbt lint: clang-format would split every array.
    f.write("/* Generated by build_card.py - synthetic ITSO CMD7 card. */\n/* clang-format off */\n#pragma once\n#include <stdint.h>\n\n")
    f.write(f'#define EXPECT_ISRN "{ISRN}"\n\n')
    f.write(carr("card_shell", shell.buf))
    f.write(carr("card_dir", d.buf))
    f.write(carr("card_dir_blocked", d_blocked.buf))
    f.write(carr("card_sector1", sector1))
    f.write(carr("card_sector2", sector2))
    f.write(carr("card_sector3", sector3))
    f.write(carr("card_sector4", sector4))
    f.write(carr("card_sector5", sector5))
    f.write(carr("card_sector9", sector9))
    f.write(carr("card_sector10", sector10))
    f.write(carr("card_sector11", sector11))
    f.write(carr("card_sector12", sector12))
    f.write(carr("card_log", log))
    f.write(carr("period_rev1_group", period_rev1_group))
    f.write(carr("period_rev2_group", period_rev2_group))
    f.write(carr("period_rev3_group", period_rev3_group))
    f.write(carr("capping1_group", capping1_group))
    f.write(carr("capping2_group", capping2_group))
    f.write(carr("purse_scaled_group", purse_scaled_group))
    f.write(carr("charge1_group", charge1_group))
    f.write(carr("charge2_group", charge2_group))
    f.write(carr("entitlement_rev1_group", entitlement_rev1_group))
    f.write(carr("entitlement_rev2_group", entitlement_rev2_group))
    f.write(carr("journey_rev3_group", journey_rev3_group))
    f.write(carr("period_rev3_id_group", period_rev3_id_group))
    f.write(carr("period_rev3_long_id_group", period_rev3_long_id_group))
    f.write(carr("reservation_group", reservation_group))
    f.write(carr("reservation_atco_group", reservation_atco_group))
    f.write("\n/* Synthetic ITSO CMD2 card. */\n")
    f.write(f'#define EXPECT_CMD2_ISRN "{CMD2_ISRN}"\n')
    f.write(carr("cmd2_shell", cmd2_shell.buf))
    f.write(carr("cmd2_dir", cmd2_dir.buf))
    f.write(carr("cmd2_sector1", cmd2_sector1))
    f.write(carr("cmd2_sector2", cmd2_sector2))
    f.write(carr("cmd2_sector18", cmd2_sector18))
    f.write("\n/* Synthetic ITSO CMD4 compact-shell card (Type 2 tag). */\n")
    f.write(f'#define EXPECT_CMD4_ISRN "{CMD4_ISRN}"\n')
    f.write(carr("cmd4_shell", cmd4_shell))
    f.write(carr("cmd4_pages", cmd4_pages))
    f.write(carr("cmd4_return", cmd4_return))
    f.write(carr("cmd4_carnet", cmd4_carnet))
    f.write(carr("cmd4_multileg", cmd4_multileg))
    f.write(carr("cmd4_blocked", cmd4_blocked))
    f.write(carr("cmd4_wrong_type", cmd4_wrong_type))
    f.write(carr("cmd4_unused", cmd4_unused))
    f.write(carr("cmd4_spent", cmd4_spent))
    f.write(carr("cmd4_fare_value", cmd4_fare_value))
    f.write(carr("cmd4_location", cmd4_location))
    f.write(carr("cmd4_journey_area", cmd4_journey_area))
    f.write(carr("cmd4_stage_area", cmd4_stage_area))
    f.write(carr("cmd4_backup_scaled", cmd4_backup_scaled))
    f.write(carr("cmd4_backup_torn", cmd4_backup_torn))
    f.write("\n/* Synthetic ITSO CMD9 and CMD10 full-shell cards (Type 2 tags). */\n")
    f.write(f'#define EXPECT_CMD9_ISRN "{CMD9_ISRN}"\n')
    f.write(f'#define EXPECT_CMD10_ISRN "{CMD10_ISRN}"\n')
    f.write(carr("cmd9_pages", cmd9_pages))
    f.write(carr("cmd9_torn", cmd9_torn))
    f.write(carr("cmd9_before", cmd9_before))
    f.write(carr("cmd10_pages", cmd10_pages))
print("ISRN:", ISRN)
print("CMD2 ISRN:", CMD2_ISRN)
print("CMD4 ISRN:", CMD4_ISRN)
print("CMD9 ISRN:", CMD9_ISRN)
print("CMD10 ISRN:", CMD10_ISRN)
print("wrote card_data.h")
