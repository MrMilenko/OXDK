// The XDK does not ship <windows.h>; <xtl.h> is the Xbox 360 equivalent and pulls in
// the same Win32 surface ported code expects. This lets upstream code that includes
// <windows.h> build unmodified.
#ifndef OXDK360_CSHIM_WINDOWS_H
#define OXDK360_CSHIM_WINDOWS_H
#include <xtl.h>
#include <stdarg.h>

/* The 360 has no FormatMessage: there is no system message table on a console. libc++'s
   system_error formats its what() through it, so oxdk360/libcxx-shim supplies a version
   that reports the error number. The flags are the ones that call site passes. */
#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_IGNORE_INSERTS  0x00000200
#define FORMAT_MESSAGE_FROM_SYSTEM     0x00001000

#ifdef __cplusplus
extern "C"
#endif
unsigned long __stdcall FormatMessageA(unsigned long flags, const void *source,
                                       unsigned long messageId, unsigned long languageId,
                                       char *buffer, unsigned long size, va_list *args);

#endif
