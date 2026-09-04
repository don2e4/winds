#include "lexer.h"
#include "str.h"

void lexer_init(Lexer *l, const char *source, const char *filename) {
    l->source = source;
    l->current = source;
    l->line_start = source;
    l->filename = filename ? filename : "<stdin>";
    l->line = 1;
    l->col = 1;
}

static SourceLoc current_loc(Lexer *l) {
    SourceLoc loc;
    loc.file = l->filename;
    loc.line = l->line;
    loc.col = (int)(l->current - l->line_start) + 1;
    loc.source_line = l->line_start;
    return loc;
}

static char peek_char(Lexer *l) {
    return *l->current;
}

static char peek_next_char(Lexer *l) {
    if (*l->current == '\0') return '\0';
    return *(l->current + 1);
}

static char advance_char(Lexer *l) {
    char c = *l->current;
    if (c != '\0') {
        l->current++;
        l->col++;
    }
    return c;
}

static void skip_whitespace_and_comments(Lexer *l) {
    while (*l->current) {
        char c = *l->current;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f') {
            advance_char(l);
        } else if (c == '\n') {
            advance_char(l);
            l->line++;
            l->col = 1;
            l->line_start = l->current;
        } else if (c == '/' && peek_next_char(l) == '/') {
            /* Single line comment */
            while (*l->current && *l->current != '\n') {
                advance_char(l);
            }
        } else if (c == '/' && peek_next_char(l) == '*') {
            /* Block comment */
            advance_char(l);
            advance_char(l);
            while (*l->current) {
                if (*l->current == '*' && peek_next_char(l) == '/') {
                    advance_char(l);
                    advance_char(l);
                    break;
                }
                if (*l->current == '\n') {
                    l->line++;
                    l->col = 1;
                    advance_char(l);
                    l->line_start = l->current;
                } else {
                    advance_char(l);
                }
            }
        } else if (c == '#' && (l->current == l->line_start || *(l->current - 1) == '\n')) {
            /* Preprocessor line - skip until end of line */
            while (*l->current && *l->current != '\n') {
                advance_char(l);
            }
        } else {
            break;
        }
    }
}

static Token make_token(Lexer *l, TokenKind kind, SourceLoc loc) {
    Token tok;
    tok.kind = kind;
    tok.loc = loc;
    tok.int_val = 0;
    tok.str_val = NULL;
    return tok;
}

static Token scan_identifier_or_keyword(Lexer *l, SourceLoc loc) {
    const char *start = l->current;
    while (isalnum(*l->current) || *l->current == '_') {
        advance_char(l);
    }
    const char *name = str_intern_range(start, l->current);
    Token tok = make_token(l, TOK_IDENT, loc);
    tok.str_val = name;

    /* Check keywords */
    if (strcmp(name, "class") == 0) tok.kind = TOK_KW_CLASS;
    else if (strcmp(name, "struct") == 0) tok.kind = TOK_KW_STRUCT;
    else if (strcmp(name, "public") == 0) tok.kind = TOK_KW_PUBLIC;
    else if (strcmp(name, "private") == 0) tok.kind = TOK_KW_PRIVATE;
    else if (strcmp(name, "protected") == 0) tok.kind = TOK_KW_PROTECTED;
    else if (strcmp(name, "namespace") == 0) tok.kind = TOK_KW_NAMESPACE;
    else if (strcmp(name, "using") == 0) tok.kind = TOK_KW_USING;
    else if (strcmp(name, "new") == 0) tok.kind = TOK_KW_NEW;
    else if (strcmp(name, "delete") == 0) tok.kind = TOK_KW_DELETE;
    else if (strcmp(name, "this") == 0) tok.kind = TOK_KW_THIS;
    else if (strcmp(name, "return") == 0) tok.kind = TOK_KW_RETURN;
    else if (strcmp(name, "if") == 0) tok.kind = TOK_KW_IF;
    else if (strcmp(name, "else") == 0) tok.kind = TOK_KW_ELSE;
    else if (strcmp(name, "while") == 0) tok.kind = TOK_KW_WHILE;
    else if (strcmp(name, "for") == 0) tok.kind = TOK_KW_FOR;
    else if (strcmp(name, "do") == 0) tok.kind = TOK_KW_DO;
    else if (strcmp(name, "break") == 0) tok.kind = TOK_KW_BREAK;
    else if (strcmp(name, "continue") == 0) tok.kind = TOK_KW_CONTINUE;
    else if (strcmp(name, "sizeof") == 0) tok.kind = TOK_KW_SIZEOF;
    else if (strcmp(name, "void") == 0) tok.kind = TOK_KW_VOID;
    else if (strcmp(name, "bool") == 0) tok.kind = TOK_KW_BOOL;
    else if (strcmp(name, "char") == 0) tok.kind = TOK_KW_CHAR;
    else if (strcmp(name, "short") == 0) tok.kind = TOK_KW_SHORT;
    else if (strcmp(name, "int") == 0) tok.kind = TOK_KW_INT;
    else if (strcmp(name, "long") == 0) tok.kind = TOK_KW_LONG;
    else if (strcmp(name, "float") == 0) tok.kind = TOK_KW_FLOAT;
    else if (strcmp(name, "double") == 0) tok.kind = TOK_KW_DOUBLE;
    else if (strcmp(name, "const") == 0) tok.kind = TOK_KW_CONST;
    else if (strcmp(name, "static") == 0) tok.kind = TOK_KW_STATIC;
    else if (strcmp(name, "extern") == 0) tok.kind = TOK_KW_EXTERN;
    else if (strcmp(name, "inline") == 0) tok.kind = TOK_KW_INLINE;
    else if (strcmp(name, "true") == 0) {
        tok.kind = TOK_KW_TRUE;
        tok.int_val = 1;
    } else if (strcmp(name, "false") == 0) {
        tok.kind = TOK_KW_FALSE;
        tok.int_val = 0;
    } else if (strcmp(name, "nullptr") == 0 || strcmp(name, "NULL") == 0) {
        tok.kind = TOK_KW_NULLPTR;
        tok.int_val = 0;
    }

    return tok;
}

static Token scan_number(Lexer *l, SourceLoc loc) {
    const char *start = l->current;
    int base = 10;
    if (*l->current == '0' && (peek_next_char(l) == 'x' || peek_next_char(l) == 'X')) {
        advance_char(l);
        advance_char(l);
        base = 16;
        while (isxdigit(*l->current)) {
            advance_char(l);
        }
    } else {
        while (isdigit(*l->current)) {
            advance_char(l);
        }
    }

    int64_t val = (int64_t)strtoll(start, NULL, base);
    Token tok = make_token(l, TOK_INT_LIT, loc);
    tok.int_val = val;
    return tok;
}

static char parse_escape_sequence(Lexer *l) {
    advance_char(l); /* Skip '\\' */
    char c = advance_char(l);
    switch (c) {
        case 'n': return '\n';
        case 't': return '\t';
        case 'r': return '\r';
        case '0': return '\0';
        case '\\': return '\\';
        case '\'': return '\'';
        case '\"': return '\"';
        default: return c;
    }
}

static Token scan_char_literal(Lexer *l, SourceLoc loc) {
    advance_char(l); /* Skip opening '\'' */
    char c = 0;
    if (*l->current == '\\') {
        c = parse_escape_sequence(l);
    } else if (*l->current) {
        c = advance_char(l);
    }
    if (*l->current == '\'') {
        advance_char(l);
    } else {
        diag_report(DIAG_ERROR, loc, "missing terminating single quote in character literal");
    }

    Token tok = make_token(l, TOK_CHAR_LIT, loc);
    tok.int_val = (int64_t)c;
    return tok;
}

static Token scan_string_literal(Lexer *l, SourceLoc loc) {
    advance_char(l); /* Skip opening '\"' */
    char buffer[4096];
    size_t len = 0;

    while (*l->current && *l->current != '\"') {
        char c;
        if (*l->current == '\\') {
            c = parse_escape_sequence(l);
        } else {
            c = advance_char(l);
        }
        if (len < sizeof(buffer) - 1) {
            buffer[len++] = c;
        }
    }

    if (*l->current == '\"') {
        advance_char(l);
    } else {
        diag_report(DIAG_ERROR, loc, "missing terminating double quote in string literal");
    }

    buffer[len] = '\0';
    Token tok = make_token(l, TOK_STR_LIT, loc);
    tok.str_val = str_intern_range(buffer, buffer + len);
    tok.int_val = (int64_t)len;
    return tok;
}

Token lexer_next(Lexer *l) {
    skip_whitespace_and_comments(l);

    SourceLoc loc = current_loc(l);
    char c = peek_char(l);

    if (c == '\0') {
        return make_token(l, TOK_EOF, loc);
    }

    if (isalpha(c) || c == '_') {
        return scan_identifier_or_keyword(l, loc);
    }

    if (isdigit(c)) {
        return scan_number(l, loc);
    }

    if (c == '\'') {
        return scan_char_literal(l, loc);
    }

    if (c == '\"') {
        return scan_string_literal(l, loc);
    }

    advance_char(l);
    char next = peek_char(l);

    switch (c) {
        case ':':
            if (next == ':') { advance_char(l); return make_token(l, TOK_COLON_COLON, loc); }
            return make_token(l, TOK_COLON, loc);
        case '-':
            if (next == '>') { advance_char(l); return make_token(l, TOK_ARROW, loc); }
            if (next == '-') { advance_char(l); return make_token(l, TOK_DEC, loc); }
            if (next == '=') { advance_char(l); return make_token(l, TOK_MINUS_EQ, loc); }
            return make_token(l, TOK_MINUS, loc);
        case '+':
            if (next == '+') { advance_char(l); return make_token(l, TOK_INC, loc); }
            if (next == '=') { advance_char(l); return make_token(l, TOK_PLUS_EQ, loc); }
            return make_token(l, TOK_PLUS, loc);
        case '*':
            if (next == '=') { advance_char(l); return make_token(l, TOK_STAR_EQ, loc); }
            return make_token(l, TOK_STAR, loc);
        case '/':
            if (next == '=') { advance_char(l); return make_token(l, TOK_SLASH_EQ, loc); }
            return make_token(l, TOK_SLASH, loc);
        case '%':
            if (next == '=') { advance_char(l); return make_token(l, TOK_PERCENT_EQ, loc); }
            return make_token(l, TOK_PERCENT, loc);
        case '=':
            if (next == '=') { advance_char(l); return make_token(l, TOK_EQ_EQ, loc); }
            return make_token(l, TOK_ASSIGN, loc);
        case '!':
            if (next == '=') { advance_char(l); return make_token(l, TOK_EXCL_EQ, loc); }
            return make_token(l, TOK_EXCL, loc);
        case '<':
            if (next == '=') { advance_char(l); return make_token(l, TOK_LESS_EQ, loc); }
            if (next == '<') {
                advance_char(l);
                if (peek_char(l) == '=') { advance_char(l); return make_token(l, TOK_SHL_EQ, loc); }
                return make_token(l, TOK_SHL, loc);
            }
            return make_token(l, TOK_LESS, loc);
        case '>':
            if (next == '=') { advance_char(l); return make_token(l, TOK_GREATER_EQ, loc); }
            if (next == '>') {
                advance_char(l);
                if (peek_char(l) == '=') { advance_char(l); return make_token(l, TOK_SHR_EQ, loc); }
                return make_token(l, TOK_SHR, loc);
            }
            return make_token(l, TOK_GREATER, loc);
        case '&':
            if (next == '&') { advance_char(l); return make_token(l, TOK_LOG_AND, loc); }
            if (next == '=') { advance_char(l); return make_token(l, TOK_AMP_EQ, loc); }
            return make_token(l, TOK_AMP, loc);
        case '|':
            if (next == '|') { advance_char(l); return make_token(l, TOK_LOG_OR, loc); }
            if (next == '=') { advance_char(l); return make_token(l, TOK_PIPE_EQ, loc); }
            return make_token(l, TOK_PIPE, loc);
        case '.':
            if (next == '.' && peek_next_char(l) == '.') {
                advance_char(l);
                advance_char(l);
                return make_token(l, TOK_ELLIPSIS, loc);
            }
            return make_token(l, TOK_DOT, loc);
        case '^':
            if (next == '=') { advance_char(l); return make_token(l, TOK_CARET_EQ, loc); }
            return make_token(l, TOK_CARET, loc);
        default:
            /* Single character token like ;, ,, (, ), {, }, [, ], etc. */
            return make_token(l, (TokenKind)c, loc);
    }
}

Token lexer_peek(Lexer *l) {
    Lexer copy = *l;
    return lexer_next(&copy);
}

const char *token_kind_str(TokenKind kind) {
    switch (kind) {
        case TOK_EOF: return "EOF";
        case TOK_IDENT: return "identifier";
        case TOK_INT_LIT: return "integer literal";
        case TOK_CHAR_LIT: return "character literal";
        case TOK_STR_LIT: return "string literal";
        case TOK_KW_CLASS: return "class";
        case TOK_KW_STRUCT: return "struct";
        case TOK_KW_PUBLIC: return "public";
        case TOK_KW_PRIVATE: return "private";
        case TOK_KW_PROTECTED: return "protected";
        case TOK_KW_NAMESPACE: return "namespace";
        case TOK_KW_USING: return "using";
        case TOK_KW_NEW: return "new";
        case TOK_KW_DELETE: return "delete";
        case TOK_KW_THIS: return "this";
        case TOK_KW_RETURN: return "return";
        case TOK_KW_IF: return "if";
        case TOK_KW_ELSE: return "else";
        case TOK_KW_WHILE: return "while";
        case TOK_KW_FOR: return "for";
        case TOK_KW_DO: return "do";
        case TOK_KW_BREAK: return "break";
        case TOK_KW_CONTINUE: return "continue";
        case TOK_KW_SIZEOF: return "sizeof";
        case TOK_KW_VOID: return "void";
        case TOK_KW_BOOL: return "bool";
        case TOK_KW_CHAR: return "char";
        case TOK_KW_SHORT: return "short";
        case TOK_KW_INT: return "int";
        case TOK_KW_LONG: return "long";
        case TOK_KW_FLOAT: return "float";
        case TOK_KW_DOUBLE: return "double";
        case TOK_KW_CONST: return "const";
        case TOK_KW_STATIC: return "static";
        case TOK_KW_EXTERN: return "extern";
        case TOK_KW_INLINE: return "inline";
        case TOK_KW_TRUE: return "true";
        case TOK_KW_FALSE: return "false";
        case TOK_KW_NULLPTR: return "nullptr";
        case TOK_COLON_COLON: return "::";
        case TOK_ARROW: return "->";
        case TOK_ELLIPSIS: return "...";
        case TOK_INC: return "++";
        case TOK_DEC: return "--";
        case TOK_EQ_EQ: return "==";
        case TOK_EXCL_EQ: return "!=";
        case TOK_LESS_EQ: return "<=";
        case TOK_GREATER_EQ: return ">=";
        case TOK_LOG_AND: return "&&";
        case TOK_LOG_OR: return "||";
        case TOK_SHL: return "<<";
        case TOK_SHR: return ">>";
        case TOK_PLUS_EQ: return "+=";
        case TOK_MINUS_EQ: return "-=";
        case TOK_STAR_EQ: return "*=";
        case TOK_SLASH_EQ: return "/=";
        case TOK_PERCENT_EQ: return "%=";
        case TOK_AMP_EQ: return "&=";
        case TOK_PIPE_EQ: return "|=";
        case TOK_CARET_EQ: return "^=";
        case TOK_SHL_EQ: return "<<=";
        case TOK_SHR_EQ: return ">>=";
        default: {
            static char buf[2];
            buf[0] = (char)kind;
            buf[1] = '\0';
            return buf;
        }
    }
}
