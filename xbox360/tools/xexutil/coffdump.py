#!/usr/bin/env python3
"""Read Xenon COFF objects (machine 0x01F2), which LLVM refuses to open.

llvm-readobj rejects these outright, since there is no PowerPC COFF support anywhere in
LLVM, and lld-link has no PPC machine type. This reads the headers, sections, symbols
and relocations so the objects in the XDK static libraries can be inspected and,
eventually, translated to PPC ELF for ld.lld.
"""
import struct, sys

MACHINE_POWERPCBE = 0x01F2

# IMAGE_REL_PPC_*. Surveying libcMT.lib, real XDK objects use only ADDR32, REL24,
# REFHI/REFLO with their PAIR entries, and SECREL/SECTION which appear in debug sections.
RELOC = {
    0x0000: 'ABSOLUTE',
    0x0001: 'ADDR64',
    0x0002: 'ADDR32',
    0x0003: 'ADDR24',
    0x0004: 'ADDR16',
    0x0005: 'ADDR14',
    0x0006: 'REL24',
    0x0007: 'REL14',
    0x000A: 'ADDR32NB',
    0x000B: 'SECREL',
    0x000C: 'SECTION',
    0x000F: 'SECREL16',
    0x0010: 'REFHI',
    0x0011: 'REFLO',
    0x0012: 'PAIR',
    0x0013: 'SECRELLO',
    0x0015: 'GPREL',
    0x0016: 'TOKEN',
}

STORAGE = {2: 'external', 3: 'static', 6: 'label', 103: 'file', 105: 'section'}


class Coff:
    def __init__(self, data):
        self.d = data
        (self.machine, self.nsections, self.timestamp, self.symptr,
         self.nsyms, self.opthdr, self.characteristics) = struct.unpack_from('<HHIIIHH', data, 0)
        if self.machine != MACHINE_POWERPCBE:
            raise ValueError(f"machine 0x{self.machine:04x}, expected 0x01F2")
        self.strtab = self.symptr + self.nsyms * 18
        self.sections = self._sections()
        self.symbols = self._symbols()

    def _name(self, raw):
        if raw[:4] == b'\0\0\0\0':
            off = struct.unpack_from('<I', raw, 4)[0]
            end = self.d.index(b'\0', self.strtab + off)
            return self.d[self.strtab + off:end].decode('latin1')
        return raw.rstrip(b'\0').decode('latin1')

    def _sections(self):
        out = []
        base = 20 + self.opthdr
        for i in range(self.nsections):
            f = struct.unpack_from('<8sIIIIIIHHI', self.d, base + i * 40)
            out.append({
                'name': self._name(f[0]), 'vsize': f[1], 'vaddr': f[2],
                'rawsize': f[3], 'rawptr': f[4], 'relptr': f[5],
                'nreloc': f[7], 'flags': f[9],
            })
        return out

    def _symbols(self):
        out = []
        i = 0
        while i < self.nsyms:
            rec = self.d[self.symptr + i * 18: self.symptr + i * 18 + 18]
            value, secnum, typ, cls, naux = struct.unpack_from('<IhHBB', rec, 8)
            # Auxiliary records are 18-byte slots in the symbol table, not symbols. Their
            # layout depends on the storage class of the symbol they follow, so keep the
            # bytes and let the caller decode the flavour it cares about.
            aux = self.d[self.symptr + (i + 1) * 18: self.symptr + (i + 1 + naux) * 18]
            out.append({'name': self._name(rec[:8]), 'value': value,
                        'section': secnum, 'class': cls, 'index': i,
                        'naux': naux, 'aux': aux})
            i += 1 + naux
        return out

    def relocations(self, sec):
        out = []
        for r in range(sec['nreloc']):
            va, symidx, rtype = struct.unpack_from('<IIH', self.d, sec['relptr'] + r * 10)
            sym = next((s['name'] for s in self.symbols if s['index'] == symidx), f'#{symidx}')
            out.append((va, sym, rtype))
        return out

    def section_data(self, sec):
        return self.d[sec['rawptr']:sec['rawptr'] + sec['rawsize']]


def main(path):
    c = Coff(open(path, 'rb').read())
    print(f"machine=0x{c.machine:04x} sections={c.nsections} symbols={c.nsyms}")
    for s in c.sections:
        print(f"  {s['name']:10s} rawsize={s['rawsize']:6d} relocs={s['nreloc']:4d} "
              f"flags=0x{s['flags']:08x}")
    print("symbols:")
    for s in c.symbols:
        if s['class'] in (2, 3) and not s['name'].startswith('.'):
            print(f"  {s['name']:32s} value=0x{s['value']:04x} sec={s['section']} "
                  f"{STORAGE.get(s['class'], s['class'])}")
    for s in c.sections:
        if s['nreloc']:
            print(f"relocations in {s['name']}:")
            for va, sym, rt in c.relocations(s)[:24]:
                print(f"  0x{va:04x}  {RELOC.get(rt, f'0x{rt:04x}'):10s} {sym}")


if __name__ == '__main__':
    for p in sys.argv[1:]:
        main(p)
