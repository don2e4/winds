#ifndef WINDS_STR_H
#define WINDS_STR_H

#include "winds.h"
#include "arena.h"

/* Initializes the global string interner with dedicated arena */
void str_intern_init(void);

/* Interns a null-terminated string */
const char *str_intern(const char *str);

/* Interns a string from pointer range [start, end) */
const char *str_intern_range(const char *start, const char *end);

/* Cleans up the string interning table and memory */
void str_intern_destroy(void);

#endif /* WINDS_STR_H */
