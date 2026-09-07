#!/usr/bin/env python3
"""Check a XEX against every loader constraint we know about, without a console.

Every rule here was either read out of the kernel's own loader or established by a
controlled experiment on hardware; see docs/XEX2.md. Pre-screening a build with this is
much cheaper than a boot.
"""
import struct, hashlib, sys

def u32(d, o): return struct.unpack_from('>I', d, o)[0]
def u16(d, o): return struct.unpack_from('>H', d, o)[0]


def check(path, verbose=True):
    d = open(path, 'rb').read()
    fails = []
    def rule(name, ok, detail=''):
        if verbose:
            print(f"  {'ok  ' if ok else 'FAIL'} {name}{'  ' + detail if detail else ''}")
        if not ok:
            fails.append(name)

    rule("magic is XEX2", d[:4] == b'XEX2')
    if d[:4] != b'XEX2':
        return fails

    pd, sec, n = u32(d, 8), u32(d, 0x10), u32(d, 0x14)

    # XexpLoadXexHeaders, before it hashes anything
    rule("pe_data_offset >= 0x800", pd >= 0x800, f"0x{pd:x}")
    rule("pe_data_offset <= 0x10000", pd <= 0x10000, f"0x{pd:x}")
    rule("pe_data_offset is 0x800 aligned", (pd & 0x7FF) == 0, f"0x{pd:x}")
    rule("reserved word at 0x0C is zero", u32(d, 0x0C) == 0)
    rule("file is at least pe_data_offset", len(d) >= pd)

    # optional header directory
    keys = [u32(d, 0x18 + i * 8) for i in range(n)]
    rule("optional headers sorted ascending", keys == sorted(keys))
    rule("security info inside header region", sec + 0x184 <= pd, f"sec=0x{sec:x}")

    # every out-of-line blob must sit inside the header region
    inside = True
    for i in range(n):
        k, v = u32(d, 0x18 + i * 8), u32(d, 0x18 + i * 8 + 4)
        szc = k & 0xFF
        if szc in (0, 1):
            continue
        ln = u32(d, v) if szc == 0xFF else szc * 4
        if not (0 < v and v + ln <= pd):
            inside = False
    rule("all header blobs lie within the header region", inside)

    # security info self-consistency
    npages = u32(d, sec + 0x180)
    rule("header_size == 0x184 + pages*24",
         u32(d, sec) == 0x184 + npages * 24,
         f"0x{u32(d, sec):x} vs 0x{0x184 + npages*24:x}")
    rule("image_info_size == 0x174", u32(d, sec + 0x108) == 0x174,
         f"0x{u32(d, sec + 0x108):x}")

    base = u32(d, sec + 0x110)
    page_size = 0x10000 if base < 0x90000000 else 0x1000
    total = sum(u32(d, sec + 0x184 + i * 24) >> 4 for i in range(npages))
    rule("page descriptors cover image_size",
         total * page_size == u32(d, sec + 4),
         f"{total} pages x 0x{page_size:x} = 0x{total*page_size:x}, image_size=0x{u32(d, sec+4):x}")
    bad_info = [i for i in range(npages) if (u32(d, sec + 0x184 + i * 24) & 0xF) == 0]
    rule("no page descriptor has info 0", not bad_info)

    # No descriptor may cross a 64K boundary. Every XEX Microsoft ships obeys this: over
    # 59 real files, re-encoding the protections as "run-length encode, break at every 64K
    # granule" reproduces the shipped descriptor list exactly, and 5247 merges of adjacent
    # same-protection pages are declined to keep it so. A 64K image with a merged run is
    # refused by the loader silently.
    crossing, rva = [], 0
    for i in range(npages):
        span = (u32(d, sec + 0x184 + i * 24) >> 4) * page_size
        if rva // 0x10000 != (rva + span - 1) // 0x10000:
            crossing.append((i, rva, span))
        rva += span
    rule("no page descriptor crosses a 64K boundary", not crossing,
         '' if not crossing else
         f"pd[{crossing[0][0]}] rva 0x{crossing[0][1]:x} spans 0x{crossing[0][2]:x}")

    # the check that actually rejects hand-built files
    stored = d[sec + 0x164:sec + 0x164 + 20]
    calc = hashlib.sha1(d[sec + 0x17C:pd] + d[0:sec + 8]).digest()
    rule("header_digest seals the header region", stored == calc,
         '' if stored == calc else f"{stored.hex()[:16]} != {calc.hex()[:16]}")

    # import digest chain
    imp = None
    for i in range(n):
        if u32(d, 0x18 + i * 8) == 0x000103FF:
            imp = u32(d, 0x18 + i * 8 + 4)
    if imp:
        size, st_sz, nlib = u32(d, imp), u32(d, imp + 4), u32(d, imp + 8)
        rule("import table ends flush at pe_data_offset", imp + size == pd,
             f"0x{imp:x}+{size}=0x{imp+size:x} vs 0x{pd:x}")
        p = imp + 12 + st_sz
        recs = []
        for _ in range(nlib):
            rs = u32(d, p)
            recs.append((p, rs))
            p += rs
        prev = b'\0' * 20
        ok = True
        for off, rs in reversed(recs):
            if d[off + 4:off + 24] != prev:
                ok = False
            prev = hashlib.sha1(d[off + 4:off + rs]).digest()
        rule("next_import_digest chain is valid", ok)

    return fails


if __name__ == '__main__':
    bad = 0
    for p in sys.argv[1:]:
        print(f"=== {p}")
        f = check(p)
        print(f"  {'PASS' if not f else 'FAILED: ' + ', '.join(f)}")
        bad += bool(f)
    sys.exit(1 if bad else 0)
