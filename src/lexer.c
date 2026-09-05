#include "lexer.h"
#include "str.h"
#include <unistd.h>

void lexer_init(Lexer *l, const char *source, const char *filename) {
    memset(l, 0, sizeof(Lexer));
    l->depth = 0;
    const char *curr = source;
    const char *lstart = source;
    int line = 1;

    /* Skip shebang (#!) line if present on line 1 (e.g. for -run scripts) */
    if (source && source[0] == '#' && source[1] == '!') {
        const char *p = source + 2;
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
        curr = p;
        lstart = p;
        line = 2;
    }

    l->buffers[0].source = source;
    l->buffers[0].current = curr;
    l->buffers[0].line_start = lstart;
    l->buffers[0].filename = filename ? filename : "<stdin>";
    l->buffers[0].line = line;
    l->buffers[0].col = 1;
    l->buffers[0].allocated_source = NULL;
}

void lexer_add_include_path(Lexer *l, const char *path) {
    if (!path || l->include_path_count >= MAX_INCLUDE_PATHS) return;
    l->include_paths[l->include_path_count++] = path;
}

int lexer_get_included_files(Lexer *l, const char ***out_files) {
    if (out_files) *out_files = l->included_files;
    return l->included_file_count;
}

static SourceLoc current_loc(Lexer *l) {
    LexerBuffer *b = &l->buffers[l->depth];
    SourceLoc loc;
    loc.file = b->filename;
    loc.line = b->line;
    loc.col = (int)(b->current - b->line_start) + 1;
    loc.source_line = b->line_start;
    loc.length = 1;
    return loc;
}

static char peek_char(Lexer *l) {
    return *l->buffers[l->depth].current;
}

static char peek_next_char(Lexer *l) {
    if (*l->buffers[l->depth].current == '\0') return '\0';
    return *(l->buffers[l->depth].current + 1);
}

static char advance_char(Lexer *l) {
    LexerBuffer *b = &l->buffers[l->depth];
    char c = *b->current;
    if (c != '\0') {
        b->current++;
        b->col++;
    }
    return c;
}

static void handle_preprocessor(Lexer *l) {
    /* Skip '#' */
    advance_char(l);
    while (peek_char(l) == ' ' || peek_char(l) == '\t') {
        advance_char(l);
    }

    char dir[64];
    int dlen = 0;
    while (isalpha((unsigned char)peek_char(l)) && dlen < (int)sizeof(dir) - 1) {
        dir[dlen++] = advance_char(l);
    }
    dir[dlen] = '\0';

    while (peek_char(l) == ' ' || peek_char(l) == '\t') {
        advance_char(l);
    }

    if (strcmp(dir, "ifndef") == 0) {
        l->cond_depth++;
        char macro[128];
        int mlen = 0;
        while ((isalnum((unsigned char)peek_char(l)) || peek_char(l) == '_') && mlen < (int)sizeof(macro) - 1) {
            macro[mlen++] = advance_char(l);
        }
        macro[mlen] = '\0';

        if (l->skip_depth == 0) {
            bool is_def = false;
            for (int i = 0; i < l->defined_macro_count; i++) {
                if (strcmp(l->defined_macros[i], macro) == 0) {
                    is_def = true;
                    break;
                }
            }
            if (is_def) {
                l->skip_depth = l->cond_depth;
            }
        }
    } else if (strcmp(dir, "ifdef") == 0) {
        l->cond_depth++;
        char macro[128];
        int mlen = 0;
        while ((isalnum((unsigned char)peek_char(l)) || peek_char(l) == '_') && mlen < (int)sizeof(macro) - 1) {
            macro[mlen++] = advance_char(l);
        }
        macro[mlen] = '\0';

        if (l->skip_depth == 0) {
            bool is_def = false;
            for (int i = 0; i < l->defined_macro_count; i++) {
                if (strcmp(l->defined_macros[i], macro) == 0) {
                    is_def = true;
                    break;
                }
            }
            if (!is_def) {
                l->skip_depth = l->cond_depth;
            }
        }
    } else if (strcmp(dir, "else") == 0) {
        if (l->skip_depth == l->cond_depth) {
            l->skip_depth = 0;
        } else if (l->skip_depth == 0) {
            l->skip_depth = l->cond_depth;
        }
    } else if (strcmp(dir, "endif") == 0) {
        if (l->skip_depth == l->cond_depth) {
            l->skip_depth = 0;
        }
        if (l->cond_depth > 0) {
            l->cond_depth--;
        }
    } else if (l->skip_depth > 0) {
        /* In skipping branch, ignore define / pragma / include */
    } else if (strcmp(dir, "define") == 0) {
        char macro[128];
        int mlen = 0;
        while ((isalnum((unsigned char)peek_char(l)) || peek_char(l) == '_') && mlen < (int)sizeof(macro) - 1) {
            macro[mlen++] = advance_char(l);
        }
        macro[mlen] = '\0';
        if (mlen > 0 && l->defined_macro_count < MAX_DEFINED_MACROS) {
            l->defined_macros[l->defined_macro_count++] = str_intern(macro);
        }
    } else if (strcmp(dir, "pragma") == 0) {
        char word[64];
        int wlen = 0;
        while (isalpha((unsigned char)peek_char(l)) && wlen < (int)sizeof(word) - 1) {
            word[wlen++] = advance_char(l);
        }
        word[wlen] = '\0';
        if (strcmp(word, "once") == 0) {
            const char *curr_file = l->buffers[l->depth].filename;
            if (curr_file && l->pragma_once_count < MAX_PRAGMA_ONCE) {
                l->pragma_once_files[l->pragma_once_count++] = curr_file;
            }
        }
    } else if (strcmp(dir, "include") == 0) {
        SourceLoc loc = current_loc(l);
        char quote = peek_char(l);
        if (quote != '"' && quote != '<') {
            diag_report(DIAG_ERROR, loc, "expected '\"' or '<' after #include");
            diag_help("example: #include \"my_header.h\" or #include <vector>");
        } else {
            advance_char(l);
            char end_quote = (quote == '"') ? '"' : '>';
            char header_name[256];
            int hlen = 0;
            while (peek_char(l) != '\0' && peek_char(l) != end_quote && peek_char(l) != '\n' && hlen < (int)sizeof(header_name) - 1) {
                header_name[hlen++] = advance_char(l);
            }
            header_name[hlen] = '\0';
            if (peek_char(l) == end_quote) {
                advance_char(l);
            }

            char resolved_path[2048];
            bool found = false;

            /* 1. If quoted include, search directory of current file first */
            if (quote == '"') {
                const char *curr_file = l->buffers[l->depth].filename;
                char dir[1024];
                const char *last_slash = strrchr(curr_file, '/');
                if (last_slash) {
                    size_t dirlen = last_slash - curr_file;
                    if (dirlen >= sizeof(dir)) dirlen = sizeof(dir) - 1;
                    strncpy(dir, curr_file, dirlen);
                    dir[dirlen] = '\0';
                    snprintf(resolved_path, sizeof(resolved_path), "%s/%s", dir, header_name);
                } else {
                    snprintf(resolved_path, sizeof(resolved_path), "%s", header_name);
                }

                FILE *tf = fopen(resolved_path, "rb");
                if (tf) {
                    fclose(tf);
                    found = true;
                } else {
                    /* Also check relative to current working directory */
                    tf = fopen(header_name, "rb");
                    if (tf) {
                        fclose(tf);
                        snprintf(resolved_path, sizeof(resolved_path), "%s", header_name);
                        found = true;
                    }
                }
            }

            /* 2. Search configured include paths */
            if (!found) {
                for (int i = 0; i < l->include_path_count; i++) {
                    snprintf(resolved_path, sizeof(resolved_path), "%s/%s", l->include_paths[i], header_name);
                    FILE *tf = fopen(resolved_path, "rb");
                    if (tf) {
                        fclose(tf);
                        found = true;
                        break;
                    }
                }
            }

            /* 3. Search default search paths */
            if (!found) {
                const char *defaults[] = { "./include", "include", "/usr/include", "/usr/local/include" };
                for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
                    snprintf(resolved_path, sizeof(resolved_path), "%s/%s", defaults[i], header_name);
                    FILE *tf = fopen(resolved_path, "rb");
                    if (tf) {
                        fclose(tf);
                        found = true;
                        break;
                    }
                }
            }

            if (!found) {
                diag_report(DIAG_ERROR, loc, "cannot find header file '%s'", header_name);
                diag_help("verify that the file exists and is readable, or specify the search directory using '-I <path>'");
            } else {
                char canonical[2048];
                if (realpath(resolved_path, canonical) == NULL) {
                    strncpy(canonical, resolved_path, sizeof(canonical) - 1);
                    canonical[sizeof(canonical) - 1] = '\0';
                }

                /* Record included file for dependency tracking (-MMD) */
                bool already_tracked = false;
                for (int k = 0; k < l->included_file_count; k++) {
                    if (strcmp(l->included_files[k], canonical) == 0) {
                        already_tracked = true;
                        break;
                    }
                }
                if (!already_tracked && l->included_file_count < MAX_INCLUDED_FILES) {
                    l->included_files[l->included_file_count++] = str_intern(canonical);
                }

                bool already_included = false;
                for (int i = 0; i < l->pragma_once_count; i++) {
                    if (strcmp(l->pragma_once_files[i], canonical) == 0) {
                        already_included = true;
                        break;
                    }
                }

                if (!already_included) {
                    if (l->depth + 1 >= MAX_INCLUDE_DEPTH) {
                        diag_report(DIAG_FATAL, loc, "maximum include depth exceeded (%d) while including '%s'", MAX_INCLUDE_DEPTH, header_name);
                    }

                    FILE *hf = fopen(canonical, "rb");
                    if (hf) {
                        fseek(hf, 0, SEEK_END);
                        long size = ftell(hf);
                        fseek(hf, 0, SEEK_SET);

                        if (size >= 0) {
                            char *hbuf = malloc(size + 1);
                            if (hbuf) {
                                size_t r = fread(hbuf, 1, size, hf);
                                hbuf[r] = '\0';
                                fclose(hf);

                                /* Advance past the rest of the #include line in current buffer */
                                while (peek_char(l) != '\0' && peek_char(l) != '\n') {
                                    advance_char(l);
                                }
                                if (peek_char(l) == '\n') {
                                    advance_char(l);
                                    l->buffers[l->depth].line++;
                                    l->buffers[l->depth].col = 1;
                                    l->buffers[l->depth].line_start = l->buffers[l->depth].current;
                                }

                                /* Push new buffer */
                                l->depth++;
                                LexerBuffer *nb = &l->buffers[l->depth];
                                nb->source = hbuf;
                                nb->current = hbuf;
                                nb->line_start = hbuf;
                                nb->filename = str_intern(canonical);
                                nb->line = 1;
                                nb->col = 1;
                                nb->allocated_source = hbuf;
                                return;
                            }
                        }
                        fclose(hf);
                    }
                }
            }
        }
    }

    /* Skip rest of preprocessor line */
    while (peek_char(l) != '\0' && peek_char(l) != '\n') {
        advance_char(l);
    }
}

static void skip_whitespace_and_comments(Lexer *l) {
    while (1) {
        char c = peek_char(l);

        if (c == '\0') {
            if (l->depth > 0) {
                l->depth--;
                continue;
            }
            break;
        }

        if (c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f') {
            advance_char(l);
        } else if (c == '\n') {
            advance_char(l);
            l->buffers[l->depth].line++;
            l->buffers[l->depth].col = 1;
            l->buffers[l->depth].line_start = l->buffers[l->depth].current;
        } else if (c == '/' && peek_next_char(l) == '/') {
            while (peek_char(l) != '\0' && peek_char(l) != '\n') {
                advance_char(l);
            }
        } else if (c == '/' && peek_next_char(l) == '*') {
            advance_char(l);
            advance_char(l);
            while (peek_char(l) != '\0') {
                if (peek_char(l) == '*' && peek_next_char(l) == '/') {
                    advance_char(l);
                    advance_char(l);
                    break;
                }
                if (peek_char(l) == '\n') {
                    l->buffers[l->depth].line++;
                    l->buffers[l->depth].col = 1;
                    advance_char(l);
                    l->buffers[l->depth].line_start = l->buffers[l->depth].current;
                } else {
                    advance_char(l);
                }
            }
        } else if (c == '#' && (l->buffers[l->depth].current == l->buffers[l->depth].line_start || *(l->buffers[l->depth].current - 1) == '\n')) {
            handle_preprocessor(l);
        } else if (l->skip_depth > 0) {
            /* If we are skipping conditional compilation block, consume characters until '#' or '\n' */
            advance_char(l);
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
    const char *start = l->buffers[l->depth].current;
    while (isalnum((unsigned char)peek_char(l)) || peek_char(l) == '_') {
        advance_char(l);
    }
    const char *name = str_intern_range(start, l->buffers[l->depth].current);
    loc.length = (int)(l->buffers[l->depth].current - start);

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
    else if (strcmp(name, "true") == 0) tok.kind = TOK_KW_TRUE;
    else if (strcmp(name, "false") == 0) tok.kind = TOK_KW_FALSE;
    else if (strcmp(name, "nullptr") == 0) tok.kind = TOK_KW_NULLPTR;

    return tok;
}

static Token scan_number(Lexer *l, SourceLoc loc) {
    const char *start = l->buffers[l->depth].current;
    int64_t val = 0;

    if (peek_char(l) == '0' && (peek_next_char(l) == 'x' || peek_next_char(l) == 'X')) {
        advance_char(l);
        advance_char(l);
        while (isxdigit((unsigned char)peek_char(l))) {
            char c = advance_char(l);
            val = val * 16 + (isdigit((unsigned char)c) ? c - '0' : tolower((unsigned char)c) - 'a' + 10);
        }
    } else {
        while (isdigit((unsigned char)peek_char(l))) {
            val = val * 10 + (advance_char(l) - '0');
        }
    }

    /* Consume suffix like L, U, LL */
    while (peek_char(l) == 'u' || peek_char(l) == 'U' || peek_char(l) == 'l' || peek_char(l) == 'L') {
        advance_char(l);
    }

    loc.length = (int)(l->buffers[l->depth].current - start);
    Token tok = make_token(l, TOK_INT_LIT, loc);
    tok.int_val = val;
    return tok;
}

static Token scan_char_literal(Lexer *l, SourceLoc loc) {
    const char *start = l->buffers[l->depth].current;
    advance_char(l); /* Skip opening quote */
    char c = advance_char(l);

    if (c == '\\') {
        char esc = advance_char(l);
        switch (esc) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case '0': c = '\0'; break;
            case '\\': c = '\\'; break;
            case '\'': c = '\''; break;
            default: c = esc; break;
        }
    }

    if (peek_char(l) == '\'') {
        advance_char(l);
    } else {
        diag_report(DIAG_ERROR, loc, "unclosed character literal");
        diag_help("missing closing single quote '");
    }

    loc.length = (int)(l->buffers[l->depth].current - start);
    Token tok = make_token(l, TOK_CHAR_LIT, loc);
    tok.int_val = (int64_t)c;
    return tok;
}

static Token scan_string_literal(Lexer *l, SourceLoc loc) {
    const char *start = l->buffers[l->depth].current;
    advance_char(l); /* Skip opening quote */

    char buffer[4096];
    int len = 0;

    while (peek_char(l) != '\"' && peek_char(l) != '\0' && peek_char(l) != '\n') {
        char c = advance_char(l);
        if (c == '\\') {
            char esc = advance_char(l);
            switch (esc) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case '0': c = '\0'; break;
                case '\\': c = '\\'; break;
                case '\"': c = '\"'; break;
                default: c = esc; break;
            }
        }
        if (len < (int)sizeof(buffer) - 1) {
            buffer[len++] = c;
        }
    }

    if (peek_char(l) == '\"') {
        advance_char(l);
    } else {
        diag_report(DIAG_ERROR, loc, "unclosed string literal");
        diag_help("missing closing double quote \"");
    }

    loc.length = (int)(l->buffers[l->depth].current - start);
    Token tok = make_token(l, TOK_STR_LIT, loc);
    tok.str_val = str_intern_range(buffer, buffer + len);
    tok.int_val = (int64_t)len;
    return tok;
}

Token lexer_next(Lexer *l) {
    while (1) {
        skip_whitespace_and_comments(l);

        SourceLoc loc = current_loc(l);
        char c = peek_char(l);

        if (c == '\0') {
            if (l->depth > 0) {
                l->depth--;
                continue;
            }
            loc.length = 1;
            return make_token(l, TOK_EOF, loc);
        }

        if (isalpha((unsigned char)c) || c == '_') {
            return scan_identifier_or_keyword(l, loc);
        }

        if (isdigit((unsigned char)c)) {
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
                if (next == ':') { advance_char(l); loc.length = 2; return make_token(l, TOK_COLON_COLON, loc); }
                return make_token(l, TOK_COLON, loc);
            case '-':
                if (next == '>') { advance_char(l); loc.length = 2; return make_token(l, TOK_ARROW, loc); }
                if (next == '-') { advance_char(l); loc.length = 2; return make_token(l, TOK_DEC, loc); }
                if (next == '=') { advance_char(l); loc.length = 2; return make_token(l, TOK_MINUS_EQ, loc); }
                return make_token(l, TOK_MINUS, loc);
            case '+':
                if (next == '+') { advance_char(l); loc.length = 2; return make_token(l, TOK_INC, loc); }
                if (next == '=') { advance_char(l); loc.length = 2; return make_token(l, TOK_PLUS_EQ, loc); }
                return make_token(l, TOK_PLUS, loc);
            case '*':
                if (next == '=') { advance_char(l); loc.length = 2; return make_token(l, TOK_STAR_EQ, loc); }
                return make_token(l, TOK_STAR, loc);
            case '/':
                if (next == '=') { advance_char(l); loc.length = 2; return make_token(l, TOK_SLASH_EQ, loc); }
                return make_token(l, TOK_SLASH, loc);
            case '%':
                if (next == '=') { advance_char(l); loc.length = 2; return make_token(l, TOK_PERCENT_EQ, loc); }
                return make_token(l, TOK_PERCENT, loc);
            case '=':
                if (next == '=') { advance_char(l); loc.length = 2; return make_token(l, TOK_EQ_EQ, loc); }
                return make_token(l, TOK_ASSIGN, loc);
            case '!':
                if (next == '=') { advance_char(l); loc.length = 2; return make_token(l, TOK_EXCL_EQ, loc); }
                return make_token(l, TOK_EXCL, loc);
            case '<':
                if (next == '<') {
                    advance_char(l);
                    if (peek_char(l) == '=') { advance_char(l); loc.length = 3; return make_token(l, TOK_SHL_EQ, loc); }
                    loc.length = 2;
                    return make_token(l, TOK_SHL, loc);
                }
                if (next == '=') { advance_char(l); loc.length = 2; return make_token(l, TOK_LESS_EQ, loc); }
                return make_token(l, TOK_LESS, loc);
            case '>':
                if (next == '>') {
                    advance_char(l);
                    if (peek_char(l) == '=') { advance_char(l); loc.length = 3; return make_token(l, TOK_SHR_EQ, loc); }
                    loc.length = 2;
                    return make_token(l, TOK_SHR, loc);
                }
                if (next == '=') { advance_char(l); loc.length = 2; return make_token(l, TOK_GREATER_EQ, loc); }
                return make_token(l, TOK_GREATER, loc);
            case '&':
                if (next == '&') { advance_char(l); loc.length = 2; return make_token(l, TOK_LOG_AND, loc); }
                if (next == '=') { advance_char(l); loc.length = 2; return make_token(l, TOK_AMP_EQ, loc); }
                return make_token(l, TOK_AMP, loc);
            case '|':
                if (next == '|') { advance_char(l); loc.length = 2; return make_token(l, TOK_LOG_OR, loc); }
                if (next == '=') { advance_char(l); loc.length = 2; return make_token(l, TOK_PIPE_EQ, loc); }
                return make_token(l, TOK_PIPE, loc);
            case '.':
                if (next == '.' && peek_next_char(l) == '.') {
                    advance_char(l);
                    advance_char(l);
                    loc.length = 3;
                    return make_token(l, TOK_ELLIPSIS, loc);
                }
                return make_token(l, TOK_DOT, loc);
            case '^':
                if (next == '=') { advance_char(l); loc.length = 2; return make_token(l, TOK_CARET_EQ, loc); }
                return make_token(l, TOK_CARET, loc);
            default:
                loc.length = 1;
                return make_token(l, (TokenKind)c, loc);
        }
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
