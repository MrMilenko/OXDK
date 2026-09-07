// Pass through the XDK <ctype.h> and add the C99 isblank it predates.
//
// libc++'s <cctype> does `using ::isblank` and then declares its own locale-aware
// isblank template in std. When the C function does not exist the using declaration
// still occupies the name and the template is rejected as conflicting with it, so a
// header that pulls in <locale> stops compiling. Defining the C function settles it.
#ifndef OXDK360_CSHIM_CTYPE_H
#define OXDK360_CSHIM_CTYPE_H

#include_next <ctype.h>

#ifndef _OXDK360_HAVE_ISBLANK
#define _OXDK360_HAVE_ISBLANK
static __inline int isblank(int c) { return c == ' ' || c == '\t'; }
#endif

#endif
