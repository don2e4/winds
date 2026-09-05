#include "winds.h"
#include "driver.h"

static void print_help(const char *prog_name) {
    printf("winds - Lightweight, Fast, Native C++ Compiler\n\n");
    printf("USAGE:\n");
    printf("  %s [options] <input.cpp>\n\n", prog_name);
    printf("OPTIONS:\n");
    printf("  -o <file>      Place the output into <file>\n");
    printf("  -S             Compile only; do not assemble or link (generate .s)\n");
    printf("  -c             Compile and assemble, but do not link (generate .o)\n");
    printf("  -O<level>      Optimization level (-O0, -O1, -O2) [default: -O1]\n");
    printf("  --emit-ast     Dump the parsed Abstract Syntax Tree\n");
    printf("  --emit-ir      Dump the 3-Address Code Intermediate Representation\n");
    printf("  -I <dir>                 Add directory to header include search path\n");
    printf("  --target=<triple>        Specify target architecture triple (default: x86_64-linux-gnu)\n");
    printf("  --sysroot=<path>         Specify system root directory for headers and libraries\n");
    printf("  --cross-prefix=<prefix>  Specify cross-toolchain prefix (e.g. x86_64-linux-gnu-)\n");
    printf("  --print-target-triple    Print target architecture triple and exit\n");
    printf("  -v, --verbose            Display compiler timing and build pipeline stages\n");
    printf("  -h, --help               Display this help information\n");
    printf("  --version                Display compiler version\n");
}

static void print_version(void) {
    printf("winds version %s (x86_64-linux)\n", WINDS_VERSION);
    printf("Target: x86_64-unknown-linux-gnu\n");
    printf("Thread model: posix\n");
}

int main(int argc, char **argv) {
    DriverConfig config = {0};
    config.opt_level = 1; /* Default to -O1 */

    if (argc < 2) {
        print_help(argv[0]);
        return 1;
    }

    bool print_target = false;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_help(argv[0]);
            return 0;
        } else if (strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        } else if (strcmp(arg, "--print-target-triple") == 0) {
            print_target = true;
        } else if (strncmp(arg, "--target=", 9) == 0) {
            config.target_triple = arg + 9;
        } else if (strcmp(arg, "--target") == 0 || strcmp(arg, "-target") == 0) {
            if (i + 1 < argc) {
                config.target_triple = argv[++i];
            } else {
                fprintf(stderr, "winds: error: missing argument to '%s'\n", arg);
                return 1;
            }
        } else if (strncmp(arg, "--sysroot=", 10) == 0) {
            config.sysroot = arg + 10;
        } else if (strcmp(arg, "--sysroot") == 0) {
            if (i + 1 < argc) {
                config.sysroot = argv[++i];
            } else {
                fprintf(stderr, "winds: error: missing argument to '--sysroot'\n");
                return 1;
            }
        } else if (strncmp(arg, "--cross-prefix=", 15) == 0) {
            config.cross_prefix = arg + 15;
        } else if (strcmp(arg, "--cross-prefix") == 0) {
            if (i + 1 < argc) {
                config.cross_prefix = argv[++i];
            } else {
                fprintf(stderr, "winds: error: missing argument to '--cross-prefix'\n");
                return 1;
            }
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            config.verbose = true;
        } else if (strcmp(arg, "-S") == 0) {
            config.emit_assembly = true;
        } else if (strcmp(arg, "-c") == 0) {
            config.compile_only = true;
        } else if (strcmp(arg, "--emit-ast") == 0) {
            config.emit_ast = true;
        } else if (strcmp(arg, "--emit-ir") == 0) {
            config.emit_ir = true;
        } else if (strcmp(arg, "-O0") == 0) {
            config.opt_level = 0;
        } else if (strcmp(arg, "-O1") == 0) {
            config.opt_level = 1;
        } else if (strcmp(arg, "-O2") == 0 || strcmp(arg, "-O3") == 0) {
            config.opt_level = 2;
        } else if (strcmp(arg, "-o") == 0) {
            if (i + 1 < argc) {
                config.output_file = argv[++i];
            } else {
                fprintf(stderr, "winds: error: missing argument to '-o'\n");
                return 1;
            }
        } else if (strncmp(arg, "-I", 2) == 0) {
            const char *inc_path = NULL;
            if (arg[2] != '\0') {
                inc_path = arg + 2;
            } else if (i + 1 < argc) {
                inc_path = argv[++i];
            } else {
                fprintf(stderr, "winds: error: missing argument to '-I'\n");
                return 1;
            }
            if (config.include_path_count < 64) {
                config.include_paths[config.include_path_count++] = inc_path;
            }
        } else if (arg[0] == '-') {
            fprintf(stderr, "winds: error: unrecognized command-line option '%s'\n", arg);
            return 1;
        } else {
            if (config.input_file) {
                fprintf(stderr, "winds: error: multiple input files are not supported in single invocation\n");
                return 1;
            }
            config.input_file = arg;
        }
    }

    if (print_target) {
        printf("%s\n", config.target_triple ? config.target_triple : "x86_64-linux-gnu");
        return 0;
    }

    return driver_run(&config);
}
