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

int driver_run(const DriverConfig *config) {
    if (!config->input_file) {
        fprintf(stderr, "winds: error: no input file specified\n");
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

    /* 6. Assemble or Link */
    if (config->compile_only) {
        const char *obj_file = config->output_file ? config->output_file : "a.o";
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "as %s -o %s", asm_file, obj_file);
        int ret = system(cmd);
        if (is_temp_asm) unlink(asm_file);
        free(source);
        arena_destroy(arena);
        str_intern_destroy();
        return ret;
    }

    /* Full executable link via gcc driver */
    const char *out_binary = config->output_file ? config->output_file : "a.out";
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "gcc %s -o %s -no-pie -lm", asm_file, out_binary);
    int ret = system(cmd);

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
