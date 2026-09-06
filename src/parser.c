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
static ASTNode *parse_class_declaration(Parser *p, bool is_struct);

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
    if (kind == TOK_IDENT && !err.str_val) {
        err.str_val = "";
    }
    advance(p);
    return err;
}

static unsigned int hash_type_str(const char *s) {
    unsigned int h = 5381;
    while (*s) {
        h = ((h << 5) + h) + (unsigned char)*s;
        s++;
    }
    return h;
}

static void parser_add_type(Parser *p, const char *name) {
    if (!name || !*name || !p->type_table) return;
    unsigned int idx = hash_type_str(name) % TYPE_TABLE_SIZE;
    for (TypeNameNode *cur = p->type_table[idx]; cur; cur = cur->next) {
        if (strcmp(cur->name, name) == 0) return;
    }
    TypeNameNode *node = arena_alloc(p->arena, sizeof(TypeNameNode));
    node->name = arena_strdup(p->arena, name);
    node->next = p->type_table[idx];
    p->type_table[idx] = node;
}

static bool parser_is_known_type(Parser *p, const char *name) {
    if (!name || !*name || !p->type_table) return false;
    unsigned int idx = hash_type_str(name) % TYPE_TABLE_SIZE;
    for (TypeNameNode *cur = p->type_table[idx]; cur; cur = cur->next) {
        if (strcmp(cur->name, name) == 0) return true;
    }
    return false;
}

static void append_node(Parser *p, ASTNode ***nodes, int *count, int *capacity, ASTNode *node) {
    if (*count == *capacity) {
        int new_capacity = *capacity > 0 ? *capacity * 2 : 8;
        ASTNode **grown = arena_alloc(p->arena, sizeof(ASTNode *) * (size_t)new_capacity);
        if (*count > 0) {
            memcpy(grown, *nodes, sizeof(ASTNode *) * (size_t)*count);
        }
        *nodes = grown;
        *capacity = new_capacity;
    }
    (*nodes)[(*count)++] = node;
}

void parser_init(Parser *p, Arena *arena, const char *source, const char *filename) {
    lexer_init(&p->lexer, source, filename);
    p->arena = arena;
    p->current_namespace = NULL;
    p->current_class = NULL;
    p->primed = false;
    p->pending_decl_count = 0;
    p->type_table = arena_alloc(arena, sizeof(TypeNameNode*) * TYPE_TABLE_SIZE);
    memset(p->type_table, 0, sizeof(TypeNameNode*) * TYPE_TABLE_SIZE);

    parser_add_type(p, "size_t");
    parser_add_type(p, "ssize_t");
    parser_add_type(p, "int8_t");
    parser_add_type(p, "int16_t");
    parser_add_type(p, "int32_t");
    parser_add_type(p, "int64_t");
    parser_add_type(p, "uint8_t");
    parser_add_type(p, "uint16_t");
    parser_add_type(p, "uint32_t");
    parser_add_type(p, "uint64_t");
    parser_add_type(p, "intptr_t");
    parser_add_type(p, "uintptr_t");
    parser_add_type(p, "ptrdiff_t");
    parser_add_type(p, "uintmax_t");
    parser_add_type(p, "intmax_t");
    parser_add_type(p, "wchar_t");
    parser_add_type(p, "nullptr_t");
}

void parser_destroy(Parser *p) {
    lexer_destroy(&p->lexer);
}

void parser_add_include_path(Parser *p, const char *path) {
    lexer_add_include_path(&p->lexer, path);
}

static void skip_gnu_attributes(Parser *p) {
    while (match(p, TOK_KW_ATTRIBUTE)) {
        if (match(p, TOK_LPAREN)) {
            int depth = 1;
            while (depth > 0 && !check(p, TOK_EOF)) {
                if (match(p, TOK_LPAREN)) {
                    depth++;
                } else if (match(p, TOK_RPAREN)) {
                    depth--;
                } else {
                    advance(p);
                }
            }
        }
    }
}

static void skip_asm_annotation(Parser *p) {
    if (match(p, TOK_KW_ASM)) {
        if (match(p, TOK_LPAREN)) {
            int depth = 1;
            while (depth > 0 && !check(p, TOK_EOF)) {
                if (match(p, TOK_LPAREN)) {
                    depth++;
                } else if (match(p, TOK_RPAREN)) {
                    depth--;
                } else {
                    advance(p);
                }
            }
        }
    }
}

static void skip_attributes_and_asm(Parser *p) {
    while (check(p, TOK_KW_ATTRIBUTE) || check(p, TOK_KW_ASM) || match(p, TOK_KW_EXTENSION)) {
        if (check(p, TOK_KW_ATTRIBUTE)) {
            skip_gnu_attributes(p);
        } else if (check(p, TOK_KW_ASM)) {
            skip_asm_annotation(p);
        }
    }
}

static bool is_type_specifier(Parser *p) {
    TokenKind k = p->current.kind;
    return k == TOK_KW_VOID || k == TOK_KW_BOOL || k == TOK_KW_CHAR ||
           k == TOK_KW_SHORT || k == TOK_KW_INT || k == TOK_KW_LONG ||
           k == TOK_KW_SIGNED || k == TOK_KW_UNSIGNED ||
           k == TOK_KW_FLOAT || k == TOK_KW_DOUBLE || k == TOK_KW_CONST ||
           k == TOK_KW_VOLATILE || k == TOK_KW_RESTRICT || k == TOK_KW_TYPEOF ||
           k == TOK_KW_CLASS || k == TOK_KW_STRUCT || k == TOK_IDENT ||
           k == TOK_KW_EXTENSION || k == TOK_KW_ATTRIBUTE;
}

static bool is_declaration_starting(Parser *p) {
    TokenKind k = p->current.kind;
    if (k == TOK_KW_CONST || k == TOK_KW_VOLATILE || k == TOK_KW_RESTRICT ||
        k == TOK_KW_SIGNED || k == TOK_KW_UNSIGNED || k == TOK_KW_EXTENSION ||
        k == TOK_KW_ATTRIBUTE || k == TOK_KW_TYPEOF) return true;
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
    if (k == TOK_KW_CONST || k == TOK_KW_VOLATILE || k == TOK_KW_RESTRICT ||
        k == TOK_KW_SIGNED || k == TOK_KW_UNSIGNED || k == TOK_KW_EXTENSION ||
        k == TOK_KW_ATTRIBUTE || k == TOK_KW_TYPEOF ||
        k == TOK_KW_VOID || k == TOK_KW_BOOL || k == TOK_KW_CHAR ||
        k == TOK_KW_SHORT || k == TOK_KW_INT || k == TOK_KW_LONG ||
        k == TOK_KW_FLOAT || k == TOK_KW_DOUBLE ||
        k == TOK_KW_CLASS || k == TOK_KW_STRUCT) {
        return true;
    }
    if (k == TOK_IDENT) {
        const char *first_ident = p->current.str_val;
        Parser saved = *p;
        advance(p);
        while (p->current.kind == TOK_COLON_COLON) {
            advance(p);
            if (p->current.kind == TOK_IDENT) {
                first_ident = p->current.str_val;
                advance(p);
            }
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
        int ptr_count = 0;
        while (p->current.kind == TOK_STAR || p->current.kind == TOK_AMP) {
            ptr_count++;
            advance(p);
        }
        bool is_close = (p->current.kind == TOK_RPAREN);
        *p = saved;
        if (!is_close) return false;
        if (ptr_count > 0) return true;
        return parser_is_known_type(p, first_ident);
    }
    return false;
}

static bool is_cast_type(Parser *p) {
    TokenKind k = p->current.kind;
    if (k == TOK_KW_CONST || k == TOK_KW_VOLATILE || k == TOK_KW_RESTRICT ||
        k == TOK_KW_SIGNED || k == TOK_KW_UNSIGNED || k == TOK_KW_EXTENSION ||
        k == TOK_KW_ATTRIBUTE || k == TOK_KW_TYPEOF ||
        k == TOK_KW_VOID || k == TOK_KW_BOOL || k == TOK_KW_CHAR ||
        k == TOK_KW_SHORT || k == TOK_KW_INT || k == TOK_KW_LONG ||
        k == TOK_KW_FLOAT || k == TOK_KW_DOUBLE ||
        k == TOK_KW_CLASS || k == TOK_KW_STRUCT) {
        return true;
    }
    if (k == TOK_IDENT) {
        const char *first_ident = p->current.str_val;
        Parser saved = *p;
        advance(p);
        while (p->current.kind == TOK_COLON_COLON) {
            advance(p);
            if (p->current.kind == TOK_IDENT) {
                first_ident = p->current.str_val;
                advance(p);
            }
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
        int ptr_count = 0;
        while (p->current.kind == TOK_STAR || p->current.kind == TOK_AMP ||
               p->current.kind == TOK_KW_CONST || p->current.kind == TOK_KW_VOLATILE ||
               p->current.kind == TOK_KW_RESTRICT) {
            if (p->current.kind == TOK_STAR || p->current.kind == TOK_AMP) ptr_count++;
            advance(p);
        }
        if (p->current.kind != TOK_RPAREN) {
            *p = saved;
            return false;
        }
        if (ptr_count > 0) {
            *p = saved;
            return true;
        }

        /* Check token immediately following ')' */
        advance(p); /* skip ')' */
        TokenKind after = p->current.kind;
        *p = saved;

        if (after == TOK_DOT || after == TOK_ARROW || after == TOK_LBRACKET ||
            after == TOK_SEMICOLON || after == TOK_COMMA || after == TOK_RPAREN ||
            after == TOK_RBRACKET || after == TOK_RBRACE || after == TOK_COLON ||
            after == TOK_ASSIGN || after == TOK_PLUS_EQ || after == TOK_MINUS_EQ ||
            after == TOK_STAR_EQ || after == TOK_SLASH_EQ || after == TOK_PERCENT_EQ ||
            after == TOK_AMP_EQ || after == TOK_PIPE_EQ || after == TOK_CARET_EQ ||
            after == TOK_SHL_EQ || after == TOK_SHR_EQ ||
            after == TOK_EQ_EQ || after == TOK_EXCL_EQ || after == TOK_LESS || after == TOK_GREATER ||
            after == TOK_LESS_EQ || after == TOK_GREATER_EQ || after == TOK_LOG_AND || after == TOK_LOG_OR ||
            after == TOK_PIPE || after == TOK_CARET || after == TOK_SLASH || after == TOK_PERCENT ||
            after == TOK_SHL || after == TOK_SHR ||
            after == TOK_INC || after == TOK_DEC) {
            return false;
        }

        return parser_is_known_type(p, first_ident);
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
    bool has_void = false;
    bool has_bool = false;
    bool has_char = false;
    bool has_short = false;
    bool has_int = false;
    int long_count = 0;
    bool has_signed = false;
    bool has_unsigned = false;
    bool has_float = false;
    bool has_double = false;

    while (1) {
        if (match(p, TOK_KW_EXTENSION)) {
            continue;
        }
        if (check(p, TOK_KW_ATTRIBUTE)) {
            skip_gnu_attributes(p);
            continue;
        }
        if (match(p, TOK_KW_CONST)) {
            is_const = true;
            continue;
        }
        if (match(p, TOK_KW_VOLATILE) || match(p, TOK_KW_RESTRICT) || match(p, TOK_KW_INLINE)) {
            continue;
        }
        if (match(p, TOK_KW_SIGNED)) {
            has_signed = true;
            continue;
        }
        if (match(p, TOK_KW_UNSIGNED)) {
            has_unsigned = true;
            continue;
        }
        if (match(p, TOK_KW_SHORT)) {
            has_short = true;
            continue;
        }
        if (match(p, TOK_KW_LONG)) {
            long_count++;
            continue;
        }
        if (match(p, TOK_KW_INT)) {
            has_int = true;
            continue;
        }
        if (match(p, TOK_KW_CHAR)) {
            has_char = true;
            continue;
        }
        if (match(p, TOK_KW_VOID)) {
            has_void = true;
            continue;
        }
        if (match(p, TOK_KW_BOOL)) {
            has_bool = true;
            continue;
        }
        if (match(p, TOK_KW_FLOAT)) {
            has_float = true;
            continue;
        }
        if (match(p, TOK_KW_DOUBLE)) {
            has_double = true;
            continue;
        }
        break;
    }

    Type *base = NULL;
    if (has_void) {
        base = g_type_void;
    } else if (has_bool) {
        base = g_type_bool;
    } else if (has_float) {
        base = g_type_int;
    } else if (has_double) {
        base = g_type_long;
    } else if (has_char) {
        base = g_type_char;
    } else if (long_count > 0) {
        base = g_type_long;
    } else if (has_short || has_int || has_signed || has_unsigned) {
        base = g_type_int;
    } else if (match(p, TOK_KW_TYPEOF)) {
        if (match(p, TOK_LPAREN)) {
            int depth = 1;
            while (depth > 0 && !check(p, TOK_EOF)) {
                if (match(p, TOK_LPAREN)) depth++;
                else if (match(p, TOK_RPAREN)) depth--;
                else advance(p);
            }
        }
        base = g_type_long;
    } else if (match(p, TOK_KW_STRUCT) || match(p, TOK_KW_CLASS)) {
        skip_gnu_attributes(p);
        if (check(p, TOK_LBRACE)) {
            ASTNode *cls = parse_class_declaration(p, true);
            if (p->pending_decl_count < 32) {
                p->pending_decls[p->pending_decl_count++] = cls;
            }
            base = type_new(p->arena, TYPE_CLASS);
            base->name = cls->class_decl.name;
            base->size = 8;
            base->align = 8;
        } else if (check(p, TOK_IDENT) && p->peek.kind == TOK_LBRACE) {
            ASTNode *cls = parse_class_declaration(p, true);
            if (p->pending_decl_count < 32) {
                p->pending_decls[p->pending_decl_count++] = cls;
            }
            base = type_new(p->arena, TYPE_CLASS);
            base->name = cls->class_decl.name;
            base->size = 8;
            base->align = 8;
        } else if (check(p, TOK_IDENT)) {
            Token tag_tok = p->current;
            advance(p);
            base = type_new(p->arena, TYPE_CLASS);
            base->name = tag_tok.str_val;
            base->size = 8;
            base->align = 8;
            skip_gnu_attributes(p);
        } else {
            diag_report(DIAG_ERROR, loc, "expected struct name or body");
            base = g_type_int;
        }
    } else if (check(p, TOK_IDENT)) {
        const char *name = p->current.str_val;
        advance(p);
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

    skip_gnu_attributes(p);

    /* Pointers and References: int**, int& */
    while (check(p, TOK_STAR) || check(p, TOK_AMP)) {
        if (match(p, TOK_STAR)) {
            base = type_ptr(p->arena, base);
            while (match(p, TOK_KW_CONST) || match(p, TOK_KW_VOLATILE) ||
                   match(p, TOK_KW_RESTRICT) || check(p, TOK_KW_ATTRIBUTE)) {
                if (check(p, TOK_KW_ATTRIBUTE)) {
                    skip_gnu_attributes(p);
                }
            }
        } else if (match(p, TOK_AMP)) {
            base = type_ref(p->arena, base);
            break; /* In C++, reference cannot be further referenced directly */
        }
    }
    skip_gnu_attributes(p);

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
            ASTNode **args = NULL;
            int arg_count = 0;
            int arg_capacity = 0;

            if (!check(p, TOK_RPAREN)) {
                do {
                    append_node(p, &args, &arg_count, &arg_capacity, parse_assignment(p));
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
                ASTNode **args = NULL;
                int arg_count = 0;
                int arg_capacity = 0;
                if (!check(p, TOK_RPAREN)) {
                    do {
                        append_node(p, &args, &arg_count, &arg_capacity, parse_assignment(p));
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

    /* C-style cast: (type)expr */
    if (check(p, TOK_LPAREN)) {
        Parser saved = *p;
        advance(p); /* skip '(' */
        if (is_cast_type(p)) {
            Type *target_t = parse_type(p);
            expect(p, TOK_RPAREN, "')' in cast");
            ASTNode *cast_expr = parse_unary(p);
            ASTNode *cast_node = ast_new(p->arena, AST_CAST, loc);
            cast_node->cast.target_type = target_t;
            cast_node->cast.expr = cast_expr;
            cast_node->type = target_t;
            return cast_node;
        }
        *p = saved;
    }

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
        ASTNode **args = NULL;
        int arg_count = 0;
        int arg_capacity = 0;
        if (match(p, TOK_LPAREN)) {
            if (!check(p, TOK_RPAREN)) {
                do {
                    append_node(p, &args, &arg_count, &arg_capacity, parse_assignment(p));
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

    ASTNode **stmts = NULL;
    int count = 0;
    int capacity = 0;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        append_node(p, &stmts, &count, &capacity, parse_statement(p));
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
            parser_add_type(p, td->typedef_decl.name);
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
        parser_add_type(p, td->typedef_decl.name);
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
            ASTNode **args = NULL;
            int arg_count = 0;
            int arg_capacity = 0;
            if (!check(p, TOK_RPAREN)) {
                do {
                    append_node(p, &args, &arg_count, &arg_capacity, parse_assignment(p));
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
    ASTNode **params = NULL;
    int param_count = 0;
    int param_capacity = 0;
    bool is_varargs = false;

    if (!check(p, TOK_RPAREN)) {
        if (check(p, TOK_KW_VOID) && p->peek.kind == TOK_RPAREN) {
            advance(p); /* Consume 'void' */
            expect(p, TOK_RPAREN, "')' in parameter list");
            *out_params = params;
            if (out_varargs) *out_varargs = false;
            return 0;
        }
        do {
            if (match(p, TOK_ELLIPSIS)) {
                is_varargs = true;
                break;
            }
            skip_attributes_and_asm(p);
            Type *pt = parse_type(p);
            skip_attributes_and_asm(p);
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
            skip_attributes_and_asm(p);
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
            append_node(p, &params, &param_count, &param_capacity, param);
        } while (match(p, TOK_COMMA));
    }
    expect(p, TOK_RPAREN, "')' in parameter list");

    *out_params = params;
    if (out_varargs) *out_varargs = is_varargs;
    return param_count;
}

/* Parse top-level declarations: namespaces, classes, functions, methods */
static const char *parse_function_or_operator_name(Parser *p, SourceLoc *out_loc, bool *out_is_op) {
    skip_attributes_and_asm(p);
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
    skip_attributes_and_asm(p);
    return ident.str_val;
}

static ASTNode *parse_class_declaration(Parser *p, bool is_struct) {
    SourceLoc loc = p->current.loc;
    skip_attributes_and_asm(p);
    const char *raw_name = NULL;
    if (check(p, TOK_LBRACE)) {
        static int anon_class_counter = 0;
        char anon_buf[64];
        snprintf(anon_buf, sizeof(anon_buf), "__anon_struct_%d", ++anon_class_counter);
        raw_name = arena_strdup(p->arena, anon_buf);
    } else {
        Token name_tok = expect(p, TOK_IDENT, is_struct ? "struct name" : "class name");
        raw_name = name_tok.str_val;
        skip_attributes_and_asm(p);
    }
    const char *class_name = raw_name;
    if (p->current_namespace) {
        char full_cls[256];
        snprintf(full_cls, sizeof(full_cls), "%s::%s", p->current_namespace, raw_name);
        class_name = arena_strdup(p->arena, full_cls);
    }
    parser_add_type(p, raw_name);
    parser_add_type(p, class_name);

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

    skip_attributes_and_asm(p);
    expect(p, TOK_LBRACE, "'{' to begin class body");

    /* Default access: public for struct, private for class */
    int current_access = is_struct ? 0 : 2; /* 0: public, 2: private */

    Field *fields_head = NULL;
    Field **fields_tail = &fields_head;

    ASTNode **methods = NULL;
    int method_count = 0;
    int method_capacity = 0;

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
            parser_add_type(p, td->typedef_decl.name);
            append_node(p, &methods, &method_count, &method_capacity, td);
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
            parser_add_type(p, td->typedef_decl.name);
            append_node(p, &methods, &method_count, &method_capacity, td);
            continue;
        }

        if (match(p, TOK_SEMICOLON)) {
            continue;
        }

        /* Check for constructor: ClassName(...) */
        if (check(p, TOK_IDENT) && raw_name && raw_name[0] != '\0' &&
            (strcmp(p->current.str_val, raw_name) == 0 || strcmp(p->current.str_val, class_name) == 0) &&
            p->peek.kind == TOK_LPAREN) {
            SourceLoc ctor_loc = p->current.loc;
            advance(p); /* Skip class name */
            expect(p, TOK_LPAREN, "'(' in constructor");

            ASTNode **params = NULL;
            bool is_va = false;
            int param_count = parse_param_list(p, &params, &is_va);

            /* Constructor initializer list: : head(h), tail(t...) */
            ASTNode **init_stmts = NULL;
            int init_count = 0;
            int init_capacity = 0;
            if (match(p, TOK_COLON)) {
                do {
                    Token mem_tok = expect(p, TOK_IDENT, "member name in constructor initializer");
                    expect(p, TOK_LPAREN, "'(' in member initializer");
                    ASTNode **m_args = NULL;
                    int m_arg_count = 0;
                    int m_arg_capacity = 0;
                    if (!check(p, TOK_RPAREN)) {
                        do {
                            append_node(p, &m_args, &m_arg_count, &m_arg_capacity, parse_assignment(p));
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
                        append_node(p, &init_stmts, &init_count, &init_capacity, stmt);
                    } else {
                        ASTNode *mcall = ast_new(p->arena, AST_CALL, mem_tok.loc);
                        mcall->call.is_method = true;
                        mcall->call.object = memb;
                        mcall->call.name = mem_tok.str_val;
                        mcall->call.args = m_args;
                        mcall->call.arg_count = m_arg_count;

                        ASTNode *stmt = ast_new(p->arena, AST_STMT_EXPR, mem_tok.loc);
                        stmt->stmt_expr.expr = mcall;
                        append_node(p, &init_stmts, &init_count, &init_capacity, stmt);
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
            append_node(p, &methods, &method_count, &method_capacity, fn);
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
            append_node(p, &methods, &method_count, &method_capacity, fn);
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
            append_node(p, &methods, &method_count, &method_capacity, fn);
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
    skip_attributes_and_asm(p);

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

    ASTNode **decls = NULL;
    int count = 0;
    int capacity = 0;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        append_node(p, &decls, &count, &capacity, parse_declaration(p));
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
    if (p->pending_decl_count > 0) {
        return p->pending_decls[--p->pending_decl_count];
    }

    while (match(p, TOK_SEMICOLON) || match(p, TOK_KW_EXTENSION) || check(p, TOK_KW_ATTRIBUTE)) {
        if (check(p, TOK_KW_ATTRIBUTE)) {
            skip_gnu_attributes(p);
        }
    }
    if (check(p, TOK_EOF)) return NULL;

    /* extern "C" { ... } or extern "C" <decl> */
    if (check(p, TOK_KW_EXTERN) && p->peek.kind == TOK_STR_LIT) {
        advance(p); /* Consume 'extern' */
        Token str_tok = p->current;
        advance(p); /* Consume string literal "C" */
        if (match(p, TOK_LBRACE)) {
            ASTNode **decls = NULL;
            int count = 0;
            int capacity = 0;
            while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
                ASTNode *d = parse_declaration(p);
                if (d) {
                    append_node(p, &decls, &count, &capacity, d);
                }
            }
            expect(p, TOK_RBRACE, "'}' to close extern block");
            ASTNode *ns = ast_new(p->arena, AST_DECL_NAMESPACE, str_tok.loc);
            ns->ns_decl.name = NULL;
            ns->ns_decl.decls = decls;
            ns->ns_decl.count = count;
            return ns;
        } else {
            ASTNode *d = parse_declaration(p);
            if (d && d->kind == AST_DECL_FUNC) {
                d->func_decl.is_extern = true;
            }
            return d;
        }
    }

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
            parser_add_type(p, td->typedef_decl.name);
            return td;
        }
    }

    if (match(p, TOK_KW_TYPEDEF)) {
        SourceLoc loc = p->current.loc;
        skip_attributes_and_asm(p);
        if (check(p, TOK_KW_STRUCT) || check(p, TOK_KW_CLASS)) {
            bool is_struct = (p->current.kind == TOK_KW_STRUCT);
            advance(p); /* Consume 'struct' or 'class' */
            skip_attributes_and_asm(p);
            if (check(p, TOK_LBRACE) || (check(p, TOK_IDENT) && p->peek.kind == TOK_LBRACE)) {
                ASTNode *cls = parse_class_declaration(p, is_struct);
                Type *aliased_type = type_new(p->arena, TYPE_CLASS);
                aliased_type->name = cls->class_decl.name;
                aliased_type->size = 8;
                aliased_type->align = 8;
                while (match(p, TOK_STAR)) {
                    aliased_type = type_ptr(p->arena, aliased_type);
                }
                skip_attributes_and_asm(p);
                Token name_tok = expect(p, TOK_IDENT, "name in typedef");
                skip_attributes_and_asm(p);
                expect(p, TOK_SEMICOLON, "';' after typedef");
                ASTNode *td = ast_new(p->arena, AST_DECL_TYPEDEF, loc);
                td->typedef_decl.name = name_tok.str_val;
                td->typedef_decl.aliased_type = aliased_type;
                parser_add_type(p, td->typedef_decl.name);
                if (p->pending_decl_count < 32) {
                    p->pending_decls[p->pending_decl_count++] = td;
                }
                return cls;
            } else if (check(p, TOK_IDENT)) {
                Token tag_tok = p->current;
                advance(p);
                Type *aliased_type = type_new(p->arena, TYPE_CLASS);
                aliased_type->name = tag_tok.str_val;
                aliased_type->size = 8;
                aliased_type->align = 8;
                while (match(p, TOK_STAR)) {
                    aliased_type = type_ptr(p->arena, aliased_type);
                }
                skip_attributes_and_asm(p);
                Token name_tok = expect(p, TOK_IDENT, "name in typedef");
                skip_attributes_and_asm(p);
                expect(p, TOK_SEMICOLON, "';' after typedef");
                ASTNode *td = ast_new(p->arena, AST_DECL_TYPEDEF, loc);
                td->typedef_decl.name = name_tok.str_val;
                td->typedef_decl.aliased_type = aliased_type;
                parser_add_type(p, td->typedef_decl.name);
                return td;
            }
        }
        Type *aliased_type = parse_type(p);
        const char *name = NULL;
        Type *fn_ptr_t = NULL;
        if (try_parse_fn_ptr_declarator(p, aliased_type, &name, &fn_ptr_t) ||
            try_parse_member_ptr_declarator(p, aliased_type, &name, &fn_ptr_t)) {
            aliased_type = fn_ptr_t;
        } else {
            skip_attributes_and_asm(p);
            Token name_tok = expect(p, TOK_IDENT, "name in typedef");
            name = name_tok.str_val;
            skip_attributes_and_asm(p);
        }
        expect(p, TOK_SEMICOLON, "';' after typedef");
        ASTNode *td = ast_new(p->arena, AST_DECL_TYPEDEF, loc);
        td->typedef_decl.name = name;
        td->typedef_decl.aliased_type = aliased_type;
        parser_add_type(p, td->typedef_decl.name);
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
                parser_add_type(p, ptok.str_val);
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
        ASTNode *cls = parse_class_declaration(p, false);
        match(p, TOK_SEMICOLON);
        return cls;
    }

    if (match(p, TOK_KW_STRUCT)) {
        ASTNode *cls = parse_class_declaration(p, true);
        match(p, TOK_SEMICOLON);
        return cls;
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
    while (match(p, TOK_KW_EXTERN) || match(p, TOK_KW_STATIC) || match(p, TOK_KW_INLINE) || match(p, TOK_KW_EXTENSION)) {
        is_extern = true;
    }
    skip_attributes_and_asm(p);

    SourceLoc loc = p->current.loc;
    Type *ret_type = parse_type(p);
    skip_attributes_and_asm(p);

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
    skip_attributes_and_asm(p);
    const char *fn_name = parse_function_or_operator_name(p, &name_loc, &is_op);
    skip_attributes_and_asm(p);

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
        skip_attributes_and_asm(p);

        ASTNode *body = NULL;
        if (check(p, TOK_LBRACE)) {
            body = parse_block(p);
        } else {
            expect(p, TOK_SEMICOLON, "';' or function body");
        }
        skip_attributes_and_asm(p);

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
    ASTNode **decls = NULL;
    int count = 0;
    int capacity = 0;

    while (!check(p, TOK_EOF)) {
        ASTNode *decl = parse_declaration(p);
        if (decl) {
            append_node(p, &decls, &count, &capacity, decl);
        }
    }

    ASTNode *prog = ast_new(p->arena, AST_PROGRAM, loc);
    prog->program.decls = decls;
    prog->program.count = count;
    prog->program.capacity = capacity;
    return prog;
}
