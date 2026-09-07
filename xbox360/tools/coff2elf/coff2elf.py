#!/usr/bin/env python3
"""Translate a Xenon COFF object (machine 0x01F2) into a PowerPC big-endian ELF32
relocatable object that ld.lld can link.

LLVM has no PowerPC COFF support at all: llvm-readobj refuses to open machine 0x01F2 and
lld-link has no PPC machine type, so the XDK's static libraries cannot be handed to any
LLVM tool as they stand. This rewrites one object into the ELF flavour ld.lld already
understands. Section contents are copied byte for byte apart from the relocation fields,
which are cleared because the addends move into RELA entries.

Relocations
-----------
Surveying the XDK's xbox libraries, only four types occur outside the debug sections:
REL24, ADDR32, REFHI and REFLO, each REFHI and REFLO followed by a PAIR entry. SECREL and
SECTION appear only in .debug$S/.debug$T, which are dropped. Exact totals depend on how
the archive is extracted. llvm-ar overwrites members that share a basename, so a plain
extraction undercounts, which is also why the handful of ADDR32NB relocations reported
in the .idata import descriptors could not be reproduced by an independent census.

    ADDR32  -> R_PPC_ADDR32     ADDR16_HA <- REFHI
    REL24   -> R_PPC_REL24      ADDR16_LO <- REFLO

Addend convention: COFF stores the addend inline in the field being relocated, PowerPC ELF
uses RELA. The addend is decoded, moved into r_addend, and the inline field is zeroed, so
the same object cannot be translated twice and the ELF is the single source of truth.
Per type:

  ADDR32  the field is the addend directly. Confirmed on chsize.obj, whose two .pdata
          entries for _chsize_s (section offset 0x8) hold 0 and 0x14c and must land on
          0x8 and 0x154; 0x154 is exactly where the object's own $LN15 label sits, so the
          field is additive against the symbol, not a replacement for it.

  REL24   the field holds the displacement the assembler computed with the target assumed
          to live at address A, that is field = A - offset. So A = displacement + offset.
          Every REL24 in the XDK decodes to A = 0, which is what a compiler emitting plain
          calls should produce.

  REFLO   the low 16 bits of the instruction are the addend, sign extended.

  REFHI   documented rule is that the instruction's immediate holds the high half of the
          addend and the following PAIR entry's SymbolTableIndex holds a signed 16-bit low
          half, giving A = (immediate << 16) + sext16(pair). Every REFHI immediate and
          every one of the 801919 PAIR displacements in the XDK is zero, so this
          combination is never exercised there and the data cannot confirm it. The rule is
          implemented as documented and a warning is printed if a nonzero one is ever seen.

r_offset for the two 16-bit forms points at the immediate field, two bytes into the
big-endian instruction, which is where PowerPC ELF expects ADDR16_LO and ADDR16_HA. The
REL24 and ADDR32 offsets are the start of the word.

COMDAT
------
MSVC puts every inline function, every template instantiation and every floating point
constant in its own COMDAT section, so deduplication is not an optional refinement: without
it ceil.obj and floor.obj cannot be linked together, because both define
__real@3ff0000000000000. A COMDAT section carries IMAGE_SCN_LNK_COMDAT and its section
symbol's auxiliary record holds a Selection byte saying how duplicates are resolved. Each
one becomes an ELF section group (SHT_GROUP with GRP_COMDAT), which lld deduplicates by the
name of the group's signature symbol, keeping the first and discarding the rest.

The COMDAT key is the leader: the first symbol defined in the section other than the
section's own symbol. It is not simply the next symbol in the table, because MSVC emits the
section symbols of the ASSOCIATIVE .debug$S and .pdata pieces in between. The leader
becomes the group's signature symbol.

  ANY, SAME_SIZE, EXACT_MATCH  a plain group. ELF has no size or content check, so the
                               first copy wins whether or not the others match it. Only ANY
                               occurs in the XDK; SAME_SIZE and EXACT_MATCH never do.

  NODUPLICATES                 a plain group as well. There is no ELF equivalent: the point
                               of NODUPLICATES is to reject a second definition, and a
                               group silently discards it instead. This is the one selection
                               whose meaning is genuinely lost. It is also a common one,
                               119926 of the 1002820 COMDAT sections that survive the drop
                               pass across the 132 xbox libraries, since every /Gy function
                               COMDAT MSVC emits from a .c file uses it, and because each
                               such name really is defined once, discarding rather than
                               rejecting never comes up in practice.

  ASSOCIATIVE                  no group of its own: the section joins the group of the
                               section its aux record's Number field names, so .pdata and
                               .xdata live or die with the .text they describe. Chains are
                               followed, though the XDK never nests them.

  LARGEST                      a plain group, so the first copy wins rather than the biggest.
                               327 sections in nuispeech.lib and nuispeechd.lib use it and
                               nothing else in the XDK does; a warning is printed.

A COMDAT whose leader is a static symbol is left ungrouped and always kept, which is what
lld-link does with an internal leader as well. It also matters for correctness here: 283
relocations in libcMT.lib alone point from one COMDAT at a static symbol defined in a
different one, and ELF has no way to redirect those to a prevailing copy the way COFF does
-- lld would reject the reference into the discarded section. Grouping only the external
leaders leaves zero such references across all 132 xbox libraries.

Not handled
-----------
ADDR32NB is image-relative and has no ELF32 PPC equivalent; it only appears in the .idata
import descriptors, which OXDK360 builds itself.

A COFF weak external carries a default symbol to fall back on. ELF has no equivalent, so
it becomes a plain weak undefined symbol and the fallback is lost.

A REFLO on a DS-form instruction (ld, lwa, std) keeps the two low bits of the field as
opcode extension, but R_PPC_ADDR16_LO overwrites all sixteen bits and ELF32 PPC has no
ADDR16_LO_DS. Plain ld and std survive because their extension is zero and the address is
word aligned; lwa does not. Sixteen such relocations exist in the XDK, all in d3dx9 and
vcomp, and they are an error unless --allow-ds-reflo is given.

.tls is emitted as an ordinary allocated data section, not SHF_TLS: on Xbox 360 it is a
template copied by the loader through the TLS directory, not an ELF TLS segment.

MSVC decorated names contain @, which is the ELF symbol version separator. Resolution is
unaffected, checked by linking two objects that define __real@3fe0000000000000 and
__real@3ff0000000000000 and confirming the references land on different addresses, but
ld.lld truncates the name at the @ in the output symbol table, so both read as __real
in nm and objdump listings.

The 2 members of libcMT.lib and the short import members of the xapi libraries are
machine 0x0000 alias objects written by the librarian, not Xenon COFF, and the parser
declines them.

Limitations
-----------
These are real and they fail loudly rather than silently, but they bound what the
translator is usable for today.

  NODUPLICATES COMDATs are deduplicated rather than rejected, and LARGEST COMDATs keep the
  first copy rather than the largest. See COMDAT above.

  Seven objects fail hard on a DS-form REFLO: dsyevx.obj and misclapack.obj in the three
  d3dx9 variants, and loop.obj in vcomp. The relocation targets an lwa instruction whose
  low two bits are opcode, and ELF32 PowerPC has no ADDR16_LO_DS to express it.
  --allow-ds-reflo proceeds anyway and corrupts the instruction; it exists for diagnosis.

  Of 9352 members across 132 xbox libraries, 8921 are machine 0x01F2 and 8914 translate.
  The remaining 431 are machine 0x0000 librarian alias records, concentrated in
  oldnames.lib and the *ltcg libraries, and are declined rather than translated.

  Weak externals keep their weak binding and now bind to the fallback their aux record
  names, which is how ??_E scalar deleting destructors reach ??_G. Losing that fallback
  put a null in the first slot of 186 of the 299 vtables in a D3DQuakeX360 link, and the
  link reported success, because a weak undefined symbol resolves to zero rather than
  erroring.

  The machine 0x0000 alias records above are still declined. Two of them, vsnprintf and
  vsnprintf_s in libcMT, are weak-external aliases of _vsnprintf and _vsnprintf_s, so
  anything asking for the C99 spelling links against nothing. Declining them is only safe
  while nothing does; mkarchive now says so on every build rather than dropping them
  quietly.
"""
import argparse
import collections
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir, 'xexutil'))
from coffdump import Coff, RELOC

# IMAGE_SCN_*
SCN_UNINIT = 0x00000080
SCN_LNK_INFO = 0x00000200
SCN_LNK_REMOVE = 0x00000800
SCN_LNK_COMDAT = 0x00001000
SCN_NRELOC_OVFL = 0x01000000
SCN_MEM_EXECUTE = 0x20000000
SCN_MEM_WRITE = 0x80000000

# IMAGE_COMDAT_SELECT_*
SEL_NODUPLICATES = 1
SEL_ASSOCIATIVE, SEL_LARGEST = 5, 6
SEL_NAMES = {1: 'NODUPLICATES', 2: 'ANY', 3: 'SAME_SIZE',
             4: 'EXACT_MATCH', 5: 'ASSOCIATIVE', 6: 'LARGEST'}

# IMAGE_REL_PPC_*
COFF_ADDR32, COFF_REL24, COFF_ADDR32NB = 0x02, 0x06, 0x0A
COFF_REFHI, COFF_REFLO, COFF_PAIR = 0x10, 0x11, 0x12

# IMAGE_SYM_CLASS_*
CLS_EXTERNAL, CLS_STATIC, CLS_LABEL, CLS_WEAK_EXTERNAL = 2, 3, 6, 105

ET_REL, EM_PPC = 1, 20
SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, SHT_RELA, SHT_NOBITS = 1, 2, 3, 4, 8
SHT_GROUP = 17
SHF_WRITE, SHF_ALLOC, SHF_EXECINSTR, SHF_INFO_LINK = 0x1, 0x2, 0x4, 0x40
SHF_GROUP = 0x200
GRP_COMDAT = 1
STB_LOCAL, STB_GLOBAL, STB_WEAK = 0, 1, 2
STT_NOTYPE, STT_OBJECT, STT_FUNC, STT_SECTION = 0, 1, 2, 3
SHN_UNDEF, SHN_ABS, SHN_COMMON = 0, 0xFFF1, 0xFFF2

R_PPC_ADDR32, R_PPC_ADDR16_LO, R_PPC_ADDR16_HA, R_PPC_REL24 = 1, 4, 6, 10

LI_MASK = 0x03FFFFFC       # displacement field of an I-form branch
DS_FORM_OPCODES = (58, 62)  # ld/lwa/ldu and std/stdu keep two opcode bits in the field

# Sections that only ever speak to the Microsoft linker.
DROP_PREFIXES = ('.debug', '.XBLD')


class Report:
    """Counters and one-shot warnings, printed as a summary at the end."""

    def __init__(self):
        self.relocs = collections.Counter()
        self.selections = collections.Counter()
        self.messages = []
        self.dropped_sections = 0
        self.groups = 0
        self.ungrouped_comdats = 0

    def warn(self, msg):
        if msg not in self.messages:
            self.messages.append(msg)


class Strtab:
    def __init__(self):
        self.buf = bytearray(b'\0')
        self.offsets = {'': 0}

    def add(self, s):
        if s not in self.offsets:
            self.offsets[s] = len(self.buf)
            self.buf += s.encode('latin1') + b'\0'
        return self.offsets[s]



# ELF gives '@' a meaning MSVC does not: lld reads name@version, and name@@version as a
# default-version definition. Every MSVC C++ mangled name contains '@@', so passing them
# through unchanged makes lld merge distinct symbols into one. That is silent - within a
# single object it resolves references to the wrong function with no diagnostic, and
# across objects it turns into a spurious duplicate-symbol error.
#
# Measured on the XDK: 2233 defined externals across 441 objects collide this way, and in
# undname.obj alone 215 relocations resolved to the wrong address. There is no lld option
# to turn version parsing off, so escape the character instead. '#' does not occur in
# MSVC decorated names, and because definitions and references are escaped identically
# the mapping is invisible to resolution. It does show up in nm and objdump output.
ESCAPE_CHAR = '#'


def elf_symbol_name(name):
    """Escape characters ELF assigns a meaning to but MSVC uses as mangling."""
    return name.replace('@', ESCAPE_CHAR)


def coff_symbol_name(name):
    """Inverse of elf_symbol_name, for tools that want to display the original."""
    return name.replace(ESCAPE_CHAR, '@')

def section_name(coff, sec):
    """Long section names are spelled "/<decimal offset>" into the COFF string table."""
    name = sec['name']
    if name.startswith('/') and name[1:].isdigit():
        off = coff.strtab + int(name[1:])
        return coff.d[off:coff.d.index(b'\0', off)].decode('latin1')
    return name


def section_align(sec):
    nibble = (sec['flags'] >> 20) & 0xF
    return 1 << (nibble - 1) if nibble else 4


def keep_section(coff, sec):
    if sec['flags'] & (SCN_LNK_INFO | SCN_LNK_REMOVE):
        return False           # .drectve and friends: directives, never mapped
    return not section_name(coff, sec).startswith(DROP_PREFIXES)


def section_definitions(coff):
    """Return (defs, section_symbols) decoded from the section symbols' aux records.

    A section symbol is a STATIC symbol whose name is the section's own name and which
    carries one auxiliary record, an IMAGE_AUX_SYMBOL_SECTION. Only its tail matters here:

        offset 12  Number      low 16 bits of the associated section, ASSOCIATIVE only
        offset 14  Selection   IMAGE_COMDAT_SELECT_*, 0 on an ordinary section
        offset 16  HighNumber  high 16 bits of Number, for objects with over 65535 sections

    defs maps a COFF section index to (selection, associated section), taken from the first
    section symbol naming that section; an object occasionally carries a second one, so the
    later records are ignored. section_symbols collects every symbol index that is
    to name its own section, because the COMDAT leader scan has to skip all of them.
    """
    defs, section_symbols = {}, set()
    for s in coff.symbols:
        i = s['section']
        if s['class'] != CLS_STATIC or len(s['aux']) < 18 or not 1 <= i <= len(coff.sections):
            continue
        if s['name'] != section_name(coff, coff.sections[i - 1]):
            continue
        section_symbols.add(s['index'])
        number, selection = struct.unpack_from('<HB', s['aux'], 12)
        high = struct.unpack_from('<H', s['aux'], 16)[0]
        defs.setdefault(i, (selection, number | (high << 16)))
    return defs, section_symbols


def comdat_groups(coff, elf_index, rep):
    """Work out the ELF section groups, as {root COFF section: (leader symbol, [members])}.

    Only sections that survived the drop pass appear, and only COMDATs whose leader is an
    external symbol: an internal leader cannot collide across objects, so lld-link keeps
    every copy, and so does this. See the COMDAT notes at the top for why that is also what
    keeps cross-COMDAT references to static symbols linkable."""
    defs, section_symbols = section_definitions(coff)

    def root_of(i):
        # An ASSOCIATIVE section is not a COMDAT in its own right, it lives or dies with
        # the section its aux record names. The XDK never nests them, but follow the chain
        # rather than assume that.
        seen = set()
        while defs.get(i, (0, 0))[0] == SEL_ASSOCIATIVE:
            seen.add(i)
            i = defs[i][1]
            if i in seen or not 1 <= i <= len(coff.sections):
                raise ValueError("ASSOCIATIVE section chain is circular or out of range")
        return i

    # The leader is the first symbol defined in the section that is not the section's own
    # symbol. Not simply the next symbol in the table: MSVC emits the section symbols of
    # the ASSOCIATIVE .debug$S and .pdata pieces before it.
    leaders = {}
    for s in coff.symbols:
        if s['section'] >= 1 and s['index'] not in section_symbols:
            leaders.setdefault(s['section'], s)

    groups = {}
    for i, sec in enumerate(coff.sections, 1):
        if not sec['flags'] & SCN_LNK_COMDAT or i not in elf_index:
            continue
        selection = defs.get(i, (0, 0))[0]
        rep.selections[selection] += 1
        if selection == SEL_LARGEST:
            rep.warn(f"{section_name(coff, sec)}: LARGEST COMDAT, an ELF group keeps the "
                     f"first copy rather than the biggest")

        root = root_of(i)
        if root not in elf_index:
            rep.warn(f"{section_name(coff, sec)}: ASSOCIATIVE with a dropped section, "
                     f"left ungrouped")
            rep.ungrouped_comdats += 1
            continue
        if root != i and not coff.sections[root - 1]['flags'] & SCN_LNK_COMDAT:
            rep.warn(f"{section_name(coff, sec)}: ASSOCIATIVE with a section that is not "
                     f"a COMDAT, left ungrouped")
            rep.ungrouped_comdats += 1
            continue
        leader = leaders.get(root)
        if leader is None or leader['class'] != CLS_EXTERNAL:
            rep.ungrouped_comdats += 1
            continue
        groups.setdefault(root, (leader, []))[1].append(i)
    return groups


def read_relocs(coff, sec):
    n, base = sec['nreloc'], sec['relptr']
    if sec['flags'] & SCN_NRELOC_OVFL and n == 0xFFFF:
        # The real count lives in the first entry's VirtualAddress and includes that entry.
        n = struct.unpack_from('<I', coff.d, base)[0] - 1
        base += 10
    return [struct.unpack_from('<IIH', coff.d, base + i * 10) for i in range(n)]


def sext16(v):
    return v - 0x10000 if v & 0x8000 else v


def translate_relocs(coff, sec, name, data, elf_sym, opts, rep):
    """Decode one section's COFF relocations into (r_offset, sym, type, addend) tuples,
    clearing the inline addend fields in data as it goes."""
    out = []
    entries = read_relocs(coff, sec)
    i = 0
    while i < len(entries):
        va, symidx, rtype = entries[i]
        i += 1
        # REFHI and REFLO are both followed by a PAIR entry whose SymbolTableIndex field
        # holds a displacement rather than a symbol.
        pair = None
        if rtype in (COFF_REFHI, COFF_REFLO) and i < len(entries) and entries[i][2] == COFF_PAIR:
            pair = entries[i][1]
            i += 1
        rep.relocs[rtype] += 1

        if rtype == COFF_PAIR:
            raise ValueError(f"{name}+0x{va:x}: PAIR without a preceding REFHI/REFLO")
        if rtype not in (COFF_ADDR32, COFF_REL24, COFF_REFHI, COFF_REFLO):
            what = RELOC.get(rtype, f'0x{rtype:04x}')
            if not opts.skip_unsupported:
                raise ValueError(f"{name}+0x{va:x}: {what} has no ELF32 PPC equivalent "
                                 f"(use --skip-unsupported to drop it)")
            rep.warn(f"dropped {what} relocation in {name}")
            continue
        if va + 4 > len(data):
            raise ValueError(f"{name}+0x{va:x}: relocation past end of section")

        word = struct.unpack_from('>I', data, va)[0]
        sym = elf_sym(symidx)

        if rtype == COFF_ADDR32:
            struct.pack_into('>I', data, va, 0)
            out.append((va, sym, R_PPC_ADDR32, word))
        elif rtype == COFF_REL24:
            if word & 0x2:
                raise ValueError(f"{name}+0x{va:x}: REL24 on an absolute branch")
            disp = word & LI_MASK
            if disp & 0x02000000:
                disp -= 0x04000000
            struct.pack_into('>I', data, va, word & ~LI_MASK)
            out.append((va, sym, R_PPC_REL24, disp + va))
        elif rtype == COFF_REFHI:
            if (word & 0xFFFF) or pair:
                rep.warn(f"{name}+0x{va:x}: nonzero REFHI addend (field 0x{word & 0xFFFF:04x}, "
                         f"pair 0x{pair or 0:04x}); the split is documented but unverified")
            addend = ((word & 0xFFFF) << 16) + sext16(pair or 0)
            struct.pack_into('>H', data, va + 2, 0)
            out.append((va + 2, sym, R_PPC_ADDR16_HA, addend))
        else:  # COFF_REFLO
            if pair:
                rep.warn(f"{name}+0x{va:x}: REFLO with a nonzero PAIR displacement 0x{pair:04x}")
            field = word & 0xFFFF
            if (word >> 26) in DS_FORM_OPCODES and field & 0x3:
                # On a DS-form instruction the bottom two bits are opcode, not
                # displacement. R_PPC_ADDR16_LO overwrites all sixteen bits, and the
                # address it writes is word aligned, so a nonzero extension is lost:
                # lwa turns into ld. Only opcode 0 survives, which is why the check is
                # on the two bits rather than on the instruction form.
                if not opts.allow_ds_reflo:
                    raise ValueError(f"{name}+0x{va:x}: REFLO on DS-form instruction "
                                     f"0x{word:08x}; R_PPC_ADDR16_LO would clobber its two "
                                     f"opcode bits (use --allow-ds-reflo to accept that)")
                rep.warn(f"{name}+0x{va:x}: DS-form REFLO translated approximately")
                field &= ~0x3
            struct.pack_into('>H', data, va + 2, 0)
            out.append((va + 2, sym, R_PPC_ADDR16_LO, sext16(field)))
    return out


def weak_fallback(coff, sym):
    """The symbol a COFF weak external falls back to.

    A weak external is not simply an undefined symbol. Its aux record names a second
    symbol in TagIndex, and Microsoft's linker binds the name to that fallback unless
    something else defines it strongly. MSVC uses this for the scalar deleting
    destructor ??_E, whose fallback is the vector deleting destructor ??_G, so the
    pair costs one function instead of two. Drop the fallback and the name resolves to
    zero, which lands as a null in the first slot of every vtable that has one."""
    if sym['naux'] < 1 or len(sym['aux']) < 8:
        return None
    tag = struct.unpack_from('<I', sym['aux'], 0)[0]
    named = next((t for t in coff.symbols if t['index'] == tag), None)
    if named is None:
        return None
    # TagIndex often points at an undefined entry for the fallback while the actual
    # definition sits in a later symbol of the same name, so resolve by name and take
    # the defined one if this object has it.
    if named['section'] > 0:
        return named
    return next((t for t in coff.symbols
                 if t['name'] == named['name'] and t['section'] > 0), named)


def build_symbols(coff, elf_index, exec_sections):

    """Return (locals, globals, coff index -> position within its list) for the COFF
    symbols worth keeping. ELF wants every local before every global."""
    defs, _ = section_definitions(coff)
    loc, glb, where = [], [], {}
    for s in coff.symbols:
        cls, secnum, name = s['class'], s['section'], s['name']
        if cls not in (CLS_EXTERNAL, CLS_STATIC, CLS_LABEL, CLS_WEAK_EXTERNAL):
            continue           # file names, .bf/.ef block markers, section-class stubs
        if secnum == -2:
            continue           # IMAGE_SYM_DEBUG

        size, value = 0, s['value']
        if secnum == -1:
            bind, typ, shndx = (STB_LOCAL if cls == CLS_STATIC else STB_GLOBAL), STT_NOTYPE, SHN_ABS
        elif secnum == 0:
            if cls == CLS_WEAK_EXTERNAL:
                # Bind to the fallback the aux record names, as MSVC's linker does. A
                # strong definition elsewhere still wins, because this stays weak.
                fb = weak_fallback(coff, s)
                if fb is not None and fb['section'] > 0 and fb['section'] in elf_index:
                    bind, shndx, value = STB_WEAK, elf_index[fb['section']], fb['value']
                    typ = STT_FUNC if fb['section'] in exec_sections else STT_OBJECT
                else:
                    bind, typ, shndx, value = STB_WEAK, STT_NOTYPE, SHN_UNDEF, 0
            elif value:
                # COFF spells a common block as an external with a size and no section.
                bind, typ, shndx, size, value = STB_GLOBAL, STT_OBJECT, SHN_COMMON, value, 4
            else:
                bind, typ, shndx = STB_GLOBAL, STT_NOTYPE, SHN_UNDEF
        else:
            if secnum not in elf_index:
                continue       # defined in a section we dropped
            shndx = elf_index[secnum]
            bind = STB_LOCAL if cls in (CLS_STATIC, CLS_LABEL) else STB_GLOBAL
            # A COFF COMDAT means "any one copy will do", which in ELF is a weak
            # definition, not a strong one. Emitting these strong makes them collide with
            # the weak copies clang produces for the same thing: given a header that
            # defines a const table, our object and the library each define it, and lld
            # ends up discarding the group it kept while the other copy has already lost.
            # The symbol then comes out undefined even though two objects defined it.
            # A COFF COMDAT means "any one copy will do". For DATA that is exactly an
            # ELF weak definition, and emitting it strong makes it collide with the weak
            # copy clang produces from a __declspec(selectany) table in a header: lld
            # keeps one group, discards the other, and the symbol ends up undefined even
            # though two objects defined it.
            #
            # Code COMDATs stay strong. A weak definition does not cause lld to extract an
            # archive member, so weakening functions loses mainCRTStartup, memset and the
            # rest of the library.
            sel = defs.get(secnum, (0, 0))[0]
            if (bind == STB_GLOBAL and sel and sel != SEL_NODUPLICATES
                    and secnum not in exec_sections):
                bind = STB_WEAK
            if cls == CLS_STATIC and value == 0 and name == section_name(coff, coff.sections[secnum - 1]):
                typ = STT_SECTION
            elif secnum in exec_sections:
                typ = STT_FUNC
            else:
                typ = STT_OBJECT

        lst = loc if bind == STB_LOCAL else glb
        where[s['index']] = (bind == STB_LOCAL, len(lst))
        lst.append({'name': name, 'value': value, 'size': size,
                    'info': (bind << 4) | typ, 'shndx': shndx})
    return loc, glb, where


def build_elf(coff, opts, rep):
    kept, elf_index, names = [], {}, []
    seen = collections.Counter()
    for i, sec in enumerate(coff.sections, 1):
        if not keep_section(coff, sec):
            rep.dropped_sections += 1
            continue
        elf_index[i] = len(kept) + 1
        kept.append(sec)
    # COMDATs give an object several sections with the same name; ELF needs them distinct.
    repeated = collections.Counter(section_name(coff, s) for s in kept)
    for sec in kept:
        n = section_name(coff, sec)
        seen[n] += 1
        names.append(n if repeated[n] == 1 else f"{n}.{seen[n]}")

    exec_sections = {i for i, s in enumerate(coff.sections, 1) if s['flags'] & SCN_MEM_EXECUTE}
    loc, glb, where = build_symbols(coff, elf_index, exec_sections)

    def elf_sym(coff_index):
        """Map a COFF symbol index to an ELF one, inventing an undefined global for
        anything that was defined in a section we dropped."""
        if coff_index in where:
            is_local, pos = where[coff_index]
            return 1 + pos + (0 if is_local else len(loc))
        name = next((s['name'] for s in coff.symbols if s['index'] == coff_index),
                    f'.coffsym{coff_index}')
        rep.warn(f"{name}: referenced but defined in a dropped section, left undefined")
        where[coff_index] = (False, len(glb))
        glb.append({'name': name, 'value': 0, 'size': 0,
                    'info': (STB_GLOBAL << 4) | STT_NOTYPE, 'shndx': SHN_UNDEF})
        return 1 + len(loc) + len(glb) - 1

    contents, relocs = [], []
    for sec, name in zip(kept, names):
        if sec['flags'] & SCN_UNINIT or sec['rawptr'] == 0:
            contents.append(None)          # .bss: SizeOfRawData is the size, there is no data
            relocs.append([])
            continue
        data = bytearray(coff.section_data(sec))
        if len(data) != sec['rawsize']:
            raise ValueError(f"{name}: section data truncated")
        relocs.append(translate_relocs(coff, sec, name, data, elf_sym, opts, rep))
        contents.append(bytes(data))

    # After the relocations, so any undefined global elf_sym had to invent already exists
    # and the symbol indices the groups quote are final.
    groups = []
    if not opts.no_comdat_groups:
        for root, (leader, members) in sorted(comdat_groups(coff, elf_index, rep).items()):
            groups.append({'sig': elf_sym(leader['index']),
                           'members': sorted(elf_index[m] for m in members)})
        rep.groups = len(groups)

    return emit(kept, names, contents, relocs, loc, glb, groups)


def emit(kept, names, contents, relocs, loc, glb, groups):
    """Lay the ELF out: header, section contents, section headers at the end."""
    shstr, strtab = Strtab(), Strtab()
    sections = [{'name': '', 'type': 0, 'flags': 0, 'link': 0, 'info': 0,
                 'align': 0, 'entsize': 0, 'data': b''}]

    # A group section has to name its members by section index, so fix the layout first:
    # contents, then one .rela per section that has relocations, then one .group per
    # COMDAT, then the symbol and string tables.
    rela_index = {}
    next_index = 1 + len(kept)
    for i, entries in enumerate(relocs, 1):
        if entries:
            rela_index[i] = next_index
            next_index += 1
    symtab_index = next_index + len(groups)

    # A member of a group must say so in its own flags, and a .rela belongs to the group
    # of the section it relocates.
    in_group = set()
    for g in groups:
        in_group.update(g['members'])
        in_group.update(rela_index[i] for i in g['members'] if i in rela_index)

    for index, (sec, name, data) in enumerate(zip(kept, names, contents), 1):
        flags = SHF_ALLOC
        if sec['flags'] & SCN_MEM_WRITE:
            flags |= SHF_WRITE
        if sec['flags'] & SCN_MEM_EXECUTE:
            flags |= SHF_EXECINSTR
        if index in in_group:
            flags |= SHF_GROUP
        nobits = data is None
        sections.append({'name': name, 'type': SHT_NOBITS if nobits else SHT_PROGBITS,
                         'flags': flags, 'link': 0, 'info': 0,
                         'align': section_align(sec), 'entsize': 0,
                         'data': b'' if nobits else data,
                         'size': sec['rawsize'] or sec['vsize'] if nobits else len(data)})

    for i, entries in enumerate(relocs, 1):
        if not entries:
            continue
        blob = bytearray()
        for offset, sym, rtype, addend in entries:
            # r_addend is signed, but an ADDR32 field arrives unsigned, so mask rather
            # than let struct reject either end of the range.
            blob += struct.pack('>III', offset, (sym << 8) | rtype, addend & 0xFFFFFFFF)
        flags = SHF_INFO_LINK | (SHF_GROUP if rela_index[i] in in_group else 0)
        sections.append({'name': '.rela' + names[i - 1], 'type': SHT_RELA,
                         'flags': flags, 'link': symtab_index, 'info': i,
                         'align': 4, 'entsize': 12, 'data': bytes(blob)})

    for g in groups:
        # First word is the flags, GRP_COMDAT, then the member section indices. sh_info is
        # the signature symbol, whose name is the only thing lld compares between objects.
        members = sorted(g['members'] + [rela_index[i] for i in g['members']
                                         if i in rela_index])
        blob = struct.pack(f'>{1 + len(members)}I', GRP_COMDAT, *members)
        sections.append({'name': '.group', 'type': SHT_GROUP, 'flags': 0,
                         'link': symtab_index, 'info': g['sig'], 'align': 4,
                         'entsize': 4, 'data': blob})

    if len(sections) != symtab_index:
        raise ValueError("section layout disagrees with the indices written into the groups")

    symblob = bytearray(struct.pack('>IIIBBH', 0, 0, 0, 0, 0, 0))
    for s in loc + glb:
        symblob += struct.pack('>IIIBBH', strtab.add(elf_symbol_name(s['name'])),
                               s['value'], s['size'], s['info'], 0, s['shndx'])
    sections.append({'name': '.symtab', 'type': SHT_SYMTAB, 'flags': 0,
                     'link': symtab_index + 1, 'info': 1 + len(loc), 'align': 4,
                     'entsize': 16, 'data': bytes(symblob)})
    sections.append({'name': '.strtab', 'type': SHT_STRTAB, 'flags': 0, 'link': 0,
                     'info': 0, 'align': 1, 'entsize': 0, 'data': bytes(strtab.buf)})
    sections.append({'name': '.shstrtab', 'type': SHT_STRTAB, 'flags': 0, 'link': 0,
                     'info': 0, 'align': 1, 'entsize': 0, 'data': None})

    for s in sections:
        s['nameoff'] = shstr.add(s['name'])
    sections[-1]['data'] = bytes(shstr.buf)

    out = bytearray(struct.pack('>16sHHIIIIIHHHHHH',
                                b'\x7fELF\x01\x02\x01' + b'\0' * 9,
                                ET_REL, EM_PPC, 1, 0, 0, 0, 0, 52, 0, 0, 40,
                                len(sections), len(sections) - 1))
    for s in sections:
        if s['type'] in (0, SHT_NOBITS):
            s['offset'] = 0
            continue
        align = max(s['align'], 1)
        out += b'\0' * (-len(out) % align)
        s['offset'] = len(out)
        out += s['data']

    out += b'\0' * (-len(out) % 4)
    shoff = len(out)
    for s in sections:
        out += struct.pack('>IIIIIIIIII', s['nameoff'], s['type'], s['flags'], 0,
                           s['offset'], s.get('size', len(s['data'])), s['link'],
                           s['info'], s['align'], s['entsize'])
    struct.pack_into('>I', out, 32, shoff)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0].replace('\n', ' '))
    ap.add_argument('input', help='Xenon COFF object, or a member extracted from a .lib')
    ap.add_argument('-o', '--output', help='output ELF (default: input with .o)')
    ap.add_argument('--allow-ds-reflo', action='store_true',
                    help='accept REFLO on DS-form instructions, losing their opcode bits')
    ap.add_argument('--skip-unsupported', action='store_true',
                    help='drop relocation types with no ELF32 PPC equivalent instead of failing')
    ap.add_argument('--no-comdat-groups', action='store_true',
                    help='emit COMDAT sections without ELF groups, so nothing is deduplicated')
    ap.add_argument('-q', '--quiet', action='store_true', help='no summary on stderr')
    opts = ap.parse_args()

    rep = Report()
    try:
        with open(opts.input, 'rb') as f:
            coff = Coff(f.read())
        elf = build_elf(coff, opts, rep)
    except ValueError as e:
        sys.exit(f"coff2elf: {opts.input}: {e}")
    out = opts.output or os.path.splitext(opts.input)[0] + '.o'
    with open(out, 'wb') as f:
        f.write(elf)

    if not opts.quiet:
        counts = ' '.join(f"{RELOC.get(t, hex(t))}={n}" for t, n in sorted(rep.relocs.items()))
        print(f"{opts.input} -> {out}: {rep.dropped_sections} sections dropped, "
              f"{counts or 'no relocations'}", file=sys.stderr)
        if rep.selections:
            sels = ' '.join(f"{SEL_NAMES.get(s, s)}={n}" for s, n in sorted(rep.selections.items()))
            print(f"  {rep.groups} section groups, {rep.ungrouped_comdats} COMDATs left "
                  f"ungrouped, selections {sels}", file=sys.stderr)
        for m in rep.messages:
            print(f"  warning: {m}", file=sys.stderr)


if __name__ == '__main__':
    main()
