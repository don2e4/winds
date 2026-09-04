#include "sema.h"
#include "str.h"

static Scope *scope_push(Sema *s, ScopeKind kind, const char *name) {
    Scope *new_scope = arena_alloc_zero(s->arena, sizeof(Scope));
    new_scope->kind = kind;
    new_scope->name = name;
    new_scope->parent = s->current_scope;
    new_scope->symbols = NULL;
    new_scope->class_type = s->current_class;
    s->current_scope = new_scope;
    return new_scope;
}

static void scope_pop(Sema *s) {
    if (s->current_scope && s->current_scope->parent) {
        s->current_scope = s->current_scope->parent;
    }
}

static void add_symbol(Scope *scope, Symbol *sym) {
    sym->next = scope->symbols;
    scope->symbols = sym;
}

static Symbol *find_symbol_in_scope(Scope *scope, const char *name) {
    for (Symbol *sym = scope->symbols; sym != NULL; sym = sym->next) {
        if (sym->name == name || (sym->name && name && strcmp(sym->name, name) == 0)) {
            return sym;
        }
    }
    return NULL;
}

static Symbol *find_symbol(Scope *scope, const char *name) {
    for (Scope *sc = scope; sc != NULL; sc = sc->parent) {
        Symbol *sym = find_symbol_in_scope(sc, name);
        if (sym) return sym;

        for (int u = 0; u < sc->using_ns_count; u++) {
            char qname[256];
            snprintf(qname, sizeof(qname), "%s::%s", sc->using_namespaces[u], name);
            Symbol *usym = find_symbol_in_scope(sc, qname);
            if (usym) return usym;
        }
    }
    return NULL;
}

static Symbol *find_scoped_function_overload(Scope *scope, const char *scope_prefix, const char *name, int arg_count) {
    Symbol *fallback = NULL;
    char qbuf[256];
    snprintf(qbuf, sizeof(qbuf), "%s::%s", scope_prefix, name);

    for (Scope *sc = scope; sc != NULL; sc = sc->parent) {
        for (Symbol *sym = sc->symbols; sym != NULL; sym = sym->next) {
            if (sym->kind == SYM_FUNC) {
                bool match = false;
                if (sym->name && strcmp(sym->name, qbuf) == 0) {
                    match = true;
                } else if (sym->name && strcmp(sym->name, name) == 0 &&
                           sym->ast_decl && sym->ast_decl->func_decl.class_owner &&
                           strcmp(sym->ast_decl->func_decl.class_owner, scope_prefix) == 0) {
                    match = true;
                }

                if (match) {
                    if (sym->type && sym->type->kind == TYPE_FUNC) {
                        if (sym->type->func.param_count == arg_count || sym->type->func.is_varargs) {
                            return sym;
                        }
                    }
                    if (!fallback) fallback = sym;
                }
            }
        }
    }
    return fallback;
}

static Symbol *find_function_overload(Scope *scope, const char *name, int arg_count) {
    Symbol *fallback = NULL;
    for (Scope *sc = scope; sc != NULL; sc = sc->parent) {
        for (Symbol *sym = sc->symbols; sym != NULL; sym = sym->next) {
            if (sym->kind == SYM_FUNC && (sym->name == name || (sym->name && strcmp(sym->name, name) == 0))) {
                if (sym->type && sym->type->kind == TYPE_FUNC) {
                    if (sym->type->func.param_count == arg_count || sym->type->func.is_varargs) {
                        return sym;
                    }
                }
                if (!fallback) fallback = sym;
            }
        }
        for (int u = 0; u < sc->using_ns_count; u++) {
            Symbol *usym = find_scoped_function_overload(sc, sc->using_namespaces[u], name, arg_count);
            if (usym) return usym;
        }
    }
    return fallback;
}

static Symbol *find_method_overload(Scope *scope, const char *cls_name, const char *method_name, int arg_count) {
    Symbol *fallback = NULL;
    for (Scope *sc = scope; sc != NULL; sc = sc->parent) {
        for (Symbol *sym = sc->symbols; sym != NULL; sym = sym->next) {
            if (sym->kind == SYM_FUNC && (sym->name == method_name || (sym->name && strcmp(sym->name, method_name) == 0))) {
                if (sym->ast_decl && sym->ast_decl->func_decl.class_owner &&
                    strcmp(sym->ast_decl->func_decl.class_owner, cls_name) == 0) {
                    if (sym->type && sym->type->kind == TYPE_FUNC && sym->type->func.param_count == arg_count) {
                        return sym;
                    }
                    if (!fallback) fallback = sym;
                }
            }
        }
    }
    return fallback;
}

static Symbol *find_class_symbol(Scope *scope, const char *name) {
    for (Scope *sc = scope; sc != NULL; sc = sc->parent) {
        for (Symbol *sym = sc->symbols; sym != NULL; sym = sym->next) {
            if (sym->kind == SYM_CLASS && (sym->name == name || (sym->name && strcmp(sym->name, name) == 0))) {
                return sym;
            }
        }
        for (int u = 0; u < sc->using_ns_count; u++) {
            char qname[256];
            snprintf(qname, sizeof(qname), "%s::%s", sc->using_namespaces[u], name);
            for (Symbol *sym = sc->symbols; sym != NULL; sym = sym->next) {
                if (sym->kind == SYM_CLASS && sym->name && strcmp(sym->name, qname) == 0) {
                    return sym;
                }
            }
        }
    }
    return NULL;
}

static Type *resolve_type(Sema *s, Type *t) {
    if (!t) return g_type_int;
    if (t->kind == TYPE_CLASS) {
        if (t->cls.fields == NULL) {
            Symbol *csym = find_class_symbol(s->global_scope, t->name);
            if (csym && csym->type) {
                return csym->type;
            }
        }
        return t;
    }
    if (t->kind == TYPE_PTR) {
        Type *base = resolve_type(s, t->ptr.base);
        if (base != t->ptr.base) {
            return type_ptr(s->arena, base);
        }
        return t;
    }
    if (t->kind == TYPE_REF) {
        Type *base = resolve_type(s, t->ref.base);
        if (base != t->ref.base) {
            return type_ref(s->arena, base);
        }
        return t;
    }
    return t;
}

static Field *find_field(Type *cls, const char *field_name) {
    if (!cls || cls->kind != TYPE_CLASS) return NULL;
    for (Field *f = cls->cls.fields; f != NULL; f = f->next) {
        if (strcmp(f->name, field_name) == 0) {
            return f;
        }
    }
    return NULL;
}

static const char *get_type_mangling_code(Type *t) {
    if (!t) return "v";
    switch (t->kind) {
        case TYPE_VOID: return "v";
        case TYPE_BOOL: return "b";
        case TYPE_CHAR: return "c";
        case TYPE_INT:  return "i";
        case TYPE_LONG: return "l";
        case TYPE_PTR:  return "P";
        case TYPE_REF:  return "R";
        case TYPE_ARRAY: return "A";
        case TYPE_CLASS: return t->name ? t->name : "C";
        case TYPE_FUNC: return "F";
    }
    return "x";
}

const char *mangle_function_name(Arena *arena, const char *class_owner, const char *name, TypeParam *params, bool is_ctor, bool is_dtor) {
    /* Standard C runtime functions and main are NOT mangled */
    if (!class_owner) {
        if (strcmp(name, "main") == 0 ||
            strcmp(name, "printf") == 0 ||
            strcmp(name, "puts") == 0 ||
            strcmp(name, "malloc") == 0 ||
            strcmp(name, "free") == 0 ||
            strcmp(name, "exit") == 0 ||
            strcmp(name, "putchar") == 0 ||
            strcmp(name, "getchar") == 0 ||
            strcmp(name, "strlen") == 0) {
            return str_intern(name);
        }
    }

    char buf[512];
    int len = 0;

    if (class_owner) {
        if (is_ctor) {
            len = snprintf(buf, sizeof(buf), "_W_%s_ctor", class_owner);
        } else if (is_dtor) {
            len = snprintf(buf, sizeof(buf), "_W_%s_dtor", class_owner);
        } else {
            len = snprintf(buf, sizeof(buf), "_W_%s_%s", class_owner, name);
        }
    } else {
        len = snprintf(buf, sizeof(buf), "_W_%s", name);
    }

    /* Append parameter types */
    for (TypeParam *p = params; p != NULL; p = p->next) {
        const char *code = get_type_mangling_code(p->type);
        len += snprintf(buf + len, sizeof(buf) - len, "_%s", code);
    }

    if (!params && !is_dtor) {
        len += snprintf(buf + len, sizeof(buf) - len, "_v");
    }

    /* Replace any ':' with '_' to produce valid GNU assembler labels */
    for (int i = 0; i < len; i++) {
        if (buf[i] == ':') {
            buf[i] = '_';
        }
    }

    return arena_strdup(arena, buf);
}

void sema_init(Sema *s, Arena *arena) {
    s->arena = arena;
    s->global_scope = arena_alloc_zero(arena, sizeof(Scope));
    s->global_scope->kind = SCOPE_GLOBAL;
    s->current_scope = s->global_scope;
    s->current_func_ret = NULL;
    s->current_class = NULL;
    s->current_stack_offset = 0;

    /* Built-in standard library function symbols */
    const char *builtins[] = { "printf", "puts", "malloc", "free", "exit", "putchar", "getchar" };
    for (size_t i = 0; i < sizeof(builtins)/sizeof(builtins[0]); i++) {
        Symbol *sym = arena_alloc_zero(arena, sizeof(Symbol));
        sym->kind = SYM_FUNC;
        sym->name = str_intern(builtins[i]);
        sym->mangled_name = sym->name;
        sym->is_global = true;
        sym->type = type_func(arena, g_type_int, NULL, 0, true);
        add_symbol(s->global_scope, sym);
    }
}

/* Forward declarations for AST analysis */
static void analyze_stmt(Sema *s, ASTNode *stmt);
static void analyze_expr(Sema *s, ASTNode *expr);

static void layout_class(Sema *s, Type *class_type) {
    (void)s;
    size_t offset = 0;
    size_t max_align = 4;

    for (Field *f = class_type->cls.fields; f != NULL; f = f->next) {
        size_t align = f->type->align > 0 ? f->type->align : 4;
        if (align > max_align) max_align = align;

        /* Align offset */
        offset = (offset + align - 1) & ~(align - 1);
        f->offset = (int)offset;
        offset += f->type->size > 0 ? f->type->size : 4;
    }

    /* Total size aligned to max_align */
    if (offset == 0) offset = 1;
    offset = (offset + max_align - 1) & ~(max_align - 1);
    class_type->size = offset;
    class_type->align = max_align;
    class_type->cls.total_size = offset;
}

static void analyze_expr(Sema *s, ASTNode *expr) {
    if (!expr) return;

    switch (expr->kind) {
        case AST_LIT_INT:
            if (!expr->type) expr->type = g_type_int;
            break;
        case AST_LIT_STR:
            if (!expr->type) expr->type = type_ptr(s->arena, g_type_char);
            break;
        case AST_LIT_BOOL:
            if (!expr->type) expr->type = g_type_bool;
            break;
        case AST_LIT_NULLPTR:
            if (!expr->type) expr->type = type_ptr(s->arena, g_type_void);
            break;

        case AST_THIS: {
            if (!s->current_class) {
                diag_report(DIAG_ERROR, expr->loc, "'this' is only valid inside member functions");
                expr->type = type_ptr(s->arena, g_type_void);
            } else {
                expr->type = type_ptr(s->arena, s->current_class);
            }
            break;
        }

        case AST_VAR_REF: {
            /* Check if it refers to a local or global symbol */
            Symbol *sym = NULL;
            if (expr->var_ref.scope_prefix) {
                char qname[256];
                snprintf(qname, sizeof(qname), "%s::%s", expr->var_ref.scope_prefix, expr->var_ref.name);
                sym = find_symbol(s->current_scope, qname);
            } else {
                sym = find_symbol(s->current_scope, expr->var_ref.name);
            }
            if (sym) {
                expr->var_ref.sym = sym;
                expr->type = sym->type;
                break;
            }

            /* If not found and we are inside a member function, check if it's a class field */
            if (s->current_class) {
                Field *f = find_field(s->current_class, expr->var_ref.name);
                if (f) {
                    /* Rewrite identifier 'field' to 'this->field' */
                    SourceLoc loc = expr->loc;
                    const char *fname = expr->var_ref.name;

                    ASTNode *this_node = ast_new(s->arena, AST_THIS, loc);
                    this_node->type = type_ptr(s->arena, s->current_class);

                    expr->kind = AST_MEMBER;
                    expr->member.object = this_node;
                    expr->member.member_name = fname;
                    expr->member.is_arrow = true;
                    expr->member.field = f;
                    expr->type = f->type;
                    break;
                }
            }

            diag_report(DIAG_ERROR, expr->loc, "use of undeclared identifier '%s'", expr->var_ref.name);
            expr->type = g_type_int;
            break;
        }

        case AST_BINARY: {
            analyze_expr(s, expr->binary.left);
            analyze_expr(s, expr->binary.right);

            /* Relational / comparison operators return bool */
            TokenKind op = expr->binary.op;
            if (op == TOK_EQ_EQ || op == TOK_EXCL_EQ ||
                op == TOK_LESS || op == TOK_LESS_EQ ||
                op == TOK_GREATER || op == TOK_GREATER_EQ ||
                op == TOK_LOG_AND || op == TOK_LOG_OR) {
                expr->type = g_type_bool;
            } else if (expr->binary.left && expr->binary.left->type) {
                expr->type = expr->binary.left->type;
            } else {
                expr->type = g_type_int;
            }
            break;
        }

        case AST_UNARY: {
            analyze_expr(s, expr->unary.operand);
            Type *op_t = expr->unary.operand ? expr->unary.operand->type : g_type_int;

            if (expr->unary.op == TOK_AMP) {
                /* Address-of: &x -> Type* */
                expr->type = type_ptr(s->arena, op_t);
            } else if (expr->unary.op == TOK_STAR) {
                /* Dereference: *p -> Type */
                if (op_t && (op_t->kind == TYPE_PTR || op_t->kind == TYPE_REF)) {
                    expr->type = op_t->ptr.base;
                } else {
                    diag_report(DIAG_ERROR, expr->loc, "indirection requires pointer operand");
                    expr->type = g_type_int;
                }
            } else if (expr->unary.op == TOK_EXCL) {
                expr->type = g_type_bool;
            } else {
                expr->type = op_t;
            }
            break;
        }

        case AST_ASSIGN: {
            analyze_expr(s, expr->assign.target);
            analyze_expr(s, expr->assign.value);
            expr->type = expr->assign.target ? expr->assign.target->type : g_type_int;
            break;
        }

        case AST_CALL: {
            for (int i = 0; i < expr->call.arg_count; i++) {
                analyze_expr(s, expr->call.args[i]);
            }

            if (expr->call.is_method) {
                analyze_expr(s, expr->call.object);
                Type *obj_t = expr->call.object ? expr->call.object->type : NULL;
                Type *cls_type = NULL;
                if (obj_t) {
                    if (obj_t->kind == TYPE_PTR || obj_t->kind == TYPE_REF) {
                        cls_type = resolve_type(s, obj_t->ptr.base);
                    } else if (obj_t->kind == TYPE_CLASS) {
                        cls_type = resolve_type(s, obj_t);
                    }
                }

                if (!cls_type || cls_type->kind != TYPE_CLASS) {
                    diag_report(DIAG_ERROR, expr->loc, "member call on non-class object");
                    expr->type = g_type_int;
                    break;
                }

                Symbol *msym = find_method_overload(s->global_scope, cls_type->name, expr->call.name, expr->call.arg_count);
                if (msym) {
                    expr->call.mangled_name = msym->mangled_name;
                    expr->call.callee_sym = msym;
                    if (msym->type && msym->type->kind == TYPE_FUNC) {
                        expr->type = msym->type->func.return_type;
                    } else {
                        expr->type = g_type_int;
                    }
                } else {
                    const char *mangled = mangle_function_name(s->arena, cls_type->name, expr->call.name, NULL, false, false);
                    expr->call.mangled_name = mangled;
                    expr->type = g_type_int;
                }
            } else {
                /* Free function call or scoped function call */
                Symbol *sym = NULL;
                if (expr->call.scope_prefix) {
                    sym = find_scoped_function_overload(s->global_scope, expr->call.scope_prefix, expr->call.name, expr->call.arg_count);
                } else {
                    sym = find_function_overload(s->current_scope, expr->call.name, expr->call.arg_count);
                }

                if (sym) {
                    expr->call.mangled_name = sym->mangled_name;
                    expr->call.callee_sym = sym;
                    if (sym->type && sym->type->kind == TYPE_FUNC) {
                        expr->type = sym->type->func.return_type;
                    } else {
                        expr->type = g_type_int;
                    }
                } else if (expr->call.scope_prefix) {
                    const char *mangled = mangle_function_name(s->arena, expr->call.scope_prefix, expr->call.name, NULL, false, false);
                    expr->call.mangled_name = mangled;
                    expr->type = g_type_int;
                } else {
                    /* Unresolved: assume int return type */
                    expr->call.mangled_name = str_intern(expr->call.name);
                    expr->type = g_type_int;
                }
            }
            break;
        }

        case AST_MEMBER: {
            analyze_expr(s, expr->member.object);
            Type *obj_t = expr->member.object ? expr->member.object->type : NULL;
            Type *cls_type = NULL;

            if (obj_t) {
                if (expr->member.is_arrow) {
                    if (obj_t->kind == TYPE_PTR || obj_t->kind == TYPE_REF) {
                        cls_type = resolve_type(s, obj_t->ptr.base);
                    }
                } else {
                    if (obj_t->kind == TYPE_CLASS) {
                        cls_type = resolve_type(s, obj_t);
                    } else if (obj_t->kind == TYPE_REF && obj_t->ref.base->kind == TYPE_CLASS) {
                        cls_type = resolve_type(s, obj_t->ref.base);
                    }
                }
            }

            if (!cls_type || cls_type->kind != TYPE_CLASS) {
                diag_report(DIAG_ERROR, expr->loc, "member access '%s' on non-class type", expr->member.member_name);
                expr->type = g_type_int;
                break;
            }

            Field *f = find_field(cls_type, expr->member.member_name);
            if (!f) {
                diag_report(DIAG_ERROR, expr->loc, "class '%s' has no member named '%s'",
                            cls_type->name, expr->member.member_name);
                expr->type = g_type_int;
            } else {
                expr->member.field = f;
                expr->type = f->type;
            }
            break;
        }

        case AST_NEW: {
            expr->new_expr.target_type = resolve_type(s, expr->new_expr.target_type);
            Type *t = expr->new_expr.target_type;
            for (int i = 0; i < expr->new_expr.arg_count; i++) {
                analyze_expr(s, expr->new_expr.args[i]);
            }
            expr->type = type_ptr(s->arena, t);
            break;
        }

        case AST_DELETE: {
            analyze_expr(s, expr->delete_expr.target);
            expr->type = g_type_void;
            break;
        }

        default:
            break;
    }
}

static void analyze_stmt(Sema *s, ASTNode *stmt) {
    if (!stmt) return;

    switch (stmt->kind) {
        case AST_STMT_EXPR:
            if (stmt->stmt_expr.expr && stmt->stmt_expr.expr->kind == AST_VAR_REF &&
                stmt->stmt_expr.expr->var_ref.scope_prefix &&
                strcmp(stmt->stmt_expr.expr->var_ref.scope_prefix, "using") == 0) {
                if (!s->current_scope->using_namespaces) {
                    s->current_scope->using_namespaces = arena_alloc(s->arena, sizeof(const char*) * 16);
                }
                s->current_scope->using_namespaces[s->current_scope->using_ns_count++] = stmt->stmt_expr.expr->var_ref.name;
                break;
            }
            analyze_expr(s, stmt->stmt_expr.expr);
            break;

        case AST_STMT_BLOCK: {
            scope_push(s, SCOPE_BLOCK, NULL);
            for (int i = 0; i < stmt->block.count; i++) {
                analyze_stmt(s, stmt->block.stmts[i]);
            }
            scope_pop(s);
            break;
        }

        case AST_STMT_VAR_DECL: {
            stmt->var_decl.var_type = resolve_type(s, stmt->var_decl.var_type);
            Type *vt = stmt->var_decl.var_type;

            if (stmt->var_decl.init) {
                analyze_expr(s, stmt->var_decl.init);
            }

            /* Allocate space on local stack frame */
            size_t size = vt->size > 0 ? vt->size : 4;
            if (size < 4) size = 4;
            /* 8-byte align stack allocation for performance */
            size = (size + 7) & ~7;
            s->current_stack_offset += (int)size;

            Symbol *sym = arena_alloc_zero(s->arena, sizeof(Symbol));
            sym->kind = SYM_VAR;
            sym->name = stmt->var_decl.name;
            sym->type = vt;
            sym->loc = stmt->loc;
            sym->stack_offset = -s->current_stack_offset;
            sym->is_ref = (vt->kind == TYPE_REF);
            stmt->var_decl.sym = sym;

            add_symbol(s->current_scope, sym);
            break;
        }

        case AST_STMT_IF:
            analyze_expr(s, stmt->if_stmt.cond);
            analyze_stmt(s, stmt->if_stmt.then_branch);
            if (stmt->if_stmt.else_branch) {
                analyze_stmt(s, stmt->if_stmt.else_branch);
            }
            break;

        case AST_STMT_WHILE:
            analyze_expr(s, stmt->while_stmt.cond);
            analyze_stmt(s, stmt->while_stmt.body);
            break;

        case AST_STMT_FOR:
            scope_push(s, SCOPE_BLOCK, NULL);
            if (stmt->for_stmt.init) analyze_stmt(s, stmt->for_stmt.init);
            if (stmt->for_stmt.cond) analyze_expr(s, stmt->for_stmt.cond);
            if (stmt->for_stmt.step) analyze_expr(s, stmt->for_stmt.step);
            analyze_stmt(s, stmt->for_stmt.body);
            scope_pop(s);
            break;

        case AST_STMT_RETURN:
            if (stmt->ret_stmt.expr) {
                analyze_expr(s, stmt->ret_stmt.expr);
            }
            break;

        case AST_STMT_BREAK:
        case AST_STMT_CONTINUE:
            break;

        default:
            break;
    }
}

static void analyze_function(Sema *s, ASTNode *fn) {
    const char *class_owner = fn->func_decl.class_owner;
    const char *name = fn->func_decl.name;
    bool is_ctor = fn->func_decl.is_ctor;
    bool is_dtor = fn->func_decl.is_dtor;

    Type *cls_type = NULL;
    if (class_owner) {
        Symbol *csym = find_class_symbol(s->global_scope, class_owner);
        if (csym && csym->kind == SYM_CLASS) {
            cls_type = csym->type;
        } else {
            fn->func_decl.is_method = false;
        }
    }

    /* Build parameters list for mangling */
    TypeParam *tparams_head = NULL;
    TypeParam **tparams_tail = &tparams_head;

    for (int i = 0; i < fn->func_decl.param_count; i++) {
        ASTNode *pnode = fn->func_decl.params[i];
        pnode->var_decl.var_type = resolve_type(s, pnode->var_decl.var_type);
        TypeParam *tp = arena_alloc_zero(s->arena, sizeof(TypeParam));
        tp->name = pnode->var_decl.name;
        tp->type = pnode->var_decl.var_type;
        *tparams_tail = tp;
        tparams_tail = &tp->next;
    }

    const char *mangled = mangle_function_name(s->arena, class_owner, name, tparams_head, is_ctor, is_dtor);
    fn->func_decl.mangled_name = mangled;

    Type *ret_t = fn->func_decl.func_type ? resolve_type(s, fn->func_decl.func_type) : g_type_void;
    Type *fn_t = type_func(s->arena, ret_t, tparams_head, fn->func_decl.param_count, false);

    /* Register function symbol */
    Symbol *fsym = arena_alloc_zero(s->arena, sizeof(Symbol));
    fsym->kind = SYM_FUNC;
    fsym->name = name;
    fsym->mangled_name = mangled;
    fsym->type = fn_t;
    fsym->loc = fn->loc;
    fsym->is_global = true;
    fsym->ast_decl = fn;
    add_symbol(s->global_scope, fsym);

    if (class_owner) {
        char qbuf[256];
        snprintf(qbuf, sizeof(qbuf), "%s::%s", class_owner, name);
        Symbol *qsym = arena_alloc_zero(s->arena, sizeof(Symbol));
        qsym->kind = SYM_FUNC;
        qsym->name = arena_strdup(s->arena, qbuf);
        qsym->mangled_name = mangled;
        qsym->type = fn_t;
        qsym->loc = fn->loc;
        qsym->is_global = true;
        qsym->ast_decl = fn;
        add_symbol(s->global_scope, qsym);
    }

    /* Analyze body if present */
    if (fn->func_decl.body) {
        scope_push(s, SCOPE_FUNCTION, name);
        s->current_func_ret = ret_t;
        s->current_class = cls_type;
        s->current_stack_offset = 0;

        /* If this is a method, inject 'this' pointer as first local/param */
        if (cls_type) {
            Symbol *this_sym = arena_alloc_zero(s->arena, sizeof(Symbol));
            this_sym->kind = SYM_VAR;
            this_sym->name = str_intern("this");
            this_sym->type = type_ptr(s->arena, cls_type);
            this_sym->is_param = true;
            s->current_stack_offset += 8;
            this_sym->stack_offset = -s->current_stack_offset;
            add_symbol(s->current_scope, this_sym);
        }

        /* Register parameters as local variables */
        for (int i = 0; i < fn->func_decl.param_count; i++) {
            ASTNode *pnode = fn->func_decl.params[i];
            Type *ptype = pnode->var_decl.var_type;
            size_t psize = ptype->size > 0 ? ptype->size : 8;
            psize = (psize + 7) & ~7;
            s->current_stack_offset += (int)psize;

            Symbol *psym = arena_alloc_zero(s->arena, sizeof(Symbol));
            psym->kind = SYM_VAR;
            psym->name = pnode->var_decl.name;
            psym->type = ptype;
            psym->loc = pnode->loc;
            psym->is_param = true;
            psym->is_ref = (ptype->kind == TYPE_REF);
            psym->stack_offset = -s->current_stack_offset;
            pnode->var_decl.sym = psym;
            add_symbol(s->current_scope, psym);
        }

        analyze_stmt(s, fn->func_decl.body);

        /* Round stack frame to 16 bytes for System V ABI alignment */
        fn->func_decl.stack_size = (s->current_stack_offset + 15) & ~15;

        s->current_func_ret = NULL;
        s->current_class = NULL;
        scope_pop(s);
    }
}

static void sema_register_classes(Sema *s, ASTNode *decl, const char *ns_prefix) {
    if (!decl) return;
    if (decl->kind == AST_DECL_CLASS) {
        Type *cls = type_new(s->arena, TYPE_CLASS);
        const char *full_name = decl->class_decl.name;
        if (ns_prefix && strstr(decl->class_decl.name, "::") == NULL) {
            char buf[256];
            snprintf(buf, sizeof(buf), "%s::%s", ns_prefix, decl->class_decl.name);
            full_name = arena_strdup(s->arena, buf);
        }
        cls->name = full_name;
        cls->cls.class_name = full_name;
        cls->cls.fields = decl->class_decl.fields;
        layout_class(s, cls);
        decl->class_decl.class_type = cls;

        Symbol *csym = arena_alloc_zero(s->arena, sizeof(Symbol));
        csym->kind = SYM_CLASS;
        csym->name = full_name;
        csym->type = cls;
        csym->loc = decl->loc;
        csym->is_global = true;
        add_symbol(s->global_scope, csym);

        if (ns_prefix) {
            Symbol *short_sym = arena_alloc_zero(s->arena, sizeof(Symbol));
            short_sym->kind = SYM_CLASS;
            short_sym->name = decl->class_decl.name;
            short_sym->type = cls;
            short_sym->loc = decl->loc;
            short_sym->is_global = true;
            add_symbol(s->global_scope, short_sym);
        }
    } else if (decl->kind == AST_DECL_NAMESPACE) {
        const char *new_prefix = decl->ns_decl.name;
        if (ns_prefix) {
            char buf[256];
            snprintf(buf, sizeof(buf), "%s::%s", ns_prefix, decl->ns_decl.name);
            new_prefix = arena_strdup(s->arena, buf);
        }
        for (int d = 0; d < decl->ns_decl.count; d++) {
            sema_register_classes(s, decl->ns_decl.decls[d], new_prefix);
        }
    }
}

static void sema_analyze_decls(Sema *s, ASTNode *decl, const char *ns_prefix) {
    if (!decl) return;
    if (decl->kind == AST_DECL_FUNC) {
        if (!decl->func_decl.class_owner && ns_prefix) {
            decl->func_decl.class_owner = ns_prefix;
        }
        analyze_function(s, decl);
    } else if (decl->kind == AST_DECL_CLASS) {
        for (int m = 0; m < decl->class_decl.method_count; m++) {
            ASTNode *mdecl = decl->class_decl.methods[m];
            if (ns_prefix && mdecl->func_decl.class_owner && strstr(mdecl->func_decl.class_owner, "::") == NULL) {
                char buf[256];
                snprintf(buf, sizeof(buf), "%s::%s", ns_prefix, mdecl->func_decl.class_owner);
                mdecl->func_decl.class_owner = arena_strdup(s->arena, buf);
            }
            analyze_function(s, mdecl);
        }
    } else if (decl->kind == AST_DECL_NAMESPACE) {
        const char *new_prefix = decl->ns_decl.name;
        if (ns_prefix) {
            char buf[256];
            snprintf(buf, sizeof(buf), "%s::%s", ns_prefix, decl->ns_decl.name);
            new_prefix = arena_strdup(s->arena, buf);
        }
        scope_push(s, SCOPE_NAMESPACE, decl->ns_decl.name);
        for (int d = 0; d < decl->ns_decl.count; d++) {
            sema_analyze_decls(s, decl->ns_decl.decls[d], new_prefix);
        }
        scope_pop(s);
    } else if (decl->kind == AST_STMT_EXPR) {
        analyze_stmt(s, decl);
    }
}

bool sema_analyze(Sema *s, ASTNode *program) {
    if (!program || program->kind != AST_PROGRAM) return false;

    /* Pass 1: Register class and struct definitions */
    for (int i = 0; i < program->program.count; i++) {
        sema_register_classes(s, program->program.decls[i], NULL);
    }

    /* Pass 2: Register functions, methods, and analyze bodies */
    for (int i = 0; i < program->program.count; i++) {
        sema_analyze_decls(s, program->program.decls[i], NULL);
    }

    return diag_error_count() == 0;
}
