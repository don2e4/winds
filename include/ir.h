#ifndef WINDS_IR_H
#define WINDS_IR_H

#include "winds.h"
#include "arena.h"
#include "ast.h"
#include "sema.h"

typedef enum {
    IR_LABEL,
    IR_IMM,
    IR_STR,
    IR_MOV,
    IR_LOAD,
    IR_STORE,
    IR_LOAD_STACK,
    IR_STORE_STACK,
    IR_ADDR_STACK,
    IR_ADD,
    IR_SUB,
    IR_MUL,
    IR_DIV,
    IR_MOD,
    IR_AND,
    IR_OR,
    IR_XOR,
    IR_SHL,
    IR_SHR,
    IR_CMP_EQ,
    IR_CMP_NE,
    IR_CMP_LT,
    IR_CMP_LE,
    IR_CMP_GT,
    IR_CMP_GE,
    IR_JMP,
    IR_JMP_IF_ZERO,
    IR_JMP_IF_NOT_ZERO,
    IR_CALL,
    IR_RET
} IROp;

typedef struct {
    int vreg;           /* Virtual register ID */
    int64_t imm;        /* Immediate value */
    const char *label;  /* String or label identifier */
    int offset;         /* Stack/struct offset */
} IROperand;

typedef struct IRInst {
    IROp op;
    IROperand dest;
    IROperand src1;
    IROperand src2;
    IROperand *call_args;
    int call_arg_count;
    int size; /* Load/store size in bytes (1, 4, 8) */
    struct IRInst *next;
    struct IRInst *prev;
} IRInst;

typedef struct IRFunction {
    const char *name;
    const char *mangled_name;
    int stack_size;
    IRInst *first_inst;
    IRInst *last_inst;
    int vreg_count;
    struct IRFunction *next;
} IRFunction;

typedef struct IRStringLiteral {
    const char *label;
    const char *data;
    size_t len;
    struct IRStringLiteral *next;
} IRStringLiteral;

struct IRModule {
    Arena *arena;
    IRFunction *functions;
    IRStringLiteral *strings;
    int label_counter;
    int str_counter;
};

IRModule *ir_module_create(Arena *arena);
IRFunction *ir_function_create(IRModule *mod, const char *name, const char *mangled_name, int stack_size);
void ir_emit(IRFunction *fn, IRInst *inst);
void ir_build_from_ast(IRModule *mod, ASTNode *program);
void ir_dump(IRModule *mod);

#endif /* WINDS_IR_H */
