#ifndef winds_string_dot_h
#define winds_string_dot_h

#include <cstring>

#ifdef __cplusplus
extern "C" {
#endif

char *strerror(int);
void *memchr(const void *, int, size_t);

#ifdef __cplusplus
}
#endif

#endif
