#!/usr/bin/env python3
"""Find the code in a linked Xbox 360 title that Xenia will not run as the console does.

Two things make a Xenia result unreliable without any warning from Xenia:

  skipped instructions  Xenia's PowerPC front end has emitters that only log
                        "not implemented" and carry on, so the instruction does
                        nothing. Anything computed through one is wrong.

  overrun functions     Xenia finds where a function ends by scanning forward
                        from its start. A forward branch moves the end out to
                        the branch target, so a forward tail call (`b` into a
                        later function) makes Xenia compile everything up to
                        that target as one function. Code reached through it
                        can run as the wrong function, or crash Xenia.

This reads the title, decodes the instructions itself, and replays the same
end-of-function rules Xenia's scanner uses (src/xenia/cpu/ppc/ppc_scanner.cc,
PPCScanner::Scan) on every function it can find. Nothing is run.

    xeniascan.py build/title.elf            # oxdklink's linked ELF, with symbols
    xeniascan.py default.xex                # a plain XEX: functions from bl targets
    xeniascan.py --xenia master title.elf   # xenia-project rather than canary
    xeniascan.py --only 'x360_*' title.elf  # just these functions (fnmatch)

Exit status is 0 when nothing was found, 1 when something was, 2 on bad input.
--report-only always exits 0, for a smoke test that wants the list but not a
failure.

The instruction lists come from reading Xenia's ppc_emit_*.cc for the emitters
that reach XEINSTRNOTIMPLEMENTED, in xenia-canary (canary_experimental) and
xenia-project (master) as of November 2026, plus canary's mcrf before it was
fixed (it ran, but wrote the wrong field). Re-check them when Xenia moves.
"""
import argparse
import fnmatch
import os
import struct
import sys

# ---------------------------------------------------------------------------
# What each Xenia leaves out. Keys are names printed in reports.
# ---------------------------------------------------------------------------

# XO-form arithmetic whose OE=1 ("o") form Xenia leaves unimplemented: it does
# not maintain XER[OV]/[SO]. Primary opcode 31, 9-bit extended opcode.
OE_FORMS = {
    266: 'add', 10: 'addc', 138: 'adde', 234: 'addme', 202: 'addze',
    489: 'divd', 457: 'divdu', 491: 'divw', 459: 'divwu',
    233: 'mulld', 235: 'mullw', 104: 'neg',
    40: 'subf', 8: 'subfc', 136: 'subfe', 232: 'subfme', 200: 'subfze',
}

# SPRs Xenia's mfspr and mtspr handle. Any other is "not implemented".
MFSPR_OK = {1, 8, 9, 256, 268, 269, 287}   # XER LR CTR VRSAVE TBL TBU PVR
MTSPR_OK = {1, 8, 9, 256}                  # XER LR CTR VRSAVE

# Standard VMX (primary opcode 4). VA-form by the low 6 bits, VX-form by the
# low 11. VMX128 also lives under opcode 4 and is told apart below.
VMX_VA = {32: 'vmhaddshs', 33: 'vmhraddshs', 34: 'vmladduhm', 36: 'vmsumubm',
          37: 'vmsummbm', 38: 'vmsumuhm', 39: 'vmsumuhs', 40: 'vmsumshm',
          41: 'vmsumshs'}
VMX_VX = {8: 'vmuloub', 72: 'vmulouh', 264: 'vmulosb', 328: 'vmulosh',
          520: 'vmuleub', 584: 'vmuleuh', 776: 'vmulesb', 840: 'vmulesh',
          1544: 'vsum4ubs', 1608: 'vsum4shs', 1672: 'vsum2sws', 1800: 'vsum4sbs',
          1928: 'vsumsws', 846: 'vupkhpx', 974: 'vupklpx',
          782: 'vpkpx', 1540: 'mfvscr', 1604: 'mtvscr'}

PROFILES = {
    # xenia-canary implements mcrf, rldic, mcrfs, mffs., the VSCR moves, vpkpx
    # and addeo that master does not. 'skip' lists what a profile gets right.
    'canary': {
        'skip': {'mcrf', 'rldic', 'mcrfs', 'mffs.', 'mfvscr', 'mtvscr', 'vpkpx', 'addeo'},
    },
    # Canary before 577fb8e (2026-09-04, "Fix mcrf: copy the CR field instead of
    # comparing it against zero"): its mcrf set the destination field from a
    # signed compare of the source field's value, so the field read back later is
    # wrong. Builds such as 02d2cb5 (March 2026) still have it.
    'canary-old': {
        'skip': {'rldic', 'mcrfs', 'mffs.', 'mfvscr', 'mtvscr', 'vpkpx', 'addeo'},
    },
    'master': {
        'skip': set(),
    },
}

ALWAYS = {'lswi', 'lswx', 'stswi', 'stswx', 'mtfsb0', 'mtfsb1'}


def sext(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def unimplemented(code):
    """The name of what Xenia skips in this instruction word, or None."""
    op = code >> 26
    if op == 19 and ((code >> 1) & 0x3FF) == 0:
        return 'mcrf'
    if op == 30 and ((code >> 2) & 0x7) == 2:
        return 'rldic' + ('.' if code & 1 else '')
    if op == 31:
        xo10 = (code >> 1) & 0x3FF
        xo9 = (code >> 1) & 0x1FF
        if xo10 == 597: return 'lswi'
        if xo10 == 533: return 'lswx'
        if xo10 == 725: return 'stswi'
        if xo10 == 661: return 'stswx'
        if (code >> 10) & 1 and xo9 in OE_FORMS:
            return OE_FORMS[xo9] + 'o' + ('.' if code & 1 else '')
        if xo10 in (339, 467):                      # mfspr, mtspr
            spr = ((code >> 16) & 0x1F) | (((code >> 11) & 0x1F) << 5)
            ok = MFSPR_OK if xo10 == 339 else MTSPR_OK
            if spr not in ok:
                return f"{'mfspr' if xo10 == 339 else 'mtspr'} {spr}"
        return None
    if op == 63:
        xo10 = (code >> 1) & 0x3FF
        if xo10 == 64: return 'mcrfs'
        if xo10 == 583 and code & 1: return 'mffs.'
        if xo10 == 70: return 'mtfsb0'
        if xo10 == 38: return 'mtfsb1'
        return None
    if op == 4:
        # VMX128 loads and stores under opcode 4 end in binary 11; the standard
        # VMX forms listed here never do.
        if code & 3 == 3:
            return None
        va = code & 0x3F
        if va in VMX_VA:
            return VMX_VA[va]
        vx = code & 0x7FF
        if vx in VMX_VX:
            return VMX_VX[vx]
    return None


def skipped(name, profile):
    if name is None:
        return False
    base = name.split()[0]
    return base not in PROFILES[profile]['skip']


# ---------------------------------------------------------------------------
# Input: oxdklink's ELF, or a plain XEX.
# ---------------------------------------------------------------------------

class Image:
    def __init__(self):
        self.mem = {}          # section start -> bytes, executable sections only
        self.funcs = []        # (start, end or None, name)
        self.restgprlr = set() # addresses of __restgprlr_* entry points

    def word(self, addr):
        for start, data in self.mem.items():
            if start <= addr < start + len(data) - 3:
                return struct.unpack_from('>I', data, addr - start)[0]
        return None

    def ranges(self):
        for start, data in sorted(self.mem.items()):
            yield start, start + len(data)


def load_elf(path, d):
    if d[4] != 1 or d[5] != 2:
        raise SystemExit("xeniascan: only 32-bit big-endian ELF is handled")
    shoff, = struct.unpack_from('>I', d, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from('>HHH', d, 0x2E)
    secs = [struct.unpack_from('>IIIIIIIIII', d, shoff + i * shentsize) for i in range(shnum)]
    img = Image()
    symtab = None
    for s in secs:
        name, typ, flags, addr, off, size, link = s[0], s[1], s[2], s[3], s[4], s[5], s[6]
        if typ == 1 and flags & 0x4 and size:          # PROGBITS, SHF_EXECINSTR
            img.mem[addr] = d[off:off + size]
        if typ == 2:                                   # SYMTAB
            symtab = s
    if symtab is None:
        raise SystemExit("xeniascan: the ELF has no symbol table (stripped?)")
    strs = secs[symtab[6]]
    seen = {}
    for i in range(symtab[5] // 16):
        st_name, value, size, info, other, shndx = struct.unpack_from('>IIIBBH', d, symtab[4] + i * 16)
        if shndx == 0 or shndx >= 0xFF00:
            continue
        n0 = strs[4] + st_name
        name = d[n0:d.index(b'\0', n0)].decode('latin-1')
        if name.startswith('__restgprlr_'):
            img.restgprlr.add(value)
        if info & 0xF != 2:                            # STT_FUNC
            continue
        if not any(a <= value < b for a, b in img.ranges()):
            continue
        # Prefer a sized symbol when two share an address.
        if value not in seen or (size and not seen[value][1]):
            seen[value] = (name, size)
    for value, (name, size) in seen.items():
        img.funcs.append((value, value + size if size else None, name))
    img.funcs.sort()
    # Unsized symbols (assembly, the XDK's objects) end where the next begins.
    fixed = []
    for i, (start, end, name) in enumerate(img.funcs):
        if end is None:
            end = img.funcs[i + 1][0] if i + 1 < len(img.funcs) else None
        fixed.append((start, end, name))
    img.funcs = fixed
    return img


def load_xex(path, d):
    u32 = lambda o: struct.unpack_from('>I', d, o)[0]
    u16 = lambda o: struct.unpack_from('>H', d, o)[0]
    pe_off, count = u32(8), u32(20)
    fmt = base = entry = None
    for i in range(count):
        k, v = u32(24 + i * 8), u32(28 + i * 8)
        if k == 0x000003FF: fmt = v
        elif k == 0x00010201: base = v
        elif k == 0x00010100: entry = v
    if fmt is None or base is None:
        raise SystemExit("xeniascan: XEX has no file format or image base header")
    if u16(fmt + 4) != 0 or u16(fmt + 6) != 1:
        raise SystemExit("xeniascan: only an unencrypted, basic-compressed XEX is handled")
    pe = bytearray()
    src = pe_off
    for b in range((u32(fmt) - 8) // 8):
        n, z = u32(fmt + 8 + b * 8), u32(fmt + 12 + b * 8)
        pe += d[src:src + n] + bytes(z)
        src += n
    lfanew = struct.unpack_from('<I', pe, 0x3C)[0]
    nsec = struct.unpack_from('<H', pe, lfanew + 6)[0]
    ohsz = struct.unpack_from('<H', pe, lfanew + 20)[0]
    img = Image()
    for i in range(nsec):
        e = lfanew + 24 + ohsz + i * 40
        vsize, rva = struct.unpack_from('<II', pe, e + 8)
        chars = struct.unpack_from('<I', pe, e + 36)[0]
        if chars & 0x20000000:
            img.mem[base + rva] = bytes(pe[rva:rva + vsize])
    # Functions are what something calls: bl targets, plus the entry point.
    starts = {entry} if entry else set()
    for lo, hi in img.ranges():
        for a in range(lo, hi - 3, 4):
            c = img.word(a)
            if c >> 26 == 18 and c & 1 and not c & 2:
                starts.add((a + sext(c & 0x03FFFFFC, 26)) & 0xFFFFFFFF)
    starts = sorted(s for s in starts if any(lo <= s < hi for lo, hi in img.ranges()))
    for i, s in enumerate(starts):
        img.funcs.append((s, starts[i + 1] if i + 1 < len(starts) else None, f"sub_{s:08x}"))
    return img


def load(path):
    d = open(path, 'rb').read()
    if d[:4] == b'\x7fELF':
        return load_elf(path, d)
    if d[:4] == b'XEX2':
        return load_xex(path, d)
    print(f"xeniascan: {path} is neither an ELF nor an XEX", file=sys.stderr)
    sys.exit(2)


# ---------------------------------------------------------------------------
# Xenia's function end, replayed.
# ---------------------------------------------------------------------------

def xenia_end(img, start, end=None):
    """Where Xenia's scanner ends the function at start, and the first branch
    inside [start, end) that pushed its furthest target to end or beyond (or
    None). The rules follow PPCScanner::Scan."""
    furthest = start
    widened_by = None

    def widen(a, target):
        nonlocal furthest, widened_by
        if target > furthest:
            furthest = target
            if widened_by is None and end is not None and a < end <= target:
                widened_by = (a, target)

    blocks = 0
    in_block = False
    leaf = True
    a = start
    while True:
        c = img.word(a)
        if c is None:
            return a, widened_by
        if c == 0:
            return a, widened_by
        op = c >> 26
        if a == start and op == 31 and ((c >> 1) & 0x3FF) == 339 and \
                (((c >> 16) & 0x1F) | (((c >> 11) & 0x1F) << 5)) == 8:
            leaf = False                               # starts with mflr
        if not in_block:
            in_block = True
            blocks += 1
        ends_fn = ends_block = False
        if c in (0x4E800020, 0x4E800420):              # blr, bctr
            if furthest <= a:
                ends_fn = True
            ends_block = True
        elif op == 18:                                 # b, ba, bl, bla
            target = c & 0x03FFFFFC
            target = target if c & 2 else (a + sext(target, 26)) & 0xFFFFFFFF
            if not c & 1:
                if start <= target < a and furthest <= a:
                    ends_fn = True
                if not ends_fn and target < start and furthest <= a:
                    ends_fn = True
                if not ends_fn and furthest <= a and target in img.restgprlr:
                    ends_fn = True
                if not ends_fn and leaf and blocks == 1:
                    ends_fn = True
                if not ends_fn and target not in img.restgprlr:
                    widen(a, target)
            ends_block = True
        elif op == 16:                                 # bc
            target = sext(c & 0xFFFC, 16)
            target = target & 0xFFFFFFFF if c & 2 else (a + target) & 0xFFFFFFFF
            if not c & 1 and target not in img.restgprlr:
                widen(a, target)
            ends_block = True
        elif op == 19 and ((c >> 1) & 0x3FF) in (16, 528):   # bclr, bcctr
            ends_block = True
        if ends_block:
            in_block = False
        if ends_fn:
            return a + 4, widened_by
        a += 4


def name_at(img, addr):
    best = None
    for start, end, name in img.funcs:
        if start <= addr and (end is None or addr < end):
            best = (start, name)
    if best is None:
        return f"0x{addr:08x}"
    start, name = best
    return name if start == addr else f"{name}+0x{addr - start:x}"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0],
                                 epilog=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('title', help="oxdklink's linked ELF, or an unencrypted XEX")
    ap.add_argument('--xenia', choices=sorted(PROFILES), default='canary',
                    help="which Xenia's gaps to check against: canary (577fb8e, "
                         "2026-09-04, or newer), canary-old (older canary, whose mcrf is "
                         "wrong) or master (default canary)")
    ap.add_argument('--only', action='append', default=[],
                    help="limit to functions matching this pattern (repeatable)")
    ap.add_argument('--no-overrun', action='store_true', help="skip the function end check")
    ap.add_argument('--all', action='store_true',
                    help="also list functions Xenia runs past only because a branch "
                         "inside them targets their own tail (a trap, say), not another function")
    ap.add_argument('--report-only', action='store_true', help="always exit 0")
    ap.add_argument('--quiet', action='store_true', help="print only the summary")
    a = ap.parse_args()

    img = load(a.title)
    funcs = img.funcs
    if a.only:
        funcs = [f for f in funcs if any(fnmatch.fnmatch(f[2], p) for p in a.only)]

    skips = []
    for start, end, name in funcs:
        stop = end
        if stop is None:
            stop = next((hi for lo, hi in img.ranges() if lo <= start < hi), start)
        for addr in range(start, stop, 4):
            c = img.word(addr)
            if c is None:
                break
            what = unimplemented(c)
            if skipped(what, a.xenia):
                skips.append((name, addr, what))

    overruns = []
    if not a.no_overrun:
        for start, end, name in funcs:
            if end is None:
                continue
            got, why = xenia_end(img, start, end)
            if got > end and (why or a.all):
                overruns.append((name, start, end, got, why))

    if not a.quiet:
        for name, addr, what in skips:
            print(f"xeniascan: skipped   {name}: {addr:08x} {what}")
        for name, start, end, got, why in overruns:
            swallowed = sorted({f[2] for f in img.funcs if end <= f[0] < got})
            more = f" (and {len(swallowed) - 4} more)" if len(swallowed) > 4 else ""
            cause = (f"branch at {why[0]:08x} to {name_at(img, why[1])}" if why
                     else "its own tail")
            print(f"xeniascan: overrun   {name}: ends at {end:08x}, Xenia takes it to {got:08x} "
                  f"({cause}); swallows {', '.join(swallowed[:4]) or 'padding'}{more}")

    by_fn = len({s[0] for s in skips})
    print(f"xeniascan: {len(funcs)} functions, Xenia ({a.xenia}): "
          f"{len(skips)} skipped instructions in {by_fn} functions, "
          f"{len(overruns)} functions it would run past their end")
    if skips or overruns:
        print("xeniascan: a Xenia result through these functions is unreliable; the console is the reference")
    return 0 if a.report_only or not (skips or overruns) else 1


if __name__ == '__main__':
    sys.exit(main())
