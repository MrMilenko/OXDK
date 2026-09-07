// The XDK does not ship <windows.h>; <xtl.h> is the Xbox equivalent and pulls
// in the same NT / Win32 surface ported code typically needs. This shim lets
// upstream code that does `#include <windows.h>` build unmodified.
#ifndef OXDK_CSHIM_WINDOWS_H
#define OXDK_CSHIM_WINDOWS_H
#include <xtl.h>
#endif
