/**
 * @file mtbddop.cxx
 * @author Filip Novak
 *
 * Implementation of MTBDD traversal, lockstep, and swap operations.
 */

#include "mtbddop.h"

#include <unordered_map>
#include <algorithm>
#include <memory>
#include "prime.h"
#include "mtbdd_cache_registry.h"
struct OwnedCache {
    BddCache cache;

    explicit OwnedCache(int size) {
        BddCache_init(&cache, size);
    }
    ~OwnedCache() {
        BddCache_done(&cache);
    }
    OwnedCache(const OwnedCache&)            = delete;
    OwnedCache& operator=(const OwnedCache&) = delete;
};
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

    auto managed = std::make_shared<OwnedCache>(4096);

    return [=](BDD root) -> BDD {

        BddCache* c = &managed->cache;
        MtbddCache_registry_register(c);
        BddCache_reset(c);  
        auto traverse = [&](auto& self, BDD node, int parent_level) -> BDD {

            // --- Cache lookup ---
            int hash = PAIR(node, parent_level);
            BddCacheData* entry = BddCache_lookup(c, hash);
            if (BddCache_is_valid(c, entry)
                && entry->a == (int)node
                && entry->b == parent_level
                && entry->c == target_level
                && entry->d == -1) {
                return (BDD)entry->r.res;
            }

            BDD working_node = node;

            if (LEVEL(node) > (unsigned)target_level || ISCONST(node)) {
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
                    PUSHREF(action(LOW(working_node)));
                    res = bdd_makenode(LEVEL(working_node),
                                       READREF(1),
                                       HIGH(working_node));
                    POPREF(1);
                } else if (action_on == Branch::R) {
                    PUSHREF(action(HIGH(working_node)));
                    res = bdd_makenode(LEVEL(working_node),
                                       LOW(working_node),
                                       READREF(1));
                    POPREF(1);
                } else { // Branch::ITSELF
                    res = action(working_node);
                }
                if (node != working_node) POPREF(1);
                entry = BddCache_lookup(c, hash);
                BddCache_store4(entry, c, (int)node, parent_level, target_level, -1, (int)res);
                return res;
            }

            // --- Descent ---
            if (pref == Branch::R) {
                PUSHREF(self(self, HIGH(working_node), (int)LEVEL(working_node)));
                res = bdd_makenode(LEVEL(working_node),
                                   LOW(working_node),
                                   READREF(1));
                POPREF(1);
            } else if (pref == Branch::L) {
                PUSHREF(self(self, LOW(working_node), (int)LEVEL(working_node)));
                res = bdd_makenode(LEVEL(working_node),
                                   READREF(1),
                                   HIGH(working_node));
                POPREF(1);
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

            if (node != working_node) POPREF(1);
            entry = BddCache_lookup(c, hash);
            BddCache_store4(entry, c, (int)node, parent_level, target_level, -1, (int)res);
            return res;
        };

        int root_level = (ISTERMINAL(root) || ISCONST(root))
                         ? bdd_varnum()
                         : (int)LEVEL(root);
        BDD res = traverse(traverse, root, root_level - 1);
        MtbddCache_registry_unregister(c); 
        return res;
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

    auto managed = std::make_shared<OwnedCache>(4096);

    std::function<BDDPair(BDD, BDD)> fn =
        [=](BDD L_root, BDD R_root) -> BDDPair {

        BddCache* c = &managed->cache;

        MtbddCache_registry_register(c);
        BddCache_reset(c);

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

            // --- Cache lookup ---
            int hash = PAIR(PAIR(L, R), PAIR(parent_lv_L, parent_lv_R));
            BddCacheData* entry = BddCache_lookup(c, hash);
            if (BddCache_is_valid(c, entry)
                && entry->a == L
                && entry->b == R
                && entry->c == parent_lv_L
                && entry->d == parent_lv_R) {
                return { (BDD)entry->r.res, (BDD)entry->r2 };
            }

            // --- Virtualize ---
            BDD wL = ((int)LEVEL(L) > target_level)
                     ? virt_node(L, parent_lv_L, pref_L)
                     : L;
            bool virt_L = (wL != L);
            if (virt_L) PUSHREF(wL);

            // Virtualize R if needed and protect the fresh node immediately
            BDD wR = ((int)LEVEL(R) > target_level)
                     ? virt_node(R, parent_lv_R, pref_R)
                     : R;
            bool virt_R = (wR != R);
            if (virt_R) PUSHREF(wR);

            BDDPair res;

            // Fire action when both sides reach target level
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

                    if (action_on_R == Branch::L) {
                        new_wR = bdd_makenode(target_level,
                                              READREF(1),
                                              HIGH(wR));
                    } else if (action_on_R == Branch::R) {
                        new_wR = bdd_makenode(target_level,
                                              LOW(wR),
                                              READREF(1));
                    }

                    POPREF(2);
                    res = BDDPair(new_wL, new_wR);
                }
            }
            // Both at same level above target: advance both
            else if (LEVEL(wL) == LEVEL(wR)) {
                int lv = LEVEL(wL);

                BDD lo_L = (pref_L == Branch::R) ? wL : LOW(wL);
                BDD hi_L = (pref_L == Branch::L) ? wL : HIGH(wL);
                BDD lo_R = (pref_R == Branch::R) ? wR : LOW(wR);
                BDD hi_R = (pref_R == Branch::L) ? wR : HIGH(wR);

                // Protect lo results before hi recursion runs
                auto lo = self(self, lo_L, lo_R, lv, lv);
                PUSHREF(lo.first);
                PUSHREF(lo.second);
                auto hi = self(self, hi_L, hi_R, lv, lv);

                // lo.first/second may be stale after hi recursion -- use refstack
                // READREF(2) = lo.first (pushed first), READREF(1) = lo.second
                res = BDDPair(bdd_makenode(lv, READREF(2), hi.first),
                              bdd_makenode(lv, READREF(1), hi.second));
                POPREF(2);
            }
            // L leads: parent_lv_L advances, parent_lv_R stays
            else if (LEVEL(wL) < LEVEL(wR)) {
                int lv = LEVEL(wL);

                BDD lo_L = (pref_L == Branch::R) ? wL : LOW(wL);
                BDD hi_L = (pref_L == Branch::L) ? wL : HIGH(wL);

                auto lo = self(self, lo_L, R, lv, parent_lv_R);
                PUSHREF(lo.first);
                PUSHREF(lo.second);
                auto hi = self(self, hi_L, R, lv, parent_lv_R);

                res = BDDPair(bdd_makenode(lv, READREF(2), hi.first),
                              bdd_makenode(lv, READREF(1), hi.second));
                POPREF(2);
            }
            // R leads: parent_lv_R advances, parent_lv_L stays
            else {
                int lv = LEVEL(wR);

                BDD lo_R = (pref_R == Branch::R) ? wR : LOW(wR);
                BDD hi_R = (pref_R == Branch::L) ? wR : HIGH(wR);

                auto lo = self(self, L, lo_R, parent_lv_L, lv);
                PUSHREF(lo.first);
                PUSHREF(lo.second);
                auto hi = self(self, L, hi_R, parent_lv_L, lv);

                res = BDDPair(bdd_makenode(lv, READREF(2), hi.first),
                              bdd_makenode(lv, READREF(1), hi.second));
                POPREF(2);
            }

            // Pop virtual nodes in reverse order of pushing (R before L)
            if (virt_R) POPREF(1);
            if (virt_L) POPREF(1);

            entry = BddCache_lookup(c, hash);
            BddCache_store4(entry, c, L, R, parent_lv_L, parent_lv_R, res.first);
            entry->r2 = res.second;

            return res;
        };

        auto root_level = [](BDD n) -> int {
            return (ISTERMINAL(n) || ISCONST(n))
                   ? bdd_varnum() : (int)LEVEL(n);
        };
        BDDPair res  = lockstep(lockstep,
                        L_root, R_root,
                        root_level(L_root) - 1,
                        root_level(R_root) - 1);
        MtbddCache_registry_unregister(c);
        return res;
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

        // new_L is a freshly made node -- protect it before put_in for R
        // which may call bdd_makenode and trigger GC
        PUSHREF(paramL.put_in(L, offer_R));
        BDD new_R = paramR.put_in(R, offer_L);
        BDD new_L = READREF(1);
        POPREF(1);

        return BDDPair(new_L, new_R);
    };
}

NodeOp mtbdd_make_swap() {
    return [](BDD node) -> BDD {
        return bdd_makenode(LEVEL(node), HIGH(node), LOW(node));
    };
}

/* EOF mtbddop.cxx */