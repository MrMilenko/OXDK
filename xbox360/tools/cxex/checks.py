#!/usr/bin/env python3
"""Compare a cxex-built XEX against the Microsoft original it was rebuilt from."""
import struct, hashlib, sys


def imports(path):
    d = open(path, 'rb').read()
    n = struct.unpack_from('>I', d, 20)[0]
    for i in range(n):
        k, v = struct.unpack_from('>II', d, 24 + i * 8)
        if k == 0x000103FF:
            return d[v:v + struct.unpack_from('>I', d, v)[0]]
    raise SystemExit("FAIL  no import table")


def main(ref, out):
    a, b = imports(ref), imports(out)
    if a != b:
        bad = [i for i, (x, y) in enumerate(zip(a, b)) if x != y]
        print(f"FAIL  import table differs at {bad[:8]} ({len(a)} vs {len(b)} bytes)")
        return 1
    print("ok    import table byte-identical, next_import_digest chain included")

    d = open(out, 'rb').read()
    pd = struct.unpack_from('>I', d, 8)[0]
    so = struct.unpack_from('>I', d, 16)[0]
    stored = d[so + 0x164:so + 0x164 + 20]
    calc = hashlib.sha1(d[so + 0x17C:pd] + d[0:so + 8]).digest()
    if stored != calc:
        print(f"FAIL  header_digest {stored.hex()} != {calc.hex()}")
        return 1
    print("ok    header_digest seals the header region")

    for name, ok in (("pe_data_offset >= 0x800", pd >= 0x800),
                     ("pe_data_offset <= 0x10000", pd <= 0x10000),
                     ("pe_data_offset is 0x800 aligned", (pd & 0x7FF) == 0),
                     ("reserved word is zero", struct.unpack_from('>I', d, 12)[0] == 0)):
        if not ok:
            print(f"FAIL  {name}")
            return 1
    print("ok    loader structural constraints")
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2]))
