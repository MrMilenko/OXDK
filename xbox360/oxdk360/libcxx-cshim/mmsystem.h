// The XDK has no <mmsystem.h>. Ported code wants timeGetTime, which on the 360 is
// GetTickCount. Routed by macro so this never competes with a real declaration.
#ifndef OXDK360_CSHIM_MMSYSTEM_H
#define OXDK360_CSHIM_MMSYSTEM_H

#include <xtl.h>

#ifndef timeGetTime
#define timeGetTime GetTickCount
#endif

#endif
