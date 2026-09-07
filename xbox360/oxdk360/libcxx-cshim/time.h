// Pass through the XDK <time.h> and add the POSIX timespec it lacks.
// libc++'s WIN32 thread backend uses __libcpp_timespec_t = ::timespec for timed
// waits, and without it threading support does not compile.
#ifndef OXDK360_CSHIM_TIME_H
#define OXDK360_CSHIM_TIME_H

#include_next <time.h>

#ifndef _STRUCT_TIMESPEC
#define _STRUCT_TIMESPEC
struct timespec {
    long tv_sec;
    long tv_nsec;
};
#endif

#endif
