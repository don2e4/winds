#ifndef WINDS_PARSER_H
#define WINDS_PARSER_H

#include "winds.h"
#include "arena.h"
#include "lexer.h"
#include "ast.h"

typedef struct {
    Lexer lexer;
    Token current;
    Token peek;
    Arena *arena;
    const char *current_namespace;
    const char *current_class;
} Parser;

/* Initialize parser */
void parser_init(Parser *p, Arena *arena, const char *source, const char *filename);

/* Parse an entire translation unit */
ASTNode *parser_parse(Parser *p);

#endif /* WINDS_PARSER_H */
