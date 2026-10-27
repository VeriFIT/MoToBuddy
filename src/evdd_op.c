/**
 * @file evdd_op.c
 * @brief EVDD operator implementations and computed tables.
 *
 * Counterpart of mtbdd_apply_*, mtbdd_ite, and mtbdd_operation in mtbdd.c
 * and mtbdd_operator_init in bddop.c. Shannon split on LEVEL; compose
 * incoming weights; leaf op is the value algebra.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bdd.h"
#include "evdd_kernel.h"
#include "evdd_op.h"
#include "prime.h"

extern int bdd_error(int);

/* Dedicated tables; grow the set as ops land. */
static EvddCache evdd_applycache;
static EvddCache evdd_scalarmulcache;

static int evdd_op_running = 0;

/* -------------------------------------------------------------------------
 * EvddCache table plumbing (same idea as BddCache_*; own entry type)
 * ---------------------------------------------------------------------- */

int EvddCache_init(EvddCache *cache, int size)
{
    size = bdd_prime_gte(size);
    cache->table = (EvddCacheData *)calloc((size_t)size, sizeof(EvddCacheData));
    if (cache->table == NULL)
        return bdd_error(BDD_MEMORY);

    cache->tablesize = size;
    cache->current_generation = 1; /* 0-filled entries are invalid */
    return 0;
}

void EvddCache_done(EvddCache *cache)
{
    free(cache->table);
    cache->table = NULL;
    cache->tablesize = 0;
    cache->current_generation = 0;
}

int EvddCache_resize(EvddCache *cache, int newsize)
{
    free(cache->table);
    cache->table = NULL;

    newsize = bdd_prime_gte(newsize);
    cache->table =
        (EvddCacheData *)calloc((size_t)newsize, sizeof(EvddCacheData));
    if (cache->table == NULL) {
        cache->tablesize = 0;
        return bdd_error(BDD_MEMORY);
    }

    cache->tablesize = newsize;
    cache->current_generation = 1;
    return 0;
}

void EvddCache_reset(EvddCache *cache)
{
    if (cache->table == NULL)
        return;
    cache->current_generation++;
    if (cache->current_generation == 0) {
        /* Wrapped; invalidate by clearing (rare). */
        memset(cache->table, 0, (size_t)cache->tablesize * sizeof(EvddCacheData));
        cache->current_generation = 1;
    }
}

/* -------------------------------------------------------------------------
 * Operator init / done / reset / resize
 * ---------------------------------------------------------------------- */

int evdd_operator_init(int cachesize)
{
    int err;

    if (evdd_op_running)
        return 0;
    if (cachesize < 2)
        cachesize = 2;

    if ((err = EvddCache_init(&evdd_applycache, cachesize)) < 0)
        return err;
    if ((err = EvddCache_init(&evdd_scalarmulcache, cachesize)) < 0) {
        EvddCache_done(&evdd_applycache);
        return err;
    }

    evdd_op_running = 1;
    return 0;
}

void evdd_operator_done(void)
{
    if (!evdd_op_running)
        return;
    EvddCache_done(&evdd_applycache);
    EvddCache_done(&evdd_scalarmulcache);
    evdd_op_running = 0;
}

void evdd_operator_reset(void)
{
    if (!evdd_op_running)
        return;
    EvddCache_reset(&evdd_applycache);
    EvddCache_reset(&evdd_scalarmulcache);
}

int evdd_operator_resize(int cachesize)
{
    int err;

    if (!evdd_op_running)
        return evdd_operator_init(cachesize);
    if (cachesize < 2)
        cachesize = 2;

    if ((err = EvddCache_resize(&evdd_applycache, cachesize)) < 0)
        return err;
    if ((err = EvddCache_resize(&evdd_scalarmulcache, cachesize)) < 0)
        return err;
    return 0;
}

/* -------------------------------------------------------------------------
 * Apply
 * ---------------------------------------------------------------------- */

/** Path value of a constant edge: zero weight → 0, else incoming weight. */
static evdd_value_id evdd_const_value(EVDD e)
{
    if (evdd_value_is_zero(e.w))
        return EVDD_VALUE_ZERO;
    return e.w;
}

static EVDD evdd_edge_low(EVDD e)
{
    EVDD c;
    c.node = EVDDLOW(e.node);
    c.w = evdd_value_mul(e.w, EVDDWLOW(e.node));
    return c;
}

static EVDD evdd_edge_high(EVDD e)
{
    EVDD c;
    c.node = EVDDHIGH(e.node);
    c.w = evdd_value_mul(e.w, EVDDWHIGH(e.node));
    return c;
}

static unsigned int evdd_apply_hash(EVDD a, EVDD b, evdd_apply_op op)
{
    return HASH5((unsigned)a.node, (unsigned)b.node, (unsigned)(size_t)op,
                 (unsigned)a.w, (unsigned)b.w);
}

static EVDD evdd_apply_rec(EVDD a, EVDD b, evdd_apply_op op)
{
    EvddCacheData *entry;
    unsigned int hash;
    EVDD res, r0, r1;
    unsigned int la, lb;
    int opc = (int)(size_t)op;

    hash = evdd_apply_hash(a, b, op);
    entry = EvddCache_lookup(&evdd_applycache, hash);
    if (EvddCache_is_valid(&evdd_applycache, entry) && entry->a_node == a.node &&
        entry->a_w == a.w && entry->b_node == b.node && entry->b_w == b.w &&
        entry->op == opc) {
        res.node = entry->res_node;
        res.w = entry->res_w;
        return res;
    }

    if (EVDDISCONST(a.node) && EVDDISCONST(b.node)) {
        evdd_value_id vw = op(evdd_const_value(a), evdd_const_value(b));
        if (evdd_value_is_zero(vw))
            res = evdd_false();
        else {
            res.node = EVDD_TRUE_NODE;
            res.w = vw;
        }
        EvddCache_store(entry, &evdd_applycache, a.node, a.w, b.node, b.w, opc,
                        res.node, res.w);
        return res;
    }

    la = EVDDISCONST(a.node) ? EVDD_MAXLEVEL : EVDDLEVEL(a.node);
    lb = EVDDISCONST(b.node) ? EVDD_MAXLEVEL : EVDDLEVEL(b.node);

    if (la == lb) {
        r0 = evdd_apply_rec(evdd_edge_low(a), evdd_edge_low(b), op);
        EVDD_PUSHREF(r0.node);
        r1 = evdd_apply_rec(evdd_edge_high(a), evdd_edge_high(b), op);
        EVDD_PUSHREF(r1.node);
        res = evdd_makenode(la, r0, r1);
        EVDD_POPREF(2);
    } else if (la < lb) {
        r0 = evdd_apply_rec(evdd_edge_low(a), b, op);
        EVDD_PUSHREF(r0.node);
        r1 = evdd_apply_rec(evdd_edge_high(a), b, op);
        EVDD_PUSHREF(r1.node);
        res = evdd_makenode(la, r0, r1);
        EVDD_POPREF(2);
    } else {
        r0 = evdd_apply_rec(a, evdd_edge_low(b), op);
        EVDD_PUSHREF(r0.node);
        r1 = evdd_apply_rec(a, evdd_edge_high(b), op);
        EVDD_PUSHREF(r1.node);
        res = evdd_makenode(lb, r0, r1);
        EVDD_POPREF(2);
    }

    EvddCache_store(entry, &evdd_applycache, a.node, a.w, b.node, b.w, opc,
                    res.node, res.w);
    return res;
}

EVDD evdd_apply(EVDD a, EVDD b, evdd_apply_op op)
{
    if (!evddrunning) {
        bdd_error(BDD_RUNNING);
        return evdd_false();
    }
    if (!evdd_op_running) {
        printf("Error: EVDD operators not initialized\n");
        bdd_error(BDD_RUNNING);
        return evdd_false();
    }
    if (op == NULL) {
        printf("Error: NULL operation passed to evdd_apply\n");
        bdd_error(BDD_OP);
        return evdd_false();
    }
    if (a.node < 0 || a.node >= evddnodesize ||
        (EVDDISNONCONST(a.node) && EVDDISFREE(a.node)) || b.node < 0 ||
        b.node >= evddnodesize ||
        (EVDDISNONCONST(b.node) && EVDDISFREE(b.node))) {
        bdd_error(BDD_ILLBDD);
        return evdd_false();
    }

    {
        EVDD res;
        int saved = evddrefstacktop;

        /* Operands stay alive through any collection during the walk. */
        EVDD_PUSHREF(a.node);
        EVDD_PUSHREF(b.node);
        res = evdd_apply_rec(a, b, op);
        evddrefstacktop = saved;
        return res;
    }
}
