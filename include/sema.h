#ifndef WINDS_SEMA_H
#define WINDS_SEMA_H

#include "winds.h"
#include "arena.h"
#include "ast.h"

typedef enum {
    SYM_VAR,
    SYM_FUNC,
    SYM_CLASS,
    SYM_NAMESPACE
} SymbolKind;

struct Symbol {
    SymbolKind kind;
    const char *name;
    const char *mangled_name;
    Type *type;
    SourceLoc loc;
    int stack_offset; /* Offset from RBP for local variables */
    bool is_global;
    bool is_param;
    bool is_ref;      /* Reference variable (implicitly dereferenced) */
    ASTNode *ast_decl;
    struct Symbol *next;
};

typedef enum {
    SCOPE_GLOBAL,
    SCOPE_NAMESPACE,
    SCOPE_CLASS,
    SCOPE_FUNCTION,
    SCOPE_BLOCK
} ScopeKind;

struct Scope {
    ScopeKind kind;
    const char *name;
    struct Scope *parent;
    Symbol *symbols;
    Type *class_type; /* Active class if in class scope */
    const char **using_namespaces;
    int using_ns_count;
};

typedef struct {
    Arena *arena;
    Scope *current_scope;
    Scope *global_scope;
    Type *current_func_ret;
    Type *current_class;
    int current_stack_offset;
} Sema;

void sema_init(Sema *s, Arena *arena);
bool sema_analyze(Sema *s, ASTNode *program);
const char *mangle_function_name(Arena *arena, const char *class_owner, const char *name, TypeParam *params, bool is_ctor, bool is_dtor);

#endif /* WINDS_SEMA_H */
