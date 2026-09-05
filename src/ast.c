#include <string.h>
#include "ast.h"
#include "str.h"

Type *g_type_void = NULL;
Type *g_type_bool = NULL;
Type *g_type_char = NULL;
Type *g_type_int  = NULL;
Type *g_type_long = NULL;

void type_system_init(Arena *arena) {
    g_type_void = arena_alloc_zero(arena, sizeof(Type));
    g_type_void->kind = TYPE_VOID;
    g_type_void->size = 0;
    g_type_void->align = 1;
    g_type_void->name = str_intern("void");

    g_type_bool = arena_alloc_zero(arena, sizeof(Type));
    g_type_bool->kind = TYPE_BOOL;
    g_type_bool->size = 1;
    g_type_bool->align = 1;
    g_type_bool->name = str_intern("bool");

    g_type_char = arena_alloc_zero(arena, sizeof(Type));
    g_type_char->kind = TYPE_CHAR;
    g_type_char->size = 1;
    g_type_char->align = 1;
    g_type_char->name = str_intern("char");

    g_type_int = arena_alloc_zero(arena, sizeof(Type));
    g_type_int->kind = TYPE_INT;
    g_type_int->size = 4;
    g_type_int->align = 4;
    g_type_int->name = str_intern("int");

    g_type_long = arena_alloc_zero(arena, sizeof(Type));
    g_type_long->kind = TYPE_LONG;
    g_type_long->size = 8;
    g_type_long->align = 8;
    g_type_long->name = str_intern("long");
}

Type *type_new(Arena *arena, TypeKind kind) {
    Type *t = arena_alloc_zero(arena, sizeof(Type));
    t->kind = kind;
    return t;
}

Type *type_ptr(Arena *arena, Type *base) {
    Type *t = type_new(arena, TYPE_PTR);
    t->size = 8;
    t->align = 8;
    t->ptr.base = base;
    return t;
}

Type *type_ref(Arena *arena, Type *base) {
    Type *t = type_new(arena, TYPE_REF);
    t->size = 8;
    t->align = 8;
    t->ref.base = base;
    return t;
}

Type *type_array(Arena *arena, Type *base, size_t count) {
    Type *t = type_new(arena, TYPE_ARRAY);
    t->size = base->size * count;
    t->align = base->align;
    t->array.base = base;
    t->array.count = count;
    return t;
}

Type *type_func(Arena *arena, Type *ret, TypeParam *params, int count, bool varargs) {
    Type *t = type_new(arena, TYPE_FUNC);
    t->size = 8;
    t->align = 8;
    t->func.return_type = ret;
    t->func.params = params;
    t->func.param_count = count;
    t->func.is_varargs = varargs;
    return t;
}

bool type_equals(Type *a, Type *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;

    switch (a->kind) {
        case TYPE_PTR:
            return type_equals(a->ptr.base, b->ptr.base);
        case TYPE_REF:
            return type_equals(a->ref.base, b->ref.base);
        case TYPE_ARRAY:
            return a->array.count == b->array.count && type_equals(a->array.base, b->array.base);
        case TYPE_CLASS: {
            if (a->name == b->name) return true;
            if (!a->name || !b->name) return false;
            if (strcmp(a->name, b->name) == 0) return true;
            const char *sa = strrchr(a->name, ':');
            sa = sa ? sa + 1 : a->name;
            const char *sb = strrchr(b->name, ':');
            sb = sb ? sb + 1 : b->name;
            return strcmp(sa, sb) == 0;
        }
        case TYPE_FUNC: {
            if (!type_equals(a->func.return_type, b->func.return_type)) return false;
            if (a->func.param_count != b->func.param_count) return false;
            TypeParam *pa = a->func.params;
            TypeParam *pb = b->func.params;
            while (pa && pb) {
                if (!type_equals(pa->type, pb->type)) return false;
                pa = pa->next;
                pb = pb->next;
            }
            return true;
        }
        default:
            return true;
    }
}

bool type_is_integer(Type *t) {
    if (!t) return false;
    return t->kind == TYPE_BOOL || t->kind == TYPE_CHAR ||
           t->kind == TYPE_INT  || t->kind == TYPE_LONG;
}

bool type_is_pointer_or_ref(Type *t) {
    if (!t) return false;
    return t->kind == TYPE_PTR || t->kind == TYPE_REF;
}

const char *type_to_string(Arena *arena, Type *t) {
    if (!t) return "unknown";
    char buf[256];
    switch (t->kind) {
        case TYPE_VOID: return "void";
        case TYPE_BOOL: return "bool";
        case TYPE_CHAR: return "char";
        case TYPE_INT:  return "int";
        case TYPE_LONG: return "long";
        case TYPE_PTR:
            snprintf(buf, sizeof(buf), "%s*", type_to_string(arena, t->ptr.base));
            return arena_strdup(arena, buf);
        case TYPE_REF:
            snprintf(buf, sizeof(buf), "%s&", type_to_string(arena, t->ref.base));
            return arena_strdup(arena, buf);
        case TYPE_ARRAY:
            snprintf(buf, sizeof(buf), "%s[%zu]", type_to_string(arena, t->array.base), t->array.count);
            return arena_strdup(arena, buf);
        case TYPE_CLASS:
            return t->name ? t->name : "class";
        case TYPE_FUNC:
            snprintf(buf, sizeof(buf), "func(...)->%s", type_to_string(arena, t->func.return_type));
            return arena_strdup(arena, buf);
    }
    return "type";
}

ASTNode *ast_new(Arena *arena, ASTNodeKind kind, SourceLoc loc) {
    ASTNode *n = arena_alloc_zero(arena, sizeof(ASTNode));
    n->kind = kind;
    n->loc = loc;
    return n;
}

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }
}

void ast_dump(ASTNode *node, int indent) {
    if (!node) return;
    print_indent(indent);

    switch (node->kind) {
        case AST_LIT_INT:
            printf("IntLiteral: %ld\n", (long)node->int_val);
            break;
        case AST_LIT_STR:
            printf("StringLiteral: \"%s\"\n", node->str_lit.val);
            break;
        case AST_LIT_BOOL:
            printf("BoolLiteral: %s\n", node->bool_val ? "true" : "false");
            break;
        case AST_LIT_NULLPTR:
            printf("Nullptr\n");
            break;
        case AST_VAR_REF:
            printf("VarRef: %s%s\n", node->var_ref.scope_prefix ? node->var_ref.scope_prefix : "", node->var_ref.name);
            break;
        case AST_THIS:
            printf("This\n");
            break;
        case AST_BINARY:
            printf("BinaryOp: %s\n", token_kind_str(node->binary.op));
            ast_dump(node->binary.left, indent + 1);
            ast_dump(node->binary.right, indent + 1);
            break;
        case AST_UNARY:
            printf("UnaryOp: %s\n", token_kind_str(node->unary.op));
            ast_dump(node->unary.operand, indent + 1);
            break;
        case AST_ASSIGN:
            printf("AssignOp: %s\n", token_kind_str(node->assign.op));
            ast_dump(node->assign.target, indent + 1);
            ast_dump(node->assign.value, indent + 1);
            break;
        case AST_CALL:
            printf("Call: %s (args: %d)\n", node->call.name ? node->call.name : "indirect", node->call.arg_count);
            for (int i = 0; i < node->call.arg_count; i++) {
                ast_dump(node->call.args[i], indent + 1);
            }
            break;
        case AST_MEMBER:
            printf("MemberAccess: %s%s\n", node->member.is_arrow ? "->" : ".", node->member.member_name);
            ast_dump(node->member.object, indent + 1);
            break;
        case AST_NEW:
            printf("NewExpr: type=%s, args=%d\n", node->new_expr.target_type->name, node->new_expr.arg_count);
            for (int i = 0; i < node->new_expr.arg_count; i++) {
                ast_dump(node->new_expr.args[i], indent + 1);
            }
            break;
        case AST_DELETE:
            printf("DeleteExpr: is_array=%d\n", node->delete_expr.is_array);
            ast_dump(node->delete_expr.target, indent + 1);
            break;
        case AST_INDEX:
            printf("IndexExpr:\n");
            ast_dump(node->index_expr.target, indent + 1);
            ast_dump(node->index_expr.index, indent + 1);
            break;
        case AST_SIZEOF:
            printf("SizeofExpr\n");
            break;
        case AST_STMT_EXPR:
            printf("ExprStmt:\n");
            ast_dump(node->stmt_expr.expr, indent + 1);
            break;
        case AST_STMT_BLOCK:
            printf("BlockStmt: (%d stmts)\n", node->block.count);
            for (int i = 0; i < node->block.count; i++) {
                ast_dump(node->block.stmts[i], indent + 1);
            }
            break;
        case AST_STMT_VAR_DECL:
            printf("VarDecl: %s %s\n", node->var_decl.var_type ? node->var_decl.var_type->name : "type", node->var_decl.name);
            if (node->var_decl.init) {
                ast_dump(node->var_decl.init, indent + 1);
            }
            break;
        case AST_STMT_IF:
            printf("IfStmt:\n");
            print_indent(indent + 1); printf("Condition:\n");
            ast_dump(node->if_stmt.cond, indent + 2);
            print_indent(indent + 1); printf("Then:\n");
            ast_dump(node->if_stmt.then_branch, indent + 2);
            if (node->if_stmt.else_branch) {
                print_indent(indent + 1); printf("Else:\n");
                ast_dump(node->if_stmt.else_branch, indent + 2);
            }
            break;
        case AST_STMT_WHILE:
            printf("WhileStmt:\n");
            ast_dump(node->while_stmt.cond, indent + 1);
            ast_dump(node->while_stmt.body, indent + 1);
            break;
        case AST_STMT_FOR:
            printf("ForStmt:\n");
            if (node->for_stmt.init) ast_dump(node->for_stmt.init, indent + 1);
            if (node->for_stmt.cond) ast_dump(node->for_stmt.cond, indent + 1);
            if (node->for_stmt.step) ast_dump(node->for_stmt.step, indent + 1);
            ast_dump(node->for_stmt.body, indent + 1);
            break;
        case AST_STMT_RETURN:
            printf("ReturnStmt:\n");
            if (node->ret_stmt.expr) ast_dump(node->ret_stmt.expr, indent + 1);
            break;
        case AST_STMT_BREAK:
            printf("BreakStmt\n");
            break;
        case AST_STMT_CONTINUE:
            printf("ContinueStmt\n");
            break;
        case AST_DECL_FUNC:
            printf("FuncDecl: %s (params: %d, method: %d)\n",
                   node->func_decl.name, node->func_decl.param_count, node->func_decl.is_method);
            if (node->func_decl.body) {
                ast_dump(node->func_decl.body, indent + 1);
            }
            break;
        case AST_DECL_CLASS:
            printf("ClassDecl: %s\n", node->class_decl.name);
            for (int i = 0; i < node->class_decl.method_count; i++) {
                ast_dump(node->class_decl.methods[i], indent + 1);
            }
            break;
        case AST_DECL_NAMESPACE:
            printf("NamespaceDecl: %s\n", node->ns_decl.name);
            for (int i = 0; i < node->ns_decl.count; i++) {
                ast_dump(node->ns_decl.decls[i], indent + 1);
            }
            break;
        case AST_DECL_TYPEDEF:
            printf("TypedefDecl: %s\n", node->typedef_decl.name);
            break;
        case AST_DECL_TEMPLATE:
            printf("TemplateDecl: typename %s\n", node->template_decl.param_name);
            ast_dump(node->template_decl.decl, indent + 1);
            break;
        case AST_PROGRAM:
            printf("Program: (%d declarations)\n", node->program.count);
            for (int i = 0; i < node->program.count; i++) {
                ast_dump(node->program.decls[i], indent + 1);
            }
            break;
        default:
            printf("Unknown AST Node (%d)\n", node->kind);
            break;
    }
}
