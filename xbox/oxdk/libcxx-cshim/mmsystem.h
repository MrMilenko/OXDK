// XDK has no <mmsystem.h>. Port code uses timeGetTime() and friends which
// map onto the kernel's GetTickCount on Xbox. Pull in xtl.h for the types
// and route timeGetTime() to GetTickCount via a macro, matching how the
// XDK's own DSound.h does it, so the two never compete and recurse.
#ifndef OXDK_CSHIM_MMSYSTEM_H
#define OXDK_CSHIM_MMSYSTEM_H

#include <xtl.h>

#ifndef timeGetTime
#define timeGetTime GetTickCount
#endif

#endif
