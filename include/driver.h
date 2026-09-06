#ifndef WINDS_DRIVER_H
#define WINDS_DRIVER_H

#include "winds.h"

#define MAX_DRIVER_INPUTS 128
#define MAX_DRIVER_OPTIONS 128

typedef struct {
    const char *input_file;
    const char *input_files[MAX_DRIVER_INPUTS];
    int input_file_count;
    const char *link_inputs[MAX_DRIVER_INPUTS];
    int link_input_count;
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

    /* Dependency generation options */
    bool gen_dependencies;       /* -MMD, -MD */
    bool phony_targets;          /* -MP */
    const char *dep_output_file; /* -MF <file> */

    /* Warning & diagnostic controls */
    bool warnings_as_errors;     /* -Werror */
    bool warn_all;               /* -Wall */
    bool warn_extra;             /* -Wextra */
    const char *color_diagnostics; /* -fdiagnostics-color=... */

    /* Preprocessor command-line controls */
    const char *defines[MAX_DRIVER_OPTIONS];
    int define_count;
    const char *undefines[MAX_DRIVER_OPTIONS];
    int undefine_count;

    /* Arguments passed to the system linker driver (-L, -l, -Wl,...) */
    const char *link_args[MAX_DRIVER_OPTIONS];
    int link_arg_count;

    /* Script execution mode */
    bool run_mode;               /* -run */
    int run_argc;
    char **run_argv;
} DriverConfig;

int driver_run(const DriverConfig *config);
int driver_run_many(const DriverConfig *config);

#endif /* WINDS_DRIVER_H */
