"""Build spec-accurate synthetic ITSO cards and emit them as a C header.

Two cards are generated: a CMD7 (DESFire) one with the default geometry, and a
CMD2 (generic micro-processor) one with the larger geometry that real CMD2 cards
turn out to use - 80-byte sectors, 64 of them, 16 directory entries, and so a
six-bit Sector Chain Table rather than the four-bit one CMD7 needs.
"""
import datetime

EPOCH = datetime.date(1997, 1, 1)
DTS_EPOCH = datetime.datetime(2028, 11, 24, 20, 16)

def date_stamp(y, m, d):
    return (datetime.date(y, m, d) - EPOCH).days

def dts(y, mo, d, h, mi):
    delta = datetime.datetime(y, mo, d, h, mi) - DTS_EPOCH
    minutes = int(delta.total_seconds() // 60)
    return minutes & 0xFFFFFF

class Bits:
    """Bit-oriented writer, MSB first, matching ITSO field packing."""
    def __init__(self, nbytes):
        self.buf = bytearray(nbytes)
    def put(self, bit_off, bit_len, value):
        for i in range(bit_len):
            bit = (value >> (bit_len - 1 - i)) & 1
            pos = bit_off + i
            if bit:
                self.buf[pos // 8] |= 1 << (7 - pos % 8)
            else:
                self.buf[pos // 8] &= ~(1 << (7 - pos % 8)) & 0xFF
    def putb(self, byte_off, data):
        self.buf[byte_off:byte_off + len(data)] = data

def bcd(digits):
    s = "".join(digits)
    assert len(s) % 2 == 0
    return bytes(int(s[i:i+2], 16) for i in range(0, len(s), 2))

def pad(data, size):
    assert len(data) <= size, (len(data), size)
    return bytes(data) + bytes(size - len(data))

def luhn(num17):
    total, dbl = 0, True
    for ch in reversed(num17):
        d = int(ch)
        if dbl:
            d *= 2
            if d > 9:
                d -= 9
        total += d
        dbl = not dbl
    return str((10 - total % 10) % 10)

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
shell.putb(22, b"\xAB\xCD") # SECRC (not verified by the app)

# ---------------------------------------------------------------- Directory (FID 0)
def dir_entry(oid, typ, ptyp, vgp, expiry, extended=False):
    e = Bits(5)
    e.put(0, 1, 1 if extended else 0)   # OID extension flag (TS 1000-2 Annex B)
    e.put(1, 13, oid & 0x1FFF)
    e.put(14, 5, typ)
    e.put(19, 5, ptyp)
    e.put(24, 1, 1 if vgp else 0)
    e.put(25, 1, 0)         # IINL
    e.put(26, 14, expiry)
    return bytes(e.buf)

def log_entry(ptr, eei, when, record_offset, passback):
    e = Bits(5)
    e.put(0, 1, 1)          # LPF: normal mode
    e.put(1, 5, ptr)
    e.put(6, 2, eei)
    e.put(8, 24, when)
    e.put(32, 2, record_offset)
    e.put(34, 6, passback)
    return bytes(e.buf)

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

# The same directory with the DIRBitMap blocking indicator set. Which of the six
# bitmap bits it is matters: it sits one below the log-configuration pair, so
# reading the wrong one would report a live card as stopped or miss the log
# entry. TS 1000-2 clause 5.1.2.
d_blocked = Bits(len(d.buf))
d_blocked.putb(0, bytes(d.buf))
d_blocked.put(11, 1, 1)

# ---------------------------------------------------------------- IPEs
def instance_and_seal():
    return bytes([0x11]) + b"\x01\x02\x03\x04" + b"\x00\x00\x01" + b"\xDE" * 8

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
sector1 = bytes(ipe2.buf) + instance_and_seal()

# E1 sector 9: the value record data group holding the balance
vg = Bits(32)
vg.put(0, 6, 8)             # VGLength = 8 blocks = 32 bytes
vg.put(6, 6, 0b110000)      # VGBitMap: two value records supported
vg.put(12, 4, 9)            # VGFormatRevision = IPEFormatRevision + 8
def value_record(txn, seq, when, tail=b"", modifier=0xC0FFEE01, action_seq=3):
    """A value record: the TS 1000-2 table 15 common header plus a 5-byte tail
    whose meaning TS 1000-5 defines per IPE type."""
    r = Bits(15)
    r.put(0, 4, txn)
    r.put(4, 12, seq)
    r.put(16, 24, when)
    r.put(40, 32, modifier)     # ISAMIDModifier: the POST that wrote the record
    r.put(72, 8, action_seq)    # ActionSequenceNumber
    r.putb(10, tail)
    return bytes(r.buf)

def purse_tail(value, valc, legs=0, cumulative=0, flags=0):
    """TYP 2 table 4, as five bytes starting at record byte 10: Value, then the
    currency and journey-leg nibbles, then a 13-bit cumulative fare and three
    flag bits."""
    t = Bits(5)
    t.put(0, 16, value & 0xFFFF)
    t.put(16, 4, valc)
    t.put(20, 4, legs)
    t.put(24, 13, cumulative)
    t.put(37, 3, flags)
    return bytes(t.buf)

def journey_tail(rides, transfers, flags):
    """TYP 23 table 33a: a ride count, not money, at record bytes 10 to 12."""
    return bytes([rides, transfers, flags, 0, 0])
vg.putb(2, value_record(4, 100, dts(2026, 9, 1, 12, 0), purse_tail(1560, 0)))
vg.putb(17, value_record(7, 101, dts(2026, 9, 14, 8, 41),
                         purse_tail(1234, 0, legs=2, cumulative=265, flags=0b001)))
sector9 = bytes(vg.buf) + instance_and_seal()

# E2 sector 2: TYP 16 ITSO ID with holder name
name_fore, name_sur = b"ALEX", b"MORGAN"
ipe16 = Bits(44)
ipe16.put(0, 6, 11)         # IPELength = 11 blocks = 44 bytes
ipe16.put(6, 6, 0b000100)   # IPEBitMap: forename and surname present
ipe16.put(12, 4, 2)         # IPEFormatRevision = 2
ipe16.buf[2] = 255          # RemoveDate
# IDFlags (table 24): personalised, female, companion allowed.
ipe16.buf[5] = 0b00010101
ipe16.put(50, 6, 30)        # PassbackTime: 30 minutes
ipe16.putb(7, bcd("19551103"))   # DateOfBirth, a Datef not a DATE
ipe16.put(130, 14, date_stamp(2024, 4, 1))   # EntitlementStartDate
ipe16.put(144, 14, date_stamp(2029, 3, 31))  # EntitlementExpiryDate
ipe16.buf[29] = 2           # EntitlementCode: limited free ride
ipe16.buf[30] = 4           # ConcessionaryClass: pensioner
ipe16.buf[31] = len(name_fore)
ipe16.putb(32, name_fore)
ipe16.buf[32 + len(name_fore)] = len(name_sur)
ipe16.putb(33 + len(name_fore), name_sur)
sector2 = bytes(ipe16.buf) + instance_and_seal()

# E3 sector 3: TYP 22 period ticket, revision 3, with NLC origin and destination
ipe22 = Bits(48)
ipe22.put(0, 6, 12)         # IPELength = 12 blocks = 48 bytes
ipe22.put(6, 6, 0b000010)   # IPEBitMap: RouteCode + ValidAtOrFrom + ValidTo present
ipe22.put(12, 4, 3)         # IPEFormatRevision = 3
ipe22.buf[2] = 255
ipe22.put(106, 14, date_stamp(2025, 1, 1))   # ValidityStartDate
ipe22.buf[36] = 203         # LocDefType: short rail NLC
ipe22.buf[37] = 4
ipe22.putb(38, b"1072")
ipe22.buf[42] = 203
ipe22.buf[43] = 4
ipe22.putb(44, b"1444")
sector3 = bytes(ipe22.buf)

# E4 sector 4: TYP 23 journey ticket, revision 2. The mandatory part ends at byte
# 29; bit 3 adds six bytes of mode and ride-value elements, then bit 1 brings
# RouteCode at 35 and the two locations from byte 40 (TS 1000-5 table 31a).
ipe23 = Bits(52)
ipe23.put(0, 6, 13)         # IPELength = 13 blocks = 52 bytes
ipe23.put(6, 6, 0b001010)   # IPEBitMap: bit 3 mode group, bit 1 route and locations
ipe23.put(12, 4, 2)         # IPEFormatRevision = 2
ipe23.buf[2] = 255
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
vg23 = Bits(32)
vg23.put(0, 6, 8)
vg23.put(6, 6, 0b110000)    # two value records supported
vg23.put(12, 4, 10)         # VGFormatRevision = IPEFormatRevision + 8
USED_TAP = dts(2026, 9, 14, 8, 41)
vg23.putb(2, value_record(11, 0x004, USED_TAP, journey_tail(1, 0, 0)))
vg23.putb(17, value_record(6, 0x005, USED_TAP, journey_tail(0, 0, 0x02)))
sector10 = bytes(vg23.buf) + instance_and_seal()

# E3 sector 11: a TYP 22 value record. A period ticket keeps a stock of
# unactivated passes and expires them separately from the pass in use
# (TS 1000-5 table 3.29).
def period_tail(passes, flags, stored_expiry, current_expiry):
    t = Bits(5)
    t.put(0, 6, passes)
    t.put(6, 6, flags)
    t.put(12, 14, stored_expiry)
    t.put(26, 14, current_expiry)
    return bytes(t.buf)

vg22 = Bits(32)
vg22.put(0, 6, 8)
vg22.put(6, 6, 0b110000)    # two value records supported
vg22.put(12, 4, 11)         # VGFormatRevision = IPEFormatRevision + 8
vg22.putb(2, value_record(1, 7, dts(2025, 1, 1, 9, 0),
                          period_tail(5, 0b01, date_stamp(2025, 12, 31),
                                      date_stamp(2025, 1, 31))))
vg22.putb(17, value_record(13, 8, dts(2025, 2, 1, 9, 0),
                           period_tail(4, 0b01, date_stamp(2025, 12, 31),
                                       date_stamp(2025, 2, 28))))
sector11 = bytes(vg22.buf) + instance_and_seal()

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
vg3 = Bits(32)
vg3.put(0, 6, 8)
vg3.put(6, 6, 0b110000)
vg3.put(12, 4, 9)
def loyalty_tail(points):
    t = Bits(5)
    t.put(0, 24, points)
    return bytes(t.buf)
vg3.putb(2, value_record(1, 20, dts(2026, 8, 1, 10, 0), loyalty_tail(1200)))
vg3.putb(17, value_record(1, 21, dts(2026, 9, 10, 10, 0), loyalty_tail(74500)))
sector12 = bytes(vg3.buf) + instance_and_seal()

# ---------------------------------------------------------------- Cyclic log (FID 1)
def loc2(def_type, body):
    """A LOC2 location: the tag then a fixed six-byte body, zero padded.

    The log stores locations this way rather than as the tag-length-value LOC1
    that an IPE uses, so a location type is only covered end to end once it has
    been through here as well.
    """
    assert len(body) <= 6, f"{len(body)} bytes does not fit a LOC2 body"
    return bytes([def_type]) + body + b"\x00" * (6 - len(body))


def tt_record(txn, when, amount, origin, dest, ipe_ptr):
    groups = b""
    bitmap = 0
    bitmap |= 1 << 0
    groups += bytes([0x80]) + (amount & 0xFFFF).to_bytes(2, "big") + b"\x00\x00"
    if dest:
        bitmap |= 1 << 1
        groups += dest
    bitmap |= 1 << 2
    groups += bytes([ipe_ptr & 0x1F])
    if origin:
        bitmap |= 1 << 3
        groups += origin

    total = 7 + len(groups)
    assert total <= 48, f"record is {total} bytes, over a 48-byte slot"
    blocks = (total + 3) // 4
    r = Bits(48)
    r.put(0, 6, blocks)
    r.put(6, 6, 0)          # TTBitMap1
    r.put(12, 4, 2)         # TTFormatRevision
    r.put(16, 12, bitmap)   # TTBitMap2
    r.put(28, 4, txn)       # TTTransactionType
    r.put(32, 24, when)     # DateTimeStamp
    r.putb(7, groups)
    return bytes(r.buf)

def tt_record_rev4(txn, when, amount, via, dest, ipe_ptr,
                   entry_when, entry_oid, candidates, no_fare=False):
    """A format revision 4 record: the shape a check-in/check-out closed system
    writes on exit. It carries the entry it closes, the products the gate weighed
    up, and a routing point (TS 1000-5 tables 64 and 66)."""
    groups = b""
    bitmap = 0

    bitmap |= 1 << 0                                    # AMT
    amt = Bits(5)
    amt.put(0, 4, 1)                                    # MOP: cash
    amt.put(4, 4, 0)                                    # currency: sterling
    amt.put(8, 16, amount & 0xFFFF)
    amt.put(27, 1, 1 if no_fare else 0)                 # NoFareCharged
    amt.put(28, 12, 2000)                               # VAT: 20.00%
    groups += bytes(amt.buf)

    bitmap |= 1 << 1                                    # DEST
    groups += dest
    bitmap |= 1 << 2                                    # IPEID
    groups += bytes([ipe_ptr & 0x1F])
    bitmap |= 1 << 5                                    # RC
    groups += via
    bitmap |= 1 << 7                                    # IIN
    groups += bcd("633597")

    bitmap |= 1 << 8                                    # CIPE
    cipe = Bits(3)
    for i, c in enumerate(candidates):
        cipe.put(i * 5, 5, c)
    cipe.put(20, 4, 0b10)                               # CIPEFlags: inspected
    groups += bytes(cipe.buf)

    bitmap |= 1 << 9                                    # ENTRY
    entry = Bits(10)
    entry.putb(0, b"\x01\x02\x03\x04")
    entry.putb(4, b"\x00\x00\x09")
    entry.put(56, 24, entry_when)
    groups += bytes(entry.buf)

    bitmap |= 1 << 10                                   # ENTRY OID
    groups += entry_oid.to_bytes(2, "big") + b"\x01"

    total = 7 + len(groups)
    assert total <= 48, f"revision 4 record is {total} bytes, over a 48-byte slot"
    blocks = (total + 3) // 4
    r = Bits(48)
    r.put(0, 6, blocks)
    r.put(6, 6, 0)
    r.put(12, 4, 4)         # TTFormatRevision = 4
    r.put(16, 12, bitmap)
    r.put(28, 4, txn)
    r.put(32, 24, when)
    r.putb(7, groups)
    return bytes(r.buf)

log = bytearray(192)
NLC_1072, NLC_1444, NLC_5685 = (loc2(203, n) for n in (b"1072", b"1444", b"5685"))
# A bus stop as the card stores one: "MANAG" and "manwpwjm" folded onto the
# keypad of TS 1000-1 table 28 and packed into four bytes of BCD.
STOP_A, STOP_B = loc2(206, bcd("00062624")), loc2(206, bcd("62697956"))

log[0:48] = tt_record(11, dts(2026, 9, 13, 17, 22), 0, NLC_1072, None, 1)      # tap in
log[48:96] = tt_record(12, dts(2026, 9, 14, 8, 41), 265, NLC_1072, NLC_1444, 1)  # tap out
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
cmd2_vg = Bits(32)
cmd2_vg.put(0, 6, 8)
cmd2_vg.put(6, 6, 0b110000)             # two value records supported
cmd2_vg.put(12, 4, 9)
cmd2_vg.putb(2, value_record(4, 1, dts(2025, 4, 16, 14, 31), purse_tail(250, 0)))
cmd2_vg.putb(17, bytes(15))             # never written
cmd2_sector18 = pad(bytes(cmd2_vg.buf) + instance_and_seal(), CMD2_B)

# E2 sector 2: an ITSO ID carrying a name.
cmd2_name_fore, cmd2_name_sur = b"JO", b"CLYDE"
cmd2_ipe16 = Bits(44)
cmd2_ipe16.put(0, 6, 11)
cmd2_ipe16.put(6, 6, 0b000100)          # forename and surname present
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
cmd2_sector2 = pad(bytes(cmd2_ipe16.buf) + instance_and_seal(), CMD2_B)

# ---------------------------------------------------------------- emit
def carr(name, data):
    body = ", ".join(f"0x{b:02X}" for b in data)
    return f"static const uint8_t {name}[] = {{{body}}};\n"

with open("card_data.h", "w") as f:
    f.write("/* Generated by build_card.py - synthetic ITSO CMD7 card. */\n#pragma once\n#include <stdint.h>\n\n")
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
    f.write("\n/* Synthetic ITSO CMD2 card. */\n")
    f.write(f'#define EXPECT_CMD2_ISRN "{CMD2_ISRN}"\n')
    f.write(carr("cmd2_shell", cmd2_shell.buf))
    f.write(carr("cmd2_dir", cmd2_dir.buf))
    f.write(carr("cmd2_sector1", cmd2_sector1))
    f.write(carr("cmd2_sector2", cmd2_sector2))
    f.write(carr("cmd2_sector18", cmd2_sector18))
print("ISRN:", ISRN)
print("CMD2 ISRN:", CMD2_ISRN)
print("wrote card_data.h")
