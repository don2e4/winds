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
    type_system_init(arena);

    /* 1. Parse */
    Parser parser;
    parser_init(&parser, arena, source, config->input_file);
    for (int i = 0; i < config->include_path_count; i++) {
        parser_add_include_path(&parser, config->include_paths[i]);
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
        free(source);
        arena_destroy(arena);
        str_intern_destroy();
        return 1;
    }

    if (config->emit_ast) {
        printf("=== AST FOR %s ===\n", config->input_file);
        ast_dump(ast, 0);
        free(source);
        arena_destroy(arena);
        str_intern_destroy();
        return 0;
    }

    /* 2. Semantic Analysis */
    Sema sema;
    sema_init(&sema, arena);
    if (!sema_analyze(&sema, ast)) {
        fprintf(stderr, "winds: compilation stopped with %d errors during semantic analysis\n", diag_error_count());
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
        /* Done emitting assembly (-S) */
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
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "%s %s -o %s", as_bin, asm_file, obj_file);
        if (config->verbose) {
            printf("winds: executing: %s\n", cmd);
        }
        int ret = system(cmd);
        if (ret != 0) {
            fprintf(stderr, "winds: error: assembler failed with exit code %d\n", ret);
        }
        if (is_temp_asm) unlink(asm_file);
        free(source);
        arena_destroy(arena);
        str_intern_destroy();
        return ret;
    }

    /* Full executable link via gcc driver */
    const char *out_binary = config->output_file ? config->output_file : "a.out";
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
    }

    if (is_temp_asm) {
        unlink(asm_file);
    }

    double t_total = get_time_ms();
    if (config->verbose) {
        printf("winds: completed build to '%s' in %.2f ms\n", out_binary, t_total - t_start);
    }

    free(source);
    arena_destroy(arena);
    str_intern_destroy();
    return ret;
}
