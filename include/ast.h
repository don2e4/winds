#ifndef WINDS_AST_H
#define WINDS_AST_H

#include "winds.h"
#include "arena.h"
#include "lexer.h"

/* Type representations */
typedef enum {
    TYPE_VOID,
    TYPE_BOOL,
    TYPE_CHAR,
    TYPE_INT,
    TYPE_LONG,
    TYPE_PTR,
    TYPE_REF,
    TYPE_ARRAY,
    TYPE_CLASS,
    TYPE_FUNC
} TypeKind;

typedef struct TypeParam {
    struct Type *type;
    const char *name;
    struct TypeParam *next;
} TypeParam;

typedef struct Field {
    const char *name;
    struct Type *type;
    int offset;
    int access; /* 0: public, 1: protected, 2: private */
    struct Field *next;
} Field;

struct Type {
    TypeKind kind;
    size_t size;
    size_t align;
    const char *name; /* Name for classes/primitives */

    /* Type details */
    union {
        struct {
            struct Type *base;
        } ptr;
        struct {
            struct Type *base;
        } ref;
        struct {
            struct Type *base;
            size_t count;
        } array;
        struct {
            const char *class_name;
            Field *fields;
            size_t total_size;
            bool is_struct;
        } cls;
        struct {
            struct Type *return_type;
            TypeParam *params;
            int param_count;
            bool is_varargs;
        } func;
    };
};

/* Builtin primitive types */
extern Type *g_type_void;
extern Type *g_type_bool;
extern Type *g_type_char;
extern Type *g_type_int;
extern Type *g_type_long;

void type_system_init(Arena *arena);
Type *type_new(Arena *arena, TypeKind kind);
Type *type_ptr(Arena *arena, Type *base);
Type *type_ref(Arena *arena, Type *base);
Type *type_array(Arena *arena, Type *base, size_t count);
Type *type_func(Arena *arena, Type *ret, TypeParam *params, int count, bool varargs);
bool type_equals(Type *a, Type *b);
bool type_is_integer(Type *t);
bool type_is_pointer_or_ref(Type *t);
const char *type_to_string(Arena *arena, Type *t);

/* AST Node Kinds */
typedef enum {
    /* Expressions */
    AST_LIT_INT,
    AST_LIT_STR,
    AST_LIT_BOOL,
    AST_LIT_NULLPTR,
    AST_VAR_REF,
    AST_BINARY,
    AST_UNARY,
    AST_ASSIGN,
    AST_CALL,
    AST_MEMBER,
    AST_NEW,
    AST_DELETE,
    AST_THIS,
    AST_CAST,

    /* Statements */
    AST_STMT_EXPR,
    AST_STMT_BLOCK,
    AST_STMT_VAR_DECL,
    AST_STMT_IF,
    AST_STMT_WHILE,
    AST_STMT_FOR,
    AST_STMT_RETURN,
    AST_STMT_BREAK,
    AST_STMT_CONTINUE,

    /* Declarations */
    AST_DECL_FUNC,
    AST_DECL_CLASS,
    AST_DECL_NAMESPACE,
    AST_PROGRAM
} ASTNodeKind;

typedef struct Symbol Symbol;

struct ASTNode {
    ASTNodeKind kind;
    SourceLoc loc;
    Type *type; /* Evaluated type during sema */

    union {
        /* AST_LIT_INT */
        int64_t int_val;

        /* AST_LIT_STR */
        struct {
            const char *val;
            size_t len;
        } str_lit;

        /* AST_LIT_BOOL */
        bool bool_val;

        /* AST_VAR_REF */
        struct {
            const char *name;
            const char *scope_prefix; /* e.g. "Foo::" */
            Symbol *sym;
        } var_ref;

        /* AST_BINARY */
        struct {
            TokenKind op;
            ASTNode *left;
            ASTNode *right;
        } binary;

        /* AST_UNARY */
        struct {
            TokenKind op;
            ASTNode *operand;
            bool is_prefix;
        } unary;

        /* AST_ASSIGN */
        struct {
            TokenKind op; /* '=', '+=', etc. */
            ASTNode *target;
            ASTNode *value;
        } assign;

        /* AST_CALL */
        struct {
            ASTNode *callee;
            const char *name;
            const char *scope_prefix;
            const char *mangled_name;
            ASTNode **args;
            int arg_count;
            bool is_method;
            bool is_arrow;
            ASTNode *object; /* 'this' or object expression for method call */
            Symbol *callee_sym;
        } call;

        /* AST_MEMBER */
        struct {
            ASTNode *object;
            const char *member_name;
            bool is_arrow;
            Field *field;
        } member;

        /* AST_NEW */
        struct {
            Type *target_type;
            ASTNode **args;
            int arg_count;
        } new_expr;

        /* AST_DELETE */
        struct {
            ASTNode *target;
            bool is_array;
        } delete_expr;

        /* AST_CAST */
        struct {
            Type *target_type;
            ASTNode *expr;
        } cast;

        /* AST_STMT_EXPR */
        struct {
            ASTNode *expr;
        } stmt_expr;

        /* AST_STMT_BLOCK */
        struct {
            ASTNode **stmts;
            int count;
        } block;

        /* AST_STMT_VAR_DECL */
        struct {
            Type *var_type;
            const char *name;
            ASTNode *init;
            Symbol *sym;
        } var_decl;

        /* AST_STMT_IF */
        struct {
            ASTNode *cond;
            ASTNode *then_branch;
            ASTNode *else_branch;
        } if_stmt;

        /* AST_STMT_WHILE */
        struct {
            ASTNode *cond;
            ASTNode *body;
        } while_stmt;

        /* AST_STMT_FOR */
        struct {
            ASTNode *init;
            ASTNode *cond;
            ASTNode *step;
            ASTNode *body;
        } for_stmt;

        /* AST_STMT_RETURN */
        struct {
            ASTNode *expr;
        } ret_stmt;

        /* AST_DECL_FUNC */
        struct {
            const char *name;
            const char *mangled_name;
            const char *class_owner;
            Type *func_type;
            ASTNode **params;
            int param_count;
            ASTNode *body;
            bool is_method;
            bool is_ctor;
            bool is_dtor;
            bool is_varargs;
            bool is_extern;
            int stack_size;
        } func_decl;

        /* AST_DECL_CLASS */
        struct {
            const char *name;
            Type *class_type;
            Field *fields;
            ASTNode **methods;
            int method_count;
        } class_decl;

        /* AST_DECL_NAMESPACE */
        struct {
            const char *name;
            ASTNode **decls;
            int count;
        } ns_decl;

        /* AST_PROGRAM */
        struct {
            ASTNode **decls;
            int count;
        } program;
    };
};

/* AST creation helpers */
ASTNode *ast_new(Arena *arena, ASTNodeKind kind, SourceLoc loc);
void ast_dump(ASTNode *node, int indent);

#endif /* WINDS_AST_H */
