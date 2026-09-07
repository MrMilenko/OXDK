#!/usr/bin/env python3
"""Unpack an unencrypted, basic-compressed XEX2 into its raw PE image."""
import struct, sys

def u32(d, o): return struct.unpack_from('>I', d, o)[0]
def u16(d, o): return struct.unpack_from('>H', d, o)[0]

def unpack(path, out):
    d = open(path, 'rb').read()
    assert d[:4] == b'XEX2', "not XEX2"
    pe_off = u32(d, 8)
    count  = u32(d, 20)
    fmt = None
    for i in range(count):
        if u32(d, 24 + i*8) == 0x000003FF:
            fmt = u32(d, 24 + i*8 + 4)
    assert fmt is not None, "no FILE_FORMAT_INFO"
    info_size = u32(d, fmt)
    enc, comp = u16(d, fmt + 4), u16(d, fmt + 6)
    assert enc == 0, f"encrypted (enc={enc}), cannot unpack"
    assert comp == 1, f"compression={comp}, only basic(1) supported"

    nblocks = (info_size - 8) // 8
    img = bytearray()
    src = pe_off
    print(f"  basic compression, {nblocks} blocks")
    for b in range(nblocks):
        data_size = u32(d, fmt + 8 + b*8)
        zero_size = u32(d, fmt + 8 + b*8 + 4)
        img += d[src:src + data_size]
        img += b'\0' * zero_size
        src += data_size
        if b < 6:
            print(f"    block[{b}] data={data_size:#x} zero={zero_size:#x}")
    open(out, 'wb').write(img)
    print(f"  -> {out}: {len(img)} bytes, starts {img[:2]!r}")
    return bytes(img)

def dump_pe(img):
    if img[:2] != b'MZ':
        print("  (no MZ header)"); return
    pe = struct.unpack_from('<I', img, 0x3c)[0]
    assert img[pe:pe+4] == b'PE\0\0', "no PE signature"
    mach, nsec = struct.unpack_from('<HH', img, pe + 4)
    ohsz = struct.unpack_from('<H', img, pe + 20)[0]
    print(f"  PE: machine=0x{mach:04x} sections={nsec} opt_hdr={ohsz}")
    magic = struct.unpack_from('<H', img, pe + 24)[0]
    entry = struct.unpack_from('<I', img, pe + 24 + 16)[0]
    base  = struct.unpack_from('<I', img, pe + 24 + 28)[0]
    salign= struct.unpack_from('<I', img, pe + 24 + 32)[0]
    falign= struct.unpack_from('<I', img, pe + 24 + 36)[0]
    print(f"      opt_magic=0x{magic:04x} entry_rva=0x{entry:x} image_base=0x{base:08x}")
    print(f"      section_align=0x{salign:x} file_align=0x{falign:x}")
    so = pe + 24 + ohsz
    for i in range(nsec):
        n, vs, va, rs, ro = struct.unpack_from('<8sIIII', img, so + i*40)
        ch = struct.unpack_from('<I', img, so + i*40 + 36)[0]
        print(f"      [{i}] {n.rstrip(chr(0).encode()).decode():9s} "
              f"vsize={vs:#08x} rva={va:#08x} rawsize={rs:#08x} "
              f"rawptr={ro:#08x} chars={ch:#010x}")

if __name__ == '__main__':
    img = unpack(sys.argv[1], sys.argv[2])
    dump_pe(img)
