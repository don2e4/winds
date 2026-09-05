#ifndef WINDS_DRIVER_H
#define WINDS_DRIVER_H

#include "winds.h"

typedef struct {
    const char *input_file;
    const char *output_file;
    const char *include_paths[64];
    int include_path_count;
    bool emit_assembly; /* -S */
    bool compile_only;  /* -c */
    bool emit_ast;      /* --emit-ast */
    bool emit_ir;       /* --emit-ir */
    int opt_level;      /* -O0, -O1, -O2 */
    bool verbose;       /* -v */

    /* Cross-compilation settings */
    const char *target_triple; /* --target=<triple> */
    const char *sysroot;       /* --sysroot=<path> */
    const char *cross_prefix;  /* --cross-prefix=<prefix> */
} DriverConfig;

int driver_run(const DriverConfig *config);

#endif /* WINDS_DRIVER_H */
