/**
 * @file mtbddop.cxx
 * @author Filip Novak
 *
 * Implementation of MTBDD traversal, lockstep, and swap operations.
 *
 * Note: per-combinator op-result caches were removed. Nesting traverse/lockstep
 * NodeOps caused use-after-free when an outer frame kept a lookup `entry` into
 * an inner cache table after BddCache_done (valgrind). Leaking those tables
 * avoided the UAF but OOMed large circuits. Structural gates are correct
 * without the op cache; BuDDy's global apply caches remain.
 */

#include "mtbddop.h"

#include <assert.h>
#include "prime.h"
#include "mtbdd_cache_registry.h"
#include "cache.h"

extern "C" void mtbdd_owned_cache_flush(void)
{
    /* no deferred combinator caches */
}

/* -------------------------------------------------------------------------
 * Primitives
 * ---------------------------------------------------------------------- */

NodeOp mtbdd_get_side(Side s) {
    return [=](BDD node) -> BDD {
        return s == Side::L ? LOW(node) : HIGH(node);
    };
}

/* -------------------------------------------------------------------------
 * Single-tree traversal
 * ---------------------------------------------------------------------- */

NodeOp mtbdd_with_traverse_to(int target_level,
                              NodeOp action,
                              Branch pref,
                              Branch action_on) {

    return [=](BDD root) -> BDD {
        auto traverse = [&](auto& self, BDD node, int parent_level) -> BDD {
            BDD working_node = node;

            if ((int)LEVEL(node) > target_level || ISCONST(node)) {
                checkSameChildren = 0;
                if (pref == Branch::LR || pref == Branch::RL) {
                    working_node = bdd_makenode(target_level, node, node);
                } else {
                    working_node = bdd_makenode(parent_level + 1, node, node);
                }
                checkSameChildren = 1;
                PUSHREF(working_node);
            }

            BDD res;

            // --- Action at target level ---
            if ((int)LEVEL(working_node) == target_level) {
                if (action_on == Branch::L) {
                    PUSHREF(HIGH(working_node));
                    PUSHREF(action(LOW(working_node)));
                    res = bdd_makenode(LEVEL(working_node),
                                       READREF(1),
                                       READREF(2));
                    POPREF(2);
                } else if (action_on == Branch::R) {
                    PUSHREF(LOW(working_node));
                    PUSHREF(action(HIGH(working_node)));
                    res = bdd_makenode(LEVEL(working_node),
                                       READREF(2),
                                       READREF(1));
                    POPREF(2);
                } else { // Branch::ITSELF
                    res = action(working_node);
                }
                // Keep res live across virtual-node POPREF (refcount starts at 0).
                PUSHREF(res);
                if (node != working_node) {
                    BDD kept = READREF(1);
                    POPREF(2); // kept + virtual working_node
                    PUSHREF(kept);
                    res = kept;
                }
                POPREF(1);
                return res;
            }

            // --- Descent ---
            if (pref == Branch::R) {
                PUSHREF(LOW(working_node));
                PUSHREF(self(self, HIGH(working_node), (int)LEVEL(working_node)));
                res = bdd_makenode(LEVEL(working_node),
                                   READREF(2),
                                   READREF(1));
                POPREF(2);
            } else if (pref == Branch::L) {
                PUSHREF(HIGH(working_node));
                PUSHREF(self(self, LOW(working_node), (int)LEVEL(working_node)));
                res = bdd_makenode(LEVEL(working_node),
                                   READREF(1),
                                   READREF(2));
                POPREF(2);
            } else if (pref == Branch::RL) {
                PUSHREF(self(self, HIGH(working_node), (int)LEVEL(working_node)));
                PUSHREF(self(self, LOW(working_node),  (int)LEVEL(working_node)));
                res = bdd_makenode(LEVEL(working_node), READREF(1), READREF(2));
                POPREF(2);
            } else { // Branch::LR
                PUSHREF(self(self, LOW(working_node),  (int)LEVEL(working_node)));
                PUSHREF(self(self, HIGH(working_node), (int)LEVEL(working_node)));
                res = bdd_makenode(LEVEL(working_node), READREF(2), READREF(1));
                POPREF(2);
            }

            PUSHREF(res);
            if (node != working_node) {
                BDD kept = READREF(1);
                POPREF(2); // kept + virtual working_node
                PUSHREF(kept);
                res = kept;
            }
            POPREF(1);
            return res;
        };

        int root_level = (ISTERMINAL(root) || ISCONST(root))
                         ? bdd_varnum()
                         : (int)LEVEL(root);
        return traverse(traverse, root, root_level - 1);
    };
}

/* -------------------------------------------------------------------------
 * Lockstep over two trees
 * ---------------------------------------------------------------------- */

BinaryNodeOp mtbdd_with_lockstep_to(int target_level,
                                    BinaryNodeOp action,
                                    Branch pref_L,
                                    Branch action_on_L,
                                    Branch pref_R,
                                    Branch action_on_R) {
    std::function<BDDPair(BDD, BDD)> fn =
        [=](BDD L_root, BDD R_root) -> BDDPair {

        auto virt_node = [&](BDD node, int parent_lv, Branch pref) -> BDD {
            if (!ISCONST(node) && (int)LEVEL(node) <= target_level)
                return node;
            int virt_lv = (pref == Branch::LR || pref == Branch::RL)
                          ? target_level
                          : parent_lv + 1;
            checkSameChildren = 0;
            BDD w = bdd_makenode(virt_lv, node, node);
            checkSameChildren = 1;
            return w;
        };

        auto lockstep = [&](auto& self,
                            BDD L, BDD R,
                            int parent_lv_L,
                            int parent_lv_R) -> BDDPair {
            // --- Virtualize ---
            BDD wL = (ISCONST(L) || (int)LEVEL(L) > target_level)
                     ? virt_node(L, parent_lv_L, pref_L)
                     : L;
            bool virt_L = (wL != L);
            if (virt_L) PUSHREF(wL);

            BDD wR = (ISCONST(R) || (int)LEVEL(R) > target_level)
                     ? virt_node(R, parent_lv_R, pref_R)
                     : R;
            bool virt_R = (wR != R);
            if (virt_R) PUSHREF(wR);

            BDDPair res;

            if ((int)LEVEL(wL) == target_level &&
                (int)LEVEL(wR) == target_level) {

                if (action_on_L == Branch::ITSELF &&
                    action_on_R == Branch::ITSELF) {
                    res = action(wL, wR);
                } else {
                    BDD in_L = (action_on_L == Branch::L) ? LOW(wL)
                              : (action_on_L == Branch::R) ? HIGH(wL)
                              : wL;
                    BDD in_R = (action_on_R == Branch::L) ? LOW(wR)
                            : (action_on_R == Branch::R) ? HIGH(wR)
                            : wR;

                    auto out_pair = action(in_L, in_R);
                    PUSHREF(out_pair.first);
                    PUSHREF(out_pair.second);

                    BDD new_wL = wL;
                    BDD new_wR = wR;

                    if (action_on_L == Branch::L) {
                        new_wL = bdd_makenode(target_level,
                                              READREF(2),
                                              HIGH(wL));
                    } else if (action_on_L == Branch::R) {
                        new_wL = bdd_makenode(target_level,
                                              LOW(wL),
                                              READREF(2));
                    }

                    PUSHREF(new_wL);

                    if (action_on_R == Branch::L) {
                        new_wR = bdd_makenode(target_level,
                                              READREF(2),
                                              HIGH(wR));
                    } else if (action_on_R == Branch::R) {
                        new_wR = bdd_makenode(target_level,
                                              LOW(wR),
                                              READREF(2));
                    }

                    PUSHREF(new_wR);
                    new_wL = READREF(2);
                    new_wR = READREF(1);
                    POPREF(4);
                    res = BDDPair(new_wL, new_wR);
                }
            }
            else if (LEVEL(wL) == LEVEL(wR)) {
                int lv = LEVEL(wL);

                BDD lo_L = (pref_L == Branch::R) ? wL : LOW(wL);
                BDD hi_L = (pref_L == Branch::L) ? wL : HIGH(wL);
                BDD lo_R = (pref_R == Branch::R) ? wR : LOW(wR);
                BDD hi_R = (pref_R == Branch::L) ? wR : HIGH(wR);

                auto lo = self(self, lo_L, lo_R, lv, lv);
                PUSHREF(lo.first);
                PUSHREF(lo.second);
                auto hi = self(self, hi_L, hi_R, lv, lv);

                PUSHREF(hi.first);
                PUSHREF(hi.second);
                BDD out_L = bdd_makenode(lv, READREF(4), READREF(2));
                PUSHREF(out_L);
                BDD out_R = bdd_makenode(lv, READREF(4), READREF(2));
                out_L = READREF(1);
                POPREF(5);
                res = BDDPair(out_L, out_R);
            }
            else if (LEVEL(wL) < LEVEL(wR)) {
                int lv = LEVEL(wL);

                BDD lo_L = (pref_L == Branch::R) ? wL : LOW(wL);
                BDD hi_L = (pref_L == Branch::L) ? wL : HIGH(wL);

                auto lo = self(self, lo_L, R, lv, parent_lv_R);
                PUSHREF(lo.first);
                PUSHREF(lo.second);
                auto hi = self(self, hi_L, R, lv, parent_lv_R);

                PUSHREF(hi.first);
                PUSHREF(hi.second);
                BDD out_L = bdd_makenode(lv, READREF(4), READREF(2));
                PUSHREF(out_L);
                BDD out_R = bdd_makenode(lv, READREF(4), READREF(2));
                out_L = READREF(1);
                POPREF(5);
                res = BDDPair(out_L, out_R);
            }
            else {
                int lv = LEVEL(wR);

                BDD lo_R = (pref_R == Branch::R) ? wR : LOW(wR);
                BDD hi_R = (pref_R == Branch::L) ? wR : HIGH(wR);

                auto lo = self(self, L, lo_R, parent_lv_L, lv);
                PUSHREF(lo.first);
                PUSHREF(lo.second);
                auto hi = self(self, L, hi_R, parent_lv_L, lv);

                PUSHREF(hi.first);
                PUSHREF(hi.second);
                BDD out_L = bdd_makenode(lv, READREF(4), READREF(2));
                PUSHREF(out_L);
                BDD out_R = bdd_makenode(lv, READREF(4), READREF(2));
                out_L = READREF(1);
                POPREF(5);
                res = BDDPair(out_L, out_R);
            }

            // Lift results above virt slots before discarding virt nodes.
            {
                PUSHREF(res.first);
                PUSHREF(res.second);
                BDD r1 = READREF(2);
                BDD r2 = READREF(1);
                POPREF(2);
                if (virt_R) POPREF(1);
                if (virt_L) POPREF(1);
                PUSHREF(r1);
                PUSHREF(r2);
                res = BDDPair(r1, r2);
            }

            POPREF(2);
            return res;
        };

        auto root_level = [](BDD n) -> int {
            return (ISTERMINAL(n) || ISCONST(n))
                   ? bdd_varnum() : (int)LEVEL(n);
        };
        return lockstep(lockstep,
                        L_root, R_root,
                        root_level(L_root) - 1,
                        root_level(R_root) - 1);
    };

    return fn;
}

/* -------------------------------------------------------------------------
 * Swap combinators
 * ---------------------------------------------------------------------- */

BinaryNodeOp mtbdd_make_swap(SwapParam paramL, SwapParam paramR) {
    return [=](BDD L, BDD R) -> BDDPair {
        BDD offer_L = paramL.put_up(L);
        BDD offer_R = paramR.put_up(R);
        PUSHREF(offer_L);
        PUSHREF(offer_R);

        PUSHREF(paramL.put_in(L, offer_R));
        BDD new_R = paramR.put_in(R, offer_L);
        PUSHREF(new_R);
        BDD new_L = READREF(2);
        new_R = READREF(1);
        POPREF(4);

        return BDDPair(new_L, new_R);
    };
}

NodeOp mtbdd_make_swap() {
    return [](BDD node) -> BDD {
        PUSHREF(HIGH(node));
        PUSHREF(LOW(node));
        BDD res = bdd_makenode(LEVEL(node), READREF(2), READREF(1));
        POPREF(2);
        return res;
    };
}

/* EOF mtbddop.cxx */
