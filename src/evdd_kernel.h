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
 * weight-normalized pair; makenode_raw stores the raw pair. No
 * ROBDD skip-reduction.
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

/* Hash mix (same formulas as BuDDy PAIR/TRIPLE; do not include kernel.h). */
#define EVDD_PAIR(a, b)                                                        \
    ((unsigned int)((((unsigned int)(a)) + ((unsigned int)(b))) *              \
                        (((unsigned int)(a)) + ((unsigned int)(b)) + 1u) / 2u + \
                    ((unsigned int)(a))))
#define EVDD_TRIPLE(a, b, c)                                                   \
    ((unsigned int)(EVDD_PAIR((unsigned int)(c), EVDD_PAIR((a), (b)))))

/** Unique-table bucket for key (level, low, high, w_low, w_high). */
#define EVDD_NODEHASH(lvl, l, h, wl, wh)                                       \
    ((((EVDD_PAIR(EVDD_TRIPLE((lvl), (l), (h)),                                \
                  EVDD_PAIR((unsigned int)(wl), (unsigned int)(wh)))) %        \
       (unsigned int)evddnodesize) +                                           \
      2u) %                                                                    \
     (unsigned int)evddnodesize)

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

#define EVDDISCONST(a)    ((a) < 2)
#define EVDDISNONCONST(a) ((a) >= 2)
#define EVDDISONE(a)      ((a) == EVDD_TRUE_NODE)
#define EVDDISZERO(a)     ((a) == EVDD_FALSE_NODE)
/** Free-list slots use low == -1 (same convention as BuDDy). */
#define EVDDISFREE(n)     (EVDDLOW(n) == -1)

extern int       evddrunning;
extern int       evddnodesize;
extern int       evddmaxnodesize;
extern EvddNode *evddnodes;
extern int       evddfreepos;
extern int       evddfreenum;
extern int       evddproduced;

#ifdef __cplusplus
}
#endif

#endif /* EVDD_KERNEL_H */
