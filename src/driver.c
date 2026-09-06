#include "driver.h"
#include "arena.h"
#include "str.h"
#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "sema.h"
#include "ir.h"
#include "opt.h"
#include "codegen_x86.h"
#include <time.h>
#include <unistd.h>

static char *read_file_to_string(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "winds: error: cannot open file '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        return NULL;
    }

    char *buf = malloc(size + 1);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "winds: error: out of memory reading file\n");
        return NULL;
    }

    size_t read_bytes = fread(buf, 1, size, f);
    buf[read_bytes] = '\0';
    fclose(f);

    if (out_size) *out_size = read_bytes;
    return buf;
}

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static bool is_supported_x86_64(const char *triple) {
    if (!triple) return true;
    if (strncmp(triple, "x86_64", 6) == 0 ||
        strncmp(triple, "amd64", 5) == 0 ||
        strncmp(triple, "x86-64", 6) == 0) {
        return true;
    }
    return false;
}

static void emit_dependency_file(const DriverConfig *config, Lexer *lexer) {
    if (!config->gen_dependencies || !config->input_file) return;

    char dep_file[1024];
    if (config->dep_output_file && config->dep_output_file[0] != '\0') {
        snprintf(dep_file, sizeof(dep_file), "%s", config->dep_output_file);
    } else if (config->output_file && config->output_file[0] != '\0') {
        snprintf(dep_file, sizeof(dep_file), "%s", config->output_file);
        char *dot = strrchr(dep_file, '.');
        if (dot) {
            strcpy(dot, ".d");
        } else {
            strncat(dep_file, ".d", sizeof(dep_file) - strlen(dep_file) - 1);
        }
    } else {
        snprintf(dep_file, sizeof(dep_file), "%s", config->input_file);
        char *dot = strrchr(dep_file, '.');
        if (dot) {
            strcpy(dot, ".d");
        } else {
            strncat(dep_file, ".d", sizeof(dep_file) - strlen(dep_file) - 1);
        }
    }

    const char *target = config->output_file ? config->output_file : "a.out";

    FILE *df = fopen(dep_file, "w");
    if (!df) {
        fprintf(stderr, "winds: error: failed to create dependency file '%s'\n", dep_file);
        return;
    }

    fprintf(df, "%s: %s", target, config->input_file);
    const char **inc_files = NULL;
    int inc_count = lexer_get_included_files(lexer, &inc_files);
    for (int i = 0; i < inc_count; i++) {
        fprintf(df, " \\\n  %s", inc_files[i]);
    }
    fprintf(df, "\n");

    if (config->phony_targets) {
        for (int i = 0; i < inc_count; i++) {
            fprintf(df, "\n%s:\n", inc_files[i]);
        }
    }

    fclose(df);
}

int driver_run(const DriverConfig *config) {
    if (!config->input_file) {
        fprintf(stderr, "winds: error: no input file specified\n");
        return 1;
    }

    if (config->target_triple && !is_supported_x86_64(config->target_triple)) {
        fprintf(stderr, "winds: error: unsupported target architecture in '%s' (winds currently supports x86_64/amd64)\n",
                config->target_triple);
        return 1;
    }

    double t_start = get_time_ms();

    size_t source_size = 0;
    char *source = read_file_to_string(config->input_file, &source_size);
    if (!source) return 1;

    Arena *arena = arena_create(128 * 1024);
    str_intern_init();
    diag_init(true);
    if (config->warnings_as_errors) {
        diag_set_warnings_as_errors(true);
    }
    if (config->color_diagnostics) {
        diag_set_color_mode(config->color_diagnostics);
    }
    type_system_init(arena);

    /* 1. Parse */
    Parser parser;
    parser_init(&parser, arena, source, config->input_file);
    for (int i = 0; i < config->include_path_count; i++) {
        parser_add_include_path(&parser, config->include_paths[i]);
    }

    char exe_buf[512];
    ssize_t exe_len = readlink("/proc/self/exe", exe_buf, sizeof(exe_buf) - 1);
    if (exe_len > 0) {
        exe_buf[exe_len] = '\0';
        char *last_slash = strrchr(exe_buf, '/');
        if (last_slash) {
            *last_slash = '\0';
            char std_inc[1024];
            snprintf(std_inc, sizeof(std_inc), "%s/../include/winds/std", exe_buf);
            if (access(std_inc, F_OK) == 0) {
                parser_add_include_path(&parser, arena_strdup(arena, std_inc));
            }
        }
    }
    if (access("include/winds/std", F_OK) == 0) {
        parser_add_include_path(&parser, arena_strdup(arena, "include/winds/std"));
    }

    if (config->sysroot && config->sysroot[0] != '\0') {
        char path1[512];
        char path2[512];
        char path3[512];
        snprintf(path1, sizeof(path1), "%s/usr/include", config->sysroot);
        snprintf(path2, sizeof(path2), "%s/usr/include/x86_64-linux-gnu", config->sysroot);
        snprintf(path3, sizeof(path3), "%s/include", config->sysroot);
        if (access(path1, F_OK) == 0) {
            char *p1 = arena_alloc(arena, strlen(path1) + 1);
            strcpy(p1, path1);
            parser_add_include_path(&parser, p1);
        }
        if (access(path2, F_OK) == 0) {
            char *p2 = arena_alloc(arena, strlen(path2) + 1);
            strcpy(p2, path2);
            parser_add_include_path(&parser, p2);
        }
        if (access(path3, F_OK) == 0) {
            char *p3 = arena_alloc(arena, strlen(path3) + 1);
            strcpy(p3, path3);
            parser_add_include_path(&parser, p3);
        }
    }

    ASTNode *ast = parser_parse(&parser);

    if (diag_error_count() > 0) {
        fprintf(stderr, "winds: compilation stopped with %d errors during parsing\n", diag_error_count());
        parser_destroy(&parser);
        free(source);
        arena_destroy(arena);
        str_intern_destroy();
        return 1;
    }

    if (config->emit_ast) {
        printf("=== AST FOR %s ===\n", config->input_file);
        ast_dump(ast, 0);
        parser_destroy(&parser);
        free(source);
        arena_destroy(arena);
        str_intern_destroy();
        return 0;
    }

    /* 2. Semantic Analysis */
    Sema sema;
    sema_init(&sema, arena);
    sema.warn_unused = config->warn_all || config->warn_extra || config->warnings_as_errors;
    if (!sema_analyze(&sema, ast) || diag_error_count() > 0) {
        fprintf(stderr, "winds: compilation stopped with %d errors during semantic analysis\n", diag_error_count());
        parser_destroy(&parser);
        free(source);
        arena_destroy(arena);
        str_intern_destroy();
        return 1;
    }

    /* 3. Lower to IR */
    IRModule *ir_mod = ir_module_create(arena);
    ir_build_from_ast(ir_mod, ast);

    /* 4. Optimization */
    if (config->opt_level > 0) {
        OptOptions opt_opts = {
            .level = config->opt_level,
            .enable_const_fold = true,
            .enable_const_prop = true,
            .enable_copy_prop = true,
            .enable_algebraic = true,
            .enable_cfg_opt = true,
            .enable_cfg_simplify = true,
            .enable_unreachable = true,
            .enable_dce = true
        };
        opt_run_pipeline(ir_mod, opt_opts);
    }

    if (config->emit_ir) {
        ir_dump(ir_mod);
        parser_destroy(&parser);
        free(source);
        arena_destroy(arena);
        str_intern_destroy();
        return 0;
    }

    /* 5. Assembly Code Generation */
    const char *asm_file = NULL;
    char tmp_asm[256];
    bool is_temp_asm = false;

    if (config->emit_assembly) {
        asm_file = config->output_file ? config->output_file : "a.s";
    } else {
        snprintf(tmp_asm, sizeof(tmp_asm), "/tmp/winds_%d.s", getpid());
        asm_file = tmp_asm;
        is_temp_asm = true;
    }

    FILE *asm_out = fopen(asm_file, "w");
    if (!asm_out) {
        fprintf(stderr, "winds: error: failed to create assembly output file '%s'\n", asm_file);
        parser_destroy(&parser);
        free(source);
        arena_destroy(arena);
        str_intern_destroy();
        return 1;
    }

    codegen_x86_emit(ir_mod, asm_out);
    fclose(asm_out);

    double t_compiled = get_time_ms();

    if (config->verbose) {
        printf("winds: parsed and compiled in %.2f ms (source: %zu bytes)\n",
               t_compiled - t_start, source_size);
    }

    if (config->emit_assembly) {
        emit_dependency_file(config, &parser.lexer);
        /* Done emitting assembly (-S) */
        parser_destroy(&parser);
        free(source);
        arena_destroy(arena);
        str_intern_destroy();
        return 0;
    }

    /* Resolve assembler and linker binaries */
    char as_bin[256] = "as";
    char gcc_bin[256] = "gcc";

    if (config->cross_prefix && config->cross_prefix[0] != '\0') {
        snprintf(as_bin, sizeof(as_bin), "%sas", config->cross_prefix);
        snprintf(gcc_bin, sizeof(gcc_bin), "%sgcc", config->cross_prefix);
    } else if (config->target_triple && config->target_triple[0] != '\0') {
        char target_as[256];
        char target_gcc[256];
        snprintf(target_as, sizeof(target_as), "%s-as", config->target_triple);
        snprintf(target_gcc, sizeof(target_gcc), "%s-gcc", config->target_triple);

        char check_cmd[300];
        snprintf(check_cmd, sizeof(check_cmd), "command -v %s >/dev/null 2>&1", target_gcc);
        if (system(check_cmd) == 0) {
            snprintf(as_bin, sizeof(as_bin), "%s", target_as);
            snprintf(gcc_bin, sizeof(gcc_bin), "%s", target_gcc);
        } else if (config->verbose) {
            printf("winds: note: cross toolchain '%s' not found; using host '%s'\n",
                   target_gcc, gcc_bin);
        }
    }

    /* 6. Assemble or Link */
    if (config->compile_only) {
        const char *obj_file = config->output_file ? config->output_file : "a.o";
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "%s %s -o %s", as_bin, asm_file, obj_file);
        if (config->verbose) {
            printf("winds: executing: %s\n", cmd);
        }
        int ret = system(cmd);
        if (ret != 0) {
            fprintf(stderr, "winds: error: assembler failed with exit code %d\n", ret);
        } else {
            emit_dependency_file(config, &parser.lexer);
        }
        if (is_temp_asm) unlink(asm_file);
        parser_destroy(&parser);
        free(source);
        arena_destroy(arena);
        str_intern_destroy();
        return ret;
    }

    /* Full executable link or direct run via gcc driver */
    char tmp_run_bin[256];
    bool is_temp_run_bin = false;
    const char *out_binary = NULL;

    if (config->run_mode) {
        snprintf(tmp_run_bin, sizeof(tmp_run_bin), "/tmp/winds_run_%d", getpid());
        out_binary = tmp_run_bin;
        is_temp_run_bin = true;
    } else {
        out_binary = config->output_file ? config->output_file : "a.out";
    }

    char cmd[1024];
    if (config->sysroot && config->sysroot[0] != '\0') {
        snprintf(cmd, sizeof(cmd), "%s %s -o %s --sysroot=%s -no-pie -lm",
                 gcc_bin, asm_file, out_binary, config->sysroot);
    } else {
        snprintf(cmd, sizeof(cmd), "%s %s -o %s -no-pie -lm",
                 gcc_bin, asm_file, out_binary);
    }
    if (config->verbose) {
        printf("winds: executing: %s\n", cmd);
    }
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "winds: error: linker failed with exit code %d\n", ret);
        if (is_temp_asm) unlink(asm_file);
        if (is_temp_run_bin) unlink(tmp_run_bin);
        parser_destroy(&parser);
        free(source);
        arena_destroy(arena);
        str_intern_destroy();
        return ret;
    }

    if (is_temp_asm) {
        unlink(asm_file);
    }

    emit_dependency_file(config, &parser.lexer);

    if (config->run_mode) {
        char run_cmd[2048];
        int written = snprintf(run_cmd, sizeof(run_cmd), "%s", out_binary);
        for (int i = 0; i < config->run_argc && written < (int)sizeof(run_cmd) - 2; i++) {
            written += snprintf(run_cmd + written, sizeof(run_cmd) - written, " %s", config->run_argv[i]);
        }
        if (config->verbose) {
            printf("winds: executing script: %s\n", run_cmd);
        }
        int script_ret = system(run_cmd);
        unlink(tmp_run_bin);
        if (script_ret != 0 && (script_ret >> 8) != 0) {
            script_ret = (script_ret >> 8);
        }
        ret = script_ret;
    }

    double t_total = get_time_ms();
    if (config->verbose) {
        printf("winds: completed build to '%s' in %.2f ms\n", out_binary, t_total - t_start);
    }

    parser_destroy(&parser);
    free(source);
    arena_destroy(arena);
    str_intern_destroy();
    return ret;
}
