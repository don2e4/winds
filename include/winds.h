#ifndef WINDS_H
#define WINDS_H

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <stdarg.h>

#define WINDS_VERSION "0.1.0"

/* Source code location for diagnostics and AST nodes */
typedef struct {
    const char *file;
    const char *source_line; /* Pointer to start of line in source */
    int line;
    int col;
    int length; /* Token length in characters for underline */
} SourceLoc;

/* Forward declarations */
typedef struct Arena Arena;
typedef struct Token Token;
typedef struct Type Type;
typedef struct ASTNode ASTNode;
typedef struct Symbol Symbol;
typedef struct Scope Scope;
typedef struct IRModule IRModule;

/* Diagnostic severity */
typedef enum {
    DIAG_NOTE,
    DIAG_WARNING,
    DIAG_ERROR,
    DIAG_FATAL
} DiagLevel;

/* Diagnostic state and functions */
void diag_init(bool use_colors);
void diag_set_warnings_as_errors(bool enable);
void diag_set_color_mode(const char *mode);
void diag_report(DiagLevel level, SourceLoc loc, const char *fmt, ...);
void diag_help(const char *fmt, ...);
void diag_note(SourceLoc loc, const char *fmt, ...);
int diag_levenshtein(const char *s1, const char *s2);
const char *diag_find_closest(const char *target, const char **candidates, int count);
int diag_error_count(void);
int diag_warning_count(void);
void diag_reset(void);

#endif /* WINDS_H */
