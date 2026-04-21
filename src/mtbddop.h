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
    BDD (*put_up)(BDD);
    BDD (*put_in)(BDD, BDD);
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

/* Swap cache */

struct SwapCacheKey {
    int    target_level;
    BDD  (*put_up_L)(BDD);
    BDD  (*put_in_L)(BDD, BDD);
    BDD  (*put_up_R)(BDD);
    BDD  (*put_in_R)(BDD, BDD);
    BDD    L;
    BDD    R;
    bool operator==(const SwapCacheKey& o) const {
        return target_level == o.target_level
            && put_up_L     == o.put_up_L
            && put_in_L     == o.put_in_L
            && put_up_R     == o.put_up_R
            && put_in_R     == o.put_in_R
            && L            == o.L
            && R            == o.R;
    }
};

struct SwapCacheKeyHash {
    std::size_t operator()(const SwapCacheKey& k) const noexcept {
        std::size_t h = 0;
        auto mix = [&](std::size_t v) {
            h ^= v + 0x9e3779b9 + (h << 6) + (h >> 2);
        };
        mix(std::hash<int>{}(k.target_level));
        mix(std::hash<void*>{}((void*)k.put_up_L));
        mix(std::hash<void*>{}((void*)k.put_in_L));
        mix(std::hash<void*>{}((void*)k.put_up_R));
        mix(std::hash<void*>{}((void*)k.put_in_R));
        mix(std::hash<int>{}(k.L));
        mix(std::hash<int>{}(k.R));
        return h;
    }
};

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