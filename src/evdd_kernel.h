/**
 * @file evdd_kernel.h
 * @brief Kernel types, macros, and globals for the EVDD package.
 *
 * Analogous to kernel.h, but bound to evddnodes[] rather than bddnodes[].
 * Do not include kernel.h macros (LOW, HIGH, LEVEL, PUSHREF) from EVDD
 * ops - they index the BDD table.
 *
 * Node layout (28 B): low, high, w_low, w_high (ValueIDs),
 * refcou/level/mark, hash, next. Unique key:
 * (level, low, high, w_low, w_high). Eager makenode stores the
 * weight-normalized pair; makenode_raw stores the raw pair.
 * Both skip-reduce when the two cofactor edges are equal.
 */

#ifndef EVDD_KERNEL_H
#define EVDD_KERNEL_H

#include "evdd.h"
#include "evdd_value.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Unique-table entry (28 B). Flat children + weights.
 *
 * Unique key: (level, low, high, w_low, w_high).
 * Public roots use EVDD; the table stays flat (no nested edge struct).
 */
typedef struct s_EvddNode {
    int low;
    int high;
    evdd_value_id w_low;
    evdd_value_id w_high;

    unsigned int refcou : 10;
    unsigned int level  : 21;
    unsigned int mark   : 1;

    int hash;
    int next;
} EvddNode;

#define EVDD_MAXLEVEL 0x1FFFFF
#define EVDD_MAXREF   0x3FF

/*
 * Same multiplicative mix and Lemire range reduction as kernel.h HASH_* /
 * HASHNODE / HASHRANGE. Duplicated (not via kernel.h) because bddio.c can
 * include both headers; reuse the shared names when kernel.h already did.
 */
#ifndef HASH_K0
#define HASH_K0 0xC13FA9A99E3779B9ULL
#define HASH_K1 0x91E10DA5C79E7B1DULL
#define HASH_K2 0xC2B2AE3D27D4EB4FULL
#define HASH_W2(a, b)                                                          \
    ((((unsigned long long)(unsigned int)(a)) << 32) | (unsigned int)(b))
#define HASH_FOLD(x) ((unsigned int)(((x) ^ ((x) << 32)) >> 32))
#define HASH1(a) ((unsigned int)(a) * (unsigned int)HASH_K0)
#define HASH2(a, b) HASH_FOLD(HASH_W2(a, b) * HASH_K0)
#define HASH3(a, b, c)                                                         \
    HASH_FOLD(HASH_W2(a, b) * HASH_K0 + (unsigned int)(c) * HASH_K1)
#define HASH4(a, b, c, d)                                                      \
    HASH_FOLD(HASH_W2(a, b) * HASH_K0 + HASH_W2(c, d) * HASH_K2)
#define HASH5(a, b, c, d, e)                                                   \
    HASH_FOLD(HASH_W2(a, b) * HASH_K0 + (unsigned int)(c) * HASH_K1 +          \
              HASH_W2(d, e) * HASH_K2)
#define HASHNODE(lvl, l, h)                                                    \
    ((unsigned int)(((HASH_W2(l, h) ^ (unsigned int)(lvl) * HASH_K1) *          \
                     HASH_K0) >>                                               \
                    32))
#endif
#ifndef HASHRANGE
#define HASHRANGE(h, n)                                                        \
    ((unsigned int)(((unsigned long long)(unsigned int)(h) *                   \
                     (unsigned int)(n)) >>                                     \
                    32))
#endif

/** Mix for unique key (level, low, high, w_low, w_high) - HASHNODE + weights. */
#define EVDD_HASHNODE(lvl, l, h, wl, wh)                                       \
    HASH_FOLD((HASH_W2(l, h) ^ (unsigned int)(lvl) * HASH_K1) * HASH_K0 +      \
              HASH_W2(wl, wh) * HASH_K2)

/** Unique-table bucket for key (level, low, high, w_low, w_high). */
#define EVDD_NODEHASH(lvl, l, h, wl, wh)                                       \
    HASHRANGE(EVDD_HASHNODE(lvl, l, h, wl, wh), evddnodesize)

/* Field access on evddnodes[] - never alias BDD LOW/HIGH/LEVEL. */
#define EVDDLEVEL(n)   (evddnodes[n].level)
#define EVDDLOW(n)     (evddnodes[n].low)
#define EVDDHIGH(n)    (evddnodes[n].high)
#define EVDDWLOW(n)    (evddnodes[n].w_low)
#define EVDDWHIGH(n)   (evddnodes[n].w_high)
#define EVDDLEVELp(p)  ((p)->level)
#define EVDDLOWp(p)    ((p)->low)
#define EVDDHIGHp(p)   ((p)->high)
#define EVDDWLOWp(p)   ((p)->w_low)
#define EVDDWHIGHp(p)  ((p)->w_high)

#define EVDDSETMARK(n)  (evddnodes[n].mark = 1)
#define EVDDUNMARK(n)   (evddnodes[n].mark = 0)
#define EVDDMARKED(n)   (evddnodes[n].mark)

#define EVDDSETMARKp(p)  ((p)->mark = 1)
#define EVDDUNMARKp(p)   ((p)->mark = 0)
#define EVDDMARKEDp(p)   ((p)->mark)

#define EVDDINCREF(n)                                                          \
    do {                                                                       \
        if (evddnodes[n].refcou < EVDD_MAXREF)                                 \
            evddnodes[n].refcou++;                                             \
    } while (0)
#define EVDDDECREF(n)                                                          \
    do {                                                                       \
        if (evddnodes[n].refcou != EVDD_MAXREF && evddnodes[n].refcou > 0)     \
            evddnodes[n].refcou--;                                             \
    } while (0)
#define EVDDHASREF(n) (evddnodes[n].refcou > 0)

#define EVDDISCONST(a)    ((a) == EVDD_TRUE_NODE)
#define EVDDISNONCONST(a) ((a) != EVDD_TRUE_NODE)
#define EVDDISONE(a)      ((a) == EVDD_TRUE_NODE)
/** Zero diagram: edge to the 1-terminal with weight ZERO. */
#define EVDDISFALSE(e)                                                         \
    (EVDDISCONST((e).node) && evdd_value_is_zero((e).w))
/** Free-list slots use low == -1 (same convention as BuDDy). */
#define EVDDISFREE(n)     (EVDDLOW(n) == -1)

extern int       evddrunning;
extern int       evddnodesize;
extern int       evddmaxnodesize;
extern EvddNode *evddnodes;
extern int       evddfreepos;
extern int       evddfreenum;
extern int       evddproduced;

void evdd_unmark(int i);

/*
 * Internal reference stack (cf. bddrefstack in kernel.h). Operations push
 * the nodes they still need while a nested evdd_makenode may run evdd_gbc;
 * evdd_gbc marks every entry. Unlike evdd_addref this never saturates, so
 * a node pushed many times is still collectable once the stack unwinds.
 * Entries may be EVDD_TRUE_NODE; marking ignores it.
 */
extern int *evddrefstack;
extern int  evddrefstacktop;  /* number of live entries */
extern int  evddrefstacksize; /* allocated entries */
void evdd_refstack_grow(void);
#define EVDD_PUSHREF(n)                                                        \
    do {                                                                       \
        if (evddrefstacktop >= evddrefstacksize)                               \
            evdd_refstack_grow();                                              \
        evddrefstack[evddrefstacktop++] = (n);                                 \
    } while (0)
#define EVDD_POPREF(k) (evddrefstacktop -= (k))

#ifdef __cplusplus
}
#endif

#endif /* EVDD_KERNEL_H */
