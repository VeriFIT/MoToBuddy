/**
 * @file evdd.h
 * @brief Public C interface for multiplicative EVDDs (MoToBuDDy).
 *
 * A diagram is an incoming edge (node, weight). Outgoing edges carry
 * interned ValueIDs. Independent of the 20-byte BDD/MTBDD kernel:
 * evdd_init / evdd_done may run beside bdd_init / bdd_done.
 *
 * Analogous to bdd.h (public handle). Node table type lives in
 * evdd_kernel.h, like BddNode in kernel.h.
 *
 * See evdd_kernel.h, evdd_value.h, evdd_op.h, evdd_convert.h.
 */
#ifndef EVDD_H
#define EVDD_H

#include "evdd_value.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief User-facing EVDD: incoming edge to a node.
 *
 * Not a bare node id (unlike BDD/MTBDD). Two diagrams may share the same
 * node and differ only by @c w.
 */
typedef struct s_EVDD {
    int node;            /**< Index into evddnodes[]. */
    evdd_value_id w;     /**< Incoming weight (intern table index). */
} EVDD;

/** Node-table indices for the two sinks (weights still live on EVDD.w). */
#define EVDD_FALSE_NODE 0
#define EVDD_TRUE_NODE  1

int  evdd_init(int initnodesize);
void evdd_done(void);
int  evdd_isrunning(void);

/** Constant edges: sinks with identity incoming weight. */
EVDD evdd_false(void);
EVDD evdd_true(void);

#ifdef __cplusplus
}
#endif

#endif /* EVDD_H */
