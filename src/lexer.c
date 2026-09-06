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
    l->buffers[0].macro_name = NULL;

    /* Predefine standard macros */
    l->macro_defs[l->macro_def_count++] = (MacroDef){
        .name = str_intern("__winds__"),
        .is_function_like = false,
        .params = NULL,
        .param_count = 0,
        .body = str_intern("1"),
        .is_active = false
    };
    l->macro_defs[l->macro_def_count++] = (MacroDef){
        .name = str_intern("__cplusplus"),
        .is_function_like = false,
        .params = NULL,
        .param_count = 0,
        .body = str_intern("201703L"),
        .is_active = false
    };
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

MacroDef *find_macro(Lexer *l, const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < l->macro_def_count; i++) {
        if (l->macro_defs[i].name && strcmp(l->macro_defs[i].name, name) == 0) {
            return &l->macro_defs[i];
        }
    }
    return NULL;
}

static bool is_macro_active(Lexer *l, const char *name) {
    if (!name) return false;
    for (int d = 1; d <= l->depth; d++) {
        if (l->buffers[d].macro_name && strcmp(l->buffers[d].macro_name, name) == 0) {
            return true;
        }
    }
    return false;
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

static void strbuf_init(StrBuf *sb) {
    sb->cap = 256;
    sb->len = 0;
    sb->data = malloc(sb->cap);
    sb->data[0] = '\0';
}

static void strbuf_append_char(StrBuf *sb, char c) {
    if (sb->len + 2 >= sb->cap) {
        sb->cap *= 2;
        sb->data = realloc(sb->data, sb->cap);
    }
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
}

static void strbuf_append_str(StrBuf *sb, const char *s) {
    if (!s) return;
    size_t slen = strlen(s);
    while (sb->len + slen + 1 >= sb->cap) {
        sb->cap *= 2;
        sb->data = realloc(sb->data, sb->cap);
    }
    memcpy(sb->data + sb->len, s, slen);
    sb->len += slen;
    sb->data[sb->len] = '\0';
}

static void strbuf_trim_trailing(StrBuf *sb) {
    while (sb->len > 0 && (sb->data[sb->len - 1] == ' ' || sb->data[sb->len - 1] == '\t')) {
        sb->data[--sb->len] = '\0';
    }
}

static char *trim_str(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    int len = (int)strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = '\0';
    }
    return s;
}

static void push_macro_buffer(Lexer *l, const char *name, const char *text) {
    if (l->depth + 1 >= MAX_INCLUDE_DEPTH) {
        SourceLoc loc = current_loc(l);
        diag_report(DIAG_FATAL, loc, "maximum macro expansion depth exceeded (%d)", MAX_INCLUDE_DEPTH);
        return;
    }
    l->depth++;
    LexerBuffer *nb = &l->buffers[l->depth];
    nb->source = text;
    nb->current = text;
    nb->line_start = text;
    nb->filename = l->buffers[l->depth - 1].filename;
    nb->line = l->buffers[l->depth - 1].line;
    nb->col = l->buffers[l->depth - 1].col;
    nb->allocated_source = NULL;
    nb->macro_name = name;
}

static char *expand_macro_body(MacroDef *m, char **args, int arg_count) {
    StrBuf sb;
    strbuf_init(&sb);

    const char *p = m->body ? m->body : "";
    while (*p != '\0') {
        /* Check for token pasting '##' */
        if (*p == '#' && *(p + 1) == '#') {
            strbuf_trim_trailing(&sb);
            p += 2;
            while (*p == ' ' || *p == '\t') p++;

            /* Next token must be pasted without leading space */
            if (*p == '#') {
                /* Stringify next parameter */
                p++;
                while (*p == ' ' || *p == '\t') p++;
                char id[128];
                int idlen = 0;
                while ((isalnum((unsigned char)*p) || *p == '_') && idlen < (int)sizeof(id) - 1) {
                    id[idlen++] = *p++;
                }
                id[idlen] = '\0';
                int pidx = -1;
                for (int i = 0; i < m->param_count; i++) {
                    if (strcmp(m->params[i], id) == 0) { pidx = i; break; }
                }
                if (pidx >= 0 && pidx < arg_count) {
                    strbuf_append_char(&sb, '"');
                    for (const char *s = args[pidx]; *s != '\0'; s++) {
                        if (*s == '"' || *s == '\\') strbuf_append_char(&sb, '\\');
                        strbuf_append_char(&sb, *s);
                    }
                    strbuf_append_char(&sb, '"');
                } else {
                    strbuf_append_str(&sb, id);
                }
            } else if (isalnum((unsigned char)*p) || *p == '_') {
                char id[128];
                int idlen = 0;
                while ((isalnum((unsigned char)*p) || *p == '_') && idlen < (int)sizeof(id) - 1) {
                    id[idlen++] = *p++;
                }
                id[idlen] = '\0';
                int pidx = -1;
                for (int i = 0; i < m->param_count; i++) {
                    if (strcmp(m->params[i], id) == 0) { pidx = i; break; }
                }
                if (pidx >= 0 && pidx < arg_count) {
                    strbuf_append_str(&sb, args[pidx]);
                } else {
                    strbuf_append_str(&sb, id);
                }
            } else {
                strbuf_append_char(&sb, *p++);
            }
            continue;
        }

        /* Check for stringification '#' */
        if (*p == '#') {
            const char *q = p + 1;
            while (*q == ' ' || *q == '\t') q++;
            if (isalpha((unsigned char)*q) || *q == '_') {
                char id[128];
                int idlen = 0;
                while ((isalnum((unsigned char)*q) || *q == '_') && idlen < (int)sizeof(id) - 1) {
                    id[idlen++] = *q++;
                }
                id[idlen] = '\0';
                int pidx = -1;
                for (int i = 0; i < m->param_count; i++) {
                    if (strcmp(m->params[i], id) == 0) { pidx = i; break; }
                }
                if (pidx >= 0 && pidx < arg_count) {
                    p = q;
                    strbuf_append_char(&sb, '"');
                    for (const char *s = args[pidx]; *s != '\0'; s++) {
                        if (*s == '"' || *s == '\\') strbuf_append_char(&sb, '\\');
                        strbuf_append_char(&sb, *s);
                    }
                    strbuf_append_char(&sb, '"');
                    continue;
                }
            }
            /* Not a parameter stringification, output '#' */
            strbuf_append_char(&sb, *p++);
            continue;
        }

        /* String literal in body */
        if (*p == '"') {
            strbuf_append_char(&sb, *p++);
            while (*p != '\0' && *p != '"') {
                if (*p == '\\' && *(p + 1) != '\0') {
                    strbuf_append_char(&sb, *p++);
                }
                strbuf_append_char(&sb, *p++);
            }
            if (*p == '"') strbuf_append_char(&sb, *p++);
            continue;
        }

        /* Char literal in body */
        if (*p == '\'') {
            strbuf_append_char(&sb, *p++);
            while (*p != '\0' && *p != '\'') {
                if (*p == '\\' && *(p + 1) != '\0') {
                    strbuf_append_char(&sb, *p++);
                }
                strbuf_append_char(&sb, *p++);
            }
            if (*p == '\'') strbuf_append_char(&sb, *p++);
            continue;
        }

        /* Identifier */
        if (isalpha((unsigned char)*p) || *p == '_') {
            char id[128];
            int idlen = 0;
            while ((isalnum((unsigned char)*p) || *p == '_') && idlen < (int)sizeof(id) - 1) {
                id[idlen++] = *p++;
            }
            id[idlen] = '\0';
            int pidx = -1;
            for (int i = 0; i < m->param_count; i++) {
                if (strcmp(m->params[i], id) == 0) { pidx = i; break; }
            }
            if (pidx >= 0 && pidx < arg_count) {
                strbuf_append_str(&sb, args[pidx]);
            } else {
                strbuf_append_str(&sb, id);
            }
            continue;
        }

        /* Any other character */
        strbuf_append_char(&sb, *p++);
    }

    return sb.data;
}

static bool param_is_stringified_or_pasted(MacroDef *m, const char *param_name) {
    if (!m->body || !param_name) return false;
    size_t plen = strlen(param_name);
    const char *p = m->body;
    while (*p != '\0') {
        if (*p == '#') {
            if (*(p + 1) == '#') {
                p += 2;
                while (*p == ' ' || *p == '\t') p++;
                if (strncmp(p, param_name, plen) == 0 &&
                    !isalnum((unsigned char)p[plen]) && p[plen] != '_') {
                    return true;
                }
            } else {
                p++;
                while (*p == ' ' || *p == '\t') p++;
                if (strncmp(p, param_name, plen) == 0 &&
                    !isalnum((unsigned char)p[plen]) && p[plen] != '_') {
                    return true;
                }
            }
        } else if (strncmp(p, param_name, plen) == 0 &&
                   !isalnum((unsigned char)p[plen]) && p[plen] != '_') {
            const char *q = p + plen;
            while (*q == ' ' || *q == '\t') q++;
            if (*q == '#' && *(q + 1) == '#') {
                return true;
            }
            p += plen;
        } else {
            p++;
        }
    }
    return false;
}

static char *expand_argument_tokens(Lexer *l, const char *arg_text) {
    if (!arg_text || arg_text[0] == '\0') return strdup("");

    bool has_ident = false;
    for (const char *p = arg_text; *p; p++) {
        if (isalpha((unsigned char)*p) || *p == '_') {
            has_ident = true;
            break;
        }
    }
    if (!has_ident) return strdup(arg_text);

    Lexer temp_l;
    lexer_init(&temp_l, arg_text, l->buffers[l->depth].filename);
    for (int i = 0; i < l->macro_def_count; i++) {
        temp_l.macro_defs[i] = l->macro_defs[i];
    }
    temp_l.macro_def_count = l->macro_def_count;

    StrBuf sb;
    strbuf_init(&sb);
    bool first = true;

    while (1) {
        Token tok = lexer_next(&temp_l);
        if (tok.kind == TOK_EOF) break;

        if (!first) {
            strbuf_append_char(&sb, ' ');
        }
        first = false;

        if (tok.kind == TOK_IDENT) {
            strbuf_append_str(&sb, tok.str_val);
        } else if (tok.kind == TOK_INT_LIT) {
            char num_buf[32];
            snprintf(num_buf, sizeof(num_buf), "%ld", (long)tok.int_val);
            strbuf_append_str(&sb, num_buf);
        } else if (tok.kind == TOK_STR_LIT) {
            strbuf_append_char(&sb, '"');
            strbuf_append_str(&sb, tok.str_val);
            strbuf_append_char(&sb, '"');
        } else if (tok.kind == TOK_CHAR_LIT) {
            char cbuf[16];
            if (tok.int_val == '\0') strcpy(cbuf, "'\\0'");
            else if (tok.int_val == '\n') strcpy(cbuf, "'\\n'");
            else if (tok.int_val == '\t') strcpy(cbuf, "'\\t'");
            else if (tok.int_val == '\'') strcpy(cbuf, "'\\''");
            else if (tok.int_val == '\\') strcpy(cbuf, "'\\\\'");
            else snprintf(cbuf, sizeof(cbuf), "'%c'", (char)tok.int_val);
            strbuf_append_str(&sb, cbuf);
        } else {
            strbuf_append_str(&sb, token_kind_str(tok.kind));
        }
    }

    return sb.data;
}

static bool try_expand_macro(Lexer *l, const char *name, SourceLoc loc) {
    /* Check predefined dynamic macros */
    if (strcmp(name, "__LINE__") == 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", loc.line);
        push_macro_buffer(l, "__LINE__", str_intern(buf));
        return true;
    }
    if (strcmp(name, "__FILE__") == 0) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "\"%s\"", loc.file ? loc.file : "<stdin>");
        push_macro_buffer(l, "__FILE__", str_intern(buf));
        return true;
    }

    MacroDef *m = find_macro(l, name);
    if (!m) return false;

    if (is_macro_active(l, name)) {
        return false;
    }

    if (!m->is_function_like) {
        push_macro_buffer(l, m->name, m->body ? m->body : "");
        return true;
    }

    /* Function-like macro: peek if next non-whitespace char is '(' */
    LexerBuffer *b = &l->buffers[l->depth];
    const char *p = b->current;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (*p != '(') {
        return false;
    }

    /* Advance to '(' */
    while (b->current < p) {
        char ch = advance_char(l);
        if (ch == '\n') {
            b->line++;
            b->col = 1;
            b->line_start = b->current;
        }
    }
    advance_char(l); /* Consume '(' */

    /* Parse arguments */
    char *args[MAX_MACRO_PARAMS];
    int arg_count = 0;

    char arg_buf[4096];
    int arg_len = 0;
    int paren_depth = 0;
    bool in_str = false;
    bool in_char = false;

    while (peek_char(l) != '\0') {
        char ch = peek_char(l);

        /* Skip comments inside arguments */
        if (!in_str && !in_char && ch == '/' && peek_next_char(l) == '/') {
            advance_char(l);
            advance_char(l);
            while (peek_char(l) != '\0' && peek_char(l) != '\n') {
                advance_char(l);
            }
            continue;
        }
        if (!in_str && !in_char && ch == '/' && peek_next_char(l) == '*') {
            advance_char(l);
            advance_char(l);
            while (peek_char(l) != '\0') {
                if (peek_char(l) == '*' && peek_next_char(l) == '/') {
                    advance_char(l);
                    advance_char(l);
                    break;
                }
                if (peek_char(l) == '\n') {
                    b->line++;
                    b->col = 1;
                    advance_char(l);
                    b->line_start = b->current;
                } else {
                    advance_char(l);
                }
            }
            if (arg_len < (int)sizeof(arg_buf) - 1) arg_buf[arg_len++] = ' ';
            continue;
        }

        ch = advance_char(l);

        if (in_str) {
            if (arg_len < (int)sizeof(arg_buf) - 1) arg_buf[arg_len++] = ch;
            if (ch == '\\' && peek_char(l) != '\0') {
                if (arg_len < (int)sizeof(arg_buf) - 1) arg_buf[arg_len++] = advance_char(l);
            } else if (ch == '"') {
                in_str = false;
            }
            continue;
        }
        if (in_char) {
            if (arg_len < (int)sizeof(arg_buf) - 1) arg_buf[arg_len++] = ch;
            if (ch == '\\' && peek_char(l) != '\0') {
                if (arg_len < (int)sizeof(arg_buf) - 1) arg_buf[arg_len++] = advance_char(l);
            } else if (ch == '\'') {
                in_char = false;
            }
            continue;
        }

        if (ch == '"') {
            in_str = true;
            if (arg_len < (int)sizeof(arg_buf) - 1) arg_buf[arg_len++] = ch;
            continue;
        }
        if (ch == '\'') {
            in_char = true;
            if (arg_len < (int)sizeof(arg_buf) - 1) arg_buf[arg_len++] = ch;
            continue;
        }

        if (ch == '(' || ch == '[' || ch == '{') {
            paren_depth++;
            if (arg_len < (int)sizeof(arg_buf) - 1) arg_buf[arg_len++] = ch;
            continue;
        }
        if (ch == ')' || ch == ']' || ch == '}') {
            if (paren_depth > 0) {
                paren_depth--;
                if (arg_len < (int)sizeof(arg_buf) - 1) arg_buf[arg_len++] = ch;
                continue;
            }
            /* Reached closing ')' of macro invocation */
            arg_buf[arg_len] = '\0';
            char *trimmed = trim_str(arg_buf);
            if (m->param_count == 0 && trimmed[0] == '\0') {
                /* Zero params and empty arg */
            } else {
                if (arg_count < MAX_MACRO_PARAMS) {
                    args[arg_count++] = strdup(trimmed);
                }
            }
            break;
        }

        if (ch == ',' && paren_depth == 0) {
            arg_buf[arg_len] = '\0';
            char *trimmed = trim_str(arg_buf);
            if (arg_count < MAX_MACRO_PARAMS) {
                args[arg_count++] = strdup(trimmed);
            }
            arg_len = 0;
            continue;
        }

        if (ch == '\n') {
            b->line++;
            b->col = 1;
            b->line_start = b->current;
        }
        if (arg_len < (int)sizeof(arg_buf) - 1) {
            arg_buf[arg_len++] = ch;
        }
    }

    if (arg_count != m->param_count) {
        diag_report(DIAG_ERROR, loc, "macro '%s' requires %d arguments, but %d were provided",
                    m->name, m->param_count, arg_count);
        for (int i = 0; i < arg_count; i++) free(args[i]);
        return false;
    }

    char *subst_args[MAX_MACRO_PARAMS];
    for (int i = 0; i < arg_count; i++) {
        if (param_is_stringified_or_pasted(m, m->params[i])) {
            subst_args[i] = strdup(args[i]);
        } else {
            subst_args[i] = expand_argument_tokens(l, args[i]);
        }
    }

    char *expanded = expand_macro_body(m, subst_args, arg_count);
    for (int i = 0; i < arg_count; i++) {
        free(args[i]);
        free(subst_args[i]);
    }

    const char *interned = str_intern(expanded);
    free(expanded);

    push_macro_buffer(l, m->name, interned);
    return true;
}

static bool eval_preprocessor_condition(Lexer *l) {
    while (peek_char(l) == ' ' || peek_char(l) == '\t') advance_char(l);

    bool invert = false;
    if (peek_char(l) == '!') {
        advance_char(l);
        while (peek_char(l) == ' ' || peek_char(l) == '\t') advance_char(l);
        invert = true;
    }

    /* Check for 'defined' keyword */
    char word[64];
    int wlen = 0;
    const char *p = l->buffers[l->depth].current;
    while (isalpha((unsigned char)*p) || *p == '_') {
        if (wlen < (int)sizeof(word) - 1) word[wlen++] = *p;
        p++;
    }
    word[wlen] = '\0';

    if (strcmp(word, "defined") == 0) {
        while (wlen-- > 0) advance_char(l);
        while (peek_char(l) == ' ' || peek_char(l) == '\t') advance_char(l);
        bool has_paren = false;
        if (peek_char(l) == '(') {
            has_paren = true;
            advance_char(l);
            while (peek_char(l) == ' ' || peek_char(l) == '\t') advance_char(l);
        }
        char mname[128];
        int mlen = 0;
        while ((isalnum((unsigned char)peek_char(l)) || peek_char(l) == '_') && mlen < (int)sizeof(mname) - 1) {
            mname[mlen++] = advance_char(l);
        }
        mname[mlen] = '\0';
        if (has_paren) {
            while (peek_char(l) == ' ' || peek_char(l) == '\t') advance_char(l);
            if (peek_char(l) == ')') advance_char(l);
        }
        bool def = (find_macro(l, mname) != NULL);
        return invert ? !def : def;
    }

    /* Check for integer literal */
    if (isdigit((unsigned char)peek_char(l))) {
        int64_t val = 0;
        while (isdigit((unsigned char)peek_char(l))) {
            val = val * 10 + (advance_char(l) - '0');
        }
        bool res = (val != 0);
        return invert ? !res : res;
    }

    /* Check for macro name */
    if (isalpha((unsigned char)peek_char(l)) || peek_char(l) == '_') {
        char mname[128];
        int mlen = 0;
        while ((isalnum((unsigned char)peek_char(l)) || peek_char(l) == '_') && mlen < (int)sizeof(mname) - 1) {
            mname[mlen++] = advance_char(l);
        }
        mname[mlen] = '\0';
        MacroDef *m = find_macro(l, mname);
        bool res = false;
        if (m && m->body) {
            const char *b = m->body;
            while (*b == ' ' || *b == '\t') b++;
            if (isdigit((unsigned char)*b)) {
                res = (atoi(b) != 0);
            } else {
                res = true;
            }
        }
        return invert ? !res : res;
    }

    return false;
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
            bool is_def = (find_macro(l, macro) != NULL);
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
            bool is_def = (find_macro(l, macro) != NULL);
            if (!is_def) {
                l->skip_depth = l->cond_depth;
            }
        }
    } else if (strcmp(dir, "if") == 0) {
        l->cond_depth++;
        if (l->skip_depth == 0) {
            bool cond = eval_preprocessor_condition(l);
            if (!cond) {
                l->skip_depth = l->cond_depth;
            }
        }
    } else if (strcmp(dir, "elif") == 0) {
        if (l->skip_depth == l->cond_depth) {
            bool cond = eval_preprocessor_condition(l);
            if (cond) {
                l->skip_depth = 0;
            }
        } else if (l->skip_depth == 0) {
            l->skip_depth = l->cond_depth;
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
        /* In skipping branch, ignore define / undef / pragma / include */
    } else if (strcmp(dir, "undef") == 0) {
        char macro[128];
        int mlen = 0;
        while ((isalnum((unsigned char)peek_char(l)) || peek_char(l) == '_') && mlen < (int)sizeof(macro) - 1) {
            macro[mlen++] = advance_char(l);
        }
        macro[mlen] = '\0';
        for (int i = 0; i < l->macro_def_count; i++) {
            if (l->macro_defs[i].name && strcmp(l->macro_defs[i].name, macro) == 0) {
                l->macro_defs[i].name = NULL;
                break;
            }
        }
    } else if (strcmp(dir, "define") == 0) {
        char macro[128];
        int mlen = 0;
        while ((isalnum((unsigned char)peek_char(l)) || peek_char(l) == '_') && mlen < (int)sizeof(macro) - 1) {
            macro[mlen++] = advance_char(l);
        }
        macro[mlen] = '\0';

        bool is_function_like = false;
        const char *params[MAX_MACRO_PARAMS];
        int param_count = 0;

        /* Check immediately for '(' without intervening whitespace */
        if (peek_char(l) == '(') {
            is_function_like = true;
            advance_char(l); /* Consume '(' */
            while (peek_char(l) == ' ' || peek_char(l) == '\t') advance_char(l);
            if (peek_char(l) == ')') {
                advance_char(l);
            } else {
                while (peek_char(l) != '\0' && peek_char(l) != ')' && peek_char(l) != '\n') {
                    while (peek_char(l) == ' ' || peek_char(l) == '\t') advance_char(l);
                    char pname[128];
                    int plen = 0;
                    while ((isalnum((unsigned char)peek_char(l)) || peek_char(l) == '_') && plen < (int)sizeof(pname) - 1) {
                        pname[plen++] = advance_char(l);
                    }
                    pname[plen] = '\0';
                    if (plen > 0 && param_count < MAX_MACRO_PARAMS) {
                        params[param_count++] = str_intern(pname);
                    }
                    while (peek_char(l) == ' ' || peek_char(l) == '\t') advance_char(l);
                    if (peek_char(l) == ',') {
                        advance_char(l);
                    } else if (peek_char(l) == ')') {
                        advance_char(l);
                        break;
                    } else {
                        break;
                    }
                }
            }
        }

        /* Skip horizontal whitespace before body */
        while (peek_char(l) == ' ' || peek_char(l) == '\t') {
            advance_char(l);
        }

        /* Read body with line continuation '\' and comment handling */
        char body_buf[4096];
        int blen = 0;
        while (peek_char(l) != '\0') {
            char c = peek_char(l);

            if (c == '\\') {
                const char *p = l->buffers[l->depth].current + 1;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == '\r' || *p == '\n') {
                    advance_char(l); /* Consume '\' */
                    while (peek_char(l) == ' ' || peek_char(l) == '\t') advance_char(l);
                    if (peek_char(l) == '\r') advance_char(l);
                    if (peek_char(l) == '\n') advance_char(l);
                    LexerBuffer *b = &l->buffers[l->depth];
                    b->line++;
                    b->col = 1;
                    b->line_start = b->current;
                    if (blen > 0 && body_buf[blen - 1] != ' ' && blen < (int)sizeof(body_buf) - 1) {
                        body_buf[blen++] = ' ';
                    }
                    continue;
                }
            }

            if (c == '/' && peek_next_char(l) == '/') {
                break; /* Single-line comment ends body */
            }
            if (c == '/' && peek_next_char(l) == '*') {
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
                if (blen > 0 && body_buf[blen - 1] != ' ' && blen < (int)sizeof(body_buf) - 1) {
                    body_buf[blen++] = ' ';
                }
                continue;
            }

            if (c == '\n' || c == '\r') {
                break;
            }

            if (blen < (int)sizeof(body_buf) - 1) {
                body_buf[blen++] = advance_char(l);
            } else {
                advance_char(l);
            }
        }
        body_buf[blen] = '\0';

        /* Trim trailing whitespace */
        while (blen > 0 && (body_buf[blen - 1] == ' ' || body_buf[blen - 1] == '\t')) {
            body_buf[--blen] = '\0';
        }

        if (mlen > 0) {
            MacroDef *target = find_macro(l, macro);
            if (!target) {
                for (int i = 0; i < l->macro_def_count; i++) {
                    if (l->macro_defs[i].name == NULL) {
                        target = &l->macro_defs[i];
                        break;
                    }
                }
            }
            if (!target && l->macro_def_count < MAX_MACRO_DEFS) {
                target = &l->macro_defs[l->macro_def_count++];
            }
            if (target) {
                target->name = str_intern(macro);
                target->is_function_like = is_function_like;
                target->param_count = param_count;
                if (param_count > 0) {
                    const char **p_arr = malloc(sizeof(const char *) * param_count);
                    for (int pi = 0; pi < param_count; pi++) {
                        p_arr[pi] = params[pi];
                    }
                    target->params = p_arr;
                } else {
                    target->params = NULL;
                }
                target->body = str_intern(body_buf);
                target->is_active = false;
            }
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
                const char *defaults[] = { "include/winds/std", "./include/winds/std", "./include", "include", "/usr/include", "/usr/local/include" };
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
                                nb->macro_name = NULL;
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
        } else if (c == '#') {
            const char *p = l->buffers[l->depth].line_start;
            bool at_line_start = true;
            while (p < l->buffers[l->depth].current) {
                if (*p != ' ' && *p != '\t' && *p != '\r') {
                    at_line_start = false;
                    break;
                }
                p++;
            }
            if (at_line_start) {
                handle_preprocessor(l);
            } else {
                break;
            }
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

    if (try_expand_macro(l, name, loc)) {
        return lexer_next(l);
    }

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
    else if (strcmp(name, "operator") == 0) tok.kind = TOK_KW_OPERATOR;
    else if (strcmp(name, "typedef") == 0) tok.kind = TOK_KW_TYPEDEF;
    else if (strcmp(name, "template") == 0) tok.kind = TOK_KW_TEMPLATE;
    else if (strcmp(name, "typename") == 0) tok.kind = TOK_KW_TYPENAME;
    else if (strcmp(name, "void") == 0) tok.kind = TOK_KW_VOID;
    else if (strcmp(name, "bool") == 0) tok.kind = TOK_KW_BOOL;
    else if (strcmp(name, "char") == 0) tok.kind = TOK_KW_CHAR;
    else if (strcmp(name, "short") == 0) tok.kind = TOK_KW_SHORT;
    else if (strcmp(name, "int") == 0) tok.kind = TOK_KW_INT;
    else if (strcmp(name, "long") == 0) tok.kind = TOK_KW_LONG;
    else if (strcmp(name, "signed") == 0) tok.kind = TOK_KW_SIGNED;
    else if (strcmp(name, "unsigned") == 0) tok.kind = TOK_KW_UNSIGNED;
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
                if (next == '>') {
                    advance_char(l);
                    if (peek_char(l) == '*') { advance_char(l); loc.length = 3; return make_token(l, TOK_ARROW_STAR, loc); }
                    loc.length = 2;
                    return make_token(l, TOK_ARROW, loc);
                }
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
                if (next == '*') {
                    advance_char(l);
                    loc.length = 2;
                    return make_token(l, TOK_DOT_STAR, loc);
                }
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
        case TOK_DOT_STAR: return ".*";
        case TOK_ARROW_STAR: return "->*";
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
