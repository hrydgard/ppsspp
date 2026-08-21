#!/usr/bin/env python3
"""PSP .PKG (NPDRM package) parser/extractor.

Lists or extracts a package - PSP game updates in particular, which pkg2zip skips.
See docs/pkg_notes.md for the format, and for what these packages turn out to contain.

    pkg.py FILE.pkg ...              list contents
    pkg.py -x OUTDIR FILE.pkg ...    extract into OUTDIR

Needs the "cryptography" module for AES.
"""
import sys, os, struct
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

PKG_PSP_KEY = bytes.fromhex("07f2c68290b50d2c33818d709b60e62b")
PKG_PS3_KEY = bytes.fromhex("2e7b71d7c9c9a14ea3221f188828b8f8")
PKG_VITA_2  = bytes.fromhex("e31a70c9ce1dd72bf3c0622963f2eccb")
PKG_VITA_3  = bytes.fromhex("423aca3a2bd5649f9686abad6fd8801f")
PKG_VITA_4  = bytes.fromhex("af07fd59652527baf13389668b17d9ea")

def aes_ecb_enc(key, block):
    c = Cipher(algorithms.AES(key), modes.ECB()).encryptor()
    return c.update(block) + c.finalize()

def ctr_xor(key, iv, block_index, data):
    ctr = (int.from_bytes(iv, 'big') + block_index) % (1 << 128)
    c = Cipher(algorithms.AES(key), modes.CTR(ctr.to_bytes(16, 'big'))).decryptor()
    return c.update(data) + c.finalize()

TYPES = {0: "?", 1: "NPDRM", 2: "NPDRM_EDAT", 3: "FILE", 4: "DIRECTORY",
         9: "SELF", 11: "PSP_FILE", 18: "DIRECTORY2"}

META_NAMES = {1: "DRM_TYPE", 2: "CONTENT_TYPE", 3: "PACKAGE_TYPE", 4: "PACKAGE_SIZE",
              5: "SDK/NPDRM_REV", 6: "TITLE_ID", 7: "QA_DIGEST", 8: "UNK_0x8",
              9: "UNK_0x9", 10: "INSTALL_DIR", 11: "UNK_0xB", 12: "UNK_0xC",
              13: "ITEMS_TABLE", 14: "PARAM_SFO", 15: "UNK_0xF"}

CONTENT_TYPES = {0x4: "PS3_GameData", 0x5: "PS3_GameExec", 0x6: "PS1_PSN",
                 0x7: "PSP_PSN", 0x9: "Theme", 0xB: "License", 0xE: "PSP_PCEngine",
                 0xF: "PSP_Minis", 0x10: "PSP_NeoGeo", 0x15: "PSVita_App",
                 0x16: "PSVita_DLC", 0x18: "PSM"}


class Item:
    __slots__ = ("name", "off", "size", "psp_type", "flags", "key")
    def is_dir(self):
        return self.flags in (4, 18)


class Pkg:
    def __init__(self, path):
        self.path = path
        self.f = open(path, 'rb')
        h = self.f.read(0x100)
        if h[:4] != b'\x7fPKG':
            raise ValueError("not a PKG")
        (self.rev, self.type, self.meta_off, self.meta_cnt, self.meta_size,
         self.item_cnt, self.total_size, self.data_off, self.data_size) = \
            struct.unpack(">HHIIIIQQQ", h[4:0x30])
        self.content_id = h[0x30:0x60].split(b'\0')[0].decode('ascii', 'replace')
        self.digest = h[0x60:0x70]
        self.riv = h[0x70:0x80]
        self.key_id = h[0xE7] & 7
        self.ext = h[0xC0:0xC4] == b'\x7fext'

        self.meta = self._read_meta()
        self.content_type = 0
        self.items_off = 0
        self.sfo_off = self.sfo_size = 0
        for ident, val in self.meta:
            if ident == 2:
                self.content_type = struct.unpack(">I", val[:4])[0]
            elif ident == 13:
                self.items_off, self.items_size = struct.unpack(">II", val[:8])
            elif ident == 14:
                self.sfo_off, self.sfo_size = struct.unpack(">II", val[:8])

        if self.type == 2:  # PSP / Vita
            if self.key_id == 1:
                self.main_key = PKG_PSP_KEY
            else:
                vk = {2: PKG_VITA_2, 3: PKG_VITA_3, 4: PKG_VITA_4}[self.key_id]
                self.main_key = aes_ecb_enc(vk, self.riv)
        else:
            self.main_key = PKG_PS3_KEY
        self.iv = self.riv

    def _read_meta(self):
        self.f.seek(self.meta_off)
        buf = self.f.read(self.meta_size if self.meta_size else 0x1000)
        out, p = [], 0
        for _ in range(self.meta_cnt):
            if p + 8 > len(buf):
                break
            ident, size = struct.unpack(">II", buf[p:p+8])
            out.append((ident, buf[p+8:p+8+size]))
            p += 8 + size
        return out

    def dec(self, offset, size, key=None):
        """Read+decrypt `size` bytes at `offset` relative to the encrypted data area."""
        key = key or self.main_key
        pre = offset & 0xF
        base = offset - pre
        self.f.seek(self.data_off + base)
        raw = self.f.read(((size + pre + 15) // 16) * 16)
        return ctr_xor(key, self.iv, base // 16, raw)[pre:pre+size]

    def items(self):
        tbl = self.dec(self.items_off, self.item_cnt * 0x20)
        res = []
        for i in range(self.item_cnt):
            no, ns, doff, dsize, psp_type, _, _, flags = struct.unpack(
                ">IIQQBBBB", tbl[i*0x20:i*0x20+0x1C])
            it = Item()
            it.psp_type, it.flags, it.off, it.size = psp_type, flags, doff, dsize
            it.key = self.main_key if (self.type != 2 or psp_type == 0x90) else PKG_PS3_KEY
            it.name = self.dec(no, ns, it.key).decode('utf-8', 'replace')
            res.append(it)
        return res

    def read_item(self, it, maxsize=None):
        n = it.size if maxsize is None else min(it.size, maxsize)
        return self.dec(it.off, n, it.key)

    def sfo(self):
        if self.sfo_size:
            return parse_sfo(self.dec(self.sfo_off, self.sfo_size))
        for it in self.items():
            if it.name.upper().endswith("PARAM.SFO"):
                return parse_sfo(self.read_item(it))
        return {}


def parse_sfo(data):
    if len(data) < 0x14 or data[:4] != b'\0PSF':
        return {}
    key_tab, data_tab, count = struct.unpack("<III", data[0x08:0x14])
    out = {}
    for i in range(count):
        e = 0x14 + i * 0x10
        if e + 0x10 > len(data):
            break
        ko, fmt, ln, maxln, do = struct.unpack("<HHIII", data[e:e+0x10])
        name = data[key_tab+ko:data.index(b'\0', key_tab+ko)].decode('ascii', 'replace')
        raw = data[data_tab+do:data_tab+do+ln]
        if fmt == 0x0404:
            out[name] = struct.unpack("<I", raw[:4])[0]
        else:
            out[name] = raw.split(b'\0')[0].decode('utf-8', 'replace')
    return out


def info(p, list_items=True):
    print("== %s" % os.path.basename(p.path))
    print("   content_id %s   key_id %d   type %d   content_type 0x%x (%s)" % (
        p.content_id, p.key_id, p.type, p.content_type,
        CONTENT_TYPES.get(p.content_type, "?")))
    print("   items %d   data 0x%x+0x%x   total 0x%x" % (
        p.item_cnt, p.data_off, p.data_size, p.total_size))
    sfo = p.sfo()
    if sfo:
        print("   SFO: " + ", ".join("%s=%r" % kv for kv in sfo.items()))
    if list_items:
        for it in p.items():
            print("     %-10s pt=%02x %10d  %s" % (
                TYPES.get(it.flags, "0x%x" % it.flags), it.psp_type, it.size, it.name))


def extract(p, outdir):
    for it in p.items():
        dst = os.path.join(outdir, it.name)
        if it.is_dir():
            os.makedirs(dst, exist_ok=True)
            continue
        os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
        with open(dst, 'wb') as o:
            left, off = it.size, it.off
            while left:
                n = min(left, 1 << 20)
                o.write(p.dec(off, n, it.key))
                off += n
                left -= n
        print("  wrote %s (%d)" % (it.name, it.size))


if __name__ == '__main__':
    args = sys.argv[1:]
    out = None
    if args and args[0] == '-x':
        out = args[1]
        args = args[2:]
    for path in args:
        p = Pkg(path)
        info(p)
        if out:
            extract(p, out)
        print()
