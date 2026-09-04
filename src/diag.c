#include "winds.h"
#include <unistd.h>

static int g_errors = 0;
static int g_warnings = 0;
static bool g_use_colors = true;

#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_RED     "\033[1;31m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_CYAN    "\033[1;36m"
#define COLOR_WHITE   "\033[1;37m"

void diag_init(bool use_colors) {
    g_errors = 0;
    g_warnings = 0;
    g_use_colors = use_colors && isatty(fileno(stderr));
}

int diag_error_count(void) {
    return g_errors;
}

int diag_warning_count(void) {
    return g_warnings;
}

void diag_reset(void) {
    g_errors = 0;
    g_warnings = 0;
}

void diag_report(DiagLevel level, SourceLoc loc, const char *fmt, ...) {
    const char *prefix_str = "note";
    const char *color = COLOR_CYAN;

    switch (level) {
        case DIAG_NOTE:
            prefix_str = "note";
            color = COLOR_CYAN;
            break;
        case DIAG_WARNING:
            prefix_str = "warning";
            color = COLOR_YELLOW;
            g_warnings++;
            break;
        case DIAG_ERROR:
            prefix_str = "error";
            color = COLOR_RED;
            g_errors++;
            break;
        case DIAG_FATAL:
            prefix_str = "fatal error";
            color = COLOR_RED;
            g_errors++;
            break;
    }

    if (g_use_colors) {
        if (loc.file) {
            fprintf(stderr, "%s%s:%d:%d: %s%s:%s ", COLOR_BOLD, loc.file, loc.line, loc.col,
                    color, prefix_str, COLOR_RESET);
        } else {
            fprintf(stderr, "%swinds: %s%s:%s ", COLOR_BOLD, color, prefix_str, COLOR_RESET);
        }
    } else {
        if (loc.file) {
            fprintf(stderr, "%s:%d:%d: %s: ", loc.file, loc.line, loc.col, prefix_str);
        } else {
            fprintf(stderr, "winds: %s: ", prefix_str);
        }
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");

    /* Print source snippet with caret indicator if available */
    if (loc.source_line && loc.col > 0) {
        /* Print line prefix */
        fprintf(stderr, "%5d | ", loc.line);
        const char *p = loc.source_line;
        while (*p && *p != '\n' && *p != '\r') {
            fputc(*p, stderr);
            p++;
        }
        fputc('\n', stderr);

        /* Caret */
        fprintf(stderr, "      | ");
        for (int i = 1; i < loc.col; i++) {
            fputc(' ', stderr);
        }
        if (g_use_colors) {
            fprintf(stderr, "%s^%s\n", COLOR_BOLD COLOR_RED, COLOR_RESET);
        } else {
            fprintf(stderr, "^\n");
        }
    }

    if (level == DIAG_FATAL) {
        exit(EXIT_FAILURE);
    }
}
