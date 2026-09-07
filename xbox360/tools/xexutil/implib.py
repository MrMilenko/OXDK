#!/usr/bin/env python3
"""Read a Xenon import library (xboxkrnl.lib, xam.lib) into name/ordinal/type records.

The XDK import libraries are MS archives whose members are IMPORT_OBJECT_HEADER
records rather than real COFF objects. They are the authoritative source for both
the ordinal and the code/data type of every import, which is exactly what building
a XEX import table needs.

Import types: 0 = code, 1 = data, 2 = const. In practice xboxkrnl.lib uses 0 for
functions and 2 for exported variables; type 2 imports get a pointer slot and no thunk.
"""
import struct, sys

IMPORT_OBJECT_MAGIC = b'\x00\x00\xff\xff'
TYPE_NAME = {0: 'code', 1: 'data', 2: 'const'}


def read(path):
    """Return {ordinal: (name, type, dll)} for every import record in the archive."""
    d = open(path, 'rb').read()
    if d[:8] != b'!<arch>\n':
        raise ValueError(f"{path}: not an archive")
    out = {}
    off = 8
    while off + 60 <= len(d):
        hdr = d[off:off + 60]
        try:
            size = int(hdr[48:58].decode().strip())
        except ValueError:
            break
        body = d[off + 60:off + 60 + size]
        if len(body) >= 20 and body[:4] == IMPORT_OBJECT_MAGIC:
            machine = struct.unpack_from('<H', body, 6)[0]
            ordinal = struct.unpack_from('<H', body, 16)[0]
            typ = struct.unpack_from('<H', body, 18)[0] & 3
            parts = body[20:].split(b'\0')
            name = parts[0].decode('latin1')
            dll = parts[1].decode('latin1') if len(parts) > 1 else ''
            if machine != 0x01F2:
                raise ValueError(f"{path}: unexpected machine 0x{machine:04x}")
            out[ordinal] = (name, typ, dll)
        off += 60 + size + (size & 1)
    return out


def main(argv):
    if not argv:
        print(__doc__)
        print("usage: implib.py <lib> [name-or-ordinal ...]")
        return 1
    imports = read(argv[0])
    if len(argv) == 1:
        print(f"{len(imports)} import records")
        for o in sorted(imports):
            name, typ, _ = imports[o]
            print(f"  {o:5d}  {TYPE_NAME.get(typ, typ):5s}  {name}")
        return 0
    by_name = {v[0]: (k, v[1]) for k, v in imports.items()}
    for q in argv[1:]:
        if q in by_name:
            o, t = by_name[q]
            print(f"  {q} -> ordinal {o} ({TYPE_NAME.get(t, t)})")
        else:
            try:
                o = int(q, 0)
            except ValueError:
                print(f"  {q} -> not found")
                continue
            name, typ, _ = imports.get(o, ('<none>', -1, ''))
            print(f"  ordinal {o} -> {name} ({TYPE_NAME.get(typ, typ)})")
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
