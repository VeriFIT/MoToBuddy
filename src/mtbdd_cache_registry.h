#ifndef BDD_CACHE_REGISTRY_H
#define BDD_CACHE_REGISTRY_H


#ifdef __cplusplus
extern "C" {
#endif
#include "cache.h"

typedef struct CacheNode {
    BddCache         *cache;
    struct CacheNode *next;
} CacheNode;

typedef struct {
    CacheNode *head;
} MtdddCacheRegistry;

extern MtdddCacheRegistry g_cache_registry;

/* Call when creating a local cache */
void MtbddCache_registry_register  (BddCache *c);

/* Call when destroying a local cache */
void MtbddCache_registry_unregister(BddCache *c);

/* Called internally by GBC and resize hooks */
void MtbddCache_registry_reset_all (void);
void MtbddCache_registry_resize_all(int newsize);

#ifdef __cplusplus
}
#endif

#endif
