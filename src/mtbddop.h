/**
 * @file mtbddop.h
 * 
 * @brief High-level MTBDD traversal and transformation operations
 *
 * Provides composable NodeOp combinators for structural tree manipulation:
 * traversal, swapping, and leaf-level transformations.
 *
 * Intended to be included from C++ translation units only.
 */

#ifndef MTBDDOP_H
#define MTBDDOP_H

#ifdef __cplusplus


#include "bdd.h"
#ifdef __cplusplus
extern "C" {
#endif
#include "mtbdd.h"
#ifdef __cplusplus
}
#endif

#include <functional>

/* Types */

using NodeOp = std::function<BDD(BDD)>;

struct BDDPair { BDD first; BDD second; };

/**
 * Defines how one side of a swap contributes and absorbs a value.
 * put_up: extract the value to offer to the other side.
 * put_in: given the original node and received value, return rebuilt node.
 */
struct SwapParam {
    std::function<BDD(BDD)>      put_up;
    std::function<BDD(BDD, BDD)> put_in;
};

/**
 * Controls which branches are followed during traversal or swap.
 * Used both as descent preference (pref) and action target (action_on).
 */
enum class Branch {
    L,       /* LOW  branch only                        */
    R,       /* HIGH branch only                        */
    LR,      /* both branches, LOW first (default)      */
    RL,      /* both branches, HIGH first               */
    ITSELF   /* pass the node itself to the action      */
};

enum class Side { L, R };

/* Primitives */

/**
 * Returns a NodeOp that extracts the LOW (Side::L) or HIGH (Side::R)
 * child of a node.
 */
NodeOp mtbdd_get_side(Side s);

/* Traversal */

/**
 * Descends a BDD to target_level and applies action there.
 *
 * @param target_level  Level at which action is fired.
 * @param action        Operation applied at target_level.
 * @param pref          Descent strategy:
 *                        Branch::L      -- follow LOW only  (HIGH unchanged)
 *                        Branch::R      -- follow HIGH only (LOW  unchanged)
 *                        Branch::LR     -- both, LOW first  (default)
 *                        Branch::RL     -- both, HIGH first
 * @param action_on     What action receives at target_level:
 *                        Branch::L      -- LOW  child of target node
 *                        Branch::R      -- HIGH child of target node
 *                        Branch::ITSELF -- the target node itself (default)
 */
NodeOp mtbdd_traverse_to(int target_level, NodeOp action,
                                 Branch pref      = Branch::LR,
                                 Branch action_on = Branch::ITSELF);

/* Swap */

/**
 * Walks the LOW and HIGH subtrees of a node in lockstep to target_level,
 * then exchanges values using the SwapParam strategies.
 *
 * @param target_level  Level at which the swap fires.
 * @param paramL        Strategy for the LOW  subtree (put_up / put_in).
 * @param paramR        Strategy for the HIGH subtree (put_up / put_in).
 *
 * @see SwapParam, mtbdd_traverse_to
 */
NodeOp mtbdd_swap(int target_level, SwapParam paramL, SwapParam paramR);

/**
 * @brief Simple swap: returns a node with LOW and HIGH children exchanged.
 */
NodeOp mtbdd_swap();

#endif /* __cplusplus */
#endif /* MTBDDOP_H   */

/* EOF mtbddop.h */