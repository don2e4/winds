#include "str.h"

#define HASH_TABLE_SIZE 4096

typedef struct InternEntry {
    const char *str;
    size_t len;
    struct InternEntry *next;
} InternEntry;

static Arena *g_str_arena = NULL;
static InternEntry *g_hash_table[HASH_TABLE_SIZE];

static uint32_t fnv1a_hash(const char *str, size_t len) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint8_t)str[i];
        hash *= 16777619u;
    }
    return hash;
}

void str_intern_init(void) {
    if (!g_str_arena) {
        g_str_arena = arena_create(32 * 1024);
    }
    memset(g_hash_table, 0, sizeof(g_hash_table));
}

const char *str_intern_range(const char *start, const char *end) {
    if (!start || !end || start >= end) return "";
    if (!g_str_arena) str_intern_init();

    size_t len = (size_t)(end - start);
    uint32_t hash = fnv1a_hash(start, len);
    size_t bucket = hash % HASH_TABLE_SIZE;

    for (InternEntry *e = g_hash_table[bucket]; e != NULL; e = e->next) {
        if (e->len == len && memcmp(e->str, start, len) == 0) {
            return e->str;
        }
    }

    /* Allocate and intern */
    char *dup = arena_alloc(g_str_arena, len + 1);
    memcpy(dup, start, len);
    dup[len] = '\0';

    InternEntry *entry = arena_alloc(g_str_arena, sizeof(InternEntry));
    entry->str = dup;
    entry->len = len;
    entry->next = g_hash_table[bucket];
    g_hash_table[bucket] = entry;

    return dup;
}

const char *str_intern(const char *str) {
    if (!str) return "";
    return str_intern_range(str, str + strlen(str));
}

void str_intern_destroy(void) {
    if (g_str_arena) {
        arena_destroy(g_str_arena);
        g_str_arena = NULL;
    }
    memset(g_hash_table, 0, sizeof(g_hash_table));
}
