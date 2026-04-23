/**
 * @file mtbddop.h
 * @brief High-level MTBDD traversal and transformation operations.
 *
 * Provides composable NodeOp combinators for structural tree manipulation:
 * traversal, lockstep descent, swapping, and leaf-level transformations.
 *
 * Intended to be included from C++ translation units only.
 */

#ifndef MTBDDOP_H
#define MTBDDOP_H

#ifdef __cplusplus

#include "bdd.h"
extern "C" {
#include "mtbdd.h"
}

#include <functional>
#include <utility>

/* -------------------------------------------------------------------------
 * Core types
 * ---------------------------------------------------------------------- */

using BDDPair      = std::pair<BDD, BDD>;
using NodeOp       = std::function<BDD(BDD)>;
using BinaryNodeOp = std::function<BDDPair(BDD, BDD)>;

/**
 * @brief Defines how one side of a swap contributes and absorbs a value.
 *
 * @param put_up  Extract the value this side offers to the other side.
 * @param put_in  Rebuild the node given the original node and the received value.
 */
struct SwapParam {
    std::function<BDD(BDD)>      put_up;
    std::function<BDD(BDD, BDD)> put_in;
};

/**
 * @brief Controls which branches are followed during traversal or swap.
 *
 * Used both as a descent preference (@p pref) and as an action target
 * (@p action_on) in traversal combinators.
 */
enum class Branch {
    L,      ///< LOW  branch only
    R,      ///< HIGH branch only
    LR,     ///< Both branches, LOW first (default)
    RL,     ///< Both branches, HIGH first
    ITSELF  ///< Pass the node itself to the action
};

enum class Side { L, R };

/* -------------------------------------------------------------------------
 * Swap cache key
 * ---------------------------------------------------------------------- */

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

/* -------------------------------------------------------------------------
 * Primitives
 * ---------------------------------------------------------------------- */

/**
 * @brief Returns a NodeOp that extracts the LOW (Side::L) or HIGH (Side::R)
 *        child of a node.
 * 
 * @param s Side to extract
 */
NodeOp mtbdd_get_side(Side s);

/* -------------------------------------------------------------------------
 * Traversal
 * ---------------------------------------------------------------------- */

/**
 * @brief Descends a BDD to @p target_level and applies @p action there.
 *
 * @param target_level  Level at which @p action is fired.
 * @param action        Operation applied at @p target_level.
 * @param pref          Descent strategy:
 *                        Branch::L      - follow LOW only (HIGH unchanged)
 *                        Branch::R      - follow HIGH only (LOW  unchanged)
 *                        Branch::LR     - both, LOW first (default)
 *                        Branch::RL     - both, HIGH first
 * @param action_on     What @p action receives at @p target_level:
 *                        Branch::L      - LOW child of the target node
 *                        Branch::R      - HIGH child of the target node
 *                        Branch::ITSELF - the target node itself (default)
 */
NodeOp mtbdd_with_traverse_to(int target_level, NodeOp action,
                               Branch pref      = Branch::LR,
                               Branch action_on = Branch::ITSELF);

/**
 * @brief Descends two BDDs in lockstep to @p target_level and applies
 *        @p action to both simultaneously.
 *
 * Each side has independent descent (@p pref_L / @p pref_R) and action-target
 * (@p action_on_L / @p action_on_R) controls, mirroring the single-tree
 * traverse_to semantics.
 * 
 * @param target_level  Level at which @p action is fired.
 * @param action        Operation applied at @p target_level, taking both sides
 *                      as input and returning a pair of new nodes.
 * @param pref_L        Descent strategy for the left BDD.
 * @param action_on_L   What the left side of @p action receives at @p target_level.
 * @param pref_R        Descent strategy for the right BDD.
 * @param action_on_R   What the right side of @p action receives at @p target_level
 * 
 * @note The lockstep traversal is memoized to avoid redundant work on shared
 *       subgraphs. The memoization key includes the current nodes and their
 *       parent levels to correctly handle virtual nodes.
 */
BinaryNodeOp mtbdd_with_lockstep_to(int target_level, BinaryNodeOp action,
                                     Branch pref_L      = Branch::LR,
                                     Branch action_on_L = Branch::ITSELF,
                                     Branch pref_R      = Branch::LR,
                                     Branch action_on_R = Branch::ITSELF);

/* -------------------------------------------------------------------------
 * Swap
 * ---------------------------------------------------------------------- */

/**
 * @brief Constructs a BinaryNodeOp that exchanges values between two nodes
 *        according to @p paramL and @p paramR.
 *
 * Each side uses its own SwapParam to define what it offers (put_up) and
 * how it integrates what it receives (put_in).
 * 
 * @param paramL SwapParam for the left side.
 * @param paramR SwapParam for the right side.
 */
BinaryNodeOp mtbdd_make_swap(SwapParam paramL, SwapParam paramR);

/**
 * @brief Simple swap: returns a NodeOp that exchanges LOW and HIGH children.
 */
NodeOp mtbdd_make_swap();

#endif /* __cplusplus */
#endif /* MTBDDOP_H   */

/* EOF mtbddop.h */