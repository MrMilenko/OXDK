// ABI shims for calls from clang-compiled code into the XDK.
//
// clang emits SysV PowerPC, whose linkage area is 8 bytes. A Xenon callee reserves 16 and
// then spills its incoming arguments into the caller's frame from +16 upward, so calling
// XDK code directly lets the callee write over our locals. Each shim allocates a frame
// with room for that spill. Arguments pass through untouched in r3-r10 and f1-f13.

.macro XSHIM name, target
    .globl  \name
\name:
    mflr    0
    stwu    1, -256(1)
    stw     0, 252(1)
    bl      \target
    lwz     0, 252(1)
    addi    1, 1, 256
    mtlr    0
    blr
.endm

    .section .text, "ax"
    XSHIM oxdk_CreateDevice, Direct3D_CreateDevice
    XSHIM oxdk_Clear,        D3DDevice_Clear
    XSHIM oxdk_Present,      D3DDevice_Present
    XSHIM oxdk_DbgPrint,     DbgPrint
    XSHIM oxdk_Reboot,       HalReturnToFirmware
