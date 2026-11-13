#!/usr/bin/env python3
"""Check returns and arguments against what Microsoft's compiler does, by running them.

Each function in cases/call_abi.c is compiled for the console, then called from
(or calls into) hand-written 64 bit code that sets and reads registers the way
Microsoft's code does, under qemu-ppc64 in user mode. See nvcheck.py for why
qemu, and call_abi.c for the rules.

    callcheck.py --qemu qemu-ppc64
    callcheck.py --llc /path/to/llc --qemu ...    # a backend change, before clang
    callcheck.py --keep DIR                       # programs to run elsewhere

Without --qemu it builds the programs and stops.
"""
import argparse, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'common'))
import toolchain
from nvcheck import run, console_asm, HOST

# name, argument registers, expected r3, bits compared, what it checks
TESTS = [
    ('ret_i64', {}, 0x1122334455667788, 64, '64 bit result whole in r3'),
    ('ret_s8', {}, 0x1122334455667788, 64, '8 byte struct result in r3'),
    ('ret_sd', {}, 0x3FF8000000000000, 64, 'struct of one double in r3, not f1'),
    ('ret_s2', {}, 0x1234, 16, '2 byte struct right justified in r3'),
    ('take_sc', {3: 0x1234567812345680}, 0xFFFFFF80, 32, 'signed char extended by the callee'),
    ('take_uc', {3: 0x123456781234FF80}, 0x80, 32, 'unsigned char extended by the callee'),
    ('take_ss', {3: 0x1234567812348000}, 0xFFFF8000, 32, 'short extended by the callee'),
    ('take_us', {3: 0x12345678FFFF8000}, 0x8000, 32, 'unsigned short extended by the callee'),
    ('take_s8', {3: 0x0000000500000003}, 2, 32, '8 byte struct argument in one register'),
    ('take_s3', {3: 10, 4: 0x010203AABBCCDDEE}, 16, 32, '3 byte struct left justified in r4'),
    ('call_ret64_lo', {}, 0x55667788, 32, 'our caller reads a 64 bit result from r3 (low)'),
    ('call_ret64_hi', {}, 0x11223344, 32, 'our caller reads a 64 bit result from r3 (high)'),
    ('call_ret8', {}, 0x1122, 32, 'our caller reads an 8 byte struct from r3'),
    ('call_take8', {}, 1, 32, 'our caller passes an 8 byte struct in one register'),
    ('call_take3', {}, 1, 32, 'our caller passes a 3 byte struct left justified'),
]

# Microsoft's side of the call_* functions.
MSVC = r'''
	.globl msvc_ret64
msvc_ret64:
	lis 3, 0x1122
	ori 3, 3, 0x3344
	sldi 3, 3, 32
	oris 3, 3, 0x5566
	ori 3, 3, 0x7788
	li 4, -1                    # nothing of the result is in r4
	blr
	.globl msvc_ret8
msvc_ret8:
	li 3, 0x11
	sldi 3, 3, 32
	ori 3, 3, 0x22
	li 4, -1
	blr
	.globl msvc_take8
msvc_take8:                     # s8 {1, 2}: r3 = 0x0000000100000002
	li 4, 1
	sldi 4, 4, 32
	ori 4, 4, 2
	cmpd 3, 4
	li 3, 0
	bnelr
	li 3, 1
	blr
	.globl msvc_take3
msvc_take3:                     # pad 7 in r3; s3 {1, 2, 3} in the top three bytes of r4
	cmpwi 3, 7
	li 3, 0
	bnelr
	srdi 4, 4, 40
	lis 5, 0x01
	ori 5, 5, 0x0203
	cmpd 4, 5
	bnelr
	li 3, 1
	blr
'''


def load64(reg, v):
    return [f'\tlis {reg}, {(v >> 48) & 0xFFFF:#x}', f'\tori {reg}, {reg}, {(v >> 32) & 0xFFFF:#x}',
            f'\tsldi {reg}, {reg}, 32', f'\toris {reg}, {reg}, {(v >> 16) & 0xFFFF:#x}',
            f'\tori {reg}, {reg}, {v & 0xFFFF:#x}']


def harness(fn, regs, want, bits):
    out = ['\t.abiversion 2', '\t.text', '\t.globl _start', '_start:',
           '\tlis 1, stack_top@ha', '\taddi 1, 1, stack_top@l']
    for r, v in regs.items():
        out += load64(r, v)
    out += [f'\tbl {fn}', '\tnop']
    if bits < 64:
        out.append(f'\tclrldi 3, 3, {64 - bits}')
    out += load64(11, want & ((1 << bits) - 1))
    out += ['\tcmpd 3, 11', '\tli 3, 1', '\tbne 9f', '\tli 3, 0',
            '9:\tli 0, 1', '\tsc', MSVC,
            '\t.bss', '\t.p2align 4', '\t.space 0x10000', 'stack_top:', '\t.space 0x100']
    return '\n'.join(out) + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0], epilog=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--cc')
    ap.add_argument('--llc')
    ap.add_argument('--qemu')
    ap.add_argument('--keep')
    a = ap.parse_args()
    cc = a.cc or toolchain.tool('clang')
    lld = toolchain.tool('ld.lld')
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        s = os.path.join(tmp, 'call_abi.s')
        console_asm(cc, a.llc, os.path.join(HERE, 'cases', 'call_abi.c'), s)
        hobj = s + '.o'
        run([cc] + HOST + ['-c', s, '-o', hobj])
        progs = []
        for fn, regs, want, bits, what in TESTS:
            hs = os.path.join(tmp, fn + '.harness.s')
            open(hs, 'w').write(harness(fn, regs, want, bits))
            run([cc] + HOST + ['-c', hs, '-o', hs + '.o'])
            prog = os.path.join(tmp, fn)
            run([lld, '-static', '-o', prog, hs + '.o', hobj])
            progs.append((fn, what, prog))
        print(f"compiler: {cc}" + (f"  then {a.llc}" if a.llc else ""))
        if a.keep:
            os.makedirs(a.keep, exist_ok=True)
            for fn, what, prog in progs:
                shutil.copy(prog, os.path.join(a.keep, fn))
            print(f"kept {len(progs)} programs in {a.keep}; each exits 0 when it matches Microsoft's convention")
        if not a.qemu:
            print("no --qemu given: built, not run")
            return 0
        for fn, what, prog in progs:
            r = subprocess.run([a.qemu, prog], capture_output=True)
            print(f"  {'ok  ' if r.returncode == 0 else 'FAIL'}  {fn:14s} {what}")
            ok &= r.returncode == 0
    print("\ncalls: " + ("match Microsoft's convention" if ok else "do NOT match Microsoft's convention"))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
