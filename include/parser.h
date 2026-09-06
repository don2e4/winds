#ifndef WINDS_PARSER_H
#define WINDS_PARSER_H

#include "winds.h"
#include "arena.h"
#include "lexer.h"
#include "ast.h"

#define TYPE_TABLE_SIZE 1024

typedef struct TypeNameNode {
    const char *name;
    struct TypeNameNode *next;
} TypeNameNode;

typedef struct {
    Lexer lexer;
    Token current;
    Token peek;
    bool primed;
    Arena *arena;
    const char *current_namespace;
    const char *current_class;
    ASTNode *pending_decls[32];
    int pending_decl_count;
    TypeNameNode **type_table;
} Parser;

/* Initialize parser */
void parser_init(Parser *p, Arena *arena, const char *source, const char *filename);

/* Destroy parser and free lexer resources */
void parser_destroy(Parser *p);

/* Add include search path (-I) */
void parser_add_include_path(Parser *p, const char *path);

/* Parse an entire translation unit */
ASTNode *parser_parse(Parser *p);

#endif /* WINDS_PARSER_H */
