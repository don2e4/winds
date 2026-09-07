#include <stdlib.h>
#include <stdio.h>
#undef assert
#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr) ((expr) ? (void)0 : (puts(#expr), abort()))
#endif
