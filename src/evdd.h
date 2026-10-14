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

#include <stdio.h>

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

/**
 * Eager makenode: weight-normalize outgoing edges, then unique-table
 * lookup/insert on (level, low, high, w_low', w_high').
 * Returns edge (incoming_factor, node).
 */
EVDD evdd_makenode(unsigned int level, EVDD low, EVDD high);

/**
 * Like makenode but skips weight-normalization. Still unique-lookups on
 * the raw (level, low, high, w_low, w_high). Diagram is not canonical
 * until @c evdd_normalize.
 */
EVDD evdd_makenode_raw(unsigned int level, EVDD low, EVDD high);

/**
 * Diagram normalize: bottom-up weight-normalize every node and re-unique.
 * Same function as building with @c evdd_makenode throughout.
 * Distinct from @c evdd_value_normalize (local weight rewrite only).
 */
EVDD evdd_normalize(EVDD root);

/**
 * Increment the reference count of a node.
*/
EVDD evdd_addref(EVDD root);

/**
 * Decrement the reference count of a node.
*/
EVDD evdd_delref(EVDD root);

/**
 * Garbage-collect unreferenced EVDD nodes.
 *
 * Marks from external roots (refcou > 0; sinks are MAXREF), sweeps the
 * rest onto the freelist, and rebuilds the unique-table hash.
 */
void evdd_gbc(void);

/**
 * Print @p root as Graphviz DOT (implemented in bddio.c).
 * Nodes = levels, dashed = low, solid = high, edge labels via to_str.
 * Invisible root edge carries the incoming weight.
 *
 * If @p hide_zeros is non-zero, omit zero-weight edges and the false (0)
 * terminal; edges into the 0-node are not drawn either.
 */
void evdd_printdot(EVDD root, int hide_zeros);
void evdd_fprintdot(FILE *ofile, EVDD root, int hide_zeros);
int  evdd_fnprintdot(char *fname, EVDD root, int hide_zeros);

#ifdef __cplusplus
}
#endif

#endif /* EVDD_H */
