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


def compact_shell():
    """The three bytes of a Compact ITSO Shell (TS 1000-2 table 4, CMD4)."""
    s = Bits(3)
    s.put(0, 6, 6)      # ShellLength = 6 blocks
    s.put(6, 6, 0)      # ShellBitMap = 0 -> compact
    s.put(12, 4, 1)     # ShellFormatRevision = 1
    s.buf[2] = 4        # FVC = 4 (Ultralight CMD4)
    return bytes(s.buf)


def typ27_dataset(issue_date, amount, child=False, euro=False, passback=0, mop=0,
                  flags=0, geo=0, event1=0, event2=0, last_use=0, photocard=0,
                  expiry_offset=0, area_type=0, fare_value=False):
    """The 28-byte logical TYP 27 (Period, space-saving) dataset (TS 1000-5 table 48).

    One contiguous field sequence, before it is scattered across the card's
    static, dynamic and OTP page regions by type2_page_memory().
    """
    d = Bits(28)
    d.put(0, 6, 7)          # IPELength = 7 blocks
    d.put(6, 6, 0)          # IPEBitMap (Seq# absent on CMD4)
    d.put(12, 4, 1)         # IPEFormatRevision = 1
    d.put(16, 14, issue_date)
    d.put(30, 1, 1 if euro else 0)     # Sterling/Euro
    d.put(31, 1, 1 if child else 0)    # Child
    d.put(32, 4, passback)  # PassbackTime, minutes
    d.put(36, 4, mop)       # AmountPaidMethodOfPayment
    d.put(40, 16, amount)   # AmountPaid, VALI, in pence
    d.put(56, 4, flags)     # TYP27PassFlags
    # GeoValidity: 100 bits, bits 60-159 (table 50). A top nibble of zero makes
    # the rest a reference fare code - SPT's network-wide tickets carry code 0 -
    # or, with the bit below it set, a fare value; the low 32 bits hold either.
    # A non-zero nibble is a LocDefType less 200, and a LOCE follows.
    d.put(60, 4, area_type)
    d.put(64, 1, 1 if fare_value else 0)
    d.put(128, 32, geo)
    d.put(160, 4, event1)   # Event1
    d.put(164, 4, event2)   # Event2
    d.put(168, 24, last_use)  # LastUseDTS
    d.put(192, 24, photocard)  # PhotocardNumber (OTP)
    d.put(216, 8, expiry_offset)  # TYP27ExpiryDate: days before the directory expiry
    return bytes(d.buf)


def _otp_backup(set_bits):
    """A 32-bit one-time-programmable ScaledQtyBackup with @p set_bits set.

    TS 1000-10 clause 5.11 sets them from OTP3 bit 0 upward, which read as a
    big-endian word over page 3 is from its least significant bit - so a return
    with one ride left (31 set) is 7F FF FF FF, as Ryan Murphy's dump shows.
    """
    set_bits = max(0, min(32, set_bits))
    return (1 << set_bits) - 1


def typ28_dataset(issue_date, amount, passback=0, mop=0, flags=0, last_use=0,
                  ticks=(0, 0, 0, 0, 0, 0), issue_day=False, expiry_day=False):
    """The 28-byte logical TYP 28 (Carnet of day passes) dataset (TS 1000-5 table 51).

    @p ticks are the six ExpiryTicks: days before the directory expiry that a pass
    was used, 0 for one still to use and 31 for one never sold.
    """
    d = Bits(28)
    d.put(0, 6, 7)
    d.put(6, 6, 0)          # IPEBitMap: no Seq# on CMD4
    d.put(12, 4, 1)         # IPEFormatRevision = 1
    d.put(16, 14, issue_date)
    d.put(32, 4, passback)
    d.put(36, 4, mop)
    d.put(40, 16, amount)
    d.put(56, 4, flags)     # TYP28PassFlags
    d.put(168, 24, last_use)
    for i, tick in enumerate(ticks):
        d.put(192 + i * 5, 5, tick)
    d.put(222, 1, 1 if issue_day else 0)    # NDoIE
    d.put(223, 1, 1 if expiry_day else 0)   # NDoEE
    return bytes(d.buf)


def typ29_dataset(issue_date, rides_left, amount=0, mop=0, coupons=False,
                  flags=0, usage_code=0, usage=b"\0\0\0\0", last_use=0, rev=1,
                  passback=0, max_daily=0, max_transfers=0, journey_start=0,
                  transfers=0, daily=0):
    """The 28-byte logical TYP 29 (Multi-Use) dataset (TS 1000-5 tables 55, 55a).

    Revision 1 is a carnet of single tickets or coupons, the SPT Subway single and
    return; revision 2 is multi-leg journeys. QtyRemaining counts up from 8191 (or
    255) minus the number bought, and the ScaledQtyBackup in page 3 keeps a bit per
    ride at a scaling factor of 1.
    """
    d = Bits(28)
    d.put(0, 6, 7)
    d.put(6, 6, 0b001000)   # IPEBitMap bit 3: the ScaledQtyBackup is in use
    d.put(12, 4, rev)
    d.put(16, 14, issue_date)
    d.put(56, 4, flags)     # TYP29PassFlags
    if rev == 1:
        d.put(31, 1, 1 if coupons else 0)
        d.put(32, 4, 1)     # ScalingFactor 1: one backup bit per ride
        d.put(36, 4, mop)
        d.put(40, 16, amount)
        d.put(144, 3, usage_code)            # TYP29UsageRecCode
        d.put(147, 13, 8191 - rides_left)    # QtyRemaining
        d.putb(20, usage)                    # UsageRec, a 4-byte LOCE
    else:
        d.put(32, 4, passback)
        d.put(36, 4, max_daily)
        d.put(40, 4, max_transfers)
        d.put(44, 4, 1)     # ScalingFactor 1
        d.put(128, 24, journey_start)        # JnyComDTS
        d.put(152, 8, 255 - rides_left)      # QtyRemaining
        d.put(160, 4, transfers)
        d.put(164, 4, daily)
        d.put(168, 24, last_use)
    d.put(192, 32, _otp_backup(32 - rides_left))
    return bytes(d.buf)


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


# The InstanceID a synthetic paper ticket carries (TS 1000-10 clause 5.6.1): key
# 0, iteration 0, as a real SPT ticket has, from an invented ISAM registered to
# SPT's extended-range OID 8323 (so "Created by" names SPT), sequence invented.
T2_INSTANCE = instance_and_seal(kid=0, inp=0, isam_id=0x041C0099, isam_seq=0x001234)[:8]
# The Seal is a MAC nobody without the key can check, so filler - but not zero,
# which is how a CMD4 product is blocked (TS 1000-10 clause 5.16).
T2_SEAL = bytes([0x5E, 0xA1, 0x5E, 0xA1, 0x5E, 0xA1, 0x5E, 0xA1])


# The lock bytes of an issued CMD4 (page 2, bytes 2-3): pages 6-13 read-only,
# as TS 1000-10 clause 5.10.2 requires and as real SPT tickets have them.
T2_LOCKS = bytes([0xC0, 0x3F])


def type2_page_memory(uid, entry, dataset=None, instance=T2_INSTANCE, seal=T2_SEAL,
                      shell=None, locks=T2_LOCKS):
    """A CMD4 tag's 64-byte page memory: shell at page 6, entry at 6.3, IPE spread.

    @p uid is the 7-byte chip serial, @p entry the 5-byte IPE Directory Entry, and
    @p dataset the 28-byte logical Space Saving IPE (or None to leave it blank).
    The dataset is scattered to the physical regions TS 1000-10 section 5 defines:
    static in pages 10-13, dynamic Data0-7 in pages 4-5, OTP0-3 in page 3. The
    InstanceID goes in pages 8-9 and the Seal in pages 14-15; an all-zero @p seal
    is a blocked product. @p shell replaces the compact shell at page 6, and
    @p locks are the two static lock bytes (MIFARE Ultralight layout).
    """
    p = bytearray(64)
    p[0:3] = uid[0:3]
    p[3] = 0x88 ^ uid[0] ^ uid[1] ^ uid[2]      # BCC0 (cascade tag 0x88)
    p[4:8] = uid[3:7]
    p[8] = uid[3] ^ uid[4] ^ uid[5] ^ uid[6]    # BCC1
    p[10:12] = locks                            # page 2 bytes 2-3: lock bytes
    p[24:27] = compact_shell() if shell is None else shell  # page 6
    p[27:32] = entry                            # page 6 byte 3 to page 7
    p[32:40] = instance                         # pages 8-9: IPE InstanceID
    p[56:64] = seal                             # pages 14-15: the Seal
    if dataset is not None:
        assert len(dataset) == 28, "a TYP 27 dataset is 28 logical bytes"
        p[40:56] = dataset[0:16]                # static -> pages 10-13
        p[16:24] = dataset[16:24]               # dynamic Data0-7 -> pages 4-5
        p[12:16] = dataset[24:28]               # dynamic OTP0-3 -> page 3
    return bytes(p)


# ------------------------------------------------ Full-shell Type 2 (CMD9/CMD10)
def sct_bits(sector_count):
    """psi: the smallest number of bits with S <= 2^psi (TS 1000-2 5.1.5.1)."""
    psi = 1
    while (1 << psi) < sector_count and psi < 8:
        psi += 1
    return psi


def directory(entries, chain, sector_count, dir_entries, sct_len, sequence,
              blocked=False, instance=None):
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
    # The Directory InstanceID follows DIRS# (TS 1000-2 table 8): the key and
    # shell iteration, then the ISAM that last sealed the directory. Some cards
    # leave it off, which a decoder has to cope with too.
    if instance is not None:
        return bytes(d.buf) + instance
    return bytes(d.buf)


# TS 1000-10 table 107: a CMD9's Abacus, bytes 1 and 3 of page 3, by state.
# 0x1000 is how the chip is delivered; the state is the count of bits set.
ABACUS = dict(enumerate([
    0x1000, 0x9000, 0xD000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00,
    0xFF80, 0xFFC0, 0xFFE0, 0xFFF0, 0xFFF8, 0xFFFC, 0xFFFE, 0xFFFF], start=1))


def type2_full_page_memory(uid, shell, dir_a, dir_b, sectors, sector_size,
                           locks=bytes([0xF7, 0x0F]), abacus=None):
    """A CMD9 or CMD10 tag's page memory, up to the end of logical sector 6.

    The layout of TS 1000-10 figures 4.1, 4.2 and 7: the chip pages, the shell
    in pages 4-11 rotated so its first byte is stored last (clause 10.11.3), the
    two Directory copies in pages 0x0C-0x15 and 0x16-0x1F, then @p sectors, a map
    of logical sector 1-6 to its bytes, from page 0x20 at @p sector_size each.
    The lock bytes default to the ones clause 10.23.1 recommends, which lock
    the shell's pages; @p abacus, a state from 1 to 16, fills bytes 1 and 3 of
    the OTP page as a CMD9's Abacus (table 107).
    """
    p = bytearray(128 + 6 * sector_size)
    p[0:3] = uid[0:3]
    p[3] = 0x88 ^ uid[0] ^ uid[1] ^ uid[2]      # BCC0
    p[4:8] = uid[3:7]
    p[8] = uid[3] ^ uid[4] ^ uid[5] ^ uid[6]    # BCC1
    p[10:12] = locks
    if abacus is not None:
        p[13] = ABACUS[abacus] >> 8
        p[15] = ABACUS[abacus] & 0xFF
    shell = bytes(shell)
    assert len(shell) == 32, "the shell block is eight pages"
    p[16:47] = shell[1:32]
    p[47] = shell[0]
    for offset, copy in ((48, dir_a), (88, dir_b)):
        assert len(copy) <= 40, "a directory copy is ten pages"
        p[offset:offset + len(copy)] = copy
    for sector, data in sectors.items():
        assert 1 <= sector <= 6 and len(data) <= sector_size
        offset = 128 + (sector - 1) * sector_size
        p[offset:offset + len(data)] = data
    return bytes(p)


def split_sectors(data, sector_size):
    """@p data cut into @p sector_size pieces, the last padded with zeros."""
    return [pad(data[i:i + sector_size], sector_size)
            for i in range(0, len(data), sector_size)]


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


def value_group(records, format_rev, extension=b""):
    """A Value Record Data Group holding @p records (TS 1000-2 clause 7).

    The upper five bits of VGBitMap count the records the group supports, so a
    group is sized by how many it was issued with rather than by how many have
    been written.
    """
    assert 1 <= len(records) <= 5, "table 14 allows five records"
    length = (2 + len(records) * 15 + len(extension) + 3) // 4
    vg = Bits(length * 4)
    vg.put(0, 6, length)
    # The LSB flags a Value Group Extension after the records (clause 7.5).
    vg.put(6, 6, (((1 << len(records)) - 1) << (6 - len(records))) | (1 if extension else 0))
    vg.put(12, 4, (format_rev + 8) & 0x0F)   # VGFormatRevision = IPEFormatRevision + 8
    for i, record in enumerate(records):
        vg.putb(2 + i * 15, record)
    vg.putb(2 + len(records) * 15, extension)
    return bytes(vg.buf)


def isam(oid, serial):
    """An ISAM ID: the OID in the top bits, extended per TS 1000-2 annex B."""
    if oid < 8192:
        return (oid << 19) | serial
    if oid < 16384:
        return ((oid & 0x1FFF) << 19) | (1 << 18) | serial
    ext = 0b110 if oid < 57344 else 0b111
    return ((oid & 0x1FFF) << 19) | (ext << 16) | serial


def capping_vgx(ref, strategy, sets, locations):
    """A Complex Capping Value Group Extension (TS 1000-5 tables AD1, AD2).

    @p sets: four (rule, last_txn, last_fare, uncapped, day, multiday, days).
    @p locations: LOC1 bytes, one for AD1, or (loc, dts) pairs for AD2.
    """
    stride = 9 if ref == 1 else 11
    body = Bits(4 + 4 * stride)
    body.putb(2, strategy.to_bytes(2, "big"))
    for a, (rule, txn, last, uncapped, day, multi, days) in enumerate(sets):
        base = 4 + a * stride
        body.put(base * 8, 4, rule)
        if ref == 1:
            body.put(base * 8 + 4, 4, txn)
            amounts = base + 1
        else:
            body.put(base * 8 + 4, 16, last & 0xFFFF)
            body.put((base + 2) * 8 + 4, 4, txn)
            amounts = base + 3
        body.putb(amounts, (uncapped & 0xFFFF).to_bytes(2, "big"))
        body.putb(amounts + 2, (day & 0xFFFF).to_bytes(2, "big"))
        body.putb(amounts + 4, (multi & 0xFFFF).to_bytes(2, "big"))
        body.putb(amounts + 6, days.to_bytes(2, "big"))
    data = bytes(body.buf)
    if ref == 1:
        data += locations
    else:
        for loc, when in locations:
            data += loc + when.to_bytes(3, "big")
    data = pad(data, (len(data) + 3) // 4 * 4)
    head = Bits(2)
    head.put(0, 6, len(data) // 4)      # VGXLength, in blocks
    head.put(6, 2, 0)                   # VGXRef bits 9-8: a reference number
    head.put(8, 8, ref)                 # ...which is this
    return bytes(head.buf) + data[2:]


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
              mop=8, valc=0, vat=0, no_fare=False, format_rev=2,
              companion=False, return_ticket=False, writer=None):
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
    amt.put(24, 1, 1 if companion else 0)               # CompanionTravelled (rev 2)
    amt.put(25, 1, 1 if return_ticket else 0)           # ReturnTicket (rev 2)
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

    return _tt_pack(bitmap, txn, when, groups, format_rev, writer)


def tt_record_rev4(txn, when, amount, via, dest, ipe_ptr,
                   entry_when, entry_oid, candidates, origin=None, no_fare=False,
                   mop=1, valc=0, vat=2000, iin="633597", cipe_flags=0b10,
                   entry_isam=b"\x01\x02\x03\x04", entry_seq=b"\x00\x00\x09",
                   entry_iin_index=1, writer=None):
    """A format revision 4 record (TS 1000-5 tables 64 and 66).

    With every group given, it is the shape a check-in/check-out closed system
    writes on exit: the entry it closes, the products the gate weighed up, and
    a routing point. Any group passed as None is left out of the bitmap, which
    is how a real card's records differ: the GWR Touch card read on 2026-09-28
    had a check-out with no amount and no entry group, and a check-in carrying
    only the entry operator (ENTRY_OID without ENTRY).
    """
    groups = b""
    bitmap = 0

    if amount is not None:
        bitmap |= 1 << 0                                # AMT
        amt = Bits(5)
        amt.put(0, 4, mop)
        amt.put(4, 4, valc)
        amt.put(8, 16, amount & 0xFFFF)
        amt.put(27, 1, 1 if no_fare else 0)             # NoFareCharged
        amt.put(28, 12, vat)                            # VATSalesTax, 0.01% steps
        groups += bytes(amt.buf)

    if dest is not None:
        bitmap |= 1 << 1                                # DEST
        groups += dest
    bitmap |= 1 << 2                                    # IPEID
    groups += bytes([ipe_ptr & 0x1F])
    if origin:
        bitmap |= 1 << 3                                # ORGN
        groups += origin
    if via is not None:
        bitmap |= 1 << 5                                # RC
        groups += via
    if iin is not None:
        bitmap |= 1 << 7                                # IIN
        groups += bcd(iin)

    if candidates is not None:
        bitmap |= 1 << 8                                # CIPE
        cipe = Bits(3)
        for i, c in enumerate(candidates):
            cipe.put(i * 5, 5, c)
        cipe.put(20, 4, cipe_flags)
        groups += bytes(cipe.buf)

    if entry_when is not None:
        bitmap |= 1 << 9                                # ENTRY
        entry = Bits(10)
        entry.putb(0, entry_isam)
        entry.putb(4, entry_seq)
        entry.put(56, 24, entry_when)
        groups += bytes(entry.buf)

    if entry_oid is not None:
        bitmap |= 1 << 10                               # ENTRY OID
        groups += entry_oid.to_bytes(2, "big") + bytes([entry_iin_index])

    return _tt_pack(bitmap, txn, when, groups, 4, writer)


def _tt_pack(bitmap, txn, when, groups, format_rev, writer=None):
    """The fixed head of a Transient Ticket Record, and its 48-byte slot.

    @p writer, if given, is the ISAM ID of the reader that wrote the record. A
    record is an Orphan IPE Data Group, so its InstanceID follows the dataset:
    a key/iteration byte, the ISAM ID, its sequence number, then the seal.
    """
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
    if writer is not None:
        assert blocks * 4 + 16 <= 48, "no room for the InstanceID and seal"
        r.putb(blocks * 4, instance_and_seal(isam_id=writer, isam_seq=0x3D8C))
    return bytes(r.buf)
