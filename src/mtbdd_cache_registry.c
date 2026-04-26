#include "mtbdd_cache_registry.h"
#include <stdlib.h>

MtdddCacheRegistry g_cache_registry = {NULL};

/* ---------- hooks ---------- */

void MtbddCache_registry_reset_all(void) {
    for (CacheNode *n = g_cache_registry.head; n; n = n->next)
        BddCache_reset(n->cache);
}

void MtbddCache_registry_resize_all(int newsize) {
    int sz = newsize / 16; // no need for large cache, only local caches are registered, and they are small
    if (sz < 1024) sz = 1024;
    for (CacheNode *n = g_cache_registry.head; n; n = n->next)
        BddCache_resize(n->cache, sz);
}

/* ---------- public API ---------- */


void MtbddCache_registry_register(BddCache *c) {
    CacheNode *node = malloc(sizeof(CacheNode));
    node->cache = c;
    node->next  = g_cache_registry.head;
    g_cache_registry.head = node;
}

void MtbddCache_registry_unregister(BddCache *c) {
    CacheNode **pp = &g_cache_registry.head;
    while (*pp) {
        if ((*pp)->cache == c) { // should be always on top as it works as a stack, but let's be safe
            CacheNode *dead = *pp;
            *pp = dead->next;
            free(dead);
            return;
        }
        pp = &(*pp)->next;
    }
}