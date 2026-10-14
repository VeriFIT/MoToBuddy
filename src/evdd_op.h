/**
 * @file evdd_op.h
 * @brief EVDD operators - apply, scalar mul, gates, computed caches.
 *
 * Analogous to the apply/ITE/operation parts of mtbdd.h / mtbdd.c.
 * Cache records are EVDD-specific (edge results are (node, ValueID));
 * do not widen BddCacheData used by BDD/MTBDD.
 */

#ifndef EVDD_OP_H
#define EVDD_OP_H

#include "evdd.h"
#include "evdd_value.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Computed-table entry / table (EVDD edges, not bare BDD ints)
 * ---------------------------------------------------------------------- */

/**
 * One cache slot. Keys are edges (node + weight); result is an edge.
 * @c op distinguishes apply vs scalar vs gate when sharing a table,
 * or can be unused (-1) for a dedicated table.
 */
typedef struct s_EvddCacheData {
    int a_node;
    evdd_value_id a_w;
    int b_node;
    evdd_value_id b_w;
    int op;

    int res_node;
    evdd_value_id res_w;

    int generation;
} EvddCacheData;

typedef struct s_EvddCache {
    EvddCacheData *table;
    int tablesize;
    int current_generation;
} EvddCache;

int  EvddCache_init(EvddCache *cache, int size);
void EvddCache_done(EvddCache *cache);
int  EvddCache_resize(EvddCache *cache, int newsize);
void EvddCache_reset(EvddCache *cache);

#define EvddCache_lookup(cache, hash)                                          \
    (&(cache)->table[(hash) % (unsigned)(cache)->tablesize])

#define EvddCache_is_valid(cache, entry)                                       \
    ((entry)->generation == (cache)->current_generation)

/** Store binary-edge op result (apply-style). */
#define EvddCache_store(entry, cache, an, aw, bn, bw, opc, rn, rw)             \
    do {                                                                       \
        (entry)->a_node = (an);                                                \
        (entry)->a_w = (aw);                                                   \
        (entry)->b_node = (bn);                                                \
        (entry)->b_w = (bw);                                                   \
        (entry)->op = (opc);                                                   \
        (entry)->res_node = (rn);                                              \
        (entry)->res_w = (rw);                                                 \
        (entry)->generation = (cache)->current_generation;                     \
    } while (0)

/* -------------------------------------------------------------------------
 * Operator lifecycle (wired from evdd_init / evdd_gbc later)
 * ---------------------------------------------------------------------- */

int  evdd_operator_init(int cachesize);
void evdd_operator_done(void);
void evdd_operator_reset(void);
int  evdd_operator_resize(int cachesize);

/**
 * Binary leaf operation for apply: combine two path values (ValueIDs).
 * Typically @c evdd_value_mul or @c evdd_value_add.
 */
typedef evdd_value_id (*evdd_apply_op)(evdd_value_id, evdd_value_id);

/**
 * Shannon apply on two EVDD edges. Folds incoming weights into child
 * edges, builds with @c evdd_makenode, caches on full edges + op.
 */
EVDD evdd_apply(EVDD a, EVDD b, evdd_apply_op op);

/* EVDD evdd_scalar_mul(EVDD f, evdd_value_id c); */

#ifdef __cplusplus
}
#endif

#endif /* EVDD_OP_H */
