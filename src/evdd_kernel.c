/**
 * @file evdd_kernel.c
 * @brief EVDD kernel - init/done, unique table, makenode, GC, refs, vars.
 *
 * Stripped counterpart of kernel.c for evddnodes. No pairs, reorder, FDD,
 * or mtbdd_delete_terminal.
 *
 * makenode: weight-normalize then unique.
 * makenode_raw: unique only (no weight-normalize);.
 * evdd_normalize: DAG pass that weight-normalizes and re-uniques.
 */

#include <stdlib.h>
#include <string.h>

#include "bdd.h"
#include "evdd_kernel.h"
#include "evdd_op.h"
#include "evdd_value.h"
#include "prime.h"

extern int bdd_error(int);

int       evddrunning = 0;
int       evddnodesize = 0;
int       evddmaxnodesize = 0;
EvddNode *evddnodes = NULL;
int       evddfreepos = 0;
int       evddfreenum = 0;
int       evddproduced = 0;

static void evdd_clear_table_state(void)
{
    evddnodes = NULL;
    evddnodesize = 0;
    evddmaxnodesize = 0;
    evddfreepos = 0;
    evddfreenum = 0;
    evddproduced = 0;
    evddrunning = 0;
}

EVDD evdd_const(evdd_value_id w)
{
    EVDD e;
    e.node = EVDD_TRUE_NODE;
    e.w = w;
    return e;
}

EVDD evdd_false(void)
{
    return evdd_const(EVDD_VALUE_ZERO);
}

int evdd_isrunning(void)
{
    return evddrunning;
}

int evdd_init(int initnodesize)
{
    int n;
    int size;

    if (evddrunning)
        return bdd_error(BDD_RUNNING);

    if (initnodesize < 2)
        initnodesize = 2;

    size = bdd_prime_gte(initnodesize);
    evddnodes = (EvddNode *)calloc((size_t)size, sizeof(EvddNode));
    if (evddnodes == NULL)
        return bdd_error(BDD_MEMORY);

    evddnodesize = size;

    for (n = 0; n < evddnodesize; n++) {
        evddnodes[n].refcou = 0;
        evddnodes[n].level = 0;
        evddnodes[n].mark = 0;
        EVDDLOW(n) = -1;
        EVDDHIGH(n) = 0;
        EVDDWLOW(n) = EVDD_VALUE_ZERO;
        EVDDWHIGH(n) = EVDD_VALUE_ZERO;
        evddnodes[n].hash = 0;
        evddnodes[n].next = n + 1;
    }
    evddnodes[evddnodesize - 1].next = 0;

    /* Sole sink: 1-terminal. Zero is (TRUE, ZERO), not a second node. */
    evddnodes[EVDD_TRUE_NODE].refcou = EVDD_MAXREF;
    EVDDLEVEL(EVDD_TRUE_NODE) = EVDD_MAXLEVEL;
    EVDDLOW(EVDD_TRUE_NODE) = EVDDHIGH(EVDD_TRUE_NODE) = EVDD_TRUE_NODE;
    EVDDWLOW(EVDD_TRUE_NODE) = EVDDWHIGH(EVDD_TRUE_NODE) = EVDD_VALUE_ZERO;
    evddnodes[EVDD_TRUE_NODE].hash = 0;
    evddnodes[EVDD_TRUE_NODE].next = 0;

    evddfreepos = 1;
    evddfreenum = evddnodesize - 1;
    evddproduced = 0;
    evddmaxnodesize = 0;
    evddrunning = 1;

    {
        int err = evdd_operator_init(size);
        if (err < 0) {
            free(evddnodes);
            evdd_clear_table_state();
            return err;
        }
    }
    return 0;
}

void evdd_done(void)
{
    if (!evddrunning && evddnodes == NULL)
        return;

    evdd_operator_done();
    free(evddnodes);
    evdd_clear_table_state();
}

/* -------------------------------------------------------------------------
 * Unique table / makenode
 * ---------------------------------------------------------------------- */

#define EVDD_DEFAULTMAXNODEINC 50000

static void evdd_rehash(void)
{
    int n;

    for (n = 0; n < evddnodesize; n++)
        evddnodes[n].hash = 0;

    for (n = evddnodesize - 1; n >= 1; n--) {
        unsigned int hash;

        if (EVDDISFREE(n))
            continue;

        hash = EVDD_NODEHASH(EVDDLEVEL(n), EVDDLOW(n), EVDDHIGH(n), EVDDWLOW(n),
                             EVDDWHIGH(n));
        evddnodes[n].next = evddnodes[hash].hash;
        evddnodes[hash].hash = n;
    }
}

static int evdd_noderesize(void)
{
    EvddNode *newnodes;
    int oldsize = evddnodesize;
    int n;

    if (evddnodesize >= evddmaxnodesize && evddmaxnodesize > 0)
        return -1;

    evddnodesize = evddnodesize << 1;

    if (evddnodesize > oldsize + EVDD_DEFAULTMAXNODEINC)
        evddnodesize = oldsize + EVDD_DEFAULTMAXNODEINC;

    if (evddnodesize > evddmaxnodesize && evddmaxnodesize > 0)
        evddnodesize = evddmaxnodesize;

    evddnodesize = bdd_prime_lte(evddnodesize);
    if (evddnodesize <= oldsize)
        return -1;

    newnodes = (EvddNode *)realloc(evddnodes, sizeof(EvddNode) * (size_t)evddnodesize);
    if (newnodes == NULL) {
        evddnodesize = oldsize;
        return bdd_error(BDD_MEMORY);
    }
    evddnodes = newnodes;

    for (n = 0; n < oldsize; n++)
        evddnodes[n].hash = 0;

    for (n = oldsize; n < evddnodesize; n++) {
        evddnodes[n].refcou = 0;
        evddnodes[n].level = 0;
        evddnodes[n].mark = 0;
        EVDDLOW(n) = -1;
        EVDDHIGH(n) = 0;
        EVDDWLOW(n) = EVDD_VALUE_ZERO;
        EVDDWHIGH(n) = EVDD_VALUE_ZERO;
        evddnodes[n].hash = 0;
        evddnodes[n].next = n + 1;
    }
    evddnodes[evddnodesize - 1].next = evddfreepos;
    evddfreepos = oldsize;
    evddfreenum += evddnodesize - oldsize;

    evdd_rehash();
    return 0;
}

EVDD evdd_makenode(unsigned int level, EVDD low, EVDD high)
{
    evddNormResult normalized_result = evdd_value_normalize(low.w, high.w);
    EVDD newnode;

    if (evdd_value_is_zero(normalized_result.incoming))
        return evdd_false();

    low.w = normalized_result.w_low;
    high.w = normalized_result.w_high;
    newnode = evdd_makenode_raw(level, low, high);
    newnode.w = evdd_value_mul(normalized_result.incoming, newnode.w);
    return newnode;
}

EVDD evdd_makenode_raw(unsigned int level, EVDD low, EVDD high)
{
    unsigned int hash;
    int res;
    int lchild = low.node;
    int hchild = high.node;
    evdd_value_id wl = low.w;
    evdd_value_id wh = high.w;
    EvddNode *node;
    EVDD out;

    if (!evddrunning) {
        bdd_error(BDD_RUNNING);
        return evdd_false();
    }

    if (lchild == hchild && wl == wh) {
        out.node = lchild;
        out.w = wl;
        return out;
    }

    /* Find the node in the unique table. */
    hash = EVDD_NODEHASH(level, lchild, hchild, wl, wh);
    res = evddnodes[hash].hash;
    while (res != 0) {
        if (EVDDLEVEL(res) == level && EVDDLOW(res) == lchild &&
            EVDDHIGH(res) == hchild && EVDDWLOW(res) == wl &&
            EVDDWHIGH(res) == wh) {
            out.node = res;
            out.w = evdd_value_unit_from_pair(wl, wh);
            return out;
        }
        res = evddnodes[res].next;
    }

    if (evddfreepos == 0) {
        evdd_gbc();
        if (evddfreepos == 0)
            evdd_noderesize();
        hash = EVDD_NODEHASH(level, lchild, hchild, wl, wh);
        if (evddfreepos == 0) {
            bdd_error(BDD_NODENUM);
            return evdd_false();
        }
    }

    res = evddfreepos;
    evddfreepos = evddnodes[evddfreepos].next;
    evddfreenum--;
    evddproduced++;

    node = &evddnodes[res];
    node->level = level;
    node->low = lchild;
    node->high = hchild;
    node->w_low = wl;
    node->w_high = wh;
    node->refcou = 0;
    node->mark = 0;
    node->next = evddnodes[hash].hash;
    evddnodes[hash].hash = res;

    out.node = res;
    out.w = evdd_value_unit_from_pair(wl, wh);
    return out;
}

typedef struct {
    int set;
    EVDD e;
} EvddNormMemo;

static EVDD evdd_normalize_node(int node, EvddNormMemo *memo)
{
    EVDD low_e, high_e, out;
    int low_n, high_n;

    if (EVDDISCONST(node)) {
        /* Caller must not rely on .w; const children are folded in the parent. */
        out.node = node;
        out.w = EVDD_VALUE_ZERO;
        return out;
    }

    if (memo[node].set)
        return memo[node].e;

    low_n = EVDDLOW(node);
    high_n = EVDDHIGH(node);

    if (EVDDISCONST(low_n)) {
        low_e.node = low_n;
        low_e.w = EVDDWLOW(node);
    } else {
        low_e = evdd_normalize_node(low_n, memo);
        low_e.w = evdd_value_mul(EVDDWLOW(node), low_e.w);
    }
    /* Low must stay marked while the high cofactor (and its makenodes) runs. */
    evdd_addref(low_e);

    if (EVDDISCONST(high_n)) {
        high_e.node = high_n;
        high_e.w = EVDDWHIGH(node);
    } else {
        high_e = evdd_normalize_node(high_n, memo);
        high_e.w = evdd_value_mul(EVDDWHIGH(node), high_e.w);
    }
    evdd_addref(high_e);

    out = evdd_makenode(EVDDLEVEL(node), low_e, high_e);
    evdd_delref(low_e);
    evdd_delref(high_e);

    /* Memo keeps the result alive until evdd_normalize finishes the walk.
     * makenode leaves refcou at 0, and evdd_gbc does not clear this memo. */
    evdd_addref(out);
    memo[node].e = out;
    memo[node].set = 1;
    return out;
}

EVDD evdd_normalize(EVDD root)
{
    EvddNormMemo *memo;
    EVDD out;
    int n;

    if (!evddrunning) {
        bdd_error(BDD_RUNNING);
        return evdd_false();
    }

    if (root.node < 0 || root.node >= evddnodesize ||
        (EVDDISNONCONST(root.node) && EVDDISFREE(root.node))) {
        bdd_error(BDD_ILLBDD);
        return evdd_false();
    }

    if (EVDDISCONST(root.node))
        return root;

    /* Memo only needs slots for nodes that existed when the walk started. */
    n = evddnodesize;
    memo = (EvddNormMemo *)calloc((size_t)n, sizeof(EvddNormMemo));
    if (memo == NULL) {
        bdd_error(BDD_MEMORY);
        return evdd_false();
    }

    /* Pin the input diagram so a collection cannot reuse nodes still being read. */
    evdd_addref(root);
    out = evdd_normalize_node(root.node, memo);
    out.w = evdd_value_mul(root.w, out.w);
    for (int i = 0; i < n; i++) {
        if (memo[i].set)
            evdd_delref(memo[i].e);
    }
    evdd_delref(root);
    free(memo);
    return out;
}

EVDD evdd_addref(EVDD root)
{   
   if (EVDDISCONST(root.node) || !evddrunning)
      return root;
   if (root.node >= evddnodesize)
   {
      bdd_error(BDD_ILLBDD);
      return evdd_false();
   }
   if (EVDDLOW(root.node) == -1)
   {
      bdd_error(BDD_ILLBDD);
      return evdd_false();
   }

   EVDDINCREF(root.node);
   return root;
}

EVDD evdd_delref(EVDD root)
{   
   if (EVDDISCONST(root.node) || !evddrunning)
      return root;
   if (root.node >= evddnodesize)
   {
      bdd_error(BDD_ILLBDD);
      return evdd_false();
   }
   if (EVDDLOW(root.node) == -1)
   {
      bdd_error(BDD_ILLBDD);
      return evdd_false();
   }

   EVDDDECREF(root.node);
   return root;
}

static void evdd_mark(int i)
{
    EvddNode *node;

    if (EVDDISCONST(i))
        return;

    node = &evddnodes[i];
    if (EVDDMARKEDp(node) || EVDDLOWp(node) == -1)
        return;

    EVDDSETMARKp(node);
    evdd_mark(EVDDLOWp(node));
    evdd_mark(EVDDHIGHp(node));
}

void evdd_unmark(int i)
{
    EvddNode *node;

    if (EVDDISCONST(i))
        return;

    node = &evddnodes[i];
    if (!EVDDMARKEDp(node) || EVDDLOWp(node) == -1)
        return;
    EVDDUNMARKp(node);

    evdd_unmark(EVDDLOWp(node));
    evdd_unmark(EVDDHIGHp(node));
}

void evdd_gbc(void)
{
    int n;

    if (!evddrunning)
        return;

    /* Mark from external roots. Terminal stays alive via MAXREF. */
    for (n = 0; n < evddnodesize; n++) {
        if (evddnodes[n].refcou > 0)
            evdd_mark(n);
        evddnodes[n].hash = 0;
    }

    evddfreepos = 0;
    evddfreenum = 0;

    for (n = evddnodesize - 1; n >= 1; n--) {
        EvddNode *node = &evddnodes[n];

        // marked and not free, keep in the table
        if (EVDDMARKEDp(node) && EVDDLOWp(node) != -1) {
            unsigned int hash;

            EVDDUNMARKp(node);
            hash = EVDD_NODEHASH(EVDDLEVELp(node), EVDDLOWp(node),
                                 EVDDHIGHp(node), EVDDWLOWp(node),
                                 EVDDWHIGHp(node));
            node->next = evddnodes[hash].hash;
            evddnodes[hash].hash = n;
        } 
        // not marked or free, free the slot
        else {
            EVDDLOWp(node) = -1;
            EVDDHIGHp(node) = 0;
            EVDDWLOWp(node) = EVDD_VALUE_ZERO;
            EVDDWHIGHp(node) = EVDD_VALUE_ZERO;
            EVDDLEVELp(node) = 0;
            node->refcou = 0;
            node->mark = 0;
            node->next = evddfreepos;
            evddfreepos = n;
            evddfreenum++;
        }
    }

    evdd_operator_reset();
}
