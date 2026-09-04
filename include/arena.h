#ifndef WINDS_ARENA_H
#define WINDS_ARENA_H

#include "winds.h"

typedef struct ArenaBlock {
    struct ArenaBlock *next;
    size_t capacity;
    size_t used;
    char data[];
} ArenaBlock;

struct Arena {
    ArenaBlock *first;
    ArenaBlock *current;
    size_t default_block_size;
    size_t total_allocated;
};

/* Creates a new arena with specified block size (0 for default 64KB) */
Arena *arena_create(size_t block_size);

/* Allocate uninitialized memory */
void *arena_alloc(Arena *arena, size_t size);

/* Allocate zero-initialized memory */
void *arena_alloc_zero(Arena *arena, size_t size);

/* Duplicate a NUL-terminated string into the arena */
char *arena_strdup(Arena *arena, const char *str);

/* Duplicate a string with explicit length into the arena */
char *arena_strndup(Arena *arena, const char *str, size_t len);

/* Reset arena allocations without freeing blocks (reusable) */
void arena_reset(Arena *arena);

/* Completely free all memory associated with the arena */
void arena_destroy(Arena *arena);

#endif /* WINDS_ARENA_H */
