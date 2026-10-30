#!/usr/bin/env python3
"""Decrypt and decode PSP ad hoc Wi-Fi traffic from a radiotap pcap capture.

A real PSP encrypts its ad hoc link traffic, so a sniffed capture of two consoles playing is
ciphertext. This tool decrypts it and decodes the PDP/PTP/matching framing, which is handy for
comparing what a game does on hardware against what PPSSPP sends at the sceNetAdhoc HLE level.

  adhoc_decrypt.py CAPTURE.pcap                     # handshake + list the data frames
  adhoc_decrypt.py CAPTURE.pcap --key HEX           # decrypt (16-byte key) or decrypt+verify (20-byte)
  adhoc_decrypt.py CAPTURE.pcap --known HEX16@CTR --known HEX16@CTR   # recover the key, then decrypt
  adhoc_decrypt.py --selftest

The key is ephemeral (negotiated per ad hoc group membership by an ECDH handshake), so it is not in
the capture. But the data cipher has a known-plaintext weakness: with one known 16-byte plaintext
block for an even-counter frame and one for an odd-counter frame, --known recovers the 16-byte key
that decrypts everything (one AES op each, validated by the frame's own counter). The last 4 bytes of
the 20-byte key only feed the integrity tag; recover them with adhoc_recover.c if you want tags
verified. This is not a break of the key exchange - it just needs a capture that contains some known
plaintext (the ad hoc envelope's constant fields plus, e.g., a game's constant packet header or a
nickname you set). Capturing needs a Wi-Fi adapter in monitor mode on the consoles' ad hoc channel.

The two fixed KIRK keys below ship in PPSSPP already (ext/libkirk/kirk_engine.c). Pure Python.
"""
import argparse
import hashlib
import struct
import sys
import zlib

# ---- AES-128, pure Python ------------------------------------------------------------------

def _xt(a):
    a <<= 1
    return (a ^ 0x11B) if a & 0x100 else a

def _mul(a, b):
    r = 0
    while b:
        if b & 1:
            r ^= a
        a = _xt(a)
        b >>= 1
    return r

SBOX = [0] * 256
INV = [0] * 256
def _init_sbox():
    for i in range(256):
        inv = 0
        if i:
            for j in range(1, 256):
                if _mul(i, j) == 1:
                    inv = j
                    break
        x = inv
        s = x ^ ((x << 1 | x >> 7) & 0xFF) ^ ((x << 2 | x >> 6) & 0xFF) ^ ((x << 3 | x >> 5) & 0xFF) ^ ((x << 4 | x >> 4) & 0xFF) ^ 0x63
        SBOX[i] = s
        INV[s] = i
_init_sbox()

def aes_expand(key):
    w = [list(key[i:i + 4]) for i in range(0, 16, 4)]
    rc = 1
    for i in range(4, 44):
        t = list(w[i - 1])
        if i % 4 == 0:
            t = [SBOX[b] for b in t[1:] + t[:1]]
            t[0] ^= rc
            rc = _xt(rc)
        w.append([w[i - 4][j] ^ t[j] for j in range(4)])
    return [sum(w[r * 4:r * 4 + 4], []) for r in range(11)]

def aes_enc(rk, b):
    s = [b[i] ^ rk[0][i] for i in range(16)]
    for r in range(1, 11):
        s = [SBOX[x] for x in s]
        s = [s[(i + 4 * (i % 4)) % 16] for i in range(16)]
        if r != 10:
            n = []
            for c in range(4):
                a = s[c * 4:c * 4 + 4]
                n += [_mul(a[0], 2) ^ _mul(a[1], 3) ^ a[2] ^ a[3], a[0] ^ _mul(a[1], 2) ^ _mul(a[2], 3) ^ a[3],
                      a[0] ^ a[1] ^ _mul(a[2], 2) ^ _mul(a[3], 3), _mul(a[0], 3) ^ a[1] ^ a[2] ^ _mul(a[3], 2)]
            s = n
        s = [s[i] ^ rk[r][i] for i in range(16)]
    return bytes(s)

def aes_dec(rk, b):
    s = [b[i] ^ rk[10][i] for i in range(16)]
    for r in range(9, -1, -1):
        s = [s[(i - 4 * (i % 4)) % 16] for i in range(16)]
        s = [INV[x] for x in s]
        s = [s[i] ^ rk[r][i] for i in range(16)]
        if r:
            n = []
            for c in range(4):
                a = s[c * 4:c * 4 + 4]
                n += [_mul(a[0], 14) ^ _mul(a[1], 11) ^ _mul(a[2], 13) ^ _mul(a[3], 9),
                      _mul(a[0], 9) ^ _mul(a[1], 14) ^ _mul(a[2], 11) ^ _mul(a[3], 13),
                      _mul(a[0], 13) ^ _mul(a[1], 9) ^ _mul(a[2], 14) ^ _mul(a[3], 11),
                      _mul(a[0], 11) ^ _mul(a[1], 13) ^ _mul(a[2], 9) ^ _mul(a[3], 14)]
            s = n
    return bytes(s)

def xor(a, b):
    return bytes(x ^ y for x, y in zip(a, b))

def kirk7(key, data):
    """KIRK command 7: AES-128-CBC decrypt, IV 0."""
    rk = aes_expand(key)
    out, prev = b'', bytes(16)
    for i in range(0, len(data) - len(data) % 16, 16):
        c = data[i:i + 16]
        out += xor(aes_dec(rk, c), prev)
        prev = c
    return out

# Fixed KIRK 4/7 keys (already in ext/libkirk/kirk_engine.c keyvault).
KIRK_KEY_02 = bytes.fromhex('B813C35EC64441E3DC3C16F5B45E6484')  # auth handshake messages
KIRK_KEY_44 = bytes.fromhex('7DF49265E3FAD678D6FE78ADBB3DFB63')  # data keystream

# The handshake's ECDH curve (same as KIRK's ec_p/ec_a/ec_b).
EC_P = 0xFFFFFFFFFFFFFFFF00000001FFFFFFFFFFFFFFFF
EC_A = EC_P - 3
EC_B = 0xA68BEDC33418029C1D3CE33B9A321FCCBB9E0F0B

def on_curve(pt):
    x = int.from_bytes(pt[:20], 'big')
    y = int.from_bytes(pt[20:40], 'big')
    return (y * y - (x * x * x + EC_A * x + EC_B)) % EC_P == 0

# ---- data cipher ---------------------------------------------------------------------------
#
# A data frame is:  00 02 | counter(8, LE) | tag(16) | ciphertext.
# The counter counts 16-byte blocks sent in that direction, cumulative, from 0.
# Keystream: KIRK-7 (AES-CBC, IV 0, key 0x44) of consecutive blocks [C||half, C+1||half', ...],
# where half = key[0:8] on even block counters and key[8:16] on odd. Ciphertext = plaintext XOR it.
# Tag: first 16 bytes of SHA1(key20 || counter_header(16) || ciphertext).

def keystream(key16, counter, nblocks):
    blocks = b''
    for i in range(nblocks):
        c = counter + i
        blocks += struct.pack('<Q', c) + (key16[0:8] if c & 1 == 0 else key16[8:16])
    return kirk7(KIRK_KEY_44, blocks)

def data_tag(key20, hdr16, ct):
    return hashlib.sha1(key20 + hdr16 + ct).digest()[:16]

def data_decrypt(key, body):
    """body = the 0x88C8 payload starting at 00 02. Returns (counter, plaintext, tag_ok).
    A 16-byte key decrypts only (tag_ok=None); a 20-byte key also verifies the tag."""
    hdr = body[2:18]
    tag = body[18:34]
    ct = body[34:]
    ct = ct[:len(ct) - len(ct) % 16]
    counter = struct.unpack('<Q', hdr[:8])[0]
    ok = (data_tag(key, hdr, ct) == tag) if len(key) == 20 else None
    pt = xor(ct, keystream(key[:16], counter, len(ct) // 16))
    return counter, pt, ok

def data_encrypt(key20, counter, pt):
    hdr = struct.pack('<Q', counter) + bytes(8)
    ct = xor(pt, keystream(key20, counter, len(pt) // 16))
    return b'\x00\x02' + hdr + data_tag(key20, hdr, ct) + ct

# ---- key recovery from known plaintext -----------------------------------------------------
#
# The first block's keystream = AES_dec(key44, C||half), so AES_enc(key44, ct[0:16] ^ pt[0:16])
# equals counter(LE64) || half. One AES_enc over a known first-block plaintext yields 8 key bytes,
# and the counter in bytes 0-7 confirming the guess is a 64-bit check. Two frames of opposite
# counter parity give the 16-byte key. The last 4 of the 20-byte key only feed the tag.

_RK44 = aes_expand(KIRK_KEY_44)

def half_from_known(counter, ct16, pt16):
    blk = aes_enc(_RK44, xor(ct16, pt16))
    return blk[8:16] if struct.unpack('<Q', blk[:8])[0] == counter else None

def recover16(knowns):
    """knowns: list of (counter, ct16, pt16). Returns the 16-byte key or None."""
    halves = {}
    for counter, ct16, pt16 in knowns:
        h = half_from_known(counter, ct16, pt16)
        if h:
            halves[counter & 1] = h
    return halves[0] + halves[1] if len(halves) == 2 else None

# ---- framing -------------------------------------------------------------------------------
#
# Decrypted payload is a PDP or PTP frame (same first 8 bytes: type, flags/00, length BE, src/dst
# port BE). PDP (type 01) then has a 16-bit sequence + 0000; a matching message rides in its
# payload as 01 opcode len16 data. PTP (type 02) has a flags byte and a 32-bit sequence, plus a
# 32-bit ack/window on control frames.

_PTP_FLAGS = {1: 'SYN', 2: 'SYN-ACK', 3: 'ACK', 4: 'DATA', 5: 'WND-ACK'}

def describe(pt):
    if len(pt) < 8:
        return 'short'
    typ = pt[0]
    ln = struct.unpack('>H', pt[2:4])[0]
    sp, dp = struct.unpack('>H', pt[4:6])[0], struct.unpack('>H', pt[6:8])[0]
    if typ == 1:  # PDP
        seq = struct.unpack('>H', pt[8:10])[0] if len(pt) >= 10 else 0
        pay = pt[12:ln] if 12 < ln <= len(pt) else pt[12:]
        s = 'PDP   %04x->%04x seq %04x len %d' % (sp, dp, seq, ln)
        if len(pay) >= 4 and pay[0] == 1:  # matching message
            op = pay[1]
            mln = struct.unpack('>H', pay[2:4])[0]
            s += '  | match op %d len %d %r' % (op, mln, bytes(pay[4:4 + min(mln, 16)]))
        return s
    if typ == 2:  # PTP
        flags = pt[1]
        seq = struct.unpack('>I', pt[8:12])[0] if len(pt) >= 12 else 0
        name = _PTP_FLAGS.get(flags, '?')
        s = 'PTP   %04x->%04x %-7s seq %d len %d' % (sp, dp, name, seq, ln)
        if flags in (1, 2, 3, 5) and len(pt) >= 16:
            s += ' ack/wnd %d' % struct.unpack('>I', pt[12:16])[0]
        return s
    return 'type %02x len %d' % (typ, ln)

# ---- pcap ----------------------------------------------------------------------------------

def read_frames(path):
    f = open(path, 'rb').read()
    magic = struct.unpack('<I', f[:4])[0]
    if magic not in (0xA1B2C3D4, 0xA1B23C4D):
        sys.exit('not a little-endian pcap')
    off = 24
    t0 = None
    while off + 16 <= len(f):
        ts, tu, cl, ol = struct.unpack('<IIII', f[off:off + 16])
        off += 16
        p = f[off:off + cl]
        off += cl
        t = ts + (tu / 1e9 if magic == 0xA1B23C4D else tu / 1e6)
        if len(p) < 8:
            continue
        rl = struct.unpack('<H', p[2:4])[0]
        if len(p) < rl + 24:
            continue
        present = struct.unpack('<I', p[4:8])[0]
        w = p[rl:]
        # radiotap flags field: bit 0 TSFT (8 bytes, 8-aligned) precedes it
        fcs = False
        if present & 2:
            o = 8
            pp = present
            while pp & 0x80000000:  # extended bitmaps
                pp = struct.unpack('<I', p[o:o + 4])[0]
                o += 4
            if present & 1:
                o = (o + 7) & ~7
                o += 8
            fcs = bool(p[o] & 0x10)
        if fcs:
            if len(w) >= 4 and zlib.crc32(w[:-4]) & 0xFFFFFFFF != struct.unpack('<I', w[-4:])[0]:
                continue  # bad FCS
            w = w[:-4]
        if len(w) < 24 or (w[0] >> 2) & 3 != 2:
            continue
        hdr = 24 + (2 if (w[0] >> 4) & 8 else 0)
        body = w[hdr:]
        if body[:8] != bytes.fromhex('AAAA0300000088C8'):
            continue
        if t0 is None:
            t0 = t
        yield t - t0, w[10:16].hex(':'), w[4:10].hex(':'), body[8:]

# ---- main ----------------------------------------------------------------------------------

def show_handshake(frames):
    pubs = {}
    for t, src, dst, b in frames:
        if b[:2] != b'\x00\x01':
            continue
        kind, mtype, ln = b[2], b[3], struct.unpack('>H', b[4:6])[0]
        print('%8.3f %s -> %s  auth msg %02x/%02x len 0x%x' % (t, src, dst, kind, mtype, ln))
        if mtype == 2:
            print('          (broadcast, plain) %s' % b[6:6 + ln].rstrip(b'\xff').hex())
            continue
        pt = kirk7(KIRK_KEY_02, b[6:6 + ln])
        for i in range(0, len(pt), 32):
            print('          %03x %s' % (i, pt[i:i + 32].hex()))
        pubs.setdefault(src, pt[0x10:0x38])
    for src, pub in pubs.items():
        print('ephemeral public key of %s (msg+0x10): %s  on curve: %s' % (src, pub.hex(), on_curve(pub)))

def main():
    ap = argparse.ArgumentParser(description='Decrypt and decode PSP ad hoc traffic from a pcap.')
    ap.add_argument('pcap', nargs='?')
    ap.add_argument('--key', help='session key (hex): 16 bytes decrypts, 20 bytes also verifies tags')
    ap.add_argument('--known', action='append', default=[], metavar='HEX16@COUNTER',
                    help='a known first-block plaintext (16 bytes) for the data frame at COUNTER; '
                         'give two of opposite counter parity to recover the 16-byte key')
    ap.add_argument('--raw', action='store_true', help='show raw plaintext hex instead of decoded framing')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()

    if a.selftest:
        k = bytes(range(20))
        pt = b'\x01\x00\x00\x10\x77\x00\x77\x00ABCDSYN\x01'
        fr = data_encrypt(k, 0x12, pt)
        c, back, ok = data_decrypt(k, fr)
        assert back == pt and ok and c == 0x12
        assert recover16([(c, fr[34:50], pt[:16]),
                          (c + 1, data_encrypt(k, c + 1, pt)[34:50], pt[:16])]) == k[:16]
        print('selftest ok')
        return

    if not a.pcap:
        ap.error('a pcap is required (or --selftest)')

    frames = list(read_frames(a.pcap))
    print('%d 0x88C8 frames' % len(frames))
    show_handshake(frames)

    data = [f for f in frames if f[3][:2] == b'\x00\x02']
    print('\n%d data frames' % len(data))
    key = bytes.fromhex(a.key) if a.key else None

    if a.known:
        byc = {}
        for _, _, _, b in data:
            byc.setdefault(struct.unpack('<Q', b[2:10])[0], b[34:50])
        knowns = []
        for item in a.known:
            pthex, _, ctr = item.partition('@')
            c = int(ctr)
            if c not in byc:
                sys.exit('no data frame at counter %d' % c)
            knowns.append((c, byc[c], bytes.fromhex(pthex)))
        key = recover16(knowns)
        if not key:
            sys.exit('could not recover the key: need one even and one odd counter, '
                     'and the known plaintext must be correct')
        print('recovered 16-byte key: %s' % key.hex())
        print('(decrypts; for tag verification recover the full 20-byte key with adhoc_recover.c)')

    bycounter = {}
    for t, src, dst, b in data:
        counter = struct.unpack('<Q', b[2:10])[0]
        bycounter.setdefault(counter, []).append((src, b[34:]))
        if not key:
            continue
        counter, pt, ok = data_decrypt(key, b)
        tagstr = '-  ' if ok is None else ('ok ' if ok else 'BAD')
        info = pt[:48].hex() if a.raw else describe(pt)
        print('%8.3f %s -> %s ctr %5d tag %s %s' % (t, src[-5:], dst[-5:], counter, tagstr, info))

    if not key:
        # Without a key, show which counters both directions used (same keystream), as a hint for
        # where to look for known plaintext: XOR of two such ciphertexts is XOR of their plaintexts.
        same = [(c, v) for c, v in sorted(bycounter.items()) if len({s for s, _ in v}) > 1]
        for t, src, dst, b in data[:30]:
            print('%8.3f %s -> %s ctr %5d len %4d' % (t, src[-5:], dst[-5:], struct.unpack('<Q', b[2:10])[0], len(b) - 34))
        print('\n%d counters used by both directions (keystream reuse); XOR of the two is the plaintext XOR' % len(same))

if __name__ == '__main__':
    main()
