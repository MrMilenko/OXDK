#!/usr/bin/env python3
"""Check that compiled code keeps every register Microsoft's code treats as nonvolatile.

The console's compiler keeps r14-r31 WHOLE across a call (all 64 bits, even in
a title with 32 bit pointers), and f14-f31, and v14-v31. Our code is called by
Microsoft's in more places than it looks: main, every thread entry point, qsort
and bsearch comparators, and any SDK callback. A callee that saves r14-r31 as
words hands its caller back registers with the upper half zeroed, which breaks
any 64 bit value the caller was keeping there.

Two checks, over the cases in cases/nv_*.c:

  static  every save of r14-r31 is a doubleword store and every restore a
          doubleword load, and there is no vector instruction anywhere (we
          build without AltiVec, so nothing may touch v14-v31)
  run     each case is called from a hand-written caller that fills r14-r31
          with patterns using all 64 bits, as an MSVC caller's registers would
          be, and f14-f31 likewise, then checks every one after the call. The
          caller runs under qemu-ppc64 (user mode, 64 bit big endian), which is
          the closest thing to the console's 64 bit core a host has. Skipped,
          and said so, without --qemu.

    nvcheck.py                                   # static only, the patched clang
    nvcheck.py --qemu qemu-ppc64                 # and run each case
    nvcheck.py --cc /path/to/clang --qemu ...
    nvcheck.py --llc /path/to/llc                # clang to IR, then this llc:
                                                 # tests a backend change without
                                                 # rebuilding clang
    nvcheck.py --keep DIR                        # keep the test programs, to run
                                                 # them elsewhere (exit 0 = kept)

The run check compiles each case for the console as usual, then assembles that
same assembly for 64 bit PowerPC Linux, so the instructions under test are the
console build's own.
"""
import argparse, glob, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'common'))
import toolchain

FLAGS = ['-target', 'powerpc-unknown-none-elf', '-O2', '-ffreestanding', '-fno-builtin',
         '-Xclang', '-target-feature', '-Xclang', '+xenon-abi']
HOST = ['-target', 'powerpc64-unknown-linux-gnu', '-mabi=elfv2']

NV = range(14, 32)


def gpr_pattern(n):
    # Distinct upper and lower halves, so a zeroed or swapped half shows.
    return ((0xA5000000 | n << 8 | n) << 32) | (0x5A000000 | n << 16 | n)


def harness(case):
    """A 64 bit caller for one case: exit status 0 if every nonvolatile register
    survived, otherwise the first that did not (14-31 a GPR, 46-63 an FPR as
    32 + n, 99 the stack pointer)."""
    # The title's code keeps addresses in 32 bits, so it gets a stack below 4 GB
    # rather than the one qemu hands a 64 bit process.
    out = ['\t.abiversion 2', '\t.text', '\t.globl _start', '_start:',
           '\tlis 1, stack_top@ha', '\taddi 1, 1, stack_top@l']
    for n in NV:
        p = gpr_pattern(n)
        out += [f'\tlis {n}, {p >> 48:#x}', f'\tori {n}, {n}, {(p >> 32) & 0xFFFF:#x}',
                f'\tsldi {n}, {n}, 32', f'\toris {n}, {n}, {(p >> 16) & 0xFFFF:#x}',
                f'\tori {n}, {n}, {p & 0xFFFF:#x}']
    out += ['\tlis 12, fprs@ha', '\taddi 12, 12, fprs@l']
    for n in NV:
        out.append(f'\tlfd {n}, {(n - 14) * 8}(12)')
    out += ['\tli 3, 16', f'\tbl {case}', '\tnop',
            '\tlis 11, stack_top@ha', '\taddi 11, 11, stack_top@l', '\tcmpd 11, 1',
            '\tli 3, 99', '\tbne fail']
    for n in NV:
        p = gpr_pattern(n)
        out += [f'\tlis 11, {p >> 48:#x}', f'\tori 11, 11, {(p >> 32) & 0xFFFF:#x}',
                '\tsldi 11, 11, 32', f'\toris 11, 11, {(p >> 16) & 0xFFFF:#x}',
                f'\tori 11, 11, {p & 0xFFFF:#x}', f'\tcmpd 11, {n}', f'\tli 3, {n}', '\tbne fail']
    out += ['\tlis 12, fprs@ha', '\taddi 12, 12, fprs@l']
    for n in NV:
        out += [f'\tlfd 0, {(n - 14) * 8}(12)', f'\tfcmpu 0, 0, {n}', f'\tli 3, {32 + n}', '\tbne fail']
    out += ['\tli 3, 0', 'fail:', '\tli 0, 1', '\tsc',
            # ext and use: nothing to do, which leaves the registers to the case
            '\t.globl ext', 'ext:', '\tblr', '\t.globl use', 'use:', '\tblr',
            '\t.bss', '\t.p2align 4', '\t.space 0x40000', 'stack_top:', '\t.space 0x100',
            '\t.data', '\t.p2align 3', 'fprs:']
    for n in NV:
        out.append(f'\t.double {1.0 + n / 64.0 + n * 1e-9!r}')
    return '\n'.join(out) + '\n'


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(' '.join(cmd) + '\n' + r.stderr.strip())
    return r.stdout


def console_asm(cc, llc, src, out):
    """The console build of a case, as assembly."""
    if llc:
        ll = out + '.ll'
        run([cc] + FLAGS + ['-S', '-emit-llvm', src, '-o', ll])
        run([llc, '-O2', ll, '-o', out])
    else:
        run([cc] + FLAGS + ['-S', src, '-o', out])


def static_check(objdump, obj):
    problems = []
    text = run([objdump, '-d', '--mattr=+altivec', obj])
    fn = None
    for line in text.splitlines():
        m = re.match(r'^[0-9a-f]+ <(.+)>:', line)
        if m:
            fn = m.group(1)
            continue
        m = re.match(r'^\s+[0-9a-f]+:\s+(?:[0-9a-f]{2} ){4}\s*(\S+)\s*(.*)$', line)
        if not m:
            continue
        mn, ops = m.groups()
        r = re.match(r'(\d+),\s*(-?\d+)\((\d+)\)', ops)
        if mn in ('stw', 'lwz') and r and 14 <= int(r.group(1)) <= 31 and \
                int(r.group(3)) in (1, 29, 30, 31):
            # lwz 31, 0(1) reloads the back chain into the frame pointer to use
            # it as a base; that is not a restore.
            if not (mn == 'lwz' and r.group(1) == '31' and r.group(2) == '0'):
                problems.append(f"{fn}: {mn} {ops}  (word save or restore of r{r.group(1)})")
        if re.match(r'v[a-z]', mn) or mn in ('lvx', 'stvx', 'lvsl', 'lvsr', 'lvxl', 'stvxl',
                                              'lvebx', 'lvehx', 'lvewx', 'stvebx', 'stvehx',
                                              'stvewx', 'mfvscr', 'mtvscr', 'mfvrsave', 'mtvrsave'):
            problems.append(f"{fn}: {mn} {ops}  (vector instruction)")
    return problems


def describe(status):
    if status == 99:
        return "r1 came back changed"
    if 14 <= status <= 31:
        return f"r{status} came back changed (want 0x{gpr_pattern(status):016x})"
    if 46 <= status <= 63:
        return f"f{status - 32} came back changed"
    return f"exit status {status}"


def show(name, problems):
    print(f"  {'ok  ' if not problems else 'FAIL'}  {name}")
    for p in problems[:6]:
        print(f"        {p}")
    if len(problems) > 6:
        print(f"        ... {len(problems) - 6} more")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0], epilog=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--cc', help='clang to test (default: the patched one OXDK finds)')
    ap.add_argument('--llc', help="compile clang's IR with this llc instead")
    ap.add_argument('--qemu', help='qemu-ppc64 to run the cases under')
    ap.add_argument('--keep', help='write the test programs here')
    a = ap.parse_args()
    cc = a.cc or toolchain.tool('clang')
    objdump = toolchain.tool('llvm-objdump')
    lld = toolchain.tool('ld.lld')
    cases = sorted(glob.glob(os.path.join(HERE, 'cases', 'nv_*.c')))
    names = [os.path.basename(s)[:-2] for s in cases]

    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        print(f"compiler: {cc}" + (f"  then {a.llc}" if a.llc else "") + "\n")
        print("static: saves and restores of r14-r31, vector instructions")
        progs = []
        for src, name in zip(cases, names):
            s = os.path.join(tmp, name + '.s')
            console_asm(cc, a.llc, src, s)
            obj = os.path.join(tmp, name + '.o')
            run([cc, '-target', 'powerpc-unknown-none-elf', '-c', s, '-o', obj])
            problems = static_check(objdump, obj)
            show(name, problems)
            ok &= not problems

            # The same assembly, assembled for the 64 bit host, under a 64 bit caller.
            hobj = os.path.join(tmp, name + '.host.o')
            run([cc] + HOST + ['-c', s, '-o', hobj])
            hs = os.path.join(tmp, name + '.harness.s')
            open(hs, 'w').write(harness(name))
            hh = hs + '.o'
            run([cc] + HOST + ['-c', hs, '-o', hh])
            prog = os.path.join(tmp, name)
            run([lld, '-static', '-o', prog, hh, hobj])
            progs.append((name, prog))

        if a.keep:
            os.makedirs(a.keep, exist_ok=True)
            for name, prog in progs:
                shutil.copy(prog, os.path.join(a.keep, name))
            print(f"\nkept {len(progs)} programs in {a.keep}; each exits 0 when its registers survive")

        print("\nrun: r14-r31 (64 bit) and f14-f31 across each case, from a 64 bit caller")
        if not a.qemu:
            print("  skip  no --qemu given (qemu-ppc64, user mode)")
        else:
            for name, prog in progs:
                r = subprocess.run([a.qemu, prog], capture_output=True, text=True)
                show(name, [] if r.returncode == 0 else [describe(r.returncode)])
                ok &= r.returncode == 0

    print("\nnonvolatile registers: " + ("all kept" if ok else "NOT kept by this compiler"))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
