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
#define COLOR_BLUE    "\033[1;34m"
#define COLOR_GREEN   "\033[1;32m"
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

static void print_source_snippet(SourceLoc loc, const char *accent_color) {
    if (!loc.source_line || loc.line <= 0) return;

    /* Expand tabs for exact caret positioning */
    char expanded_line[1024];
    int exp_idx = 0;
    int col_in_expanded = 1;
    int target_col = loc.col > 0 ? loc.col : 1;
    int curr_col = 1;

    const char *p = loc.source_line;
    while (*p && *p != '\n' && *p != '\r' && exp_idx < (int)sizeof(expanded_line) - 8) {
        if (curr_col == target_col) {
            col_in_expanded = exp_idx + 1;
        }
        if (*p == '\t') {
            int spaces = 4 - (exp_idx % 4);
            for (int s = 0; s < spaces; s++) {
                expanded_line[exp_idx++] = ' ';
            }
        } else {
            expanded_line[exp_idx++] = *p;
        }
        curr_col++;
        p++;
    }
    expanded_line[exp_idx] = '\0';
    if (curr_col <= target_col) {
        col_in_expanded = exp_idx + 1;
    }

    const char *bar_color = g_use_colors ? COLOR_BLUE : "";
    const char *reset_color = g_use_colors ? COLOR_RESET : "";
    const char *bold_color = g_use_colors ? COLOR_BOLD : "";

    /* Print source snippet */
    fprintf(stderr, "%s    |%s\n", bar_color, reset_color);
    fprintf(stderr, "%s%4d|%s %s\n", bold_color, loc.line, reset_color, expanded_line);

    /* Print underline / caret */
    fprintf(stderr, "%s    |%s ", bar_color, reset_color);
    for (int i = 1; i < col_in_expanded; i++) {
        fputc(' ', stderr);
    }

    int span_len = loc.length > 0 ? loc.length : 1;
    if (g_use_colors) {
        fprintf(stderr, "%s", accent_color);
    }
    for (int i = 0; i < span_len; i++) {
        fputc('^', stderr);
    }
    if (g_use_colors) {
        fprintf(stderr, "%s", COLOR_RESET);
    }
    fprintf(stderr, "\n");
}

void diag_report(DiagLevel level, SourceLoc loc, const char *fmt, ...) {
    const char *prefix_str = "error";
    const char *color = COLOR_RED;

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

    /* Print header: error: <message> */
    if (g_use_colors) {
        fprintf(stderr, "%s%s:%s ", color, prefix_str, COLOR_BOLD COLOR_WHITE);
    } else {
        fprintf(stderr, "%s: ", prefix_str);
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    if (g_use_colors) {
        fprintf(stderr, "%s\n", COLOR_RESET);
    } else {
        fprintf(stderr, "\n");
    }

    /* Print file location: --> file:line:col */
    if (loc.file) {
        if (g_use_colors) {
            fprintf(stderr, "  %s-->%s %s:%d:%d\n", COLOR_BLUE, COLOR_RESET, loc.file, loc.line, loc.col);
        } else {
            fprintf(stderr, "  --> %s:%d:%d\n", loc.file, loc.line, loc.col);
        }
        print_source_snippet(loc, color);
    }

    if (level == DIAG_FATAL) {
        exit(EXIT_FAILURE);
    }
}

void diag_help(const char *fmt, ...) {
    if (g_use_colors) {
        fprintf(stderr, "   %s= help:%s ", COLOR_GREEN, COLOR_RESET);
    } else {
        fprintf(stderr, "   = help: ");
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

void diag_note(SourceLoc loc, const char *fmt, ...) {
    if (g_use_colors) {
        fprintf(stderr, "   %s= note:%s ", COLOR_CYAN, COLOR_RESET);
    } else {
        fprintf(stderr, "   = note: ");
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");

    if (loc.file) {
        if (g_use_colors) {
            fprintf(stderr, "  %s-->%s %s:%d:%d\n", COLOR_BLUE, COLOR_RESET, loc.file, loc.line, loc.col);
        } else {
            fprintf(stderr, "  --> %s:%d:%d\n", loc.file, loc.line, loc.col);
        }
        print_source_snippet(loc, COLOR_CYAN);
    }
}

int diag_levenshtein(const char *s1, const char *s2) {
    if (!s1 || !s2) return 999;
    int len1 = (int)strlen(s1);
    int len2 = (int)strlen(s2);

    if (len1 > 128 || len2 > 128) return 999;

    int d[130][130];
    for (int i = 0; i <= len1; i++) d[i][0] = i;
    for (int j = 0; j <= len2; j++) d[0][j] = j;

    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            int cost = (tolower((unsigned char)s1[i - 1]) == tolower((unsigned char)s2[j - 1])) ? 0 : 1;
            int del = d[i - 1][j] + 1;
            int ins = d[i][j - 1] + 1;
            int sub = d[i - 1][j - 1] + cost;
            int min = del < ins ? del : ins;
            d[i][j] = min < sub ? min : sub;
        }
    }
    return d[len1][len2];
}

const char *diag_find_closest(const char *target, const char **candidates, int count) {
    if (!target || !candidates || count <= 0) return NULL;
    int best_dist = 999;
    const char *best_match = NULL;
    int target_len = (int)strlen(target);

    for (int i = 0; i < count; i++) {
        if (!candidates[i]) continue;
        int dist = diag_levenshtein(target, candidates[i]);
        /* Only consider candidates with distance <= 3 and within reasonable ratio */
        int threshold = target_len <= 4 ? 2 : 3;
        if (dist <= threshold && dist < best_dist) {
            best_dist = dist;
            best_match = candidates[i];
        }
    }
    return best_match;
}
