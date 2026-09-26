"""Build the byte structures ITSO TS 1000 defines, for synthetic cards.

Two scripts write cards: tools/test/build_card.py, whose cards are the host
test suite's fixtures, and tools/demo/build_demo_cards.py, whose cards are
saved-card files for the device. They differ in what they assemble, not in how
a shell or a value record is laid out, so the layouts live here and are stated
once - a field offset that is wrong in two places is wrong twice as quietly.

Everything here is bit offsets from the specification and nothing here is a
card: no dates, no operators, no balances. Those belong to the script that is
building a particular card.
"""
import datetime

EPOCH = datetime.date(1997, 1, 1)
DTS_EPOCH = datetime.datetime(2028, 11, 24, 20, 16)


def date_stamp(y, m, d):
    """A 14-bit DATE: days from 1997-01-01 (TS 1000-1 clause 4.2.2)."""
    return (datetime.date(y, m, d) - EPOCH).days


def dts(y, mo, d, h, mi):
    """A 24-bit DTS: two's complement minutes from 2028-11-24 20:16."""
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


def pad_sector(data, sector_size):
    """Round a block up to whole sectors, which is how the card stores one."""
    sectors = (len(data) + sector_size - 1) // sector_size
    return pad(data, sectors * sector_size)


def crc_b(data):
    """CRC_B over data, as ITSO TS 1000-2 Annex A defines it.

    The shell carries one as its SECRC, so a synthetic shell has to compute it
    rather than invent it: the decoder checks it, and a made-up value would make
    every synthetic card look corrupt.
    """
    crc = 0xFFFF
    for b in data:
        ch = b ^ (crc & 0xFF)
        ch = (ch ^ (ch << 4)) & 0xFF
        crc = ((crc >> 8) ^ (ch << 8) ^ (ch << 3) ^ (ch >> 4)) & 0xFFFF
    return (~crc) & 0xFFFF


def put_secrc(shell):
    """Seal a shell with its SECRC, low byte first (TS 1000-2 clause 4.1.15).

    ShellLength says where it goes, which is byte 22 without an MCRN and byte 30
    with one, and the CRC covers everything before it.
    """
    dataset_len = (shell.buf[0] >> 2) * 4
    crc = crc_b(bytes(shell.buf[:dataset_len - 2]))
    shell.putb(dataset_len - 2, bytes([crc & 0xFF, crc >> 8]))


def luhn(num17):
    """The 18th digit of an ISRN, which the decoder checks."""
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


def isrn(iin, oid, issn):
    """The 18-digit card number: issuer, operator, serial, check digit."""
    body = iin + oid + issn
    return body + luhn(body)


# ---------------------------------------------------------------- Shell
def shell_dataset(iin, oid, issn, fvc, ksc, kvc, expiry, b, s, e, sctl, mcrn=None):
    """The ITSO Shell Environment Data Group (TS 1000-2 clause 4).

    Six blocks of four bytes without an MCRN and eight with one, which is what
    moves the SECRC from byte 22 to byte 30. The buffer is 32 bytes either way:
    a reader gets a whole file back rather than as many bytes as the shell says
    it uses, and the decoder is expected to cope with that.
    """
    shell = Bits(32)
    shell.put(0, 6, 8 if mcrn else 6)        # ShellLength, in 4-byte blocks
    shell.put(6, 6, 0b000011 if mcrn else 0b000001)  # ShellBitMap: bit 1 is the MCRN
    shell.put(12, 4, 1)                      # ShellFormatRevision
    shell.putb(2, bcd(iin))
    shell.putb(5, bcd(oid))
    shell.putb(7, bcd(issn + luhn(iin + oid + issn)))
    shell.buf[11] = fvc                      # Format Version Code: the media type
    shell.buf[12] = ksc
    shell.buf[13] = kvc
    shell.put(114, 14, expiry)               # EXP, two RFU bits into byte 14
    shell.buf[16] = b                        # B: bytes per sector
    shell.buf[17] = s                        # S: sectors
    shell.buf[18] = e                        # e#: directory entries
    shell.buf[19] = sctl                     # SCTL: Sector Chain Table length
    if mcrn:
        # BCD, terminated and padded with 0xF to ten bytes (clause 4.1.14).
        digits = mcrn + "F" * (20 - len(mcrn))
        shell.putb(20, bcd(digits))
    put_secrc(shell)
    return shell


# ---------------------------------------------------------------- Directory
def dir_entry(oid, typ, ptyp, vgp, expiry, extended=False, foreign=False):
    """One IPE Directory Entry (TS 1000-2 clause 6.1)."""
    e = Bits(5)
    e.put(0, 1, 1 if extended else 0)   # OID extension flag (TS 1000-2 Annex B)
    e.put(1, 13, oid & 0x1FFF)
    e.put(14, 5, typ)
    e.put(19, 5, ptyp)
    e.put(24, 1, 1 if vgp else 0)       # VGP: a Value Record Data Group follows
    e.put(25, 1, 1 if foreign else 0)   # IINL: owner is on another network
    e.put(26, 14, expiry)
    return bytes(e.buf)


def log_entry(ptr, eei, when, record_offset, passback, normal_mode=True):
    """The Log Directory Entry (TS 1000-2 clause 8.1)."""
    e = Bits(5)
    e.put(0, 1, 1 if normal_mode else 0)  # LPF
    e.put(1, 5, ptr)
    e.put(6, 2, eei)                      # entry/exit indicator: 0 = outside
    e.put(8, 24, when)
    e.put(32, 2, record_offset)           # RO: the next slot to be written
    e.put(34, 6, passback)
    return bytes(e.buf)


def instance_and_seal(kid=1, inp=1, isam_id=0x01020304, isam_seq=1, seal=0xDE):
    """The IPE InstanceID and the seal that follows a dataset (table 11).

    The seal is a MAC over a key held in an ISAM, so its bytes are arbitrary
    here: nothing without the key can tell a real one from filler.
    """
    return (bytes([((kid & 0x0F) << 4) | (inp & 0x0F)]) +
            isam_id.to_bytes(4, "big") + isam_seq.to_bytes(3, "big") +
            bytes([seal]) * 8)


# ---------------------------------------------------------------- Value records
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


def value_group(records, format_rev):
    """A Value Record Data Group holding @p records (TS 1000-2 clause 7).

    The upper five bits of VGBitMap count the records the group supports, so a
    group is sized by how many it was issued with rather than by how many have
    been written.
    """
    assert 1 <= len(records) <= 5, "table 14 allows five records"
    length = (2 + len(records) * 15 + 3) // 4
    vg = Bits(length * 4)
    vg.put(0, 6, length)
    vg.put(6, 6, ((1 << len(records)) - 1) << (6 - len(records)))
    vg.put(12, 4, (format_rev + 8) & 0x0F)   # VGFormatRevision = IPEFormatRevision + 8
    for i, record in enumerate(records):
        vg.putb(2 + i * 15, record)
    return bytes(vg.buf)


def purse_tail(value, valc=0, legs=0, cumulative=0, flags=0):
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


def journey_tail(rides, transfers, flags, stored_expiry=0):
    """TYP 23 table 33a: a ride count, not money, at record bytes 10 to 12.

    ExpiryDateSRJ is revision 3 only (table 33b); in earlier revisions those
    bits are RFU and the decoder does not read them.
    """
    t = Bits(5)
    t.put(0, 8, rides)
    t.put(8, 8, transfers)
    t.put(16, 8, flags)
    t.put(26, 14, stored_expiry)
    return bytes(t.buf)


def period_tail(passes, flags, stored_expiry, current_expiry):
    """TYP 22 table 3.29: a stock of unactivated passes and two expiry dates -
    one for the stock, one for the pass in use."""
    t = Bits(5)
    t.put(0, 6, passes)
    t.put(6, 6, flags)
    t.put(12, 14, stored_expiry)
    t.put(26, 14, current_expiry)
    return bytes(t.buf)


def loyalty_tail(points):
    """TYP 3 table 9: LoyaltyPoints is three bytes wide, so a points balance
    does not fit the two-byte slot a purse balance uses."""
    t = Bits(5)
    t.put(0, 24, points)
    return bytes(t.buf)


def charge_tail(transactions, last_reset, legs=0):
    """TYP 5 table 17: transactions used this charge period, and the date the
    count was last cleared."""
    t = Bits(5)
    t.put(0, 8, transactions)
    t.put(10, 14, last_reset)
    t.put(36, 4, legs)
    return bytes(t.buf)


def voucher_tail(remaining, auto_renew=False):
    """TYP 25 and TYP 26, tables 38 and 42: a count and a renewal flag."""
    t = Bits(5)
    t.put(0, 8, remaining)
    t.put(15, 1, 1 if auto_renew else 0)
    return bytes(t.buf)


def count_tail(remaining):
    """TYP 24 table 139, and any other type whose tail is a bare count."""
    t = Bits(5)
    t.put(0, 8, remaining)
    return bytes(t.buf)


# ---------------------------------------------------------------- Locations
def loc2(def_type, body):
    """A LOC2 location: the tag then a fixed six-byte body, zero padded.

    The log stores locations this way rather than as the tag-length-value LOC1
    that an IPE uses, so a location type is only covered end to end once it has
    been through here as well.
    """
    assert len(body) <= 6, f"{len(body)} bytes does not fit a LOC2 body"
    return bytes([def_type]) + body + b"\x00" * (6 - len(body))


def loc1(def_type, body):
    """A LOC1 location: tag, length, body - the form an IPE dataset carries."""
    return bytes([def_type, len(body)]) + bytes(body)


def nlc(code):
    """A short rail NLC (LocDefType 203): four ASCII characters."""
    return code.encode("ascii")


def sncode(text):
    """An SNCODE service number: four 5-bit characters, right justified and
    padded with 0x1F (TS 1000-1 clause 4.2.4.2)."""
    alphabet = "0123456789ABCDEFGHKLMNPRSTVWXYZ "
    packed = 0
    for c in text.rjust(4)[-4:]:
        packed = (packed << 5) | (alphabet.index(c) if c != " " else 0x1F)
    return packed


def sncode2(text):
    """An SNCODE2 service number: four 6-bit characters, 0x3F the pad."""
    out = 0
    for c in text.rjust(4)[-4:]:
        if c == " ":
            code = 0x3F
        elif c.isdigit():
            code = int(c)
        else:
            code = 0x0A + ord(c.upper()) - ord("A")
        out = (out << 6) | code
    return out.to_bytes(3, "big")


def naptan(code):
    """A NaptanCode folded onto the telephone keypad of TS 1000-1 table 28 and
    packed into four bytes of BCD (LocDefType 206)."""
    keypad = {letter: digit
              for digit, letters in {"2": "ABC", "3": "DEF", "4": "GHI", "5": "JKL",
                                     "6": "MNO", "7": "PQRS", "8": "TUV",
                                     "9": "WXYZ"}.items()
              for letter in letters}
    folded = "".join(keypad.get(c.upper(), c) for c in code)
    return bcd(folded.rjust(8, "0"))


# ---------------------------------------------------------------- Tap records
def tt_record(txn, when, amount, origin, dest, ipe_ptr, route=None,
              mop=8, valc=0, vat=0, no_fare=False, format_rev=2):
    """A Transient Ticket Record, format revision 1 or 2 (TS 1000-5 table 59).

    Groups are written in bitmap order, because that is the order the reader
    walks them in: a group out of order shifts every group after it.
    """
    groups = b""
    bitmap = 0

    bitmap |= 1 << 0                                    # AMT
    amt = Bits(5)
    amt.put(0, 4, mop)
    amt.put(4, 4, valc)
    amt.put(8, 16, amount & 0xFFFF)
    amt.put(27, 1, 1 if no_fare else 0)                 # NoFareCharged
    amt.put(28, 12, vat)
    groups += bytes(amt.buf)

    if dest:
        bitmap |= 1 << 1
        groups += dest
    bitmap |= 1 << 2
    groups += bytes([ipe_ptr & 0x1F])
    if origin:
        bitmap |= 1 << 3
        groups += origin
    if route:
        bitmap |= 1 << 5
        groups += route

    return _tt_pack(bitmap, txn, when, groups, format_rev)


def tt_record_rev4(txn, when, amount, via, dest, ipe_ptr,
                   entry_when, entry_oid, candidates, origin=None, no_fare=False,
                   mop=1, valc=0, vat=2000, iin="633597", cipe_flags=0b10,
                   entry_isam=b"\x01\x02\x03\x04", entry_seq=b"\x00\x00\x09"):
    """A format revision 4 record: the shape a check-in/check-out closed system
    writes on exit. It carries the entry it closes, the products the gate weighed
    up, and a routing point (TS 1000-5 tables 64 and 66)."""
    groups = b""
    bitmap = 0

    bitmap |= 1 << 0                                    # AMT
    amt = Bits(5)
    amt.put(0, 4, mop)
    amt.put(4, 4, valc)
    amt.put(8, 16, amount & 0xFFFF)
    amt.put(27, 1, 1 if no_fare else 0)                 # NoFareCharged
    amt.put(28, 12, vat)                                # VATSalesTax, 0.01% steps
    groups += bytes(amt.buf)

    bitmap |= 1 << 1                                    # DEST
    groups += dest
    bitmap |= 1 << 2                                    # IPEID
    groups += bytes([ipe_ptr & 0x1F])
    if origin:
        bitmap |= 1 << 3                                # ORGN
        groups += origin
    bitmap |= 1 << 5                                    # RC
    groups += via
    bitmap |= 1 << 7                                    # IIN
    groups += bcd(iin)

    bitmap |= 1 << 8                                    # CIPE
    cipe = Bits(3)
    for i, c in enumerate(candidates):
        cipe.put(i * 5, 5, c)
    cipe.put(20, 4, cipe_flags)
    groups += bytes(cipe.buf)

    bitmap |= 1 << 9                                    # ENTRY
    entry = Bits(10)
    entry.putb(0, entry_isam)
    entry.putb(4, entry_seq)
    entry.put(56, 24, entry_when)
    groups += bytes(entry.buf)

    bitmap |= 1 << 10                                   # ENTRY OID
    groups += entry_oid.to_bytes(2, "big") + b"\x01"

    return _tt_pack(bitmap, txn, when, groups, 4)


def _tt_pack(bitmap, txn, when, groups, format_rev):
    """The fixed head of a Transient Ticket Record, and its 48-byte slot."""
    total = 7 + len(groups)
    assert total <= 48, f"record is {total} bytes, over a 48-byte slot"
    blocks = (total + 3) // 4
    r = Bits(48)
    r.put(0, 6, blocks)
    r.put(6, 6, 0)          # TTBitMap1
    r.put(12, 4, format_rev)
    r.put(16, 12, bitmap)   # TTBitMap2
    r.put(28, 4, txn)       # TTTransactionType
    r.put(32, 24, when)     # DateTimeStamp
    r.putb(7, groups)
    return bytes(r.buf)
