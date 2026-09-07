// Pass through the XDK <time.h> and add the POSIX `timespec` struct it lacks.
// libc++'s WIN32 thread backend declares __libcpp_timespec_t = ::timespec for
// timed waits; without it, threading support fails to compile.
#ifndef OXDK_CSHIM_TIME_H
#define OXDK_CSHIM_TIME_H

#include_next <time.h>

#ifndef _STRUCT_TIMESPEC
#define _STRUCT_TIMESPEC
struct timespec {
    long tv_sec;
    long tv_nsec;
};
#endif

#endif
