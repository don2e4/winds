#include "parser.h"
#include "str.h"

/* Forward declarations */
static ASTNode *parse_declaration(Parser *p);
static ASTNode *parse_statement(Parser *p);
static ASTNode *parse_expression(Parser *p);
static ASTNode *parse_assignment(Parser *p);
static Type *parse_type(Parser *p);
static bool try_parse_fn_ptr_declarator(Parser *p, Type *ret_type, const char **out_name, Type **out_type);
static bool try_parse_member_ptr_declarator(Parser *p, Type *member_type, const char **out_name, Type **out_type);

static void advance(Parser *p) {
    p->current = p->peek;
    p->peek = lexer_next(&p->lexer);
}

static bool check(Parser *p, TokenKind kind) {
    return p->current.kind == kind;
}

static bool match(Parser *p, TokenKind kind) {
    if (check(p, kind)) {
        advance(p);
        return true;
    }
    return false;
}

static Token expect(Parser *p, TokenKind kind, const char *msg) {
    if (check(p, kind)) {
        Token tok = p->current;
        advance(p);
        return tok;
    }
    if (kind == TOK_SEMICOLON) {
        if (msg) {
            diag_report(DIAG_ERROR, p->current.loc, "expected %s, but found '%s'",
                        msg, token_kind_str(p->current.kind));
        } else {
            diag_report(DIAG_ERROR, p->current.loc, "expected ';' after statement, but found '%s'",
                        token_kind_str(p->current.kind));
        }
        diag_help("insert a semicolon ';' at the end of the statement");
    } else if (kind == TOK_RPAREN) {
        diag_report(DIAG_ERROR, p->current.loc, "expected ')' to close parentheses, but found '%s'",
                    token_kind_str(p->current.kind));
        diag_help("make sure opening and closing parentheses are balanced");
    } else if (kind == TOK_RBRACE) {
        diag_report(DIAG_ERROR, p->current.loc, "expected '}' to close block, but found '%s'",
                    token_kind_str(p->current.kind));
        diag_help("make sure opening and closing braces are balanced");
    } else {
        diag_report(DIAG_ERROR, p->current.loc, "expected %s, but found '%s'",
                    msg ? msg : token_kind_str(kind), token_kind_str(p->current.kind));
    }
    Token err = p->current;
    advance(p);
    return err;
}

void parser_init(Parser *p, Arena *arena, const char *source, const char *filename) {
    lexer_init(&p->lexer, source, filename);
    p->arena = arena;
    p->current_namespace = NULL;
    p->current_class = NULL;
    p->primed = false;
}

void parser_add_include_path(Parser *p, const char *path) {
    lexer_add_include_path(&p->lexer, path);
}

static bool is_type_specifier(Parser *p) {
    TokenKind k = p->current.kind;
    return k == TOK_KW_VOID || k == TOK_KW_BOOL || k == TOK_KW_CHAR ||
           k == TOK_KW_SHORT || k == TOK_KW_INT || k == TOK_KW_LONG ||
           k == TOK_KW_SIGNED || k == TOK_KW_UNSIGNED ||
           k == TOK_KW_FLOAT || k == TOK_KW_DOUBLE || k == TOK_KW_CONST ||
           k == TOK_KW_CLASS || k == TOK_KW_STRUCT || k == TOK_IDENT;
}

static bool is_declaration_starting(Parser *p) {
    TokenKind k = p->current.kind;
    if (k == TOK_KW_CONST || k == TOK_KW_SIGNED || k == TOK_KW_UNSIGNED) return true;
    if (k == TOK_KW_VOID || k == TOK_KW_BOOL || k == TOK_KW_CHAR ||
        k == TOK_KW_SHORT || k == TOK_KW_INT || k == TOK_KW_LONG ||
        k == TOK_KW_FLOAT || k == TOK_KW_DOUBLE ||
        k == TOK_KW_CLASS || k == TOK_KW_STRUCT) {
        return true;
    }
    if (k == TOK_IDENT) {
        Parser saved = *p;
        advance(p);
        while (p->current.kind == TOK_COLON_COLON) {
            advance(p);
            if (p->current.kind == TOK_IDENT) advance(p);
        }
        if (p->current.kind == TOK_LESS) {
            advance(p);
            int depth = 1;
            while (depth > 0 && p->current.kind != TOK_EOF && p->current.kind != TOK_SEMICOLON) {
                if (p->current.kind == TOK_LESS) depth++;
                else if (p->current.kind == TOK_GREATER) depth--;
                advance(p);
            }
        }
        while (p->current.kind == TOK_STAR || p->current.kind == TOK_AMP) {
            advance(p);
        }
        bool is_decl = (p->current.kind == TOK_IDENT || p->current.kind == TOK_KW_OPERATOR ||
                        (p->current.kind == TOK_LPAREN && p->peek.kind == TOK_STAR));
        *p = saved;
        return is_decl;
    }
    return false;
}

static bool is_sizeof_type(Parser *p) {
    TokenKind k = p->current.kind;
    if (k == TOK_KW_CONST || k == TOK_KW_SIGNED || k == TOK_KW_UNSIGNED ||
        k == TOK_KW_VOID || k == TOK_KW_BOOL || k == TOK_KW_CHAR ||
        k == TOK_KW_SHORT || k == TOK_KW_INT || k == TOK_KW_LONG ||
        k == TOK_KW_FLOAT || k == TOK_KW_DOUBLE ||
        k == TOK_KW_CLASS || k == TOK_KW_STRUCT) {
        return true;
    }
    if (k == TOK_IDENT) {
        Parser saved = *p;
        advance(p);
        while (p->current.kind == TOK_COLON_COLON) {
            advance(p);
            if (p->current.kind == TOK_IDENT) advance(p);
        }
        if (p->current.kind == TOK_LESS) {
            advance(p);
            int depth = 1;
            while (depth > 0 && p->current.kind != TOK_EOF && p->current.kind != TOK_SEMICOLON) {
                if (p->current.kind == TOK_LESS) depth++;
                else if (p->current.kind == TOK_GREATER) depth--;
                advance(p);
            }
        }
        while (p->current.kind == TOK_STAR || p->current.kind == TOK_AMP) {
            advance(p);
        }
        bool is_type = (p->current.kind == TOK_RPAREN);
        *p = saved;
        return is_type;
    }
    return false;
}

static bool try_parse_member_ptr_declarator(Parser *p, Type *member_type, const char **out_name, Type **out_type) {
    if (p->current.kind != TOK_IDENT) {
        return false;
    }
    Parser saved = *p;

    char cls_name[256];
    cls_name[0] = '\0';
    size_t written = 0;

    const char *id = p->current.str_val;
    advance(p);
    written += snprintf(cls_name + written, sizeof(cls_name) - written, "%s", id);

    bool found_member_ptr = false;
    while (check(p, TOK_COLON_COLON)) {
        advance(p); /* skip '::' */
        if (check(p, TOK_STAR)) {
            advance(p); /* skip '*' */
            found_member_ptr = true;
            break;
        }
        if (check(p, TOK_IDENT)) {
            written += snprintf(cls_name + written, sizeof(cls_name) - written, "::%s", p->current.str_val);
            advance(p);
        } else {
            break;
        }
    }

    if (!found_member_ptr) {
        *p = saved;
        return false;
    }

    const char *name = NULL;
    bool is_array = false;
    size_t arr_size = 0;

    if (check(p, TOK_IDENT)) {
        name = p->current.str_val;
        advance(p);

        if (match(p, TOK_LBRACKET)) {
            if (check(p, TOK_INT_LIT)) {
                arr_size = (size_t)p->current.int_val;
                advance(p);
            }
            if (!match(p, TOK_RBRACKET)) {
                *p = saved;
                return false;
            }
            is_array = true;
        }
    }

    Type *cls_t = type_new(p->arena, TYPE_CLASS);
    cls_t->name = arena_strdup(p->arena, cls_name);
    cls_t->size = 8;
    cls_t->align = 8;

    Type *res_t = type_member_ptr(p->arena, cls_t, member_type);
    if (is_array) {
        res_t = type_array(p->arena, res_t, arr_size);
    }

    if (out_name) *out_name = name;
    if (out_type) *out_type = res_t;
    return true;
}

static bool try_parse_fn_ptr_declarator(Parser *p, Type *ret_type, const char **out_name, Type **out_type) {
    if (p->current.kind != TOK_LPAREN) {
        return false;
    }
    Parser saved = *p;
    advance(p); /* skip '(' */

    bool is_member_fn = false;
    char cls_name[256];
    cls_name[0] = '\0';
    size_t cls_written = 0;

    int ptr_depth = 0;
    if (check(p, TOK_STAR)) {
        while (match(p, TOK_STAR)) {
            ptr_depth++;
        }
    } else if (check(p, TOK_IDENT)) {
        /* Member function pointer: (Class::*name)(args) or (Class::*)(args) */
        const char *id = p->current.str_val;
        advance(p);
        cls_written += snprintf(cls_name + cls_written, sizeof(cls_name) - cls_written, "%s", id);

        while (check(p, TOK_COLON_COLON)) {
            advance(p); /* skip '::' */
            if (check(p, TOK_STAR)) {
                advance(p); /* skip '*' */
                is_member_fn = true;
                ptr_depth = 1;
                break;
            }
            if (check(p, TOK_IDENT)) {
                cls_written += snprintf(cls_name + cls_written, sizeof(cls_name) - cls_written, "::%s", p->current.str_val);
                advance(p);
            } else {
                break;
            }
        }
        if (!is_member_fn) {
            *p = saved;
            return false;
        }
    } else {
        *p = saved;
        return false;
    }

    if (ptr_depth == 0) {
        *p = saved;
        return false;
    }

    const char *name = NULL;
    bool is_array = false;
    size_t arr_size = 0;

    if (p->current.kind == TOK_IDENT) {
        name = p->current.str_val;
        advance(p);

        if (match(p, TOK_LBRACKET)) {
            if (check(p, TOK_INT_LIT)) {
                arr_size = (size_t)p->current.int_val;
                advance(p);
            }
            if (!match(p, TOK_RBRACKET)) {
                *p = saved;
                return false;
            }
            is_array = true;
        }
    }

    if (!match(p, TOK_RPAREN)) {
        *p = saved;
        return false;
    }

    if (!match(p, TOK_LPAREN)) {
        *p = saved;
        return false;
    }

    TypeParam *params = NULL;
    TypeParam **tail = &params;
    int param_count = 0;
    bool is_varargs = false;

    if (!check(p, TOK_RPAREN)) {
        while (1) {
            if (match(p, TOK_ELLIPSIS)) {
                is_varargs = true;
                break;
            }

            Type *param_t = parse_type(p);
            const char *param_name = NULL;
            Type *nested_fp = NULL;
            if (try_parse_fn_ptr_declarator(p, param_t, &param_name, &nested_fp) ||
                try_parse_member_ptr_declarator(p, param_t, &param_name, &nested_fp)) {
                param_t = nested_fp;
            } else if (check(p, TOK_IDENT)) {
                param_name = p->current.str_val;
                advance(p);
            }

            TypeParam *tp = arena_alloc_zero(p->arena, sizeof(TypeParam));
            tp->type = param_t;
            tp->name = param_name;
            *tail = tp;
            tail = &tp->next;
            param_count++;

            if (match(p, TOK_COMMA)) {
                continue;
            } else {
                break;
            }
        }
    }

    if (!match(p, TOK_RPAREN)) {
        *p = saved;
        return false;
    }

    if (is_member_fn && match(p, TOK_KW_CONST)) {
        /* optional const member function qualifier */
    }

    Type *fn_t = type_func(p->arena, ret_type, params, param_count, is_varargs);
    Type *res_t = NULL;
    if (is_member_fn) {
        Type *cls_t = type_new(p->arena, TYPE_CLASS);
        cls_t->name = arena_strdup(p->arena, cls_name);
        cls_t->size = 8;
        cls_t->align = 8;
        res_t = type_member_func_ptr(p->arena, cls_t, fn_t);
    } else {
        res_t = type_ptr(p->arena, fn_t);
        for (int i = 1; i < ptr_depth; i++) {
            res_t = type_ptr(p->arena, res_t);
        }
    }
    if (is_array) {
        res_t = type_array(p->arena, res_t, arr_size);
    }

    if (out_name) *out_name = name;
    if (out_type) *out_type = res_t;
    return true;
}

static Type *parse_type(Parser *p) {
    SourceLoc loc = p->current.loc;
    bool is_const = false;
    if (match(p, TOK_KW_CONST)) {
        is_const = true;
    }

    bool is_unsigned = false;
    if (match(p, TOK_KW_UNSIGNED)) {
        is_unsigned = true;
    } else if (match(p, TOK_KW_SIGNED)) {
        /* signed */
    }

    Type *base = NULL;
    if (match(p, TOK_KW_VOID)) base = g_type_void;
    else if (match(p, TOK_KW_BOOL)) base = g_type_bool;
    else if (match(p, TOK_KW_CHAR)) base = g_type_char;
    else if (match(p, TOK_KW_SHORT)) base = g_type_int;
    else if (match(p, TOK_KW_INT)) base = g_type_int;
    else if (match(p, TOK_KW_LONG)) base = g_type_long;
    else if (is_unsigned) base = g_type_int; /* e.g. 'unsigned x = 10;' */
    else if (match(p, TOK_KW_CLASS) || match(p, TOK_KW_STRUCT) || check(p, TOK_IDENT)) {
        const char *name = p->current.str_val;
        expect(p, TOK_IDENT, "class/type name");
        while (match(p, TOK_COLON_COLON)) {
            Token sub = expect(p, TOK_IDENT, "type name after '::'");
            char qname[256];
            snprintf(qname, sizeof(qname), "%s::%s", name, sub.str_val);
            name = arena_strdup(p->arena, qname);
        }

        /* Check for template arguments: Name<Arg1, Arg2> */
        if (check(p, TOK_LESS)) {
            advance(p);
            char templ_name[256];
            int written = snprintf(templ_name, sizeof(templ_name), "%s__", name);
            if (!check(p, TOK_GREATER)) {
                while (1) {
                    if (check(p, TOK_INT_LIT)) {
                        written += snprintf(templ_name + written, sizeof(templ_name) - written, "%ld", (long)p->current.int_val);
                        advance(p);
                    } else {
                        Type *arg = parse_type(p);
                        const char *arg_name = (arg && arg->name) ? arg->name : "type";
                        written += snprintf(templ_name + written, sizeof(templ_name) - written, "%s", arg_name);
                        if (match(p, TOK_ELLIPSIS)) {
                            written += snprintf(templ_name + written, sizeof(templ_name) - written, "...");
                        }
                    }
                    if (match(p, TOK_COMMA)) {
                        written += snprintf(templ_name + written, sizeof(templ_name) - written, "_");
                    } else {
                        break;
                    }
                }
            }
            expect(p, TOK_GREATER, "'>' after template argument");
            name = arena_strdup(p->arena, templ_name);
        }

        base = type_new(p->arena, TYPE_CLASS);
        base->name = name;
        base->size = 8; /* Resolved later in sema */
        base->align = 8;
    } else {
        diag_report(DIAG_ERROR, loc, "expected type specifier");
        return g_type_int;
    }

    /* Pointers and References: int**, int& */
    while (check(p, TOK_STAR) || check(p, TOK_AMP)) {
        if (match(p, TOK_STAR)) {
            base = type_ptr(p->arena, base);
        } else if (match(p, TOK_AMP)) {
            base = type_ref(p->arena, base);
            break; /* In C++, reference cannot be further referenced directly */
        }
    }

    /* Check for unnamed member pointer type: base Class::* */
    {
        const char *name = NULL;
        Type *mptr_t = NULL;
        Parser saved = *p;
        if (try_parse_member_ptr_declarator(p, base, &name, &mptr_t)) {
            if (name == NULL) {
                base = mptr_t;
            } else {
                *p = saved;
            }
        }
    }

    /* Check for unnamed function pointer or member function pointer: ret (*) (params) or ret (Class::*) (params) */
    if (p->current.kind == TOK_LPAREN) {
        const char *name = NULL;
        Type *fp_t = NULL;
        Parser saved = *p;
        if (try_parse_fn_ptr_declarator(p, base, &name, &fp_t)) {
            if (name == NULL) {
                base = fp_t;
            } else {
                *p = saved;
            }
        }
    }

    (void)is_const;
    return base;
}

/* Expression parsing with operator precedence */
static ASTNode *parse_primary(Parser *p) {
    SourceLoc loc = p->current.loc;

    if (check(p, TOK_INT_LIT)) {
        ASTNode *node = ast_new(p->arena, AST_LIT_INT, loc);
        node->int_val = p->current.int_val;
        node->type = g_type_int;
        advance(p);
        return node;
    }

    if (check(p, TOK_STR_LIT)) {
        ASTNode *node = ast_new(p->arena, AST_LIT_STR, loc);
        node->str_lit.val = p->current.str_val;
        node->str_lit.len = (size_t)p->current.int_val;
        node->type = type_ptr(p->arena, g_type_char);
        advance(p);
        return node;
    }

    if (check(p, TOK_CHAR_LIT)) {
        ASTNode *node = ast_new(p->arena, AST_LIT_INT, loc);
        node->int_val = p->current.int_val;
        node->type = g_type_char;
        advance(p);
        return node;
    }

    if (match(p, TOK_KW_TRUE)) {
        ASTNode *node = ast_new(p->arena, AST_LIT_BOOL, loc);
        node->bool_val = true;
        node->type = g_type_bool;
        return node;
    }

    if (match(p, TOK_KW_FALSE)) {
        ASTNode *node = ast_new(p->arena, AST_LIT_BOOL, loc);
        node->bool_val = false;
        node->type = g_type_bool;
        return node;
    }

    if (match(p, TOK_KW_NULLPTR)) {
        ASTNode *node = ast_new(p->arena, AST_LIT_NULLPTR, loc);
        node->int_val = 0;
        node->type = type_ptr(p->arena, g_type_void);
        return node;
    }

    if (match(p, TOK_KW_THIS)) {
        ASTNode *node = ast_new(p->arena, AST_THIS, loc);
        return node;
    }

    if (check(p, TOK_IDENT)) {
        const char *name = p->current.str_val;
        advance(p);

        /* Check for scope resolution: Foo::Bar or Foo::Bar::Baz */
        const char *scope_prefix = NULL;
        if (check(p, TOK_COLON_COLON)) {
            char prefix[256];
            snprintf(prefix, sizeof(prefix), "%s", name);
            while (match(p, TOK_COLON_COLON)) {
                name = expect(p, TOK_IDENT, "identifier after '::'").str_val;
                if (check(p, TOK_COLON_COLON)) {
                    size_t plen = strlen(prefix);
                    snprintf(prefix + plen, sizeof(prefix) - plen, "::%s", name);
                }
            }
            scope_prefix = arena_strdup(p->arena, prefix);
        }

        /* Check for explicit template arguments in expression: Name<Arg1, Arg2> followed by '(' or '::' */
        if (check(p, TOK_LESS)) {
            Parser saved = *p;
            advance(p);
            int depth = 1;
            while (depth > 0 && p->current.kind != TOK_EOF && p->current.kind != TOK_SEMICOLON) {
                if (p->current.kind == TOK_LESS) depth++;
                else if (p->current.kind == TOK_GREATER) depth--;
                advance(p);
            }
            bool is_template = (depth == 0 && (p->current.kind == TOK_LPAREN || p->current.kind == TOK_COLON_COLON));
            *p = saved;
            if (is_template) {
                advance(p);
                char templ_name[256];
                int written = snprintf(templ_name, sizeof(templ_name), "%s__", name);
                if (!check(p, TOK_GREATER)) {
                    while (1) {
                        if (check(p, TOK_INT_LIT)) {
                            written += snprintf(templ_name + written, sizeof(templ_name) - written, "%ld", (long)p->current.int_val);
                            advance(p);
                        } else {
                            Type *arg = parse_type(p);
                            const char *arg_name = (arg && arg->name) ? arg->name : "type";
                            written += snprintf(templ_name + written, sizeof(templ_name) - written, "%s", arg_name);
                            if (match(p, TOK_ELLIPSIS)) {
                                written += snprintf(templ_name + written, sizeof(templ_name) - written, "...");
                            }
                        }
                        if (match(p, TOK_COMMA)) {
                            written += snprintf(templ_name + written, sizeof(templ_name) - written, "_");
                        } else {
                            break;
                        }
                    }
                }
                expect(p, TOK_GREATER, "'>' after template arguments");
                name = arena_strdup(p->arena, templ_name);
            }
        }

        ASTNode *node = ast_new(p->arena, AST_VAR_REF, loc);
        node->var_ref.name = name;
        node->var_ref.scope_prefix = scope_prefix;
        return node;
    }

    if (match(p, TOK_LPAREN)) {
        ASTNode *expr = parse_expression(p);
        expect(p, TOK_RPAREN, "')' to close parenthesized expression");
        return expr;
    }

    diag_report(DIAG_ERROR, loc, "unexpected token in primary expression: '%s'", token_kind_str(p->current.kind));
    advance(p);
    ASTNode *dummy = ast_new(p->arena, AST_LIT_INT, loc);
    dummy->int_val = 0;
    dummy->type = g_type_int;
    return dummy;
}

static ASTNode *parse_postfix(Parser *p) {
    ASTNode *expr = parse_primary(p);

    while (true) {
        SourceLoc loc = p->current.loc;

        /* Function call: expr(args) */
        if (match(p, TOK_LPAREN)) {
            ASTNode **args = arena_alloc(p->arena, sizeof(ASTNode*) * 32);
            int arg_count = 0;

            if (!check(p, TOK_RPAREN)) {
                do {
                    args[arg_count++] = parse_assignment(p);
                } while (match(p, TOK_COMMA));
            }
            expect(p, TOK_RPAREN, "')' in function call");

            ASTNode *call = ast_new(p->arena, AST_CALL, loc);
            call->call.callee = expr;
            call->call.args = args;
            call->call.arg_count = arg_count;
            if (expr->kind == AST_VAR_REF) {
                call->call.name = expr->var_ref.name;
                call->call.scope_prefix = expr->var_ref.scope_prefix;
            }
            expr = call;
        }
        /* Member access: expr.member or expr->member */
        else if (check(p, TOK_DOT) || check(p, TOK_ARROW)) {
            bool is_arrow = match(p, TOK_ARROW);
            if (!is_arrow) advance(p); /* Skip '.' */

            Token member_tok = expect(p, TOK_IDENT, "member name");
            ASTNode *member = ast_new(p->arena, AST_MEMBER, loc);
            member->member.object = expr;
            member->member.member_name = member_tok.str_val;
            member->member.is_arrow = is_arrow;

            /* Check if this member access is followed by a call: obj.method(args) */
            if (check(p, TOK_LPAREN)) {
                advance(p); /* Skip '(' */
                ASTNode **args = arena_alloc(p->arena, sizeof(ASTNode*) * 32);
                int arg_count = 0;
                if (!check(p, TOK_RPAREN)) {
                    do {
                        args[arg_count++] = parse_assignment(p);
                    } while (match(p, TOK_COMMA));
                }
                expect(p, TOK_RPAREN, "')' in method call");

                ASTNode *call = ast_new(p->arena, AST_CALL, loc);
                call->call.name = member_tok.str_val;
                call->call.args = args;
                call->call.arg_count = arg_count;
                call->call.is_method = true;
                call->call.is_arrow = is_arrow;
                call->call.object = expr;
                expr = call;
            } else {
                expr = member;
            }
        }
        /* Array/subscript indexing: expr[idx] */
        else if (match(p, TOK_LBRACKET)) {
            ASTNode *index = parse_expression(p);
            expect(p, TOK_RBRACKET, "']' in array index");

            ASTNode *idx = ast_new(p->arena, AST_INDEX, loc);
            idx->index_expr.target = expr;
            idx->index_expr.index = index;
            expr = idx;
        }
        /* Postfix increment / decrement */
        else if (check(p, TOK_INC) || check(p, TOK_DEC)) {
            TokenKind op = p->current.kind;
            advance(p);
            ASTNode *un = ast_new(p->arena, AST_UNARY, loc);
            un->unary.op = op;
            un->unary.operand = expr;
            un->unary.is_prefix = false;
            expr = un;
        }
        /* Parameter pack expansion: expr... */
        else if (match(p, TOK_ELLIPSIS)) {
            ASTNode *pack = ast_new(p->arena, AST_PACK_EXPANSION, loc);
            pack->pack_expansion.expr = expr;
            expr = pack;
        } else {
            break;
        }
    }

    return expr;
}

static ASTNode *parse_unary(Parser *p) {
    SourceLoc loc = p->current.loc;

    /* Prefix unary operators */
    if (check(p, TOK_PLUS) || check(p, TOK_MINUS) || check(p, TOK_EXCL) ||
        check(p, TOK_TILDE) || check(p, TOK_STAR) || check(p, TOK_AMP) ||
        check(p, TOK_INC) || check(p, TOK_DEC)) {
        TokenKind op = p->current.kind;
        advance(p);
        ASTNode *node = ast_new(p->arena, AST_UNARY, loc);
        node->unary.op = op;
        node->unary.operand = parse_unary(p);
        node->unary.is_prefix = true;
        return node;
    }

    /* new expr: new Type or new Type(args) */
    if (match(p, TOK_KW_NEW)) {
        Type *t = parse_type(p);
        ASTNode **args = arena_alloc(p->arena, sizeof(ASTNode*) * 16);
        int arg_count = 0;
        if (match(p, TOK_LPAREN)) {
            if (!check(p, TOK_RPAREN)) {
                do {
                    args[arg_count++] = parse_assignment(p);
                } while (match(p, TOK_COMMA));
            }
            expect(p, TOK_RPAREN, "')' in new expression");
        }
        ASTNode *node = ast_new(p->arena, AST_NEW, loc);
        node->new_expr.target_type = t;
        node->new_expr.args = args;
        node->new_expr.arg_count = arg_count;
        node->type = type_ptr(p->arena, t);
        return node;
    }

    /* delete expr: delete ptr or delete[] ptr */
    if (match(p, TOK_KW_DELETE)) {
        bool is_array = false;
        if (match(p, TOK_LBRACKET)) {
            expect(p, TOK_RBRACKET, "']' after '[' in delete[]");
            is_array = true;
        }
        ASTNode *target = parse_unary(p);
        ASTNode *node = ast_new(p->arena, AST_DELETE, loc);
        node->delete_expr.target = target;
        node->delete_expr.is_array = is_array;
        node->type = g_type_void;
        return node;
    }

    /* sizeof(type), sizeof(expr), or sizeof...(pack) */
    if (match(p, TOK_KW_SIZEOF)) {
        SourceLoc sloc = p->current.loc;
        if (match(p, TOK_ELLIPSIS)) {
            expect(p, TOK_LPAREN, "'(' after sizeof...");
            Token ptok = expect(p, TOK_IDENT, "parameter pack name in sizeof...");
            expect(p, TOK_RPAREN, "')' after sizeof...");
            ASTNode *vref = ast_new(p->arena, AST_VAR_REF, sloc);
            vref->var_ref.name = ptok.str_val;
            ASTNode *node = ast_new(p->arena, AST_SIZEOF, sloc);
            node->sizeof_expr.target_expr = vref;
            node->type = g_type_long;
            return node;
        }
        expect(p, TOK_LPAREN, "'(' after sizeof");
        if (is_sizeof_type(p) || is_declaration_starting(p)) {
            Type *t = parse_type(p);
            expect(p, TOK_RPAREN, "')' after sizeof(type)");
            ASTNode *node = ast_new(p->arena, AST_SIZEOF, sloc);
            node->sizeof_expr.target_type = t;
            node->type = g_type_long;
            return node;
        } else {
            ASTNode *expr = parse_expression(p);
            expect(p, TOK_RPAREN, "')' after sizeof(expr)");
            ASTNode *node = ast_new(p->arena, AST_SIZEOF, sloc);
            node->sizeof_expr.target_expr = expr;
            node->type = g_type_long;
            return node;
        }
    }

    return parse_postfix(p);
}

/* Binary operator precedence climbing */
static int get_binary_op_precedence(TokenKind kind) {
    switch (kind) {
        case TOK_DOT_STAR:
        case TOK_ARROW_STAR:
            return 12;
        case TOK_STAR:
        case TOK_SLASH:
        case TOK_PERCENT:
            return 11;
        case TOK_PLUS:
        case TOK_MINUS:
            return 10;
        case TOK_SHL:
        case TOK_SHR:
            return 9;
        case TOK_LESS:
        case TOK_LESS_EQ:
        case TOK_GREATER:
        case TOK_GREATER_EQ:
            return 8;
        case TOK_EQ_EQ:
        case TOK_EXCL_EQ:
            return 7;
        case TOK_AMP:
            return 6;
        case TOK_CARET:
            return 5;
        case TOK_PIPE:
            return 4;
        case TOK_LOG_AND:
            return 3;
        case TOK_LOG_OR:
            return 2;
        default:
            return -1;
    }
}

static ASTNode *parse_binary_expr(Parser *p, int min_prec) {
    ASTNode *left = parse_unary(p);

    while (true) {
        int prec = get_binary_op_precedence(p->current.kind);
        if (prec < min_prec) break;

        TokenKind op = p->current.kind;
        SourceLoc loc = p->current.loc;
        advance(p);

        ASTNode *right = parse_binary_expr(p, prec + 1);

        if (op == TOK_DOT_STAR || op == TOK_ARROW_STAR) {
            ASTNode *access = ast_new(p->arena, AST_MEMBER_PTR_ACCESS, loc);
            access->member_ptr_access.object = left;
            access->member_ptr_access.member_ptr = right;
            access->member_ptr_access.is_arrow = (op == TOK_ARROW_STAR);
            left = access;
        } else {
            ASTNode *binary = ast_new(p->arena, AST_BINARY, loc);
            binary->binary.op = op;
            binary->binary.left = left;
            binary->binary.right = right;
            left = binary;
        }
    }

    return left;
}

static bool is_assign_op(TokenKind kind) {
    return kind == TOK_ASSIGN || kind == TOK_PLUS_EQ || kind == TOK_MINUS_EQ ||
           kind == TOK_STAR_EQ || kind == TOK_SLASH_EQ || kind == TOK_PERCENT_EQ ||
           kind == TOK_AMP_EQ || kind == TOK_PIPE_EQ || kind == TOK_CARET_EQ ||
           kind == TOK_SHL_EQ || kind == TOK_SHR_EQ;
}

static ASTNode *parse_assignment(Parser *p) {
    ASTNode *expr = parse_binary_expr(p, 0);

    if (is_assign_op(p->current.kind)) {
        TokenKind op = p->current.kind;
        SourceLoc loc = p->current.loc;
        advance(p);
        ASTNode *val = parse_assignment(p);

        ASTNode *assign = ast_new(p->arena, AST_ASSIGN, loc);
        assign->assign.op = op;
        assign->assign.target = expr;
        assign->assign.value = val;
        return assign;
    }

    return expr;
}

static ASTNode *parse_expression(Parser *p) {
    return parse_assignment(p);
}

/* Statement parsing */
static ASTNode *parse_block(Parser *p) {
    SourceLoc loc = p->current.loc;
    expect(p, TOK_LBRACE, "'{' to begin block");

    ASTNode **stmts = arena_alloc(p->arena, sizeof(ASTNode*) * 128);
    int count = 0;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        stmts[count++] = parse_statement(p);
    }
    expect(p, TOK_RBRACE, "'}' to end block");

    ASTNode *block = ast_new(p->arena, AST_STMT_BLOCK, loc);
    block->block.stmts = stmts;
    block->block.count = count;
    return block;
}

static ASTNode *parse_statement(Parser *p) {
    SourceLoc loc = p->current.loc;

    if (match(p, TOK_SEMICOLON)) {
        return ast_new(p->arena, AST_STMT_EXPR, loc);
    }

    if (check(p, TOK_LBRACE)) {
        return parse_block(p);
    }

    if (match(p, TOK_KW_RETURN)) {
        ASTNode *ret = ast_new(p->arena, AST_STMT_RETURN, loc);
        if (!check(p, TOK_SEMICOLON)) {
            ret->ret_stmt.expr = parse_expression(p);
        }
        expect(p, TOK_SEMICOLON, "';' after return statement");
        return ret;
    }

    if (match(p, TOK_KW_IF)) {
        expect(p, TOK_LPAREN, "'(' after 'if'");
        ASTNode *cond = parse_expression(p);
        expect(p, TOK_RPAREN, "')' after if condition");
        ASTNode *then_b = parse_statement(p);
        ASTNode *else_b = NULL;
        if (match(p, TOK_KW_ELSE)) {
            else_b = parse_statement(p);
        }
        ASTNode *stmt = ast_new(p->arena, AST_STMT_IF, loc);
        stmt->if_stmt.cond = cond;
        stmt->if_stmt.then_branch = then_b;
        stmt->if_stmt.else_branch = else_b;
        return stmt;
    }

    if (match(p, TOK_KW_WHILE)) {
        expect(p, TOK_LPAREN, "'(' after 'while'");
        ASTNode *cond = parse_expression(p);
        expect(p, TOK_RPAREN, "')' after while condition");
        ASTNode *body = parse_statement(p);
        ASTNode *stmt = ast_new(p->arena, AST_STMT_WHILE, loc);
        stmt->while_stmt.cond = cond;
        stmt->while_stmt.body = body;
        return stmt;
    }

    if (match(p, TOK_KW_FOR)) {
        expect(p, TOK_LPAREN, "'(' after 'for'");
        ASTNode *init = NULL;
        if (!match(p, TOK_SEMICOLON)) {
            if (is_type_specifier(p)) {
                Type *t = parse_type(p);
                Token name_tok = expect(p, TOK_IDENT, "variable name in for loop init");
                ASTNode *var = ast_new(p->arena, AST_STMT_VAR_DECL, loc);
                var->var_decl.var_type = t;
                var->var_decl.name = name_tok.str_val;
                if (match(p, TOK_ASSIGN)) {
                    var->var_decl.init = parse_expression(p);
                }
                expect(p, TOK_SEMICOLON, "';' after for init");
                init = var;
            } else {
                init = parse_expression(p);
                expect(p, TOK_SEMICOLON, "';' after for init");
            }
        }
        ASTNode *cond = NULL;
        if (!match(p, TOK_SEMICOLON)) {
            cond = parse_expression(p);
            expect(p, TOK_SEMICOLON, "';' after for condition");
        }
        ASTNode *step = NULL;
        if (!check(p, TOK_RPAREN)) {
            step = parse_expression(p);
        }
        expect(p, TOK_RPAREN, "')' after for step");
        ASTNode *body = parse_statement(p);

        ASTNode *stmt = ast_new(p->arena, AST_STMT_FOR, loc);
        stmt->for_stmt.init = init;
        stmt->for_stmt.cond = cond;
        stmt->for_stmt.step = step;
        stmt->for_stmt.body = body;
        return stmt;
    }

    if (match(p, TOK_KW_BREAK)) {
        expect(p, TOK_SEMICOLON, "';' after break");
        return ast_new(p->arena, AST_STMT_BREAK, loc);
    }

    if (match(p, TOK_KW_CONTINUE)) {
        expect(p, TOK_SEMICOLON, "';' after continue");
        return ast_new(p->arena, AST_STMT_CONTINUE, loc);
    }

    if (match(p, TOK_KW_USING)) {
        if (match(p, TOK_KW_NAMESPACE)) {
            Token ns_tok = expect(p, TOK_IDENT, "namespace name after 'using namespace'");
            expect(p, TOK_SEMICOLON, "';' after 'using namespace'");
            ASTNode *un = ast_new(p->arena, AST_STMT_EXPR, loc);
            ASTNode *vref = ast_new(p->arena, AST_VAR_REF, loc);
            vref->var_ref.name = ns_tok.str_val;
            vref->var_ref.scope_prefix = str_intern("using");
            un->stmt_expr.expr = vref;
            return un;
        } else {
            Token name_tok = expect(p, TOK_IDENT, "alias name in 'using'");
            expect(p, TOK_ASSIGN, "'=' in 'using'");
            Type *aliased_type = parse_type(p);
            expect(p, TOK_SEMICOLON, "';' after 'using'");
            ASTNode *td = ast_new(p->arena, AST_DECL_TYPEDEF, loc);
            td->typedef_decl.name = name_tok.str_val;
            td->typedef_decl.aliased_type = aliased_type;
            return td;
        }
    }

    if (match(p, TOK_KW_TYPEDEF)) {
        Type *aliased_type = parse_type(p);
        const char *name = NULL;
        Type *fn_ptr_t = NULL;
        if (try_parse_fn_ptr_declarator(p, aliased_type, &name, &fn_ptr_t) ||
            try_parse_member_ptr_declarator(p, aliased_type, &name, &fn_ptr_t)) {
            aliased_type = fn_ptr_t;
        } else {
            Token name_tok = expect(p, TOK_IDENT, "name in typedef");
            name = name_tok.str_val;
        }
        expect(p, TOK_SEMICOLON, "';' after typedef");
        ASTNode *td = ast_new(p->arena, AST_DECL_TYPEDEF, loc);
        td->typedef_decl.name = name;
        td->typedef_decl.aliased_type = aliased_type;
        return td;
    }

    /* Variable declaration inside statement: Type name [= init]; or Type name(args); */
    if (is_declaration_starting(p)) {
        Type *t = parse_type(p);
        const char *var_name = NULL;
        Type *fn_ptr_t = NULL;
        bool is_fn_ptr = try_parse_fn_ptr_declarator(p, t, &var_name, &fn_ptr_t);
        if (!is_fn_ptr) {
            is_fn_ptr = try_parse_member_ptr_declarator(p, t, &var_name, &fn_ptr_t);
        }
        if (is_fn_ptr) {
            t = fn_ptr_t;
        } else {
            Token name_tok = expect(p, TOK_IDENT, "variable name");
            var_name = name_tok.str_val;
            if (match(p, TOK_LBRACKET)) {
                size_t arr_size = 0;
                if (check(p, TOK_INT_LIT)) {
                    arr_size = (size_t)p->current.int_val;
                    advance(p);
                }
                expect(p, TOK_RBRACKET, "']' in array variable");
                t = type_array(p->arena, t, arr_size);
            }
        }
        ASTNode *var = ast_new(p->arena, AST_STMT_VAR_DECL, loc);
        var->var_decl.var_type = t;
        var->var_decl.name = var_name;

        /* Check for constructor call: Point p(1, 2); */
        if (!is_fn_ptr && match(p, TOK_LPAREN)) {
            ASTNode **args = arena_alloc(p->arena, sizeof(ASTNode*) * 16);
            int arg_count = 0;
            if (!check(p, TOK_RPAREN)) {
                do {
                    args[arg_count++] = parse_assignment(p);
                } while (match(p, TOK_COMMA));
            }
            expect(p, TOK_RPAREN, "')' in object constructor call");

            /* Constructor invocation lowered as new/ctor */
            ASTNode *ctor_call = ast_new(p->arena, AST_NEW, loc);
            ctor_call->new_expr.target_type = t;
            ctor_call->new_expr.args = args;
            ctor_call->new_expr.arg_count = arg_count;
            var->var_decl.init = ctor_call;
        } else if (match(p, TOK_ASSIGN)) {
            var->var_decl.init = parse_expression(p);
        }

        expect(p, TOK_SEMICOLON, "';' after variable declaration");
        return var;
    }

    /* Expression statement */
    ASTNode *expr = parse_expression(p);
    expect(p, TOK_SEMICOLON, "';' after expression");
    ASTNode *stmt = ast_new(p->arena, AST_STMT_EXPR, loc);
    stmt->stmt_expr.expr = expr;
    return stmt;
}

static int parse_param_list(Parser *p, ASTNode ***out_params, bool *out_varargs) {
    ASTNode **params = arena_alloc(p->arena, sizeof(ASTNode*) * 16);
    int param_count = 0;
    bool is_varargs = false;

    if (!check(p, TOK_RPAREN)) {
        do {
            if (match(p, TOK_ELLIPSIS)) {
                is_varargs = true;
                break;
            }
            Type *pt = parse_type(p);
            bool is_pack = false;
            if (match(p, TOK_ELLIPSIS)) {
                is_pack = true;
            }
            const char *pname = NULL;
            Type *fn_ptr_t = NULL;
            if (try_parse_fn_ptr_declarator(p, pt, &pname, &fn_ptr_t) ||
                try_parse_member_ptr_declarator(p, pt, &pname, &fn_ptr_t)) {
                pt = fn_ptr_t;
            } else if (check(p, TOK_IDENT)) {
                pname = p->current.str_val;
                advance(p);
            }
            if (match(p, TOK_ELLIPSIS)) {
                is_pack = true;
            }
            if (!pname) {
                char auto_name[32];
                snprintf(auto_name, sizeof(auto_name), "__arg%d", param_count);
                pname = str_intern(auto_name);
            }
            ASTNode *param = ast_new(p->arena, AST_STMT_VAR_DECL, p->current.loc);
            param->var_decl.var_type = pt;
            param->var_decl.name = pname;
            param->var_decl.is_pack = is_pack;
            params[param_count++] = param;
        } while (match(p, TOK_COMMA));
    }
    expect(p, TOK_RPAREN, "')' in parameter list");

    *out_params = params;
    if (out_varargs) *out_varargs = is_varargs;
    return param_count;
}

/* Parse top-level declarations: namespaces, classes, functions, methods */
static const char *parse_function_or_operator_name(Parser *p, SourceLoc *out_loc, bool *out_is_op) {
    if (out_loc) *out_loc = p->current.loc;
    if (out_is_op) *out_is_op = false;

    if (match(p, TOK_KW_OPERATOR)) {
        if (out_is_op) *out_is_op = true;
        if (match(p, TOK_SHL)) return str_intern("operator<<");
        if (match(p, TOK_SHR)) return str_intern("operator>>");
        if (match(p, TOK_LBRACKET)) {
            expect(p, TOK_RBRACKET, "']' after 'operator['");
            return str_intern("operator[]");
        }
        if (match(p, TOK_PLUS_EQ)) return str_intern("operator+=");
        if (match(p, TOK_MINUS_EQ)) return str_intern("operator-=");
        if (match(p, TOK_STAR_EQ)) return str_intern("operator*=");
        if (match(p, TOK_SLASH_EQ)) return str_intern("operator/=");
        if (match(p, TOK_PLUS)) return str_intern("operator+");
        if (match(p, TOK_MINUS)) return str_intern("operator-");
        if (match(p, TOK_STAR)) return str_intern("operator*");
        if (match(p, TOK_SLASH)) return str_intern("operator/");
        if (match(p, TOK_EQ_EQ)) return str_intern("operator==");
        if (match(p, TOK_EXCL_EQ)) return str_intern("operator!=");
        if (match(p, TOK_LESS_EQ)) return str_intern("operator<=");
        if (match(p, TOK_GREATER_EQ)) return str_intern("operator>=");
        if (match(p, TOK_LESS)) return str_intern("operator<");
        if (match(p, TOK_GREATER)) return str_intern("operator>");
        if (match(p, TOK_ASSIGN)) return str_intern("operator=");
        diag_report(DIAG_ERROR, p->current.loc, "unsupported operator after 'operator' keyword");
        return str_intern("operator_unknown");
    }

    Token ident = expect(p, TOK_IDENT, "identifier or operator");
    return ident.str_val;
}

static ASTNode *parse_class_declaration(Parser *p, bool is_struct) {
    SourceLoc loc = p->current.loc;
    Token name_tok = expect(p, TOK_IDENT, is_struct ? "struct name" : "class name");
    const char *raw_name = name_tok.str_val;
    const char *class_name = raw_name;
    if (p->current_namespace) {
        char full_cls[256];
        snprintf(full_cls, sizeof(full_cls), "%s::%s", p->current_namespace, raw_name);
        class_name = arena_strdup(p->arena, full_cls);
    }

    if (match(p, TOK_SEMICOLON)) {
        ASTNode *fwd = ast_new(p->arena, AST_DECL_CLASS, loc);
        fwd->class_decl.name = class_name;
        fwd->class_decl.fields = NULL;
        fwd->class_decl.methods = NULL;
        fwd->class_decl.method_count = 0;
        return fwd;
    }

    if (match(p, TOK_LESS)) {
        int depth = 1;
        while (depth > 0 && !check(p, TOK_EOF) && !check(p, TOK_SEMICOLON)) {
            if (match(p, TOK_LESS)) depth++;
            else if (match(p, TOK_GREATER)) depth--;
            else advance(p);
        }
        if (match(p, TOK_SEMICOLON)) {
            ASTNode *fwd = ast_new(p->arena, AST_DECL_CLASS, loc);
            fwd->class_decl.name = class_name;
            fwd->class_decl.fields = NULL;
            fwd->class_decl.methods = NULL;
            fwd->class_decl.method_count = 0;
            return fwd;
        }
    }

    expect(p, TOK_LBRACE, "'{' to begin class body");

    /* Default access: public for struct, private for class */
    int current_access = is_struct ? 0 : 2; /* 0: public, 2: private */

    Field *fields_head = NULL;
    Field **fields_tail = &fields_head;

    ASTNode **methods = arena_alloc(p->arena, sizeof(ASTNode*) * 64);
    int method_count = 0;

    const char *prev_class = p->current_class;
    p->current_class = class_name;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        if (match(p, TOK_KW_PUBLIC)) {
            expect(p, TOK_COLON, "':' after 'public'");
            current_access = 0;
            continue;
        }
        if (match(p, TOK_KW_PRIVATE)) {
            expect(p, TOK_COLON, "':' after 'private'");
            current_access = 2;
            continue;
        }
        if (match(p, TOK_KW_PROTECTED)) {
            expect(p, TOK_COLON, "':' after 'protected'");
            current_access = 1;
            continue;
        }

        /* Type alias inside class: using name = type; */
        if (match(p, TOK_KW_USING)) {
            SourceLoc loc = p->current.loc;
            Token alias_tok = expect(p, TOK_IDENT, "alias name in 'using'");
            expect(p, TOK_ASSIGN, "'=' after alias name");
            Type *aliased_type = parse_type(p);
            expect(p, TOK_SEMICOLON, "';' after type alias");
            ASTNode *td = ast_new(p->arena, AST_DECL_TYPEDEF, loc);
            td->typedef_decl.name = alias_tok.str_val;
            td->typedef_decl.aliased_type = aliased_type;
            methods[method_count++] = td;
            continue;
        }

        /* Typedef inside class: typedef type name; */
        if (match(p, TOK_KW_TYPEDEF)) {
            SourceLoc loc = p->current.loc;
            Type *aliased_type = parse_type(p);
            const char *name = NULL;
            Type *fn_ptr_t = NULL;
            if (try_parse_fn_ptr_declarator(p, aliased_type, &name, &fn_ptr_t) ||
                try_parse_member_ptr_declarator(p, aliased_type, &name, &fn_ptr_t)) {
                aliased_type = fn_ptr_t;
            } else {
                Token name_tok = expect(p, TOK_IDENT, "name in typedef");
                name = name_tok.str_val;
            }
            expect(p, TOK_SEMICOLON, "';' after typedef");
            ASTNode *td = ast_new(p->arena, AST_DECL_TYPEDEF, loc);
            td->typedef_decl.name = name;
            td->typedef_decl.aliased_type = aliased_type;
            methods[method_count++] = td;
            continue;
        }

        /* Check for constructor: ClassName(...) */
        if (check(p, TOK_IDENT) &&
            (strcmp(p->current.str_val, raw_name) == 0 || strcmp(p->current.str_val, class_name) == 0) &&
            p->peek.kind == TOK_LPAREN) {
            SourceLoc ctor_loc = p->current.loc;
            advance(p); /* Skip class name */
            expect(p, TOK_LPAREN, "'(' in constructor");

            ASTNode **params = NULL;
            bool is_va = false;
            int param_count = parse_param_list(p, &params, &is_va);

            /* Constructor initializer list: : head(h), tail(t...) */
            ASTNode **init_stmts = arena_alloc(p->arena, sizeof(ASTNode*) * 16);
            int init_count = 0;
            if (match(p, TOK_COLON)) {
                do {
                    Token mem_tok = expect(p, TOK_IDENT, "member name in constructor initializer");
                    expect(p, TOK_LPAREN, "'(' in member initializer");
                    ASTNode **m_args = arena_alloc(p->arena, sizeof(ASTNode*) * 16);
                    int m_arg_count = 0;
                    if (!check(p, TOK_RPAREN)) {
                        do {
                            m_args[m_arg_count++] = parse_assignment(p);
                        } while (match(p, TOK_COMMA));
                    }
                    expect(p, TOK_RPAREN, "')' in member initializer");

                    ASTNode *this_node = ast_new(p->arena, AST_THIS, mem_tok.loc);
                    ASTNode *memb = ast_new(p->arena, AST_MEMBER, mem_tok.loc);
                    memb->member.object = this_node;
                    memb->member.member_name = mem_tok.str_val;
                    memb->member.is_arrow = true;

                    if (m_arg_count == 1 && m_args[0]->kind != AST_PACK_EXPANSION) {
                        ASTNode *assign = ast_new(p->arena, AST_ASSIGN, mem_tok.loc);
                        assign->assign.op = TOK_ASSIGN;
                        assign->assign.target = memb;
                        assign->assign.value = m_args[0];

                        ASTNode *stmt = ast_new(p->arena, AST_STMT_EXPR, mem_tok.loc);
                        stmt->stmt_expr.expr = assign;
                        init_stmts[init_count++] = stmt;
                    } else {
                        ASTNode *mcall = ast_new(p->arena, AST_CALL, mem_tok.loc);
                        mcall->call.is_method = true;
                        mcall->call.object = memb;
                        mcall->call.name = mem_tok.str_val;
                        mcall->call.args = m_args;
                        mcall->call.arg_count = m_arg_count;

                        ASTNode *stmt = ast_new(p->arena, AST_STMT_EXPR, mem_tok.loc);
                        stmt->stmt_expr.expr = mcall;
                        init_stmts[init_count++] = stmt;
                    }
                } while (match(p, TOK_COMMA));
            }

            ASTNode *body = NULL;
            if (check(p, TOK_LBRACE)) {
                body = parse_block(p);
            } else {
                expect(p, TOK_SEMICOLON, "';' or body for constructor");
            }

            if (init_count > 0) {
                if (!body) {
                    body = ast_new(p->arena, AST_STMT_BLOCK, ctor_loc);
                    body->block.stmts = init_stmts;
                    body->block.count = init_count;
                } else {
                    int total = init_count + body->block.count;
                    ASTNode **merged = arena_alloc(p->arena, sizeof(ASTNode*) * total);
                    for (int i = 0; i < init_count; i++) merged[i] = init_stmts[i];
                    for (int i = 0; i < body->block.count; i++) merged[init_count + i] = body->block.stmts[i];
                    body->block.stmts = merged;
                    body->block.count = total;
                }
            }

            ASTNode *fn = ast_new(p->arena, AST_DECL_FUNC, ctor_loc);
            fn->func_decl.name = class_name;
            fn->func_decl.class_owner = class_name;
            fn->func_decl.params = params;
            fn->func_decl.param_count = param_count;
            fn->func_decl.body = body;
            fn->func_decl.is_method = true;
            fn->func_decl.is_ctor = true;
            methods[method_count++] = fn;
            continue;
        }

        /* Check for destructor: ~ClassName() */
        if (match(p, TOK_TILDE)) {
            SourceLoc dtor_loc = p->current.loc;
            expect(p, TOK_IDENT, "class name after '~'");
            expect(p, TOK_LPAREN, "'(' in destructor");
            expect(p, TOK_RPAREN, "')' in destructor");

            ASTNode *body = NULL;
            if (check(p, TOK_LBRACE)) {
                body = parse_block(p);
            } else {
                expect(p, TOK_SEMICOLON, "';' or body for destructor");
            }

            ASTNode *fn = ast_new(p->arena, AST_DECL_FUNC, dtor_loc);
            fn->func_decl.name = str_intern("~dtor");
            fn->func_decl.class_owner = class_name;
            fn->func_decl.params = NULL;
            fn->func_decl.param_count = 0;
            fn->func_decl.body = body;
            fn->func_decl.is_method = true;
            fn->func_decl.is_dtor = true;
            methods[method_count++] = fn;
            continue;
        }

        /* Member function, Operator, or Field declaration */
        Type *t = parse_type(p);
        const char *fn_ptr_name = NULL;
        Type *fn_ptr_t = NULL;
        if (try_parse_fn_ptr_declarator(p, t, &fn_ptr_name, &fn_ptr_t) ||
            try_parse_member_ptr_declarator(p, t, &fn_ptr_name, &fn_ptr_t)) {
            Field *f = arena_alloc_zero(p->arena, sizeof(Field));
            f->name = fn_ptr_name;
            f->type = fn_ptr_t;
            f->access = current_access;
            *fields_tail = f;
            fields_tail = &f->next;
            expect(p, TOK_SEMICOLON, "';' after member function pointer");
            continue;
        }
        SourceLoc name_loc;
        bool is_op = false;
        const char *mname = parse_function_or_operator_name(p, &name_loc, &is_op);

        /* If followed by '(', it's a member function / operator */
        if (match(p, TOK_LPAREN)) {
            ASTNode **params = NULL;
            bool is_va = false;
            int param_count = parse_param_list(p, &params, &is_va);

            ASTNode *body = NULL;
            if (check(p, TOK_LBRACE)) {
                body = parse_block(p);
            } else {
                expect(p, TOK_SEMICOLON, "';' or body for method");
            }

            ASTNode *fn = ast_new(p->arena, AST_DECL_FUNC, name_loc);
            fn->func_decl.name = mname;
            fn->func_decl.class_owner = class_name;
            fn->func_decl.func_type = t;
            fn->func_decl.params = params;
            fn->func_decl.param_count = param_count;
            fn->func_decl.body = body;
            fn->func_decl.is_method = true;
            fn->func_decl.is_operator = is_op;
            methods[method_count++] = fn;
        } else {
            /* Array field: int data[4]; */
            if (match(p, TOK_LBRACKET)) {
                size_t arr_size = 0;
                if (check(p, TOK_INT_LIT)) {
                    arr_size = (size_t)p->current.int_val;
                    advance(p);
                }
                expect(p, TOK_RBRACKET, "']' in array field");
                t = type_array(p->arena, t, arr_size);
            }

            /* Field declaration */
            Field *f = arena_alloc_zero(p->arena, sizeof(Field));
            f->name = mname;
            f->type = t;
            f->access = current_access;
            *fields_tail = f;
            fields_tail = &f->next;
            expect(p, TOK_SEMICOLON, "';' after member variable");
        }
    }

    expect(p, TOK_RBRACE, "'}' to close class body");
    expect(p, TOK_SEMICOLON, "';' after class declaration");

    p->current_class = prev_class;

    ASTNode *cls = ast_new(p->arena, AST_DECL_CLASS, loc);
    cls->class_decl.name = class_name;
    cls->class_decl.fields = fields_head;
    cls->class_decl.methods = methods;
    cls->class_decl.method_count = method_count;
    return cls;
}

static ASTNode *parse_namespace(Parser *p) {
    SourceLoc loc = p->current.loc;
    Token name_tok = expect(p, TOK_IDENT, "namespace name");
    const char *ns_name = name_tok.str_val;

    expect(p, TOK_LBRACE, "'{' to begin namespace body");

    const char *prev_ns = p->current_namespace;
    if (prev_ns) {
        char full_ns[256];
        snprintf(full_ns, sizeof(full_ns), "%s::%s", prev_ns, ns_name);
        p->current_namespace = arena_strdup(p->arena, full_ns);
    } else {
        p->current_namespace = ns_name;
    }

    ASTNode **decls = arena_alloc(p->arena, sizeof(ASTNode*) * 128);
    int count = 0;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        decls[count++] = parse_declaration(p);
    }
    expect(p, TOK_RBRACE, "'}' to close namespace body");

    p->current_namespace = prev_ns;

    ASTNode *ns = ast_new(p->arena, AST_DECL_NAMESPACE, loc);
    ns->ns_decl.name = ns_name;
    ns->ns_decl.decls = decls;
    ns->ns_decl.count = count;
    return ns;
}

static ASTNode *parse_declaration(Parser *p) {
    if (match(p, TOK_KW_USING)) {
        if (match(p, TOK_KW_NAMESPACE)) {
            Token ns_tok = expect(p, TOK_IDENT, "namespace name after 'using namespace'");
            expect(p, TOK_SEMICOLON, "';' after 'using namespace'");
            ASTNode *un = ast_new(p->arena, AST_STMT_EXPR, ns_tok.loc);
            ASTNode *vref = ast_new(p->arena, AST_VAR_REF, ns_tok.loc);
            vref->var_ref.name = ns_tok.str_val;
            vref->var_ref.scope_prefix = str_intern("using");
            un->stmt_expr.expr = vref;
            return un;
        } else {
            SourceLoc loc = p->current.loc;
            Token name_tok = expect(p, TOK_IDENT, "alias name in 'using'");
            expect(p, TOK_ASSIGN, "'=' in 'using'");
            Type *aliased_type = parse_type(p);
            expect(p, TOK_SEMICOLON, "';' after 'using'");
            ASTNode *td = ast_new(p->arena, AST_DECL_TYPEDEF, loc);
            td->typedef_decl.name = name_tok.str_val;
            td->typedef_decl.aliased_type = aliased_type;
            return td;
        }
    }

    if (match(p, TOK_KW_TYPEDEF)) {
        SourceLoc loc = p->current.loc;
        Type *aliased_type = parse_type(p);
        const char *name = NULL;
        Type *fn_ptr_t = NULL;
        if (try_parse_fn_ptr_declarator(p, aliased_type, &name, &fn_ptr_t) ||
            try_parse_member_ptr_declarator(p, aliased_type, &name, &fn_ptr_t)) {
            aliased_type = fn_ptr_t;
        } else {
            Token name_tok = expect(p, TOK_IDENT, "name in typedef");
            name = name_tok.str_val;
        }
        expect(p, TOK_SEMICOLON, "';' after typedef");
        ASTNode *td = ast_new(p->arena, AST_DECL_TYPEDEF, loc);
        td->typedef_decl.name = name;
        td->typedef_decl.aliased_type = aliased_type;
        return td;
    }

    if (match(p, TOK_KW_TEMPLATE)) {
        SourceLoc loc = p->current.loc;
        expect(p, TOK_LESS, "'<' after 'template'");
        const char *param_names[16];
        bool is_pack[16] = {0};
        int param_count = 0;
        bool is_variadic = false;
        do {
            bool param_is_pack = false;
            if (match(p, TOK_KW_TYPENAME) || match(p, TOK_KW_CLASS)) {
                if (match(p, TOK_ELLIPSIS)) {
                    param_is_pack = true;
                }
            }
            Token ptok = expect(p, TOK_IDENT, "template parameter name");
            if (match(p, TOK_ELLIPSIS)) {
                param_is_pack = true;
            }
            if (param_count < 16) {
                param_names[param_count] = ptok.str_val;
                is_pack[param_count] = param_is_pack;
                if (param_is_pack) is_variadic = true;
                param_count++;
            }
        } while (match(p, TOK_COMMA));
        expect(p, TOK_GREATER, "'>' after template parameter");
        ASTNode *body_decl = parse_declaration(p);
        ASTNode *templ = ast_new(p->arena, AST_DECL_TEMPLATE, loc);
        templ->template_decl.param_count = param_count;
        templ->template_decl.is_variadic = is_variadic;
        for (int i = 0; i < param_count; i++) {
            templ->template_decl.param_names[i] = param_names[i];
            templ->template_decl.is_pack[i] = is_pack[i];
        }
        templ->template_decl.param_name = param_names[0];
        templ->template_decl.decl = body_decl;
        return templ;
    }

    if (match(p, TOK_KW_NAMESPACE)) {
        return parse_namespace(p);
    }

    if (match(p, TOK_KW_CLASS)) {
        return parse_class_declaration(p, false);
    }

    if (match(p, TOK_KW_STRUCT)) {
        return parse_class_declaration(p, true);
    }

    /* Out-of-line method / constructor / destructor definition:
       ClassName::MethodName(...) or ClassName::ClassName(...) */
    if (check(p, TOK_IDENT) && p->peek.kind == TOK_COLON_COLON) {
        SourceLoc loc = p->current.loc;
        const char *class_owner = p->current.str_val;
        advance(p); /* Skip class name */
        advance(p); /* Skip '::' */

        bool is_dtor = false;
        if (match(p, TOK_TILDE)) {
            is_dtor = true;
        }

        SourceLoc name_loc = p->current.loc;
        bool is_op = false;
        const char *method_name = is_dtor ? str_intern("~dtor") : parse_function_or_operator_name(p, &name_loc, &is_op);
        bool is_ctor = (!is_dtor && !is_op && strcmp(method_name, class_owner) == 0);

        expect(p, TOK_LPAREN, "'(' in method parameter list");
        ASTNode **params = NULL;
        bool is_va = false;
        int param_count = parse_param_list(p, &params, &is_va);

        ASTNode *body = parse_block(p);

        ASTNode *fn = ast_new(p->arena, AST_DECL_FUNC, loc);
        fn->func_decl.name = method_name;
        fn->func_decl.class_owner = class_owner;
        fn->func_decl.func_type = g_type_void;
        fn->func_decl.params = params;
        fn->func_decl.param_count = param_count;
        fn->func_decl.body = body;
        fn->func_decl.is_method = true;
        fn->func_decl.is_ctor = is_ctor;
        fn->func_decl.is_dtor = is_dtor;
        fn->func_decl.is_operator = is_op;
        return fn;
    }

    /* Normal function or out-of-line method with return type */
    bool is_extern = false;
    while (match(p, TOK_KW_EXTERN) || match(p, TOK_KW_STATIC) || match(p, TOK_KW_INLINE)) {
        is_extern = true;
    }

    SourceLoc loc = p->current.loc;
    Type *ret_type = parse_type(p);

    const char *fn_ptr_name = NULL;
    Type *fn_ptr_t = NULL;
    if (try_parse_fn_ptr_declarator(p, ret_type, &fn_ptr_name, &fn_ptr_t) ||
        try_parse_member_ptr_declarator(p, ret_type, &fn_ptr_name, &fn_ptr_t)) {
        ASTNode *var = ast_new(p->arena, AST_STMT_VAR_DECL, loc);
        var->var_decl.var_type = fn_ptr_t;
        var->var_decl.name = fn_ptr_name;
        if (match(p, TOK_ASSIGN)) {
            var->var_decl.init = parse_expression(p);
        }
        expect(p, TOK_SEMICOLON, "';' after global variable declaration");
        return var;
    }

    const char *class_owner = NULL;
    SourceLoc name_loc;
    bool is_op = false;
    const char *fn_name = parse_function_or_operator_name(p, &name_loc, &is_op);

    if (match(p, TOK_COLON_COLON)) {
        class_owner = fn_name;
        fn_name = parse_function_or_operator_name(p, &name_loc, &is_op);
        while (match(p, TOK_COLON_COLON)) {
            char full_owner[256];
            snprintf(full_owner, sizeof(full_owner), "%s::%s", class_owner, fn_name);
            class_owner = arena_strdup(p->arena, full_owner);
            fn_name = parse_function_or_operator_name(p, &name_loc, &is_op);
        }
    } else if (p->current_namespace != NULL) {
        class_owner = p->current_namespace;
    }

    /* If followed by '(', it's a function declaration/definition */
    if (match(p, TOK_LPAREN)) {
        ASTNode **params = NULL;
        bool is_va = false;
        int param_count = parse_param_list(p, &params, &is_va);

        ASTNode *body = NULL;
        if (check(p, TOK_LBRACE)) {
            body = parse_block(p);
        } else {
            expect(p, TOK_SEMICOLON, "';' or function body");
        }

        bool is_method = (class_owner != NULL && class_owner != p->current_namespace);
        ASTNode *fn = ast_new(p->arena, AST_DECL_FUNC, loc);
        fn->func_decl.name = fn_name;
        fn->func_decl.class_owner = class_owner;
        fn->func_decl.func_type = ret_type;
        fn->func_decl.params = params;
        fn->func_decl.param_count = param_count;
        fn->func_decl.body = body;
        fn->func_decl.is_method = is_method;
        fn->func_decl.is_varargs = is_va;
        fn->func_decl.is_extern = is_extern;
        fn->func_decl.is_operator = is_op;
        return fn;
    }

    /* Global variable declaration */
    if (match(p, TOK_LBRACKET)) {
        size_t arr_size = 0;
        if (check(p, TOK_INT_LIT)) {
            arr_size = (size_t)p->current.int_val;
            advance(p);
        }
        expect(p, TOK_RBRACKET, "']' in global array variable");
        ret_type = type_array(p->arena, ret_type, arr_size);
    }
    ASTNode *var = ast_new(p->arena, AST_STMT_VAR_DECL, loc);
    var->var_decl.var_type = ret_type;
    var->var_decl.name = fn_name;
    if (match(p, TOK_ASSIGN)) {
        var->var_decl.init = parse_expression(p);
    }
    expect(p, TOK_SEMICOLON, "';' after global variable declaration");
    return var;
}

ASTNode *parser_parse(Parser *p) {
    if (!p->primed) {
        p->current = lexer_next(&p->lexer);
        p->peek = lexer_next(&p->lexer);
        p->primed = true;
    }
    SourceLoc loc = p->current.loc;
    ASTNode **decls = arena_alloc(p->arena, sizeof(ASTNode*) * 1024);
    int count = 0;

    while (!check(p, TOK_EOF)) {
        ASTNode *decl = parse_declaration(p);
        if (decl) {
            decls[count++] = decl;
        }
    }

    ASTNode *prog = ast_new(p->arena, AST_PROGRAM, loc);
    prog->program.decls = decls;
    prog->program.count = count;
    return prog;
}
