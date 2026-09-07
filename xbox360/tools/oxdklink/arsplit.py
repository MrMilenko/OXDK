#!/usr/bin/env python3
"""Split an MS COFF archive into its members, without llvm-ar.

llvm-ar is unreliable on these archives: XDK member names are long Windows paths
containing backslashes, stored indirectly through the // longnames member, and extraction
silently drops some of them. Parsing the archive directly is both more reliable and lets
us tell the three kinds of member apart:

  regular COFF objects (machine 0x01F2): translate these
  short import records (00 00 FF FF)    these are XEX imports, not code
  linker members ("/" and "//")         the symbol index and the longname table
"""
import os, struct, sys

IMPORT_MAGIC = b'\x00\x00\xff\xff'


def members(path):
    """Yield (name, kind, data) for every member. kind is coff, import or linker."""
    d = open(path, 'rb').read()
    if d[:8] != b'!<arch>\n':
        raise ValueError(f"{path}: not an archive")
    longnames = b''
    off = 8
    idx = 0
    while off + 60 <= len(d):
        hdr = d[off:off + 60]
        raw = hdr[:16].decode('latin1').rstrip()
        try:
            size = int(hdr[48:58].decode().strip())
        except ValueError:
            break
        body = d[off + 60:off + 60 + size]
        off += 60 + size + (size & 1)

        if raw == '//':
            longnames = body
            yield raw, 'linker', body
            continue
        if raw == '/':
            yield raw, 'linker', body
            continue
        name = raw.rstrip('/')
        if name.startswith('/') and name[1:].isdigit() and longnames:
            start = int(name[1:])
            end = longnames.find(b'\0', start)
            name = longnames[start:end].decode('latin1')

        if len(body) >= 20 and body[:4] == IMPORT_MAGIC:
            yield name, 'import', body
        else:
            yield name, 'coff', body
        idx += 1


def import_record(body):
    """Decode a short import member into (name, ordinal, type, dll)."""
    ordinal = struct.unpack_from('<H', body, 16)[0]
    typ = struct.unpack_from('<H', body, 18)[0] & 3
    parts = body[20:].split(b'\0')
    return (parts[0].decode('latin1'), ordinal, typ,
            parts[1].decode('latin1') if len(parts) > 1 else '')


def weak_aliases(body):
    """Alias pairs from a librarian alias member.

    The librarian writes tiny machine 0x0000 members that carry no code, only a weak
    external naming the symbol it falls back to: vsnprintf -> _vsnprintf. coff2elf
    declines them because they are not Xenon COFF, so the alias would otherwise be lost
    and anything asking for the C99 spelling would link against nothing."""
    out = []
    if len(body) < 20:
        return out
    machine, nsec, ts, symptr, nsyms = struct.unpack_from('<HHIII', body, 0)
    if machine != 0 or not nsyms or symptr + nsyms * 18 > len(body):
        return out
    strtab = symptr + nsyms * 18

    def name_of(rec):
        if rec[0:4] == b'\0\0\0\0':
            off = struct.unpack_from('<I', rec, 4)[0]
            end = body.find(b'\0', strtab + off)
            return body[strtab + off:end].decode('latin1')
        return rec[0:8].split(b'\0')[0].decode('latin1')

    i = 0
    while i < nsyms:
        rec = body[symptr + i * 18: symptr + i * 18 + 18]
        if len(rec) < 18:
            break
        _val, _sec, _typ, storage, naux = struct.unpack_from('<IhHBB', rec, 8)
        if storage == 105 and naux:
            aux = body[symptr + (i + 1) * 18: symptr + (i + 1) * 18 + 18]
            if len(aux) >= 4:
                tag = struct.unpack_from('<I', aux, 0)[0]
                if tag < nsyms:
                    target = body[symptr + tag * 18: symptr + tag * 18 + 18]
                    out.append((name_of(rec), name_of(target)))
        i += 1 + naux
    return out


def split(path, outdir):
    """Write every COFF member to outdir; return paths, imports and alias pairs."""
    os.makedirs(outdir, exist_ok=True)
    objs, imports, aliases = [], [], []
    for i, (name, kind, body) in enumerate(members(path)):
        if kind == 'linker':
            continue
        if kind == 'import':
            imports.append(import_record(body))
            continue
        aliases += weak_aliases(body)
        base = os.path.basename(name.replace('\\', '/')) or f'member{i}'
        out = os.path.join(outdir, f'{i:05d}_{base}')
        with open(out, 'wb') as f:
            f.write(body)
        objs.append(out)
    return objs, imports, aliases


if __name__ == '__main__':
    objs, imports = split(sys.argv[1], sys.argv[2])
    print(f"{len(objs)} COFF objects, {len(imports)} import records")
    dlls = {}
    for _, _, _, dll in imports:
        dlls[dll] = dlls.get(dll, 0) + 1
    for dll, n in dlls.items():
        print(f"   {n} imports from {dll}")
