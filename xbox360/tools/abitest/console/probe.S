// A callee written by hand to the Xenon convention, checking that the compiler's
// caller side puts arguments where Microsoft's compiler would.
//
// abi_probe(int a, float b, int c, int d, int e, int f, int g, int h, int i)
//
// One eight byte slot per argument. Arguments one to eight also have a register, but
// the float takes an FPR while still consuming its slot, so the integer register that
// shares that slot is skipped: a->r3, b->f1 (r4 skipped), c->r5, d->r6, e->r7, f->r8,
// g->r9, h->r10. Argument nine has no register and lives in slot 9, right justified,
// at caller_sp+84. Returns a bit per argument that arrived where it should.

    .macro CHECKI reg, want, bit
    cmpwi   \reg, \want
    bne     9f
    ori     11, 11, \bit
9:
    .endm

    .section .rdata, "a"
    .align  2
two_f:
    .long   0x40000000              // 2.0f

    .section .text, "ax"
    .globl  abi_probe
abi_probe:
    li      11, 0                   // result mask

    CHECKI  3,  1, 0x001            // argument 1
    CHECKI  5,  3, 0x004            // argument 3, in r5 only if the float took a slot
    CHECKI  6,  4, 0x008
    CHECKI  7,  5, 0x010
    CHECKI  8,  6, 0x020
    CHECKI  9,  7, 0x040
    CHECKI  10, 8, 0x080            // argument 8

    // Argument 9 has no register: slot 9 begins at caller_sp+80 and a four byte scalar
    // sits at the end of it.
    lwz     12, 84(1)
    cmpwi   12, 9
    bne     1f
    ori     11, 11, 0x100
1:
    // Argument 2 is the float, in f1 because floating point registers run sequentially.
    lis     12, two_f@ha
    lfs     0, two_f@l(12)
    fcmpu   0, 0, 1
    bne     2f
    ori     11, 11, 0x002
2:
    mr      3, 11
    blr
