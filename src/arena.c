#include "arena.h"

#define DEFAULT_ARENA_BLOCK_SIZE (64 * 1024)
#define ARENA_ALIGNMENT (sizeof(void *))

static size_t align_up(size_t n, size_t align) {
    return (n + align - 1) & ~(align - 1);
}

static ArenaBlock *arena_new_block(size_t min_capacity) {
    size_t cap = min_capacity < DEFAULT_ARENA_BLOCK_SIZE ? DEFAULT_ARENA_BLOCK_SIZE : min_capacity;
    ArenaBlock *block = malloc(sizeof(ArenaBlock) + cap);
    if (!block) {
        fprintf(stderr, "winds: fatal error: out of memory in arena_new_block\n");
        exit(EXIT_FAILURE);
    }
    block->next = NULL;
    block->capacity = cap;
    block->used = 0;
    return block;
}

Arena *arena_create(size_t block_size) {
    Arena *arena = malloc(sizeof(Arena));
    if (!arena) {
        fprintf(stderr, "winds: fatal error: out of memory allocating arena\n");
        exit(EXIT_FAILURE);
    }
    size_t default_size = block_size > 0 ? block_size : DEFAULT_ARENA_BLOCK_SIZE;
    arena->default_block_size = default_size;
    arena->first = arena_new_block(default_size);
    arena->current = arena->first;
    arena->total_allocated = 0;
    return arena;
}

void *arena_alloc(Arena *arena, size_t size) {
    if (!arena || size == 0) return NULL;

    size = align_up(size, ARENA_ALIGNMENT);

    /* Does it fit in the current block? */
    if (arena->current->used + size <= arena->current->capacity) {
        void *ptr = arena->current->data + arena->current->used;
        arena->current->used += size;
        arena->total_allocated += size;
        return ptr;
    }

    /* Allocate a new block large enough */
    size_t req = size > arena->default_block_size ? size : arena->default_block_size;
    ArenaBlock *block = arena_new_block(req);
    arena->current->next = block;
    arena->current = block;

    void *ptr = block->data;
    block->used = size;
    arena->total_allocated += size;
    return ptr;
}

void *arena_alloc_zero(Arena *arena, size_t size) {
    void *ptr = arena_alloc(arena, size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

char *arena_strdup(Arena *arena, const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char *dup = arena_alloc(arena, len + 1);
    memcpy(dup, str, len + 1);
    return dup;
}

char *arena_strndup(Arena *arena, const char *str, size_t len) {
    if (!str) return NULL;
    char *dup = arena_alloc(arena, len + 1);
    memcpy(dup, str, len);
    dup[len] = '\0';
    return dup;
}

void arena_reset(Arena *arena) {
    if (!arena) return;
    for (ArenaBlock *b = arena->first; b != NULL; b = b->next) {
        b->used = 0;
    }
    arena->current = arena->first;
    arena->total_allocated = 0;
}

void arena_destroy(Arena *arena) {
    if (!arena) return;
    ArenaBlock *curr = arena->first;
    while (curr) {
        ArenaBlock *next = curr->next;
        free(curr);
        curr = next;
    }
    free(arena);
}
