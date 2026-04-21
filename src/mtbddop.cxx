/**
 * @file mtbddop.cxx
 * @author Filip Novak
 *  
 * Implementation of MTBDD traversal and transformation ops
 */

#include "mtbddop.h"
#include <unordered_map>

/* Primitives */

NodeOp mtbdd_get_side(Side s) {
    return [=](BDD node) -> BDD {
        return s == Side::L ? LOW(node) : HIGH(node);
    };
}

/* Traversal */

NodeOp mtbdd_traverse_to(int target_level, NodeOp action,
                          Branch pref, Branch action_on) {
    return [=](BDD node) -> BDD {
        BDD working_node = node;

        if (LEVEL(node) > target_level) {
            if (pref == Branch::LR || pref == Branch::RL) {
                /* Safe to jump: both branches get rebuilt anyway */
                checkSameChildren = 0;
                working_node = bdd_makenode(target_level, node, node);
                checkSameChildren = 1;
            } else {
                /* Single step: preserve untouched branch structure */
                checkSameChildren = 0;
                working_node = bdd_makenode(LEVEL(node) - 1, node, node);
                checkSameChildren = 1;
                return mtbdd_traverse_to(target_level, action,
                                         pref, action_on)(working_node);
            }
        }

        if (LEVEL(working_node) == target_level) {
            if (action_on == Branch::R)
                return bdd_makenode(LEVEL(working_node),
                                    LOW(working_node),
                                    action(HIGH(working_node)));
            if (action_on == Branch::L)
                return bdd_makenode(LEVEL(working_node),
                                    action(LOW(working_node)),
                                    HIGH(working_node));
            return action(working_node);  /* Branch::ITSELF */
        }

        auto recur = mtbdd_traverse_to(target_level, action, pref, action_on);

        if (pref == Branch::R)
            return bdd_makenode(LEVEL(working_node),
                                LOW(working_node),
                                recur(HIGH(working_node)));
        if (pref == Branch::L)
            return bdd_makenode(LEVEL(working_node),
                                recur(LOW(working_node)),
                                HIGH(working_node));

        /* Branch::RL */
        if (pref == Branch::RL) {
            BDD new_high = recur(HIGH(working_node));
            BDD new_low  = recur(LOW(working_node));
            return bdd_makenode(LEVEL(working_node), new_low, new_high);
        }

        /* Branch::LR (default) */
        BDD new_low  = recur(LOW(working_node));
        BDD new_high = recur(HIGH(working_node));
        return bdd_makenode(LEVEL(working_node), new_low, new_high);
    };
}

/* Swap */

NodeOp mtbdd_swap(int target_level, SwapParam paramL, SwapParam paramR) {
    return [=](BDD node) -> BDD {

        // ---- local memo for this single top-level call ----
        struct PairKey {
            BDD L, R;
            bool operator==(const PairKey& o) const {
                return L == o.L && R == o.R;
            }
        };
        struct PairKeyHash {
            std::size_t operator()(const PairKey& k) const noexcept {
                std::size_t h = std::hash<int>{}(k.L);
                h ^= std::hash<int>{}(k.R) + 0x9e3779b9 + (h << 6) + (h >> 2);
                return h;
            }
        };
        std::unordered_map<PairKey, BDDPair, PairKeyHash> memo;

        // ---- lockstep with memo ----
        auto lockstep = [=, &memo](auto& self, BDD L, BDD R) -> BDDPair {

            // cache lookup
            PairKey key{L, R};
            auto it = memo.find(key);
            if (it != memo.end()) {
                printf("cache hit\n");
                return it->second;
            }

            BDD working_L = L;
            BDD working_R = R;

            if (LEVEL(L) > target_level || ISCONST(L)) {
                checkSameChildren = 0;
                working_L = bdd_makenode(target_level, L, L);
                checkSameChildren = 1;
            }

            if (LEVEL(R) > target_level || ISCONST(R)) {
                checkSameChildren = 0;
                working_R = bdd_makenode(target_level, R, R);
                checkSameChildren = 1;
            }

            BDDPair result;

            if (LEVEL(working_L) == target_level && LEVEL(working_R) == target_level) {
                BDD offer_L = paramL.put_up(working_L);
                BDD offer_R = paramR.put_up(working_R);
                result = { paramL.put_in(working_L, offer_R),
                           paramR.put_in(working_R, offer_L) };
            }
            else if (LEVEL(working_L) == LEVEL(working_R)) {
                auto lo = self(self, LOW(working_L),  LOW(working_R));
                auto hi = self(self, HIGH(working_L), HIGH(working_R));
                result = { bdd_makenode(LEVEL(working_L), lo.first,  hi.first),
                           bdd_makenode(LEVEL(working_R), lo.second, hi.second) };
            }
            else if (LEVEL(working_L) < LEVEL(working_R)) {
                auto lo = self(self, LOW(working_L),  working_R);
                auto hi = self(self, HIGH(working_L), working_R);
                result = { bdd_makenode(LEVEL(working_L), lo.first,  hi.first),
                           bdd_makenode(LEVEL(working_L), lo.second, hi.second) };
            }
            else { // LEVEL(working_L) > LEVEL(working_R)
                auto lo = self(self, working_L, LOW(working_R));
                auto hi = self(self, working_L, HIGH(working_R));
                result = { bdd_makenode(LEVEL(working_R), lo.first,  hi.first),
                           bdd_makenode(LEVEL(working_R), lo.second, hi.second) };
            }

            memo.emplace(key, result);
            return result;
        };

        auto result = lockstep(lockstep, LOW(node), HIGH(node));
        return bdd_makenode(LEVEL(node), result.first, result.second);
    };
}

NodeOp mtbdd_swap() {
    return [](BDD node) -> BDD {
        return bdd_makenode(LEVEL(node), HIGH(node), LOW(node));
    };
}

/* EOF mtbddop.cxx */