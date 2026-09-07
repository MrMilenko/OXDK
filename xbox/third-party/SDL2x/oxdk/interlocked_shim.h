/* oxdk_interlocked_shim.h
 *
 * Force-included before SDL2 source files. Declares the Interlocked*
 * intrinsic family without dragging in clang's <intrin.h> (which pulls
 * in SSE3 intrinsic headers we can't compile on Pentium III) and without
 * relying on winbase.h's declarations (which are gated by !_NTOS_ and
 * conflict with clang's intrinsic versions when active).
 *
 * clang -target i386-pc-windows-msvc treats these as MSVC builtins, so
 * the declarations here just satisfy the type checker; codegen uses the
 * intrinsic. _ReadWriteBarrier is also a clang builtin.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

long _InterlockedIncrement(long volatile *Addend);
long _InterlockedDecrement(long volatile *Addend);
long _InterlockedExchange(long volatile *Target, long Value);
long _InterlockedExchangeAdd(long volatile *Addend, long Value);
long _InterlockedCompareExchange(long volatile *Destination,
                                 long Exchange, long Comparand);

void _ReadWriteBarrier(void);

#ifdef __cplusplus
}
#endif

/* Friendly aliases for code that uses the un-underscored names. */
#ifndef InterlockedIncrement
#define InterlockedIncrement       _InterlockedIncrement
#endif
#ifndef InterlockedDecrement
#define InterlockedDecrement       _InterlockedDecrement
#endif
#ifndef InterlockedExchange
#define InterlockedExchange        _InterlockedExchange
#endif
#ifndef InterlockedExchangeAdd
#define InterlockedExchangeAdd     _InterlockedExchangeAdd
#endif
#ifndef InterlockedCompareExchange
#define InterlockedCompareExchange _InterlockedCompareExchange
#endif
