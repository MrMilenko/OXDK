// Pass through the XDK <stdio.h>, then add the C99 functions the MSVC 7.1
// CRT does not ship. libc++'s <cstdio> imports these into std:: via
// using-if-exists, so adding the declaration here is enough.
#ifndef OXDK_CSHIM_STDIO_H
#define OXDK_CSHIM_STDIO_H

#include_next <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

int __cdecl snprintf(char *buf, size_t n, const char *fmt, ...);
int __cdecl vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);

// XDK <stdio.h> declares _vsnprintf without an explicit calling convention.
// Under -fdefault-calling-conv=stdcall that mangles to _vsnprintf@16, which
// the CRT (cdecl) doesn't export. Redeclare here to lock in __cdecl.
int __cdecl _vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);

#ifdef __cplusplus
}
#endif

#endif
