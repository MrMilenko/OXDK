#!/usr/bin/env python3
"""Emit a cxex manifest from an existing XEX, for round-tripping against a known-good file."""
import struct, sys

def u32(d, o): return struct.unpack_from('>I', d, o)[0]
def u16(d, o): return struct.unpack_from('>H', d, o)[0]


def main(xex_path, image_path):
    d = open(xex_path, 'rb').read()
    img = open(image_path, 'rb').read()
    count = u32(d, 20)
    keys = {u32(d, 24 + i*8): u32(d, 24 + i*8 + 4) for i in range(count)}

    base  = keys.get(0x00010201, 0x82000000)
    entry = keys.get(0x00010100, 0)
    stack = keys.get(0x00020200, 0x40000)
    sysfl = keys.get(0x00030000, 1)

    title = 0
    if 0x00040006 in keys:
        title = u32(d, keys[0x00040006] + 12)

    pename = "default.exe"
    if 0x000183FF in keys:
        o = keys[0x000183FF]
        sz = u32(d, o)
        pename = d[o+4:o+sz].split(b'\0')[0].decode('latin1') or pename

    print(f"# generated from {xex_path}")
    print(f"base 0x{base:08x}")
    print(f"entry 0x{entry:08x}")
    print(f"stack 0x{stack:x}")
    print(f"titleid 0x{title:08x}")
    print(f"sysflags 0x{sysfl:08x}")
    print(f"pename {pename}")

    if 0x000103FF not in keys:
        return
    o = keys[0x000103FF]
    st_size = u32(d, o + 4)
    nlib = u32(d, o + 8)
    names = [s.decode('latin1') for s in d[o+12:o+12+st_size].split(b'\0') if s]
    p = o + 12 + st_size
    for i in range(nlib):
        lib_size = u32(d, p)
        lib_id = u32(d, p + 24)
        ver = u32(d, p + 28)
        vmin = u32(d, p + 32)
        nidx = u16(d, p + 36)
        cnt = u16(d, p + 38)
        recs = [u32(d, p + 40 + j*4) for j in range(cnt)]
        fmtv = lambda v: f"{v>>28}.{(v>>24)&0xF}.{(v>>8)&0xFFFF}.{v&0xFF}"
        print(f"\nlibrary {names[nidx]} {fmtv(ver)} {fmtv(vmin)} 0x{lib_id:08x}")
        # A record is a slot; if the next record is not a slot (not 4 past this one and
        # not itself a slot address) it is this import's thunk.
        j = 0
        while j < len(recs):
            slot = recs[j]
            ordinal = struct.unpack_from('>I', img, slot - base)[0] & 0xFFFF
            thunk = 0
            if j + 1 < len(recs):
                nxt = recs[j+1]
                # slots live in a dense 4-byte-strided table; a thunk is far away
                if nxt != slot + 4:
                    thunk = nxt
            if thunk:
                print(f"import {ordinal} {slot:x} {thunk:x}")
                j += 2
            else:
                print(f"import {ordinal} {slot:x}")
                j += 1
        p += lib_size


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
