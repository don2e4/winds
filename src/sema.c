#include "sema.h"
#include "str.h"
#include <ctype.h>

typedef struct ClassTemplate {
    const char *name;
    const char *param_names[16];
    bool is_pack[16];
    int param_count;
    bool is_variadic;
    ASTNode *class_decl;
    struct ClassTemplate *next;
} ClassTemplate;

typedef struct FuncTemplate {
    const char *name;
    const char *param_names[16];
    bool is_pack[16];
    int param_count;
    bool is_variadic;
    ASTNode *func_decl;
    struct FuncTemplate *next;
} FuncTemplate;

typedef struct TemplateEnv {
    const char *param_names[16];
    bool is_pack[16];
    int param_count;
    Type *arg_types[16];
    int total_arg_count;
    int pack_idx;
    Type **pack_args;
    int pack_arg_count;
    const char *old_cls;
    const char *new_cls;
} TemplateEnv;

static ClassTemplate *s_templates = NULL;
static FuncTemplate *s_func_templates = NULL;
static ASTNode *s_current_program = NULL;

static void sema_register_classes(Sema *s, ASTNode *decl, const char *ns_prefix);
static void sema_analyze_decls(Sema *s, ASTNode *decl, const char *ns_prefix);
static Symbol *find_class_symbol(Sema *s, const char *name);

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
        if (s->warn_unused) {
            for (Symbol *sym = s->current_scope->symbols; sym != NULL; sym = sym->next) {
                if (sym->kind == SYM_VAR && !sym->is_used && !sym->is_param && !sym->is_global) {
                    if (sym->name && sym->name[0] != '_') {
                        diag_report(DIAG_WARNING, sym->loc, "unused variable '%s'", sym->name);
                        diag_help("if this is intentional, prefix the name with an underscore '_'");
                    }
                }
            }
        }
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

static const char *find_closest_symbol(Sema *s, const char *name) {
    const char *candidates[128];
    int count = 0;
    for (Scope *sc = s->current_scope; sc != NULL && count < 128; sc = sc->parent) {
        for (Symbol *sym = sc->symbols; sym != NULL && count < 128; sym = sym->next) {
            candidates[count++] = sym->name;
        }
    }
    return diag_find_closest(name, candidates, count);
}

static Symbol *find_typedef_symbol(Scope *scope, const char *name) {
    for (Scope *sc = scope; sc != NULL; sc = sc->parent) {
        for (Symbol *sym = sc->symbols; sym != NULL; sym = sym->next) {
            if (sym->kind == SYM_TYPEDEF && (sym->name == name || (sym->name && strcmp(sym->name, name) == 0))) {
                return sym;
            }
        }
        for (int u = 0; u < sc->using_ns_count; u++) {
            char qname[256];
            snprintf(qname, sizeof(qname), "%s::%s", sc->using_namespaces[u], name);
            for (Symbol *sym = sc->symbols; sym != NULL; sym = sym->next) {
                if (sym->kind == SYM_TYPEDEF && sym->name && strcmp(sym->name, qname) == 0) {
                    return sym;
                }
            }
        }
    }
    return NULL;
}

static int score_type_match(Type *expected, Type *actual) {
    if (!expected || !actual) return 1;
    if (expected->kind == TYPE_REF) expected = expected->ref.base;
    if (actual->kind == TYPE_REF) actual = actual->ref.base;

    if (type_equals(expected, actual)) return 10;
    if (type_is_integer(expected) && type_is_integer(actual)) return 8;
    if (type_is_pointer_or_ref(expected) && type_is_pointer_or_ref(actual)) return 7;
    if (expected->kind == actual->kind) return 6;
    return 0;
}

static Symbol *find_scoped_function_overload(Scope *scope, const char *scope_prefix, const char *name, int arg_count) {
    Symbol *fallback = NULL;
    char qbuf[256];
    snprintf(qbuf, sizeof(qbuf), "%s::%s", scope_prefix, name);

    for (Scope *sc = scope; sc != NULL; sc = sc->parent) {
        for (Symbol *sym = sc->symbols; sym != NULL; sym = sym->next) {
            if (sym->kind == SYM_FUNC) {
                if (sym->ast_decl && sym->ast_decl->func_decl.is_method) continue;
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
                if (sym->ast_decl && sym->ast_decl->func_decl.is_method) continue;
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

static Symbol *find_function_overload_typed(Scope *scope, const char *name, ASTNode **args, int arg_count) {
    Symbol *best_sym = NULL;
    int best_score = 0;

    for (Scope *sc = scope; sc != NULL; sc = sc->parent) {
        for (Symbol *sym = sc->symbols; sym != NULL; sym = sym->next) {
            if (sym->kind == SYM_FUNC && (sym->name == name || (sym->name && strcmp(sym->name, name) == 0))) {
                if (sym->ast_decl && sym->ast_decl->func_decl.is_method) continue;
                if (sym->type && sym->type->kind == TYPE_FUNC) {
                    int pcount = sym->type->func.param_count;
                    bool is_va = sym->type->func.is_varargs;
                    if (pcount == arg_count || (is_va && arg_count >= pcount)) {
                        int score = 0;
                        TypeParam *tp = sym->type->func.params;
                        bool match_ok = true;
                        for (int i = 0; i < arg_count && tp != NULL; i++, tp = tp->next) {
                            Type *act_t = args[i] ? args[i]->type : NULL;
                            int s_val = score_type_match(tp->type, act_t);
                            if (s_val <= 0) { match_ok = false; break; }
                            score += s_val;
                        }
                        if (match_ok && score > best_score) {
                            best_score = score;
                            best_sym = sym;
                        }
                    }
                }
            }
        }
        for (int u = 0; u < sc->using_ns_count; u++) {
            char qbuf[256];
            snprintf(qbuf, sizeof(qbuf), "%s::%s", sc->using_namespaces[u], name);
            for (Symbol *sym = sc->symbols; sym != NULL; sym = sym->next) {
                if (sym->kind == SYM_FUNC && (sym->name == qbuf || (sym->name && strcmp(sym->name, qbuf) == 0))) {
                    if (sym->ast_decl && sym->ast_decl->func_decl.is_method) continue;
                    if (sym->type && sym->type->kind == TYPE_FUNC && sym->type->func.param_count == arg_count) {
                        int score = 0;
                        TypeParam *tp = sym->type->func.params;
                        bool match_ok = true;
                        for (int i = 0; i < arg_count && tp != NULL; i++, tp = tp->next) {
                            Type *act_t = args[i] ? args[i]->type : NULL;
                            int s_val = score_type_match(tp->type, act_t);
                            if (s_val <= 0) { match_ok = false; break; }
                            score += s_val;
                        }
                        if (match_ok && score > best_score) {
                            best_score = score;
                            best_sym = sym;
                        }
                    }
                }
            }
        }
    }
    return best_sym ? best_sym : find_function_overload(scope, name, arg_count);
}

static bool match_class_names(const char *a, const char *b) {
    if (!a || !b) return false;
    if (strcmp(a, b) == 0) return true;
    const char *sa = strrchr(a, ':');
    sa = sa ? sa + 1 : a;
    const char *sb = strrchr(b, ':');
    sb = sb ? sb + 1 : b;
    return strcmp(sa, sb) == 0;
}

static Symbol *find_method_overload(Scope *scope, const char *cls_name, const char *method_name, int arg_count) {
    Symbol *fallback = NULL;
    for (Scope *sc = scope; sc != NULL; sc = sc->parent) {
        for (Symbol *sym = sc->symbols; sym != NULL; sym = sym->next) {
            if (sym->kind == SYM_FUNC && (sym->name == method_name || (sym->name && strcmp(sym->name, method_name) == 0))) {
                if (sym->ast_decl && sym->ast_decl->func_decl.class_owner &&
                    match_class_names(sym->ast_decl->func_decl.class_owner, cls_name)) {
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

static Symbol *find_method_overload_typed(Scope *scope, const char *cls_name, const char *method_name, ASTNode **args, int arg_count) {
    Symbol *best_sym = NULL;
    int best_score = 0;

    for (Scope *sc = scope; sc != NULL; sc = sc->parent) {
        for (Symbol *sym = sc->symbols; sym != NULL; sym = sym->next) {
            if (sym->kind == SYM_FUNC && (sym->name == method_name || (sym->name && strcmp(sym->name, method_name) == 0))) {
                if (sym->ast_decl && sym->ast_decl->func_decl.class_owner &&
                    match_class_names(sym->ast_decl->func_decl.class_owner, cls_name)) {
                    if (sym->type && sym->type->kind == TYPE_FUNC) {
                        int pcount = sym->type->func.param_count;
                        bool is_va = sym->type->func.is_varargs;
                        if (pcount == arg_count || (is_va && arg_count >= pcount)) {
                            int score = 0;
                            bool match_ok = true;
                            TypeParam *tp = sym->type->func.params;
                            for (int i = 0; i < arg_count && tp != NULL; i++, tp = tp->next) {
                                Type *act_t = args[i] ? args[i]->type : NULL;
                                int s_val = score_type_match(tp->type, act_t);
                                if (s_val <= 0) { match_ok = false; break; }
                                score += s_val;
                            }
                            if (match_ok && score > best_score) {
                                best_score = score;
                                best_sym = sym;
                            }
                        }
                    }
                }
            }
        }
    }
    return best_sym;
}

static Type *substitute_type(Arena *arena, Type *t, TemplateEnv *env) {
    if (!t || !env) return t;
    if (t->kind == TYPE_CLASS && t->name) {
        if (env->old_cls && env->new_cls) {
            const char *old_short = strrchr(env->old_cls, ':');
            old_short = old_short ? old_short + 1 : env->old_cls;
            const char *t_short = strrchr(t->name, ':');
            t_short = t_short ? t_short + 1 : t->name;
            if (strcmp(t->name, env->old_cls) == 0 || strcmp(t_short, old_short) == 0) {
                Type *nt = type_new(arena, TYPE_CLASS);
                nt->name = env->new_cls;
                nt->size = 8;
                nt->align = 8;
                return nt;
            }
        }
        for (int i = 0; i < env->param_count; i++) {
            if (env->is_pack[i]) continue;
            if (strcmp(t->name, env->param_names[i]) == 0) {
                return env->arg_types[i];
            }
        }
        if (env->pack_idx != -1 && strcmp(t->name, env->param_names[env->pack_idx]) == 0) {
            if (env->pack_arg_count > 0 && env->pack_args && env->pack_args[0]) {
                return env->pack_args[0];
            }
            return g_type_void;
        }

        const char *sep = strstr(t->name, "__");
        if (sep) {
            char base[128];
            size_t blen = (size_t)(sep - t->name);
            if (blen < sizeof(base)) {
                strncpy(base, t->name, blen);
                base[blen] = '\0';
                char buf[256];
                int written = snprintf(buf, sizeof(buf), "%s__", base);
                const char *p = sep + 2;
                bool any_sub = false;
                bool first_arg = true;
                while (*p && written < (int)sizeof(buf) - 1) {
                    char arg[64];
                    const char *next_sep = strchr(p, '_');
                    size_t alen = next_sep ? (size_t)(next_sep - p) : strlen(p);
                    if (alen >= sizeof(arg)) break;
                    strncpy(arg, p, alen);
                    arg[alen] = '\0';

                    size_t arg_len = strlen(arg);
                    bool is_arg_pack = (arg_len > 3 && strcmp(arg + arg_len - 3, "...") == 0);
                    char base_arg[64];
                    if (is_arg_pack) {
                        memcpy(base_arg, arg, arg_len - 3);
                        base_arg[arg_len - 3] = '\0';
                    } else {
                        strncpy(base_arg, arg, sizeof(base_arg) - 1);
                        base_arg[sizeof(base_arg) - 1] = '\0';
                    }

                    if (is_arg_pack && env->pack_idx != -1 && strcmp(base_arg, env->param_names[env->pack_idx]) == 0) {
                        any_sub = true;
                        for (int k = 0; k < env->pack_arg_count; k++) {
                            const char *rep = (env->pack_args[k] && env->pack_args[k]->name) ? env->pack_args[k]->name : "int";
                            if (!first_arg) written += snprintf(buf + written, sizeof(buf) - written, "_");
                            written += snprintf(buf + written, sizeof(buf) - written, "%s", rep);
                            first_arg = false;
                        }
                    } else {
                        const char *rep = arg;
                        for (int i = 0; i < env->param_count; i++) {
                            if (strcmp(base_arg, env->param_names[i]) == 0) {
                                if (env->is_pack[i]) {
                                    if (env->pack_arg_count > 0 && env->pack_args && env->pack_args[0] && env->pack_args[0]->name) {
                                        rep = env->pack_args[0]->name;
                                        any_sub = true;
                                    }
                                } else if (env->arg_types[i] && env->arg_types[i]->name) {
                                    rep = env->arg_types[i]->name;
                                    any_sub = true;
                                }
                                break;
                            }
                        }
                        if (!first_arg) written += snprintf(buf + written, sizeof(buf) - written, "_");
                        written += snprintf(buf + written, sizeof(buf) - written, "%s", rep);
                        first_arg = false;
                    }

                    if (!next_sep) break;
                    p = next_sep + 1;
                }
                if (any_sub) {
                    Type *nt = type_new(arena, TYPE_CLASS);
                    nt->name = arena_strdup(arena, buf);
                    nt->size = 8;
                    nt->align = 8;
                    return nt;
                }
            }
        }
        return t;
    }
    if (t->kind == TYPE_PTR) {
        Type *nb = substitute_type(arena, t->ptr.base, env);
        return type_ptr(arena, nb);
    }
    if (t->kind == TYPE_REF) {
        Type *nb = substitute_type(arena, t->ref.base, env);
        return type_ref(arena, nb);
    }
    if (t->kind == TYPE_ARRAY) {
        Type *nb = substitute_type(arena, t->array.base, env);
        return type_array(arena, nb, t->array.count);
    }
    if (t->kind == TYPE_FUNC) {
        Type *ret = substitute_type(arena, t->func.return_type, env);
        TypeParam *phead = NULL;
        TypeParam **ptail = &phead;
        int pcount = 0;
        for (TypeParam *tp = t->func.params; tp != NULL; tp = tp->next) {
            bool is_pack_tp = false;
            if (tp->type && tp->type->name && env->pack_idx != -1) {
                if (strstr(tp->type->name, env->param_names[env->pack_idx])) {
                    is_pack_tp = true;
                }
            }
            if (is_pack_tp && env->pack_idx != -1) {
                for (int k = 0; k < env->pack_arg_count; k++) {
                    TypeParam *ntp = arena_alloc_zero(arena, sizeof(TypeParam));
                    ntp->type = env->pack_args[k];
                    char pbuf[64];
                    snprintf(pbuf, sizeof(pbuf), "%s_%d", tp->name ? tp->name : "arg", k);
                    ntp->name = arena_strdup(arena, pbuf);
                    *ptail = ntp;
                    ptail = &ntp->next;
                    pcount++;
                }
            } else {
                TypeParam *ntp = arena_alloc_zero(arena, sizeof(TypeParam));
                ntp->type = substitute_type(arena, tp->type, env);
                ntp->name = tp->name;
                *ptail = ntp;
                ptail = &ntp->next;
                pcount++;
            }
        }
        return type_func(arena, ret, phead, pcount, t->func.is_varargs);
    }
    if (t->kind == TYPE_MEMBER_PTR) {
        Type *cls = substitute_type(arena, t->member_ptr.class_type, env);
        Type *mem = substitute_type(arena, t->member_ptr.member_type, env);
        return type_member_ptr(arena, cls, mem);
    }
    if (t->kind == TYPE_MEMBER_FUNC_PTR) {
        Type *cls = substitute_type(arena, t->member_func_ptr.class_type, env);
        Type *fn = substitute_type(arena, t->member_func_ptr.func_type, env);
        return type_member_func_ptr(arena, cls, fn);
    }
    return t;
}

static ASTNode *clone_and_substitute_ast(Arena *arena, ASTNode *node, TemplateEnv *env) {
    if (!node || !env) return node;
    ASTNode *res = ast_new(arena, node->kind, node->loc);
    *res = *node;

    switch (node->kind) {
        case AST_STMT_BLOCK: {
            if (node->block.count > 0) {
                ASTNode **stmts = arena_alloc(arena, sizeof(ASTNode*) * node->block.count);
                for (int i = 0; i < node->block.count; i++) {
                    stmts[i] = clone_and_substitute_ast(arena, node->block.stmts[i], env);
                }
                res->block.stmts = stmts;
            }
            break;
        }
        case AST_STMT_EXPR:
            res->stmt_expr.expr = clone_and_substitute_ast(arena, node->stmt_expr.expr, env);
            break;
        case AST_STMT_VAR_DECL:
            res->var_decl.var_type = substitute_type(arena, node->var_decl.var_type, env);
            res->var_decl.init = clone_and_substitute_ast(arena, node->var_decl.init, env);
            res->var_decl.sym = NULL;
            break;
        case AST_STMT_IF:
            res->if_stmt.cond = clone_and_substitute_ast(arena, node->if_stmt.cond, env);
            res->if_stmt.then_branch = clone_and_substitute_ast(arena, node->if_stmt.then_branch, env);
            res->if_stmt.else_branch = clone_and_substitute_ast(arena, node->if_stmt.else_branch, env);
            break;
        case AST_STMT_WHILE:
            res->while_stmt.cond = clone_and_substitute_ast(arena, node->while_stmt.cond, env);
            res->while_stmt.body = clone_and_substitute_ast(arena, node->while_stmt.body, env);
            break;
        case AST_STMT_FOR:
            res->for_stmt.init = clone_and_substitute_ast(arena, node->for_stmt.init, env);
            res->for_stmt.cond = clone_and_substitute_ast(arena, node->for_stmt.cond, env);
            res->for_stmt.step = clone_and_substitute_ast(arena, node->for_stmt.step, env);
            res->for_stmt.body = clone_and_substitute_ast(arena, node->for_stmt.body, env);
            break;
        case AST_STMT_RETURN:
            res->ret_stmt.expr = clone_and_substitute_ast(arena, node->ret_stmt.expr, env);
            break;
        case AST_BINARY:
            res->binary.left = clone_and_substitute_ast(arena, node->binary.left, env);
            res->binary.right = clone_and_substitute_ast(arena, node->binary.right, env);
            break;
        case AST_UNARY:
            res->unary.operand = clone_and_substitute_ast(arena, node->unary.operand, env);
            break;
        case AST_ASSIGN:
            res->assign.target = clone_and_substitute_ast(arena, node->assign.target, env);
            res->assign.value = clone_and_substitute_ast(arena, node->assign.value, env);
            break;
        case AST_INDEX:
            res->index_expr.target = clone_and_substitute_ast(arena, node->index_expr.target, env);
            res->index_expr.index = clone_and_substitute_ast(arena, node->index_expr.index, env);
            break;
        case AST_CALL: {
            res->call.callee = clone_and_substitute_ast(arena, node->call.callee, env);
            res->call.object = clone_and_substitute_ast(arena, node->call.object, env);
            if (node->call.arg_count > 0) {
                int max_args = node->call.arg_count + (env->pack_arg_count > 0 ? env->pack_arg_count * 2 : 1) + 16;
                ASTNode **args = arena_alloc(arena, sizeof(ASTNode*) * max_args);
                int new_arg_count = 0;
                for (int i = 0; i < node->call.arg_count; i++) {
                    ASTNode *arg = node->call.args[i];
                    bool is_pack_arg = false;
                    const char *pack_var_name = NULL;
                    if (arg->kind == AST_PACK_EXPANSION) {
                        is_pack_arg = true;
                        if (arg->pack_expansion.expr && arg->pack_expansion.expr->kind == AST_VAR_REF) {
                            pack_var_name = arg->pack_expansion.expr->var_ref.name;
                        }
                    } else if (arg->kind == AST_VAR_REF && env->pack_idx != -1) {
                        if (strcmp(arg->var_ref.name, env->param_names[env->pack_idx]) == 0) {
                            is_pack_arg = true;
                            pack_var_name = arg->var_ref.name;
                        }
                    }
                    if (is_pack_arg && env->pack_idx != -1) {
                        for (int k = 0; k < env->pack_arg_count; k++) {
                            char vbuf[64];
                            snprintf(vbuf, sizeof(vbuf), "%s_%d", pack_var_name ? pack_var_name : "arg", k);
                            ASTNode *vref = ast_new(arena, AST_VAR_REF, arg->loc);
                            vref->var_ref.name = arena_strdup(arena, vbuf);
                            args[new_arg_count++] = vref;
                        }
                    } else {
                        args[new_arg_count++] = clone_and_substitute_ast(arena, arg, env);
                    }
                }
                res->call.args = args;
                res->call.arg_count = new_arg_count;
            }
            if (node->call.name) {
                if (strstr(node->call.name, "__")) {
                    Type dummy;
                    memset(&dummy, 0, sizeof(dummy));
                    dummy.kind = TYPE_CLASS;
                    dummy.name = node->call.name;
                    Type *subbed = substitute_type(arena, &dummy, env);
                    if (subbed && subbed->name) {
                        res->call.name = subbed->name;
                    }
                } else if (env->old_cls) {
                    const char *old_short = strrchr(env->old_cls, ':');
                    old_short = old_short ? old_short + 1 : env->old_cls;
                    const char *name_short = strrchr(node->call.name, ':');
                    name_short = name_short ? name_short + 1 : node->call.name;
                    if (strcmp(node->call.name, env->old_cls) == 0 || strcmp(name_short, old_short) == 0) {
                        res->call.name = env->new_cls;
                    }
                }
            }
            res->call.callee_sym = NULL;
            res->call.mangled_name = NULL;
            break;
        }
        case AST_MEMBER:
            res->member.object = clone_and_substitute_ast(arena, node->member.object, env);
            res->member.field = NULL;
            break;
        case AST_MEMBER_PTR_ACCESS:
            res->member_ptr_access.object = clone_and_substitute_ast(arena, node->member_ptr_access.object, env);
            res->member_ptr_access.member_ptr = clone_and_substitute_ast(arena, node->member_ptr_access.member_ptr, env);
            break;
        case AST_NEW: {
            res->new_expr.target_type = substitute_type(arena, node->new_expr.target_type, env);
            if (node->new_expr.arg_count > 0) {
                int max_args = node->new_expr.arg_count + (env->pack_arg_count > 0 ? env->pack_arg_count * 2 : 1) + 16;
                ASTNode **args = arena_alloc(arena, sizeof(ASTNode*) * max_args);
                int new_arg_count = 0;
                for (int i = 0; i < node->new_expr.arg_count; i++) {
                    ASTNode *arg = node->new_expr.args[i];
                    bool is_pack_arg = false;
                    const char *pack_var_name = NULL;
                    if (arg->kind == AST_PACK_EXPANSION) {
                        is_pack_arg = true;
                        if (arg->pack_expansion.expr && arg->pack_expansion.expr->kind == AST_VAR_REF) {
                            pack_var_name = arg->pack_expansion.expr->var_ref.name;
                        }
                    } else if (arg->kind == AST_VAR_REF && env->pack_idx != -1) {
                        if (strcmp(arg->var_ref.name, env->param_names[env->pack_idx]) == 0) {
                            is_pack_arg = true;
                            pack_var_name = arg->var_ref.name;
                        }
                    }
                    if (is_pack_arg && env->pack_idx != -1) {
                        for (int k = 0; k < env->pack_arg_count; k++) {
                            char vbuf[64];
                            snprintf(vbuf, sizeof(vbuf), "%s_%d", pack_var_name ? pack_var_name : "arg", k);
                            ASTNode *vref = ast_new(arena, AST_VAR_REF, arg->loc);
                            vref->var_ref.name = arena_strdup(arena, vbuf);
                            args[new_arg_count++] = vref;
                        }
                    } else {
                        args[new_arg_count++] = clone_and_substitute_ast(arena, arg, env);
                    }
                }
                res->new_expr.args = args;
                res->new_expr.arg_count = new_arg_count;
            }
            break;
        }
        case AST_DELETE:
            res->delete_expr.target = clone_and_substitute_ast(arena, node->delete_expr.target, env);
            break;
        case AST_CAST:
            res->cast.target_type = substitute_type(arena, node->cast.target_type, env);
            res->cast.expr = clone_and_substitute_ast(arena, node->cast.expr, env);
            break;
        case AST_SIZEOF:
            if (node->sizeof_expr.target_type) {
                res->sizeof_expr.target_type = substitute_type(arena, node->sizeof_expr.target_type, env);
            }
            if (node->sizeof_expr.target_expr) {
                if (node->sizeof_expr.target_expr->kind == AST_VAR_REF && env->pack_idx != -1 &&
                    strcmp(node->sizeof_expr.target_expr->var_ref.name, env->param_names[env->pack_idx]) == 0) {
                    res->kind = AST_LIT_INT;
                    res->int_val = env->pack_arg_count;
                    res->type = g_type_long;
                    return res;
                }
                res->sizeof_expr.target_expr = clone_and_substitute_ast(arena, node->sizeof_expr.target_expr, env);
            }
            break;
        case AST_PACK_EXPANSION:
            res->pack_expansion.expr = clone_and_substitute_ast(arena, node->pack_expansion.expr, env);
            break;
        default:
            break;
    }
    return res;
}

static Symbol *try_instantiate_class_template(Sema *s, const char *name) {
    const char *sep = strstr(name, "__");
    if (!sep) return NULL;

    char base_name[128];
    size_t base_len = sep - name;
    if (base_len >= sizeof(base_name)) return NULL;
    strncpy(base_name, name, base_len);
    base_name[base_len] = '\0';

    const char *p = sep + 2;
    Type *arg_types[16] = {0};
    int arg_count = 0;
    while (*p && arg_count < 16) {
        char arg_name[64];
        const char *next_sep = strchr(p, '_');
        size_t len = next_sep ? (size_t)(next_sep - p) : strlen(p);
        if (len >= sizeof(arg_name)) break;
        strncpy(arg_name, p, len);
        arg_name[len] = '\0';

        Type *at = NULL;
        if (strcmp(arg_name, "int") == 0) at = g_type_int;
        else if (strcmp(arg_name, "long") == 0) at = g_type_long;
        else if (strcmp(arg_name, "char") == 0) at = g_type_char;
        else if (strcmp(arg_name, "bool") == 0) at = g_type_bool;
        else if (strcmp(arg_name, "void") == 0) at = g_type_void;
        else {
            Symbol *sym = find_class_symbol(s, arg_name);
            if (sym && sym->type) at = sym->type;
            else {
                at = type_new(s->arena, TYPE_CLASS);
                at->name = arena_strdup(s->arena, arg_name);
                at->size = 8;
                at->align = 8;
            }
        }
        arg_types[arg_count++] = at;
        if (!next_sep) break;
        p = next_sep + 1;
    }

    ClassTemplate *best_ct = NULL;
    for (ClassTemplate *it = s_templates; it != NULL; it = it->next) {
        if (strcmp(it->name, base_name) == 0 || match_class_names(it->name, base_name)) {
            if (!it->is_variadic && it->param_count == arg_count) {
                best_ct = it;
                break;
            } else if (it->is_variadic && !best_ct) {
                int fixed_params = 0;
                for (int i = 0; i < it->param_count; i++) {
                    if (!it->is_pack[i]) fixed_params++;
                }
                if (arg_count >= fixed_params) {
                    best_ct = it;
                }
            }
        }
    }
    if (!best_ct) return NULL;
    ClassTemplate *ct = best_ct;

    char canon_name[256];
    snprintf(canon_name, sizeof(canon_name), "%s%s", ct->name, sep);
    const char *short_base = strrchr(ct->name, ':');
    short_base = short_base ? short_base + 1 : ct->name;
    char short_name[256];
    snprintf(short_name, sizeof(short_name), "%s%s", short_base, sep);

    for (Scope *sc = s->global_scope; sc != NULL; sc = sc->parent) {
        for (Symbol *sym = sc->symbols; sym != NULL; sym = sym->next) {
            if (sym->kind == SYM_CLASS && sym->name && strcmp(sym->name, canon_name) == 0) {
                if (strcmp(canon_name, short_name) != 0) {
                    bool has_short = false;
                    for (Symbol *ss = s->global_scope->symbols; ss != NULL; ss = ss->next) {
                        if (ss->kind == SYM_CLASS && ss->name && strcmp(ss->name, short_name) == 0) {
                            has_short = true;
                            break;
                        }
                    }
                    if (!has_short) {
                        Symbol *short_sym = arena_alloc_zero(s->arena, sizeof(Symbol));
                        short_sym->kind = SYM_CLASS;
                        short_sym->name = arena_strdup(s->arena, short_name);
                        short_sym->type = sym->type;
                        short_sym->loc = sym->loc;
                        short_sym->is_global = true;
                        add_symbol(s->global_scope, short_sym);
                    }
                }
                return sym;
            }
        }
    }

    int pack_idx = -1;
    for (int i = 0; i < ct->param_count; i++) {
        if (ct->is_pack[i]) {
            pack_idx = i;
            break;
        }
    }

    const char *inst_name = arena_strdup(s->arena, canon_name);
    ASTNode *new_cls = ast_new(s->arena, AST_DECL_CLASS, ct->class_decl->loc);
    new_cls->class_decl.name = inst_name;

    TemplateEnv env;
    memset(&env, 0, sizeof(env));
    env.param_count = ct->param_count;
    for (int i = 0; i < ct->param_count; i++) {
        env.param_names[i] = ct->param_names[i];
        env.is_pack[i] = ct->is_pack[i];
    }
    for (int i = 0; i < arg_count && i < 16; i++) {
        env.arg_types[i] = arg_types[i];
    }
    env.total_arg_count = arg_count;
    env.pack_idx = pack_idx;
    env.pack_args = (pack_idx != -1) ? &arg_types[pack_idx] : NULL;
    env.pack_arg_count = (pack_idx != -1) ? (arg_count >= pack_idx ? arg_count - pack_idx : 0) : 0;
    env.old_cls = ct->class_decl->class_decl.name;
    env.new_cls = inst_name;

    if (ct->class_decl->class_decl.fields == NULL && ct->class_decl->class_decl.method_count == 0) {
        new_cls->class_decl.fields = NULL;
        new_cls->class_decl.methods = NULL;
        new_cls->class_decl.method_count = 0;
        sema_register_classes(s, new_cls, NULL);
        if (s_current_program) {
            s_current_program->program.decls[s_current_program->program.count++] = new_cls;
        }
        return find_class_symbol(s, canon_name);
    }

    Field *new_fields_head = NULL;
    Field **new_fields_tail = &new_fields_head;
    for (Field *f = ct->class_decl->class_decl.fields; f != NULL; f = f->next) {
        Field *nf = arena_alloc_zero(s->arena, sizeof(Field));
        nf->name = f->name;
        nf->type = substitute_type(s->arena, f->type, &env);
        nf->access = f->access;
        *new_fields_tail = nf;
        new_fields_tail = &nf->next;
    }
    new_cls->class_decl.fields = new_fields_head;

    int mcount = ct->class_decl->class_decl.method_count;
    ASTNode **new_methods = arena_alloc(s->arena, sizeof(ASTNode*) * (mcount + 1));
    for (int m = 0; m < mcount; m++) {
        ASTNode *orig_m = ct->class_decl->class_decl.methods[m];
        ASTNode *nm = ast_new(s->arena, AST_DECL_FUNC, orig_m->loc);
        nm->func_decl = orig_m->func_decl;
        nm->func_decl.class_owner = inst_name;
        const char *old_short = strrchr(ct->class_decl->class_decl.name, ':');
        old_short = old_short ? old_short + 1 : ct->class_decl->class_decl.name;
        const char *m_short = strrchr(orig_m->func_decl.name, ':');
        m_short = m_short ? m_short + 1 : orig_m->func_decl.name;
        if (strcmp(orig_m->func_decl.name, ct->class_decl->class_decl.name) == 0 || strcmp(m_short, old_short) == 0) {
            nm->func_decl.name = inst_name;
            nm->func_decl.is_ctor = true;
        }
        nm->func_decl.func_type = substitute_type(s->arena, orig_m->func_decl.func_type, &env);

        if (orig_m->func_decl.param_count > 0) {
            int max_params = orig_m->func_decl.param_count + (env.pack_arg_count > 0 ? env.pack_arg_count * 2 : 1) + 16;
            ASTNode **new_params = arena_alloc(s->arena, sizeof(ASTNode*) * max_params);
            int new_pcount = 0;
            for (int pi = 0; pi < orig_m->func_decl.param_count; pi++) {
                ASTNode *pnode = orig_m->func_decl.params[pi];
                bool is_pack_p = pnode->var_decl.is_pack;
                if (!is_pack_p && env.pack_idx != -1 && pnode->var_decl.var_type && pnode->var_decl.var_type->name) {
                    if (strstr(pnode->var_decl.var_type->name, env.param_names[env.pack_idx])) {
                        is_pack_p = true;
                    }
                }
                if (is_pack_p && env.pack_idx != -1) {
                    for (int k = 0; k < env.pack_arg_count; k++) {
                        char pbuf[64];
                        snprintf(pbuf, sizeof(pbuf), "%s_%d", pnode->var_decl.name, k);
                        ASTNode *np = ast_new(s->arena, AST_STMT_VAR_DECL, pnode->loc);
                        np->var_decl.name = arena_strdup(s->arena, pbuf);
                        np->var_decl.var_type = env.pack_args[k];
                        new_params[new_pcount++] = np;
                    }
                } else {
                    ASTNode *np = ast_new(s->arena, AST_STMT_VAR_DECL, pnode->loc);
                    np->var_decl.name = pnode->var_decl.name;
                    np->var_decl.var_type = substitute_type(s->arena, pnode->var_decl.var_type, &env);
                    new_params[new_pcount++] = np;
                }
            }
            nm->func_decl.params = new_params;
            nm->func_decl.param_count = new_pcount;
        }

        nm->func_decl.body = clone_and_substitute_ast(s->arena, orig_m->func_decl.body, &env);
        new_methods[m] = nm;
    }
    new_cls->class_decl.methods = new_methods;
    new_cls->class_decl.method_count = mcount;

    sema_register_classes(s, new_cls, NULL);
    sema_analyze_decls(s, new_cls, NULL);

    if (strcmp(canon_name, short_name) != 0) {
        Symbol *short_sym = arena_alloc_zero(s->arena, sizeof(Symbol));
        short_sym->kind = SYM_CLASS;
        short_sym->name = arena_strdup(s->arena, short_name);
        short_sym->type = new_cls->class_decl.class_type;
        short_sym->loc = new_cls->loc;
        short_sym->is_global = true;
        add_symbol(s->global_scope, short_sym);
    }

    if (s_current_program) {
        s_current_program->program.decls[s_current_program->program.count++] = new_cls;
    }

    return find_class_symbol(s, canon_name);
}

static Symbol *find_class_symbol(Sema *s, const char *name) {
    if (!name) return NULL;
    for (Scope *sc = s->global_scope; sc != NULL; sc = sc->parent) {
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
    return try_instantiate_class_template(s, name);
}

static Type *resolve_type(Sema *s, Type *t) {
    if (!t) return g_type_int;
    if (t->kind == TYPE_CLASS && t->name != NULL) {
        Symbol *td_sym = find_typedef_symbol(s->current_scope, t->name);
        if (td_sym && td_sym->type) {
            return resolve_type(s, td_sym->type);
        }
        if (t->cls.fields == NULL) {
            Symbol *csym = find_class_symbol(s, t->name);
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
    if (t->kind == TYPE_ARRAY) {
        Type *base = resolve_type(s, t->array.base);
        if (base != t->array.base) {
            return type_array(s->arena, base, t->array.count);
        }
        return t;
    }
    if (t->kind == TYPE_FUNC) {
        Type *ret = resolve_type(s, t->func.return_type);
        TypeParam *phead = NULL;
        TypeParam **ptail = &phead;
        for (TypeParam *tp = t->func.params; tp != NULL; tp = tp->next) {
            TypeParam *ntp = arena_alloc_zero(s->arena, sizeof(TypeParam));
            ntp->type = resolve_type(s, tp->type);
            ntp->name = tp->name;
            *ptail = ntp;
            ptail = &ntp->next;
        }
        return type_func(s->arena, ret, phead, t->func.param_count, t->func.is_varargs);
    }
    if (t->kind == TYPE_MEMBER_PTR) {
        Type *cls = resolve_type(s, t->member_ptr.class_type);
        Type *mem = resolve_type(s, t->member_ptr.member_type);
        if (cls != t->member_ptr.class_type || mem != t->member_ptr.member_type) {
            return type_member_ptr(s->arena, cls, mem);
        }
        return t;
    }
    if (t->kind == TYPE_MEMBER_FUNC_PTR) {
        Type *cls = resolve_type(s, t->member_func_ptr.class_type);
        Type *fn = resolve_type(s, t->member_func_ptr.func_type);
        if (cls != t->member_func_ptr.class_type || fn != t->member_func_ptr.func_type) {
            return type_member_func_ptr(s->arena, cls, fn);
        }
        return t;
    }
    return t;
}

static Symbol *try_instantiate_func_template(Sema *s, ASTNode *call_expr) {
    if (!call_expr || !call_expr->call.name) return NULL;

    const char *call_name = call_expr->call.name;

    /* Base name of function: could be "make_tuple" or "make_tuple__int_int" */
    char base_name[128];
    const char *sep = strstr(call_name, "__");
    if (sep) {
        size_t blen = (size_t)(sep - call_name);
        if (blen >= sizeof(base_name)) return NULL;
        memcpy(base_name, call_name, blen);
        base_name[blen] = '\0';
    } else {
        snprintf(base_name, sizeof(base_name), "%s", call_name);
    }

    FuncTemplate *ft = NULL;
    for (FuncTemplate *it = s_func_templates; it != NULL; it = it->next) {
        if (strcmp(it->name, base_name) == 0 || match_class_names(it->name, base_name)) {
            ft = it;
            break;
        }
    }
    if (!ft) return NULL;

    /* Deduce or extract template arguments */
    Type *arg_types[16];
    int arg_count = 0;

    if (sep) {
        /* Explicit template arguments, e.g. fn__int_double */
        const char *p = sep + 2;
        while (*p && arg_count < 16) {
            char aname[64];
            const char *next_sep = strchr(p, '_');
            size_t alen = next_sep ? (size_t)(next_sep - p) : strlen(p);
            if (alen >= sizeof(aname)) break;
            memcpy(aname, p, alen);
            aname[alen] = '\0';

            Type *at = NULL;
            if (strcmp(aname, "int") == 0) at = g_type_int;
            else if (strcmp(aname, "char") == 0) at = g_type_char;
            else if (strcmp(aname, "long") == 0) at = g_type_long;
            else if (strcmp(aname, "bool") == 0) at = g_type_bool;
            else if (strcmp(aname, "void") == 0) at = g_type_void;
            else {
                Symbol *csym = find_class_symbol(s, aname);
                if (csym && csym->type) at = csym->type;
                else {
                    at = type_new(s->arena, TYPE_CLASS);
                    at->name = arena_strdup(s->arena, aname);
                    at->size = 8;
                    at->align = 8;
                }
            }
            arg_types[arg_count++] = at;
            if (!next_sep) break;
            p = next_sep + 1;
        }
    } else {
        /* Deduce from call argument types */
        for (int i = 0; i < call_expr->call.arg_count && i < 16; i++) {
            Type *at = call_expr->call.args[i] ? call_expr->call.args[i]->type : g_type_int;
            if (at && at->kind == TYPE_REF) at = at->ref.base;
            arg_types[arg_count++] = at ? at : g_type_int;
        }
    }

    /* Build canonical specialized function name: base_name__arg0_arg1 */
    char canon_name[256];
    int written = snprintf(canon_name, sizeof(canon_name), "%s__", ft->name);
    for (int i = 0; i < arg_count; i++) {
        const char *aname = (arg_types[i] && arg_types[i]->name) ? arg_types[i]->name : "int";
        if (i > 0) written += snprintf(canon_name + written, sizeof(canon_name) - written, "_");
        written += snprintf(canon_name + written, sizeof(canon_name) - written, "%s", aname);
    }

    const char *short_base = strrchr(ft->name, ':');
    short_base = short_base ? short_base + 1 : ft->name;
    char short_name[256];
    int swritten = snprintf(short_name, sizeof(short_name), "%s__", short_base);
    for (int i = 0; i < arg_count; i++) {
        const char *aname = (arg_types[i] && arg_types[i]->name) ? arg_types[i]->name : "int";
        if (i > 0) swritten += snprintf(short_name + swritten, sizeof(short_name) - swritten, "_");
        swritten += snprintf(short_name + swritten, sizeof(short_name) - swritten, "%s", aname);
    }

    /* Check if already instantiated */
    for (Scope *sc = s->global_scope; sc != NULL; sc = sc->parent) {
        for (Symbol *sym = sc->symbols; sym != NULL; sym = sym->next) {
            if (sym->kind == SYM_FUNC && sym->name &&
                (strcmp(sym->name, canon_name) == 0 || strcmp(sym->name, short_name) == 0)) {
                return sym;
            }
        }
    }

    /* Build TemplateEnv */
    int pack_idx = -1;
    for (int i = 0; i < ft->param_count; i++) {
        if (ft->is_pack[i]) {
            pack_idx = i;
            break;
        }
    }

    TemplateEnv env;
    memset(&env, 0, sizeof(env));
    env.param_count = ft->param_count;
    for (int i = 0; i < ft->param_count; i++) {
        env.param_names[i] = ft->param_names[i];
        env.is_pack[i] = ft->is_pack[i];
    }
    for (int i = 0; i < arg_count && i < 16; i++) {
        env.arg_types[i] = arg_types[i];
    }
    env.total_arg_count = arg_count;
    env.pack_idx = pack_idx;
    env.pack_args = (pack_idx != -1) ? &arg_types[pack_idx] : NULL;
    env.pack_arg_count = (pack_idx != -1) ? (arg_count >= pack_idx ? arg_count - pack_idx : 0) : 0;
    env.old_cls = NULL;
    env.new_cls = NULL;

    /* Instantiate new function AST */
    ASTNode *orig_fn = ft->func_decl;
    ASTNode *new_fn = ast_new(s->arena, AST_DECL_FUNC, orig_fn->loc);
    new_fn->func_decl = orig_fn->func_decl;
    new_fn->func_decl.name = arena_strdup(s->arena, canon_name);
    new_fn->func_decl.func_type = substitute_type(s->arena, orig_fn->func_decl.func_type, &env);

    if (orig_fn->func_decl.param_count > 0) {
        int max_params = orig_fn->func_decl.param_count + (env.pack_arg_count > 0 ? env.pack_arg_count * 2 : 1) + 16;
        ASTNode **new_params = arena_alloc(s->arena, sizeof(ASTNode*) * max_params);
        int new_pcount = 0;
        for (int pi = 0; pi < orig_fn->func_decl.param_count; pi++) {
            ASTNode *pnode = orig_fn->func_decl.params[pi];
            bool is_pack_p = pnode->var_decl.is_pack;
            if (!is_pack_p && env.pack_idx != -1 && pnode->var_decl.var_type && pnode->var_decl.var_type->name) {
                if (strstr(pnode->var_decl.var_type->name, env.param_names[env.pack_idx])) {
                    is_pack_p = true;
                }
            }
            if (is_pack_p && env.pack_idx != -1) {
                for (int k = 0; k < env.pack_arg_count; k++) {
                    char pbuf[64];
                    snprintf(pbuf, sizeof(pbuf), "%s_%d", pnode->var_decl.name, k);
                    ASTNode *np = ast_new(s->arena, AST_STMT_VAR_DECL, pnode->loc);
                    np->var_decl.name = arena_strdup(s->arena, pbuf);
                    np->var_decl.var_type = env.pack_args[k];
                    new_params[new_pcount++] = np;
                }
            } else {
                ASTNode *np = ast_new(s->arena, AST_STMT_VAR_DECL, pnode->loc);
                np->var_decl.name = pnode->var_decl.name;
                np->var_decl.var_type = substitute_type(s->arena, pnode->var_decl.var_type, &env);
                new_params[new_pcount++] = np;
            }
        }
        new_fn->func_decl.params = new_params;
        new_fn->func_decl.param_count = new_pcount;
    }

    new_fn->func_decl.body = clone_and_substitute_ast(s->arena, orig_fn->func_decl.body, &env);

    sema_analyze_decls(s, new_fn, NULL);

    if (s_current_program) {
        s_current_program->program.decls[s_current_program->program.count++] = new_fn;
    }

    Symbol *main_sym = NULL;
    for (Scope *sc = s->global_scope; sc != NULL; sc = sc->parent) {
        for (Symbol *sym = sc->symbols; sym != NULL; sym = sym->next) {
            if (sym->kind == SYM_FUNC && sym->name && strcmp(sym->name, canon_name) == 0) {
                main_sym = sym;
                break;
            }
        }
        if (main_sym) break;
    }

    if (strcmp(canon_name, short_name) != 0) {
        Symbol *short_sym = arena_alloc_zero(s->arena, sizeof(Symbol));
        short_sym->kind = SYM_FUNC;
        short_sym->name = arena_strdup(s->arena, short_name);
        short_sym->type = new_fn->func_decl.func_type;
        short_sym->loc = new_fn->loc;
        short_sym->is_global = true;
        short_sym->mangled_name = main_sym ? main_sym->mangled_name : canon_name;
        short_sym->ast_decl = new_fn;
        add_symbol(s->global_scope, short_sym);
    }

    return main_sym;
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

static const char *get_type_mangling_code(Arena *arena, Type *t) {
    if (!t) return "v";
    switch (t->kind) {
        case TYPE_VOID: return "v";
        case TYPE_BOOL: return "b";
        case TYPE_CHAR: return "c";
        case TYPE_INT:  return "i";
        case TYPE_LONG: return "l";
        case TYPE_PTR: {
            const char *base = get_type_mangling_code(arena, t->ptr.base);
            char buf[64];
            snprintf(buf, sizeof(buf), "P%s", base);
            return arena_strdup(arena, buf);
        }
        case TYPE_REF: {
            const char *base = get_type_mangling_code(arena, t->ref.base);
            char buf[64];
            snprintf(buf, sizeof(buf), "R%s", base);
            return arena_strdup(arena, buf);
        }
        case TYPE_ARRAY: {
            const char *base = get_type_mangling_code(arena, t->array.base);
            char buf[64];
            snprintf(buf, sizeof(buf), "A%s", base);
            return arena_strdup(arena, buf);
        }
        case TYPE_CLASS: {
            if (!t->name) return "C";
            char clean[128];
            size_t ci = 0;
            for (size_t i = 0; t->name[i] && ci < sizeof(clean) - 1; i++) {
                if (t->name[i] == ':' || t->name[i] == '<' || t->name[i] == '>' || t->name[i] == ',') {
                    clean[ci++] = '_';
                } else {
                    clean[ci++] = t->name[i];
                }
            }
            clean[ci] = '\0';
            return arena_strdup(arena, clean);
        }
        case TYPE_FUNC: return "F";
        case TYPE_MEMBER_PTR: return "M";
        case TYPE_MEMBER_FUNC_PTR: return "MF";
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
            strcmp(name, "strlen") == 0 ||
            strcmp(name, "fputs") == 0 ||
            strcmp(name, "fputc") == 0 ||
            strcmp(name, "fprintf") == 0 ||
            strcmp(name, "sprintf") == 0 ||
            strcmp(name, "snprintf") == 0 ||
            strcmp(name, "fopen") == 0 ||
            strcmp(name, "fclose") == 0 ||
            strcmp(name, "fflush") == 0 ||
            strcmp(name, "fread") == 0 ||
            strcmp(name, "fwrite") == 0 ||
            strcmp(name, "realloc") == 0 ||
            strcmp(name, "calloc") == 0 ||
            strcmp(name, "memcpy") == 0 ||
            strcmp(name, "memmove") == 0 ||
            strcmp(name, "memset") == 0 ||
            strcmp(name, "strcmp") == 0 ||
            strcmp(name, "strncmp") == 0 ||
            strcmp(name, "strcpy") == 0 ||
            strcmp(name, "strncpy") == 0 ||
            strcmp(name, "strcat") == 0 ||
            strcmp(name, "strncat") == 0 ||
            strcmp(name, "abort") == 0 ||
            strcmp(name, "atoi") == 0 ||
            strcmp(name, "atol") == 0 ||
            strcmp(name, "abs") == 0 ||
            strcmp(name, "labs") == 0 ||
            strcmp(name, "scanf") == 0) {
            return str_intern(name);
        }
    }

    char buf[512];
    int len = 0;

    const char *op_suffix = NULL;
    if (strncmp(name, "operator", 8) == 0) {
        const char *op = name + 8;
        if (strcmp(op, "<<") == 0) op_suffix = "_ls";
        else if (strcmp(op, ">>") == 0) op_suffix = "_rs";
        else if (strcmp(op, "[]") == 0) op_suffix = "_ix";
        else if (strcmp(op, "+=") == 0) op_suffix = "_pe";
        else if (strcmp(op, "-=") == 0) op_suffix = "_me";
        else if (strcmp(op, "*=") == 0) op_suffix = "_te";
        else if (strcmp(op, "/=") == 0) op_suffix = "_de";
        else if (strcmp(op, "==") == 0) op_suffix = "_eq";
        else if (strcmp(op, "!=") == 0) op_suffix = "_ne";
        else if (strcmp(op, "<=") == 0) op_suffix = "_le";
        else if (strcmp(op, ">=") == 0) op_suffix = "_ge";
        else if (strcmp(op, "<") == 0) op_suffix = "_lt";
        else if (strcmp(op, ">") == 0) op_suffix = "_gt";
        else if (strcmp(op, "+") == 0) op_suffix = "_pl";
        else if (strcmp(op, "-") == 0) op_suffix = "_mi";
        else if (strcmp(op, "*") == 0) op_suffix = "_mu";
        else if (strcmp(op, "/") == 0) op_suffix = "_di";
        else if (strcmp(op, "=") == 0) op_suffix = "_as";
        else op_suffix = "_op";
    }

    if (class_owner) {
        if (is_ctor) {
            len = snprintf(buf, sizeof(buf), "_W_%s_ctor", class_owner);
        } else if (is_dtor) {
            len = snprintf(buf, sizeof(buf), "_W_%s_dtor", class_owner);
        } else if (op_suffix) {
            len = snprintf(buf, sizeof(buf), "_W_%s_op%s", class_owner, op_suffix);
        } else {
            len = snprintf(buf, sizeof(buf), "_W_%s_%s", class_owner, name);
        }
    } else {
        if (op_suffix) {
            len = snprintf(buf, sizeof(buf), "_W_op%s", op_suffix);
        } else {
            len = snprintf(buf, sizeof(buf), "_W_%s", name);
        }
    }

    /* Append parameter types */
    for (TypeParam *p = params; p != NULL; p = p->next) {
        const char *code = get_type_mangling_code(arena, p->type);
        len += snprintf(buf + len, sizeof(buf) - len, "_%s", code);
    }

    if (!params && !is_dtor) {
        len += snprintf(buf + len, sizeof(buf) - len, "_v");
    }

    /* Replace non-alphanumeric characters with '_' to produce valid GNU assembler labels */
    for (int i = 0; i < len; i++) {
        if (!isalnum((unsigned char)buf[i]) && buf[i] != '_') {
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
    const char *builtins[] = {
        "printf", "puts", "malloc", "free", "exit", "putchar", "getchar",
        "fputs", "fputc", "fprintf", "sprintf", "snprintf", "fopen", "fclose",
        "fflush", "fread", "fwrite", "realloc", "calloc", "memcpy", "memmove",
        "memset", "strcmp", "strncmp", "strcpy", "strncpy", "strcat", "strncat",
        "abort", "atoi", "atol", "abs", "labs", "scanf"
    };
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
    size_t offset = 0;
    size_t max_align = 4;

    for (Field *f = class_type->cls.fields; f != NULL; f = f->next) {
        f->type = resolve_type(s, f->type);
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
                sym->is_used = true;
                expr->var_ref.sym = sym;
                if (sym->kind == SYM_FUNC) {
                    expr->type = type_ptr(s->arena, sym->type);
                } else {
                    expr->type = sym->type;
                }
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
            const char *suggestion = find_closest_symbol(s, expr->var_ref.name);
            if (suggestion) {
                diag_help("did you mean '%s'?", suggestion);
            }
            expr->type = g_type_int;
            break;
        }

        case AST_BINARY: {
            analyze_expr(s, expr->binary.left);
            analyze_expr(s, expr->binary.right);

            /* Check for operator overloading */
            const char *op_name = NULL;
            switch (expr->binary.op) {
                case TOK_SHL: op_name = "operator<<"; break;
                case TOK_SHR: op_name = "operator>>"; break;
                case TOK_PLUS: op_name = "operator+"; break;
                case TOK_MINUS: op_name = "operator-"; break;
                case TOK_STAR: op_name = "operator*"; break;
                case TOK_SLASH: op_name = "operator/"; break;
                case TOK_EQ_EQ: op_name = "operator=="; break;
                case TOK_EXCL_EQ: op_name = "operator!="; break;
                case TOK_LESS: op_name = "operator<"; break;
                case TOK_GREATER: op_name = "operator>"; break;
                case TOK_LESS_EQ: op_name = "operator<="; break;
                case TOK_GREATER_EQ: op_name = "operator>="; break;
                default: break;
            }

            if (op_name) {
                Type *lt = expr->binary.left ? expr->binary.left->type : NULL;
                Type *rt = expr->binary.right ? expr->binary.right->type : NULL;
                if (lt && lt->kind == TYPE_REF) lt = lt->ref.base;
                if (rt && rt->kind == TYPE_REF) rt = rt->ref.base;

                if ((lt && lt->kind == TYPE_CLASS) || (rt && rt->kind == TYPE_CLASS)) {
                    ASTNode *left_node = expr->binary.left;
                    ASTNode *right_node = expr->binary.right;

                    if (lt && lt->kind == TYPE_CLASS) {
                        ASTNode *args[1] = { right_node };
                        Symbol *msym = find_method_overload_typed(s->global_scope, lt->name, op_name, args, 1);
                        if (msym) {
                            expr->kind = AST_CALL;
                            expr->call.name = op_name;
                            expr->call.is_method = true;
                            expr->call.is_arrow = false;
                            expr->call.object = left_node;
                            expr->call.args = arena_alloc(s->arena, sizeof(ASTNode*));
                            expr->call.args[0] = right_node;
                            expr->call.arg_count = 1;
                            expr->call.mangled_name = msym->mangled_name;
                            expr->call.callee_sym = msym;
                            if (msym->type && msym->type->kind == TYPE_FUNC) {
                                expr->type = msym->type->func.return_type;
                            } else {
                                expr->type = lt;
                            }
                            break;
                        }
                    }

                    ASTNode *args[2] = { left_node, right_node };
                    Symbol *fsym = find_function_overload_typed(s->current_scope, op_name, args, 2);
                    if (!fsym) {
                        fsym = find_function_overload_typed(s->global_scope, op_name, args, 2);
                    }
                    if (!fsym && lt && lt->name && strstr(lt->name, "::")) {
                        const char *col = strstr(lt->name, "::");
                        size_t nlen = (size_t)(col - lt->name);
                        char qname[256];
                        if (nlen < sizeof(qname) - 32) {
                            char ns[128];
                            memcpy(ns, lt->name, nlen);
                            ns[nlen] = '\0';
                            snprintf(qname, sizeof(qname), "%s::%s", ns, op_name);
                            fsym = find_function_overload_typed(s->global_scope, qname, args, 2);
                        }
                    }
                    if (!fsym && rt && rt->name && strstr(rt->name, "::")) {
                        const char *col = strstr(rt->name, "::");
                        size_t nlen = (size_t)(col - rt->name);
                        char qname[256];
                        if (nlen < sizeof(qname) - 32) {
                            char ns[128];
                            memcpy(ns, rt->name, nlen);
                            ns[nlen] = '\0';
                            snprintf(qname, sizeof(qname), "%s::%s", ns, op_name);
                            fsym = find_function_overload_typed(s->global_scope, qname, args, 2);
                        }
                    }
                    if (!fsym) {
                        char qname[256];
                        snprintf(qname, sizeof(qname), "std::%s", op_name);
                        fsym = find_function_overload_typed(s->global_scope, qname, args, 2);
                    }
                    if (fsym) {
                        expr->kind = AST_CALL;
                        expr->call.name = op_name;
                        expr->call.is_method = false;
                        expr->call.is_arrow = false;
                        expr->call.object = NULL;
                        expr->call.args = arena_alloc(s->arena, sizeof(ASTNode*) * 2);
                        expr->call.args[0] = left_node;
                        expr->call.args[1] = right_node;
                        expr->call.arg_count = 2;
                        expr->call.mangled_name = fsym->mangled_name;
                        expr->call.callee_sym = fsym;
                        if (fsym->type && fsym->type->kind == TYPE_FUNC) {
                            expr->type = fsym->type->func.return_type;
                        } else {
                            expr->type = g_type_int;
                        }
                        break;
                    }
                }
            }

            /* Relational / comparison operators return bool */
            TokenKind op = expr->binary.op;
            if (op == TOK_EQ_EQ || op == TOK_EXCL_EQ ||
                op == TOK_LESS || op == TOK_LESS_EQ ||
                op == TOK_GREATER || op == TOK_GREATER_EQ ||
                op == TOK_LOG_AND || op == TOK_LOG_OR) {
                expr->type = g_type_bool;
            } else if (expr->binary.op == TOK_PLUS && expr->binary.right && expr->binary.right->type &&
                       (expr->binary.right->type->kind == TYPE_PTR || expr->binary.right->type->kind == TYPE_ARRAY)) {
                Type *rt = expr->binary.right->type;
                if (rt->kind == TYPE_ARRAY) {
                    expr->type = type_ptr(s->arena, rt->array.base);
                } else {
                    expr->type = rt;
                }
            } else if (expr->binary.left && expr->binary.left->type) {
                Type *lt = expr->binary.left->type;
                if (lt->kind == TYPE_ARRAY) {
                    expr->type = type_ptr(s->arena, lt->array.base);
                } else {
                    expr->type = lt;
                }
            } else {
                expr->type = g_type_int;
            }
            break;
        }

        case AST_UNARY: {
            if (expr->unary.op == TOK_AMP && expr->unary.operand &&
                expr->unary.operand->kind == AST_VAR_REF && expr->unary.operand->var_ref.scope_prefix != NULL) {
                const char *cls_name = expr->unary.operand->var_ref.scope_prefix;
                const char *mem_name = expr->unary.operand->var_ref.name;
                Symbol *csym = find_class_symbol(s, cls_name);
                if (csym && csym->type && csym->type->kind == TYPE_CLASS) {
                    Field *f = find_field(csym->type, mem_name);
                    if (f) {
                        expr->kind = AST_LIT_INT;
                        expr->int_val = f->offset;
                        expr->type = type_member_ptr(s->arena, csym->type, f->type);
                        break;
                    }
                    Symbol *msym = find_method_overload(s->global_scope, cls_name, mem_name, -1);
                    if (!msym) {
                        for (Scope *sc = s->global_scope; sc != NULL; sc = sc->parent) {
                            for (Symbol *sym = sc->symbols; sym != NULL; sym = sym->next) {
                                if (sym->kind == SYM_FUNC && sym->name && strcmp(sym->name, mem_name) == 0) {
                                    if (sym->ast_decl && sym->ast_decl->func_decl.class_owner &&
                                        match_class_names(sym->ast_decl->func_decl.class_owner, cls_name)) {
                                        msym = sym;
                                        break;
                                    }
                                }
                            }
                            if (msym) break;
                        }
                    }
                    if (msym) {
                        expr->kind = AST_VAR_REF;
                        expr->var_ref.sym = msym;
                        expr->var_ref.name = msym->name;
                        expr->var_ref.scope_prefix = NULL;
                        expr->type = type_member_func_ptr(s->arena, csym->type, msym->type);
                        break;
                    }
                }
            }

            analyze_expr(s, expr->unary.operand);
            Type *op_t = expr->unary.operand ? expr->unary.operand->type : g_type_int;

            if (expr->unary.op == TOK_AMP) {
                /* Address-of: &x -> Type* */
                if (op_t && op_t->kind == TYPE_PTR && op_t->ptr.base && op_t->ptr.base->kind == TYPE_FUNC) {
                    expr->type = op_t;
                } else if (op_t && op_t->kind == TYPE_FUNC) {
                    expr->type = type_ptr(s->arena, op_t);
                } else {
                    expr->type = type_ptr(s->arena, op_t);
                }
            } else if (expr->unary.op == TOK_STAR) {
                /* Dereference: *p -> Type */
                if (op_t && (op_t->kind == TYPE_PTR || op_t->kind == TYPE_REF)) {
                    if (op_t->ptr.base && op_t->ptr.base->kind == TYPE_FUNC) {
                        expr->type = op_t;
                    } else {
                        expr->type = op_t->ptr.base;
                    }
                } else if (op_t && op_t->kind == TYPE_ARRAY) {
                    expr->type = op_t->array.base;
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

            const char *op_name = NULL;
            switch (expr->assign.op) {
                case TOK_PLUS_EQ: op_name = "operator+="; break;
                case TOK_MINUS_EQ: op_name = "operator-="; break;
                case TOK_STAR_EQ: op_name = "operator*="; break;
                case TOK_SLASH_EQ: op_name = "operator/="; break;
                case TOK_ASSIGN: op_name = "operator="; break;
                default: break;
            }

            if (op_name) {
                Type *tt = expr->assign.target ? expr->assign.target->type : NULL;
                if (tt && tt->kind == TYPE_REF) tt = tt->ref.base;

                if (tt && tt->kind == TYPE_CLASS) {
                    ASTNode *target_node = expr->assign.target;
                    ASTNode *val_node = expr->assign.value;
                    ASTNode *args[1] = { val_node };
                    Symbol *msym = find_method_overload_typed(s->global_scope, tt->name, op_name, args, 1);
                    if (msym) {
                        expr->kind = AST_CALL;
                        expr->call.name = op_name;
                        expr->call.is_method = true;
                        expr->call.is_arrow = false;
                        expr->call.object = target_node;
                        expr->call.args = arena_alloc(s->arena, sizeof(ASTNode*));
                        expr->call.args[0] = val_node;
                        expr->call.arg_count = 1;
                        expr->call.mangled_name = msym->mangled_name;
                        expr->call.callee_sym = msym;
                        if (msym->type && msym->type->kind == TYPE_FUNC) {
                            expr->type = msym->type->func.return_type;
                        } else {
                            expr->type = tt;
                        }
                        break;
                    }
                }
            }

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

                Symbol *msym = find_method_overload_typed(s->global_scope, cls_type->name, expr->call.name, expr->call.args, expr->call.arg_count);
                if (!msym) {
                    msym = find_method_overload(s->global_scope, cls_type->name, expr->call.name, expr->call.arg_count);
                }
                if (!msym && cls_type->name) {
                    /* Check if this is a constructor call on a subobject/member: obj.ClassName(...) */
                    const char *short_cls = strrchr(cls_type->name, ':');
                    short_cls = short_cls ? short_cls + 1 : cls_type->name;
                    char base_cls[128];
                    const char *sep = strstr(short_cls, "__");
                    if (sep) {
                        size_t blen = (size_t)(sep - short_cls);
                        if (blen < sizeof(base_cls)) {
                            memcpy(base_cls, short_cls, blen);
                            base_cls[blen] = '\0';
                        } else {
                            base_cls[0] = '\0';
                        }
                    } else {
                        snprintf(base_cls, sizeof(base_cls), "%s", short_cls);
                    }

                    msym = find_method_overload_typed(s->global_scope, cls_type->name, cls_type->name, expr->call.args, expr->call.arg_count);
                    if (!msym && base_cls[0]) {
                        msym = find_method_overload_typed(s->global_scope, cls_type->name, base_cls, expr->call.args, expr->call.arg_count);
                    }
                    if (!msym && short_cls) {
                        msym = find_method_overload_typed(s->global_scope, cls_type->name, short_cls, expr->call.args, expr->call.arg_count);
                    }
                    if (!msym) {
                        msym = find_method_overload(s->global_scope, cls_type->name, cls_type->name, expr->call.arg_count);
                    }
                    if (!msym && base_cls[0]) {
                        msym = find_method_overload(s->global_scope, cls_type->name, base_cls, expr->call.arg_count);
                    }
                    if (msym) {
                        expr->call.name = msym->name;
                    }
                }
                if (msym) {
                    expr->call.mangled_name = msym->mangled_name;
                    expr->call.callee_sym = msym;
                    if (msym->type && msym->type->kind == TYPE_FUNC) {
                        expr->type = msym->type->func.return_type;
                    } else {
                        expr->type = g_type_int;
                    }
                } else {
                    Field *f = find_field(cls_type, expr->call.name);
                    if (f && f->type && (f->type->kind == TYPE_PTR || f->type->kind == TYPE_FUNC)) {
                        /* Member is a function pointer field: obj.callback(args) */
                        ASTNode *memb = ast_new(s->arena, AST_MEMBER, expr->loc);
                        memb->member.object = expr->call.object;
                        memb->member.member_name = expr->call.name;
                        memb->member.is_arrow = expr->call.is_arrow;
                        memb->member.field = f;
                        memb->type = f->type;

                        expr->call.is_method = false;
                        expr->call.callee = memb;
                        expr->call.name = NULL;
                        expr->call.mangled_name = NULL;

                        Type *func_t = (f->type->kind == TYPE_PTR && f->type->ptr.base) ? f->type->ptr.base : f->type;
                        expr->type = (func_t && func_t->kind == TYPE_FUNC && func_t->func.return_type) ? func_t->func.return_type : g_type_void;
                    } else {
                        TypeParam *actual_params = NULL;
                        TypeParam **tail = &actual_params;
                        for (int i = 0; i < expr->call.arg_count; i++) {
                            TypeParam *tp = arena_alloc_zero(s->arena, sizeof(TypeParam));
                            tp->type = expr->call.args[i] ? expr->call.args[i]->type : g_type_int;
                            *tail = tp;
                            tail = &tp->next;
                        }
                        const char *owner = (cls_type->cls.class_name && strstr(cls_type->cls.class_name, "::")) ? cls_type->cls.class_name : cls_type->name;
                        const char *mangled = mangle_function_name(s->arena, owner, expr->call.name, actual_params, false, false);
                        expr->call.mangled_name = mangled;
                        expr->type = g_type_int;
                    }
                }
            } else {
                /* Free function call, scoped function call, or indirect function pointer call */
                Symbol *sym = NULL;
                if (expr->call.name != NULL) {
                    if (expr->call.scope_prefix) {
                        sym = find_scoped_function_overload(s->global_scope, expr->call.scope_prefix, expr->call.name, expr->call.arg_count);
                    } else {
                        sym = find_function_overload_typed(s->current_scope, expr->call.name, expr->call.args, expr->call.arg_count);
                    }
                }

                if (!sym && expr->call.name != NULL) {
                    sym = try_instantiate_func_template(s, expr);
                }

                if (sym) {
                    expr->call.mangled_name = sym->mangled_name;
                    expr->call.callee_sym = sym;
                    if (sym->type && sym->type->kind == TYPE_FUNC) {
                        expr->type = sym->type->func.return_type;
                    } else {
                        expr->type = g_type_int;
                    }
                } else if (expr->call.callee != NULL) {
                    /* Check for indirect function pointer call: fp(args), (*fp)(args), table[i](args) */
                    analyze_expr(s, expr->call.callee);
                    Type *callee_t = expr->call.callee->type;
                    if (callee_t != NULL && callee_t->kind == TYPE_MEMBER_FUNC_PTR &&
                        expr->call.callee->kind == AST_MEMBER_PTR_ACCESS) {
                        expr->call.is_method = true;
                        expr->call.object = expr->call.callee->member_ptr_access.object;
                        expr->call.is_arrow = expr->call.callee->member_ptr_access.is_arrow;
                        expr->call.callee = expr->call.callee->member_ptr_access.member_ptr;
                        expr->call.name = NULL;
                        expr->call.mangled_name = NULL;
                        Type *fn_t = callee_t->member_func_ptr.func_type;
                        expr->type = (fn_t && fn_t->kind == TYPE_FUNC && fn_t->func.return_type) ? fn_t->func.return_type : g_type_int;
                    } else {
                        Type *func_t = NULL;
                        if (callee_t != NULL) {
                            if (callee_t->kind == TYPE_PTR && callee_t->ptr.base && callee_t->ptr.base->kind == TYPE_FUNC) {
                                func_t = callee_t->ptr.base;
                            } else if (callee_t->kind == TYPE_FUNC) {
                                func_t = callee_t;
                            }
                        }

                        if (func_t != NULL) {
                            expr->call.mangled_name = NULL;
                            expr->type = func_t->func.return_type ? func_t->func.return_type : g_type_void;
                        } else if (expr->call.scope_prefix && expr->call.name) {
                            const char *mangled = mangle_function_name(s->arena, expr->call.scope_prefix, expr->call.name, NULL, false, false);
                            expr->call.mangled_name = mangled;
                            expr->type = g_type_int;
                        } else {
                            expr->type = g_type_int;
                        }
                    }
                } else if (expr->call.name != NULL) {
                    /* Check for temporary object construction: ClassName(args) */
                    Symbol *csym = find_class_symbol(s, expr->call.name);
                    if (!csym && expr->call.scope_prefix) {
                        char qname[256];
                        snprintf(qname, sizeof(qname), "%s::%s", expr->call.scope_prefix, expr->call.name);
                        csym = find_class_symbol(s, qname);
                    }
                    if (csym && csym->kind == SYM_CLASS) {
                        Type *ct = resolve_type(s, csym->type);
                        if (ct) {
                            TypeParam *cparams_head = NULL;
                            TypeParam **cparams_tail = &cparams_head;
                            for (int i = 0; i < expr->call.arg_count; i++) {
                                TypeParam *tp = arena_alloc_zero(s->arena, sizeof(TypeParam));
                                tp->type = expr->call.args[i] ? expr->call.args[i]->type : g_type_int;
                                *cparams_tail = tp;
                                cparams_tail = &tp->next;
                            }
                            const char *owner = (ct->cls.class_name && strstr(ct->cls.class_name, "::")) ? ct->cls.class_name : ct->name;
                            const char *ctor_name = mangle_function_name(s->arena, owner, owner, cparams_head, true, false);
                            expr->call.mangled_name = ctor_name;
                            expr->type = ct;
                            expr->call.callee_sym = csym;
                        } else {
                            expr->type = g_type_int;
                        }
                    } else if (expr->call.scope_prefix && expr->call.name) {
                        const char *mangled = mangle_function_name(s->arena, expr->call.scope_prefix, expr->call.name, NULL, false, false);
                        expr->call.mangled_name = mangled;
                        expr->type = g_type_int;
                    } else {
                        expr->call.mangled_name = str_intern(expr->call.name);
                        expr->type = g_type_int;
                    }
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
                const char *candidates[64];
                int ccount = 0;
                for (Field *curr = cls_type->cls.fields; curr != NULL && ccount < 64; curr = curr->next) {
                    candidates[ccount++] = curr->name;
                }
                const char *sugg = diag_find_closest(expr->member.member_name, candidates, ccount);
                if (sugg) {
                    diag_help("did you mean member '%s'?", sugg);
                }
                expr->type = g_type_int;
            } else {
                expr->member.field = f;
                expr->type = f->type;
            }
            break;
        }

        case AST_MEMBER_PTR_ACCESS: {
            analyze_expr(s, expr->member_ptr_access.object);
            analyze_expr(s, expr->member_ptr_access.member_ptr);

            Type *obj_t = expr->member_ptr_access.object ? expr->member_ptr_access.object->type : NULL;
            Type *cls_type = NULL;
            if (obj_t) {
                if (expr->member_ptr_access.is_arrow) {
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
                diag_report(DIAG_ERROR, expr->loc, "left-hand operand of .* or ->* must be a class or class pointer");
            }

            Type *mpt = expr->member_ptr_access.member_ptr ? expr->member_ptr_access.member_ptr->type : NULL;
            if (mpt) mpt = resolve_type(s, mpt);

            if (mpt && mpt->kind == TYPE_MEMBER_PTR) {
                expr->type = mpt->member_ptr.member_type;
            } else if (mpt && mpt->kind == TYPE_MEMBER_FUNC_PTR) {
                expr->type = mpt;
            } else {
                diag_report(DIAG_ERROR, expr->loc, "pointer to member required on right of .* or ->*");
                expr->type = g_type_int;
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

            if (t && t->kind == TYPE_CLASS) {
                const char *owner = (t->cls.class_name && strstr(t->cls.class_name, "::")) ? t->cls.class_name : t->name;
                const char *short_owner = strrchr(owner, ':');
                short_owner = short_owner ? short_owner + 1 : owner;
                char base_owner[128];
                const char *sep = strstr(short_owner, "__");
                if (sep) {
                    size_t blen = (size_t)(sep - short_owner);
                    if (blen < sizeof(base_owner)) {
                        memcpy(base_owner, short_owner, blen);
                        base_owner[blen] = '\0';
                    } else base_owner[0] = '\0';
                } else snprintf(base_owner, sizeof(base_owner), "%s", short_owner);

                Symbol *msym = find_method_overload_typed(s->global_scope, owner, owner, expr->new_expr.args, expr->new_expr.arg_count);
                if (!msym && base_owner[0]) {
                    msym = find_method_overload_typed(s->global_scope, owner, base_owner, expr->new_expr.args, expr->new_expr.arg_count);
                }
                if (!msym && short_owner) {
                    msym = find_method_overload_typed(s->global_scope, owner, short_owner, expr->new_expr.args, expr->new_expr.arg_count);
                }
                if (!msym) {
                    msym = find_method_overload(s->global_scope, owner, owner, expr->new_expr.arg_count);
                }
                if (!msym && base_owner[0]) {
                    msym = find_method_overload(s->global_scope, owner, base_owner, expr->new_expr.arg_count);
                }
                if (msym) {
                    expr->new_expr.ctor_mangled_name = msym->mangled_name;
                }
            }
            break;
        }

        case AST_DELETE: {
            analyze_expr(s, expr->delete_expr.target);
            expr->type = g_type_void;
            break;
        }

        case AST_PACK_EXPANSION: {
            if (expr->pack_expansion.expr) {
                analyze_expr(s, expr->pack_expansion.expr);
                expr->type = expr->pack_expansion.expr->type;
            } else {
                expr->type = g_type_void;
            }
            break;
        }

        case AST_INDEX: {
            analyze_expr(s, expr->index_expr.target);
            analyze_expr(s, expr->index_expr.index);

            Type *tt = expr->index_expr.target ? expr->index_expr.target->type : NULL;
            if (tt && tt->kind == TYPE_REF) tt = tt->ref.base;

            if (tt && tt->kind == TYPE_CLASS) {
                ASTNode *args[1] = { expr->index_expr.index };
                Symbol *msym = find_method_overload_typed(s->global_scope, tt->name, "operator[]", args, 1);
                if (msym) {
                    ASTNode *target = expr->index_expr.target;
                    ASTNode *idx = expr->index_expr.index;
                    expr->kind = AST_CALL;
                    expr->call.name = "operator[]";
                    expr->call.is_method = true;
                    expr->call.is_arrow = false;
                    expr->call.object = target;
                    expr->call.args = arena_alloc(s->arena, sizeof(ASTNode*));
                    expr->call.args[0] = idx;
                    expr->call.arg_count = 1;
                    expr->call.mangled_name = msym->mangled_name;
                    expr->call.callee_sym = msym;
                    if (msym->type && msym->type->kind == TYPE_FUNC) {
                        expr->type = msym->type->func.return_type;
                    } else {
                        expr->type = g_type_int;
                    }
                    break;
                }
            }

            /* Built-in array or pointer index */
            if (tt && tt->kind == TYPE_ARRAY) {
                expr->type = tt->array.base;
            } else if (tt && (tt->kind == TYPE_PTR || tt->kind == TYPE_REF)) {
                expr->type = tt->ptr.base;
            } else {
                expr->type = g_type_int;
            }
            break;
        }

        case AST_SIZEOF: {
            size_t sz = 8;
            if (expr->sizeof_expr.target_type) {
                Type *t = resolve_type(s, expr->sizeof_expr.target_type);
                if (t && t->kind == TYPE_CLASS && t->name && t->cls.fields == NULL) {
                    Symbol *sym = find_symbol(s->current_scope, t->name);
                    if (sym && sym->kind == SYM_VAR && sym->type) {
                        t = sym->type;
                    }
                }
                sz = (t && t->size > 0) ? t->size : 8;
            } else if (expr->sizeof_expr.target_expr) {
                analyze_expr(s, expr->sizeof_expr.target_expr);
                Type *t = expr->sizeof_expr.target_expr->type;
                sz = (t && t->size > 0) ? t->size : 8;
            }
            expr->kind = AST_LIT_INT;
            expr->int_val = (int64_t)sz;
            expr->type = g_type_long;
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
            if (stmt->stmt_expr.expr) {
                analyze_expr(s, stmt->stmt_expr.expr);
            }
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

            /* Check if class variable needs default constructor invocation */
            if (stmt->var_decl.init == NULL && vt && vt->kind == TYPE_CLASS && vt->name) {
                Symbol *ctor_sym = find_method_overload(s->global_scope, vt->name, vt->name, 0);
                if (!ctor_sym) {
                    const char *short_name = strrchr(vt->name, ':');
                    short_name = short_name ? short_name + 1 : vt->name;
                    ctor_sym = find_method_overload(s->global_scope, vt->name, short_name, 0);
                }
                if (ctor_sym) {
                    ASTNode *ctor_call = ast_new(s->arena, AST_NEW, stmt->loc);
                    ctor_call->new_expr.target_type = vt;
                    ctor_call->new_expr.args = NULL;
                    ctor_call->new_expr.arg_count = 0;
                    stmt->var_decl.init = ctor_call;
                }
            }

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
    if (!fn) return;
    if (fn->func_decl.mangled_name != NULL) return;

    const char *class_owner = fn->func_decl.class_owner;
    const char *name = fn->func_decl.name;
    bool is_ctor = fn->func_decl.is_ctor;
    bool is_dtor = fn->func_decl.is_dtor;

    Type *cls_type = NULL;
    if (class_owner) {
        Symbol *csym = find_class_symbol(s, class_owner);
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

    /* Also add unqualified alias in global scope if function was qualified */
    if (strstr(name, "::")) {
        const char *short_name = strrchr(name, ':');
        short_name = short_name ? short_name + 1 : name;
        Symbol *short_sym = arena_alloc_zero(s->arena, sizeof(Symbol));
        short_sym->kind = SYM_FUNC;
        short_sym->name = short_name;
        short_sym->mangled_name = mangled;
        short_sym->type = fn_t;
        short_sym->loc = fn->loc;
        short_sym->is_global = true;
        short_sym->ast_decl = fn;
        add_symbol(s->global_scope, short_sym);
    }

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
        Type *saved_ret = s->current_func_ret;
        Type *saved_cls = s->current_class;
        int saved_stack = s->current_stack_offset;

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

        s->current_func_ret = saved_ret;
        s->current_class = saved_cls;
        s->current_stack_offset = saved_stack;
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

        for (int m = 0; m < decl->class_decl.method_count; m++) {
            ASTNode *mdecl = decl->class_decl.methods[m];
            if (mdecl->kind == AST_DECL_TYPEDEF) {
                Type *resolved = resolve_type(s, mdecl->typedef_decl.aliased_type);
                Symbol *tsym = arena_alloc_zero(s->arena, sizeof(Symbol));
                tsym->kind = SYM_TYPEDEF;
                tsym->name = mdecl->typedef_decl.name;
                tsym->type = resolved;
                tsym->loc = mdecl->loc;
                tsym->is_global = true;
                add_symbol(s->global_scope, tsym);

                char qname[256];
                snprintf(qname, sizeof(qname), "%s::%s", full_name, mdecl->typedef_decl.name);
                Symbol *qsym = arena_alloc_zero(s->arena, sizeof(Symbol));
                qsym->kind = SYM_TYPEDEF;
                qsym->name = arena_strdup(s->arena, qname);
                qsym->type = resolved;
                qsym->loc = mdecl->loc;
                qsym->is_global = true;
                add_symbol(s->global_scope, qsym);
            }
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
            const char *sname = strrchr(decl->class_decl.name, ':');
            sname = sname ? sname + 1 : decl->class_decl.name;
            Symbol *short_sym = arena_alloc_zero(s->arena, sizeof(Symbol));
            short_sym->kind = SYM_CLASS;
            short_sym->name = sname;
            short_sym->type = cls;
            short_sym->loc = decl->loc;
            short_sym->is_global = true;
            add_symbol(s->global_scope, short_sym);
        }
    } else if (decl->kind == AST_DECL_TYPEDEF) {
        const char *full_name = decl->typedef_decl.name;
        if (ns_prefix && strstr(decl->typedef_decl.name, "::") == NULL) {
            char buf[256];
            snprintf(buf, sizeof(buf), "%s::%s", ns_prefix, decl->typedef_decl.name);
            full_name = arena_strdup(s->arena, buf);
        }
        Type *resolved = resolve_type(s, decl->typedef_decl.aliased_type);
        Symbol *tsym = arena_alloc_zero(s->arena, sizeof(Symbol));
        tsym->kind = SYM_TYPEDEF;
        tsym->name = full_name;
        tsym->type = resolved;
        tsym->loc = decl->loc;
        tsym->is_global = true;
        add_symbol(s->global_scope, tsym);

        if (ns_prefix) {
            const char *sname = strrchr(decl->typedef_decl.name, ':');
            sname = sname ? sname + 1 : decl->typedef_decl.name;
            Symbol *short_sym = arena_alloc_zero(s->arena, sizeof(Symbol));
            short_sym->kind = SYM_TYPEDEF;
            short_sym->name = sname;
            short_sym->type = resolved;
            short_sym->loc = decl->loc;
            short_sym->is_global = true;
            add_symbol(s->global_scope, short_sym);
        }
    } else if (decl->kind == AST_DECL_TEMPLATE) {
        if (decl->template_decl.decl && decl->template_decl.decl->kind == AST_DECL_CLASS) {
            ClassTemplate *ct = arena_alloc_zero(s->arena, sizeof(ClassTemplate));
            const char *cname = decl->template_decl.decl->class_decl.name;
            if (ns_prefix && strstr(cname, "::") == NULL) {
                char buf[256];
                snprintf(buf, sizeof(buf), "%s::%s", ns_prefix, cname);
                cname = arena_strdup(s->arena, buf);
            }
            ct->name = cname;
            ct->param_count = decl->template_decl.param_count > 0 ? decl->template_decl.param_count : 1;
            ct->is_variadic = decl->template_decl.is_variadic;
            for (int i = 0; i < ct->param_count; i++) {
                ct->param_names[i] = decl->template_decl.param_names[i] ? decl->template_decl.param_names[i] : decl->template_decl.param_name;
                ct->is_pack[i] = decl->template_decl.is_pack[i];
            }
            ct->class_decl = decl->template_decl.decl;
            ct->next = s_templates;
            s_templates = ct;
        } else if (decl->template_decl.decl && decl->template_decl.decl->kind == AST_DECL_FUNC) {
            FuncTemplate *ft = arena_alloc_zero(s->arena, sizeof(FuncTemplate));
            const char *fname = decl->template_decl.decl->func_decl.name;
            if (ns_prefix && strstr(fname, "::") == NULL) {
                char buf[256];
                snprintf(buf, sizeof(buf), "%s::%s", ns_prefix, fname);
                fname = arena_strdup(s->arena, buf);
            }
            ft->name = fname;
            ft->param_count = decl->template_decl.param_count > 0 ? decl->template_decl.param_count : 1;
            ft->is_variadic = decl->template_decl.is_variadic;
            for (int i = 0; i < ft->param_count; i++) {
                ft->param_names[i] = decl->template_decl.param_names[i] ? decl->template_decl.param_names[i] : decl->template_decl.param_name;
                ft->is_pack[i] = decl->template_decl.is_pack[i];
            }
            ft->func_decl = decl->template_decl.decl;
            ft->next = s_func_templates;
            s_func_templates = ft;
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
            if (mdecl->kind != AST_DECL_FUNC) continue;
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
    } else if (decl->kind == AST_STMT_VAR_DECL) {
        decl->var_decl.var_type = resolve_type(s, decl->var_decl.var_type);
        const char *full_name = decl->var_decl.name;
        if (ns_prefix && strstr(decl->var_decl.name, "::") == NULL) {
            char buf[256];
            snprintf(buf, sizeof(buf), "%s::%s", ns_prefix, decl->var_decl.name);
            full_name = arena_strdup(s->arena, buf);
        }
        Symbol *sym = arena_alloc_zero(s->arena, sizeof(Symbol));
        sym->kind = SYM_VAR;
        sym->name = full_name;
        sym->type = decl->var_decl.var_type;
        sym->loc = decl->loc;
        sym->is_global = true;
        sym->mangled_name = full_name;
        decl->var_decl.sym = sym;
        add_symbol(s->global_scope, sym);

        if (ns_prefix) {
            Symbol *ssym = arena_alloc_zero(s->arena, sizeof(Symbol));
            ssym->kind = SYM_VAR;
            ssym->name = decl->var_decl.name;
            ssym->type = decl->var_decl.var_type;
            ssym->loc = decl->loc;
            ssym->is_global = true;
            ssym->mangled_name = full_name;
            add_symbol(s->global_scope, ssym);
        }

        if (decl->var_decl.init) {
            analyze_expr(s, decl->var_decl.init);
        }
    }
}

bool sema_analyze(Sema *s, ASTNode *program) {
    if (!program || program->kind != AST_PROGRAM) return false;

    s_current_program = program;
    s_templates = NULL;
    s_func_templates = NULL;

    /* Pass 1: Register class, struct, typedef, and template definitions */
    for (int i = 0; i < program->program.count; i++) {
        sema_register_classes(s, program->program.decls[i], NULL);
    }

    /* Pass 2: Register functions, methods, globals, and analyze bodies */
    for (int i = 0; i < program->program.count; i++) {
        sema_analyze_decls(s, program->program.decls[i], NULL);
    }

    return diag_error_count() == 0;
}
