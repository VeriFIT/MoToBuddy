/**
 * @file mtbddop.cxx
 * @author Filip Novak
 *  
 * Implementation of MTBDD traversal and transformation ops
 */

#include "mtbddop.h"

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
        auto lockstep = [=](auto& self, BDD L, BDD R) -> BDDPair {
            if ((ISTERMINAL(L) || ISCONST(L)) && (ISTERMINAL(R) || ISCONST(R)))
                return { L, R };

            if (LEVEL(L) == target_level && LEVEL(R) == target_level) {
                BDD offer_L = paramL.put_up(L);
                BDD offer_R = paramR.put_up(R);
                return { paramL.put_in(L, offer_R),
                         paramR.put_in(R, offer_L) };
            }
            if (LEVEL(L) == LEVEL(R)) {
                auto lo = self(self, LOW(L),  LOW(R));
                auto hi = self(self, HIGH(L), HIGH(R));
                return { bdd_makenode(LEVEL(L), lo.first,  hi.first),
                         bdd_makenode(LEVEL(R), lo.second, hi.second) };
            }
            if (LEVEL(L) < LEVEL(R)) {
                auto lo = self(self, LOW(L),  R);
                auto hi = self(self, HIGH(L), R);
                return { bdd_makenode(LEVEL(L), lo.first, hi.first), lo.second };
            }
            auto lo = self(self, L, LOW(R));
            auto hi = self(self, L, HIGH(R));
            return { lo.first, bdd_makenode(LEVEL(R), lo.second, hi.second) };
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