// Our own variadic function, which must read arguments from the same places a
// Microsoft-compiled caller writes them. Quake has fifteen of these.
#include <stdarg.h>
int sum(int n, ...)
{
    va_list ap; int t = 0, i;
    va_start(ap, n);
    for (i = 0; i < n; i++) t += va_arg(ap, int);
    va_end(ap);
    return t;
}
