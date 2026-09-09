/**
 * @file evdd_kernel.c
 * @brief EVDD kernel - init/done, unique table, makenode, GC, refs, vars.
 *
 * Stripped counterpart of kernel.c for evddnodes. No pairs, reorder, FDD,
 * or mtbdd_delete_terminal. Eager evdd_makenode is the default; unreduced
 * alloc and user-called evdd_reduce live here as well.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bdd.h"
#include "evdd_kernel.h"
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

EVDD evdd_false(void)
{
    EVDD e;
    e.node = EVDD_FALSE_NODE;
    e.w = EVDD_VALUE_ONE;
    return e;
}

EVDD evdd_true(void)
{
    EVDD e;
    e.node = EVDD_TRUE_NODE;
    e.w = EVDD_VALUE_ONE;
    return e;
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

    if (initnodesize < 3)
        initnodesize = 3;

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
        EVDDWLOW(n) = EVDD_VALUE_ONE;
        EVDDWHIGH(n) = EVDD_VALUE_ONE;
        evddnodes[n].hash = 0;
        evddnodes[n].next = n + 1;
    }
    evddnodes[evddnodesize - 1].next = 0;

    /* Pin sinks: 0 = zero, 1 = T1 (like bddfalse / bddtrue). */
    evddnodes[EVDD_FALSE_NODE].refcou = EVDD_MAXREF;
    evddnodes[EVDD_TRUE_NODE].refcou = EVDD_MAXREF;
    EVDDLEVEL(EVDD_FALSE_NODE) = EVDD_MAXLEVEL;
    EVDDLEVEL(EVDD_TRUE_NODE) = EVDD_MAXLEVEL;
    EVDDLOW(EVDD_FALSE_NODE) = EVDDHIGH(EVDD_FALSE_NODE) = EVDD_FALSE_NODE;
    EVDDLOW(EVDD_TRUE_NODE) = EVDDHIGH(EVDD_TRUE_NODE) = EVDD_TRUE_NODE;
    EVDDWLOW(EVDD_FALSE_NODE) = EVDDWHIGH(EVDD_FALSE_NODE) = EVDD_VALUE_ONE;
    EVDDWLOW(EVDD_TRUE_NODE) = EVDDWHIGH(EVDD_TRUE_NODE) = EVDD_VALUE_ONE;
    evddnodes[EVDD_FALSE_NODE].hash = 0;
    evddnodes[EVDD_TRUE_NODE].hash = 0;
    evddnodes[EVDD_FALSE_NODE].next = 0;
    evddnodes[EVDD_TRUE_NODE].next = 0;

    evddfreepos = 2;
    evddfreenum = evddnodesize - 2;
    evddproduced = 0;
    evddmaxnodesize = 0;
    evddrunning = 1;
    return 0;
}

void evdd_done(void)
{
    if (!evddrunning && evddnodes == NULL)
        return;

    free(evddnodes);
    evdd_clear_table_state();
}
