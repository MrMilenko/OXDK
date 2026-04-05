// xdk_compat.h -- Compatibility shim for building Xbox XDK code with clang
//
// Force-include this file (-include xdk_compat.h) when cross-compiling
// MS XDK projects with clang + lld-link.

#ifndef _XDK_COMPAT_H
#define _XDK_COMPAT_H

// excpt.h defines EXCEPTION_DISPOSITION which ntdef.h needs.
#include <crt/excpt.h>

// Pull in base NT types and CONTEXT struct in the right order.
#include <ntdef.h>
#include <nti386.h>

// The NT kernel headers above define the core Win32 types (_CONTEXT,
// _LIST_ENTRY, _LARGE_INTEGER, etc.). Prevent the local XDK winnt.h
// from redefining them by setting its include guard.
#define _WINNT_

// Mark the Interlocked functions as already declared so winbase.h
// doesn't redeclare them with conflicting signatures.
#define _INTERLOCKED_DEFINED

#endif
