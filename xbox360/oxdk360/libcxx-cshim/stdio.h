// Pass through the XDK <stdio.h>, then declare the C99 functions its CRT does not.
// libc++'s <cstdio> imports these into std:: with using-if-exists, so a declaration
// here is enough for the C++ side; the definitions live in oxdk360/builtins.
#ifndef OXDK360_CSHIM_STDIO_H
#define OXDK360_CSHIM_STDIO_H

#include_next <stdio.h>
#include <locale.h>

#ifdef __cplusplus
extern "C" {
#endif

int __cdecl snprintf(char *buf, size_t n, const char *fmt, ...);
int __cdecl vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);

/* libc++'s Windows locale backend formats through this one UCRT internal, which the
   XDK's CRT predates by a decade. The options word selects C99 return semantics; the
   implementation in oxdk360/libcxx-shim provides them over what the XDK does have. */
#define _CRT_INTERNAL_LOCAL_PRINTF_OPTIONS 0
#define _CRT_INTERNAL_PRINTF_STANDARD_SNPRINTF_BEHAVIOR 1
int __cdecl __stdio_common_vsprintf(unsigned long long options, char *buf, size_t n,
                                    const char *fmt, _locale_t loc, va_list ap);

#ifdef __cplusplus
}
#endif

#endif
