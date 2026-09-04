#include "parser.h"
#include "str.h"

/* Forward declarations */
static ASTNode *parse_declaration(Parser *p);
static ASTNode *parse_statement(Parser *p);
static ASTNode *parse_expression(Parser *p);
static ASTNode *parse_assignment(Parser *p);
static Type *parse_type(Parser *p);

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
           k == TOK_KW_FLOAT || k == TOK_KW_DOUBLE || k == TOK_KW_CONST ||
           k == TOK_KW_CLASS || k == TOK_KW_STRUCT || k == TOK_IDENT;
}

static bool is_declaration_starting(Parser *p) {
    TokenKind k = p->current.kind;
    if (k == TOK_KW_CONST) return true;
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
        while (p->current.kind == TOK_STAR || p->current.kind == TOK_AMP) {
            advance(p);
        }
        bool is_decl = (p->current.kind == TOK_IDENT);
        *p = saved;
        return is_decl;
    }
    return false;
}

static Type *parse_type(Parser *p) {
    SourceLoc loc = p->current.loc;
    bool is_const = false;
    if (match(p, TOK_KW_CONST)) {
        is_const = true;
    }

    Type *base = NULL;
    if (match(p, TOK_KW_VOID)) base = g_type_void;
    else if (match(p, TOK_KW_BOOL)) base = g_type_bool;
    else if (match(p, TOK_KW_CHAR)) base = g_type_char;
    else if (match(p, TOK_KW_SHORT) || match(p, TOK_KW_INT)) base = g_type_int;
    else if (match(p, TOK_KW_LONG)) base = g_type_long;
    else if (match(p, TOK_KW_CLASS) || match(p, TOK_KW_STRUCT) || check(p, TOK_IDENT)) {
        const char *name = p->current.str_val;
        expect(p, TOK_IDENT, "class/type name");
        while (match(p, TOK_COLON_COLON)) {
            Token sub = expect(p, TOK_IDENT, "type name after '::'");
            char qname[256];
            snprintf(qname, sizeof(qname), "%s::%s", name, sub.str_val);
            name = arena_strdup(p->arena, qname);
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

        /* Check for scope resolution: Foo::Bar */
        const char *scope_prefix = NULL;
        if (match(p, TOK_COLON_COLON)) {
            scope_prefix = name;
            name = expect(p, TOK_IDENT, "identifier after '::'").str_val;
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
        /* Array indexing: expr[idx] -> *(expr + idx) */
        else if (match(p, TOK_LBRACKET)) {
            ASTNode *index = parse_expression(p);
            expect(p, TOK_RBRACKET, "']' in array index");

            ASTNode *add = ast_new(p->arena, AST_BINARY, loc);
            add->binary.op = TOK_PLUS;
            add->binary.left = expr;
            add->binary.right = index;

            ASTNode *deref = ast_new(p->arena, AST_UNARY, loc);
            deref->unary.op = TOK_STAR;
            deref->unary.operand = add;
            deref->unary.is_prefix = true;
            expr = deref;
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

    return parse_postfix(p);
}

/* Binary operator precedence climbing */
static int get_binary_op_precedence(TokenKind kind) {
    switch (kind) {
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

        ASTNode *binary = ast_new(p->arena, AST_BINARY, loc);
        binary->binary.op = op;
        binary->binary.left = left;
        binary->binary.right = right;
        left = binary;
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
        }
    }

    /* Variable declaration inside statement: Type name [= init]; or Type name(args); */
    if (is_declaration_starting(p)) {
        Type *t = parse_type(p);
        Token name_tok = expect(p, TOK_IDENT, "variable name");
        ASTNode *var = ast_new(p->arena, AST_STMT_VAR_DECL, loc);
        var->var_decl.var_type = t;
        var->var_decl.name = name_tok.str_val;

        /* Check for constructor call: Point p(1, 2); */
        if (match(p, TOK_LPAREN)) {
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
            const char *pname = NULL;
            if (check(p, TOK_IDENT)) {
                pname = p->current.str_val;
                advance(p);
            } else {
                char auto_name[32];
                snprintf(auto_name, sizeof(auto_name), "__arg%d", param_count);
                pname = str_intern(auto_name);
            }
            ASTNode *param = ast_new(p->arena, AST_STMT_VAR_DECL, p->current.loc);
            param->var_decl.var_type = pt;
            param->var_decl.name = pname;
            params[param_count++] = param;
        } while (match(p, TOK_COMMA));
    }
    expect(p, TOK_RPAREN, "')' in parameter list");

    *out_params = params;
    if (out_varargs) *out_varargs = is_varargs;
    return param_count;
}

/* Parse top-level declarations: namespaces, classes, functions, methods */
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

            ASTNode *body = NULL;
            if (check(p, TOK_LBRACE)) {
                body = parse_block(p);
            } else {
                expect(p, TOK_SEMICOLON, "';' or body for constructor");
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

        /* Member function or Field declaration */
        Type *t = parse_type(p);
        Token ident = expect(p, TOK_IDENT, "member or method name");

        /* If followed by '(', it's a member function */
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

            ASTNode *fn = ast_new(p->arena, AST_DECL_FUNC, ident.loc);
            fn->func_decl.name = ident.str_val;
            fn->func_decl.class_owner = class_name;
            fn->func_decl.func_type = t;
            fn->func_decl.params = params;
            fn->func_decl.param_count = param_count;
            fn->func_decl.body = body;
            fn->func_decl.is_method = true;
            methods[method_count++] = fn;
        } else {
            /* Field declaration */
            Field *f = arena_alloc_zero(p->arena, sizeof(Field));
            f->name = ident.str_val;
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
        }
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

        Token method_name = expect(p, TOK_IDENT, "method name or constructor");
        bool is_ctor = (strcmp(method_name.str_val, class_owner) == 0);

        expect(p, TOK_LPAREN, "'(' in method parameter list");
        ASTNode **params = NULL;
        bool is_va = false;
        int param_count = parse_param_list(p, &params, &is_va);

        ASTNode *body = parse_block(p);

        ASTNode *fn = ast_new(p->arena, AST_DECL_FUNC, loc);
        fn->func_decl.name = is_dtor ? str_intern("~dtor") : method_name.str_val;
        fn->func_decl.class_owner = class_owner;
        fn->func_decl.func_type = g_type_void;
        fn->func_decl.params = params;
        fn->func_decl.param_count = param_count;
        fn->func_decl.body = body;
        fn->func_decl.is_method = true;
        fn->func_decl.is_ctor = is_ctor;
        fn->func_decl.is_dtor = is_dtor;
        return fn;
    }

    /* Normal function or out-of-line method with return type */
    bool is_extern = false;
    while (match(p, TOK_KW_EXTERN) || match(p, TOK_KW_STATIC) || match(p, TOK_KW_INLINE)) {
        is_extern = true;
    }

    SourceLoc loc = p->current.loc;
    Type *ret_type = parse_type(p);

    const char *class_owner = NULL;
    Token name_tok = expect(p, TOK_IDENT, "function or variable name");
    const char *fn_name = name_tok.str_val;

    if (match(p, TOK_COLON_COLON)) {
        class_owner = fn_name;
        fn_name = expect(p, TOK_IDENT, "method name after '::'").str_val;
        while (match(p, TOK_COLON_COLON)) {
            char full_owner[256];
            snprintf(full_owner, sizeof(full_owner), "%s::%s", class_owner, fn_name);
            class_owner = arena_strdup(p->arena, full_owner);
            fn_name = expect(p, TOK_IDENT, "method name after '::'").str_val;
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
        return fn;
    }

    /* Global variable declaration */
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
    ASTNode **decls = arena_alloc(p->arena, sizeof(ASTNode*) * 256);
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
