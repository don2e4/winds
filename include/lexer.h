#ifndef WINDS_LEXER_H
#define WINDS_LEXER_H

#include "winds.h"

typedef enum {
    TOK_EOF = 0,
    TOK_IDENT = 256,
    TOK_INT_LIT,
    TOK_CHAR_LIT,
    TOK_STR_LIT,

    /* Keywords */
    TOK_KW_CLASS,
    TOK_KW_STRUCT,
    TOK_KW_PUBLIC,
    TOK_KW_PRIVATE,
    TOK_KW_PROTECTED,
    TOK_KW_NAMESPACE,
    TOK_KW_USING,
    TOK_KW_NEW,
    TOK_KW_DELETE,
    TOK_KW_THIS,
    TOK_KW_RETURN,
    TOK_KW_IF,
    TOK_KW_ELSE,
    TOK_KW_WHILE,
    TOK_KW_FOR,
    TOK_KW_DO,
    TOK_KW_BREAK,
    TOK_KW_CONTINUE,
    TOK_KW_SIZEOF,

    /* Types */
    TOK_KW_VOID,
    TOK_KW_BOOL,
    TOK_KW_CHAR,
    TOK_KW_SHORT,
    TOK_KW_INT,
    TOK_KW_LONG,
    TOK_KW_FLOAT,
    TOK_KW_DOUBLE,
    TOK_KW_CONST,
    TOK_KW_STATIC,
    TOK_KW_EXTERN,
    TOK_KW_INLINE,
    TOK_KW_TRUE,
    TOK_KW_FALSE,
    TOK_KW_NULLPTR,

    /* Multi-character operators */
    TOK_COLON_COLON,    /* :: */
    TOK_ARROW,          /* -> */
    TOK_ELLIPSIS,       /* ... */
    TOK_INC,            /* ++ */
    TOK_DEC,            /* -- */
    TOK_EQ_EQ,          /* == */
    TOK_EXCL_EQ,        /* != */
    TOK_LESS_EQ,        /* <= */
    TOK_GREATER_EQ,     /* >= */
    TOK_LOG_AND,        /* && */
    TOK_LOG_OR,         /* || */
    TOK_SHL,            /* << */
    TOK_SHR,            /* >> */
    TOK_PLUS_EQ,        /* += */
    TOK_MINUS_EQ,       /* -= */
    TOK_STAR_EQ,        /* *= */
    TOK_SLASH_EQ,       /* /= */
    TOK_PERCENT_EQ,     /* %= */
    TOK_AMP_EQ,         /* &= */
    TOK_PIPE_EQ,        /* |= */
    TOK_CARET_EQ,       /* ^= */
    TOK_SHL_EQ,         /* <<= */
    TOK_SHR_EQ,         /* >>= */

    /* Single-character punctuators (represented by their ASCII value) */
    TOK_LPAREN   = '(',
    TOK_RPAREN   = ')',
    TOK_LBRACKET = '[',
    TOK_RBRACKET = ']',
    TOK_LBRACE   = '{',
    TOK_RBRACE   = '}',
    TOK_SEMICOLON= ';',
    TOK_COMMA    = ',',
    TOK_DOT      = '.',
    TOK_COLON    = ':',
    TOK_QUESTION = '?',
    TOK_ASSIGN   = '=',
    TOK_PLUS     = '+',
    TOK_MINUS    = '-',
    TOK_STAR     = '*',
    TOK_SLASH    = '/',
    TOK_PERCENT  = '%',
    TOK_AMP      = '&',
    TOK_PIPE     = '|',
    TOK_CARET    = '^',
    TOK_TILDE    = '~',
    TOK_EXCL     = '!',
    TOK_LESS     = '<',
    TOK_GREATER  = '>'
} TokenKind;

struct Token {
    TokenKind kind;
    SourceLoc loc;
    int64_t int_val;
    const char *str_val; /* Interned string for identifiers / string literals */
};

typedef struct {
    const char *source;
    const char *current;
    const char *line_start;
    const char *filename;
    int line;
    int col;
} Lexer;

/* Initialize lexer with source string and filename */
void lexer_init(Lexer *l, const char *source, const char *filename);

/* Read the next token */
Token lexer_next(Lexer *l);

/* Peek the next token without advancing */
Token lexer_peek(Lexer *l);

/* Convert token kind to human-readable string */
const char *token_kind_str(TokenKind kind);

#endif /* WINDS_LEXER_H */
