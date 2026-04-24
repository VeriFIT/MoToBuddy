/**
 * @file mosf_parser.h
 * @brief MOSF v1.0 -- JSON parser that reconstructs NodeOp / BinaryNodeOp
 *        from a MOSF file using the mtbddop.h combinator API.
 * @author Filip Novak
 */

#ifndef MOSF_PARSER_H
#define MOSF_PARSER_H

#ifdef __cplusplus

#include "mtbddop.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace mosf {

using json = nlohmann::json;

/* -------------------------------------------------------------------------
 * Extension registry
 *
 * User-defined op types are registered here before calling parse() / load().
 * Each factory receives the full JSON op object so it can read its own
 * parameters from the same entry as built-in ops.
 *
 * node_ops   -- factories that produce a NodeOp       (BDD -> BDD)
 * binary_ops -- factories that produce a BinaryNodeOp (BDD, BDD -> BDDPair)
 *
 * A type present in binary_ops is treated as BinaryNodeOp by is_binary()
 * and by traverse_to's implicit rebuild path; a type present in node_ops is
 * treated as NodeOp. Registering the same name in both maps is undefined.
 * ---------------------------------------------------------------------- */

struct ExtensionRegistry {
    std::unordered_map<std::string, std::function<NodeOp      (const json&)>> node_ops;
    std::unordered_map<std::string, std::function<BinaryNodeOp(const json&)>> binary_ops;
};

/* -------------------------------------------------------------------------
 * Event types
 * ---------------------------------------------------------------------- */

enum class EventKind {
    OP,           ///< A parsed NodeOp, ready to apply.
    GROUP_BEGIN,  ///< Entering a group -- fired once, carries repeat count.
    GROUP_END     ///< Group body done -- fired once after last op in body.
};

struct Event {
    EventKind   kind;
    NodeOp      op;           ///< OP only; default-constructed (null) for group events

    std::string group_name;   ///< GROUP_BEGIN / GROUP_END
    std::size_t rep_total;    ///< GROUP_BEGIN: symbolic repeat count
                              ///< GROUP_END:   mirrors GROUP_BEGIN rep_total
                              ///< OP:          0 (not meaningful)
    std::size_t op_repeat;    ///< OP: symbolic repeat count (default 1)
                              ///< GROUP_BEGIN / GROUP_END: 0 (not meaningful)
};

/* -------------------------------------------------------------------------
 * Internal parser context
 * ---------------------------------------------------------------------- */

namespace detail {

using DefMap = std::unordered_map<std::string, SwapParam>;
using VarMap = std::unordered_map<std::string, int>;

/* ---- helpers ------------------------------------------------------------- */

Branch parse_branch(const std::string& s) {
    if (s == "L")      return Branch::L;
    if (s == "R")      return Branch::R;
    if (s == "LR")     return Branch::LR;
    if (s == "RL")     return Branch::RL;
    if (s == "ITSELF") return Branch::ITSELF;
    throw std::runtime_error("Unknown Branch value: " + s);
}

int resolve_target(const json& j, const VarMap& vars) {
    if (j.contains("level")) return j["level"].get<int>();
    if (j.contains("var"))   return vars.at(j["var"].get<std::string>());
    throw std::runtime_error("Target must have \"level\" or \"var\"");
}

/* ---- forward declarations ------------------------------------------------ */

NodeOp       parse_node_op   (const json& j, const DefMap& defs, const VarMap& vars,
                               const ExtensionRegistry& reg);
BinaryNodeOp parse_binary_op (const json& j, const DefMap& defs, const VarMap& vars,
                               const ExtensionRegistry& reg);
SwapParam    parse_swap_param(const json& j, const DefMap& defs, const VarMap& vars,
                               const ExtensionRegistry& reg);

/* ---- PutInOp: BDD(BDD node, BDD received) --------------------------------
 * Separate parser for SwapParam::put_in, which has signature BDD(BDD, BDD).
 * "received" is only meaningful here -- it refers to the value sent by the
 * opposite swap side, i.e. the second argument of put_in.
 *
 * Note: Has nothing to do with the president of Russia as of 2026.
 */

using PutInOp = std::function<BDD(BDD, BDD)>;

PutInOp parse_put_in_op(const json& j, const DefMap& defs, const VarMap& vars,
                        const ExtensionRegistry& reg) {
    const std::string type = j.at("type").get<std::string>();

    if (type == "received") {
        return [](BDD /*node*/, BDD received) -> BDD {
            return received;
        };
    }

    if (type == "get_side") {
        NodeOp get = mtbdd_get_side(j.at("side").get<std::string>() == "L"
                                    ? Side::L : Side::R);
        return [get](BDD node, BDD /*received*/) -> BDD {
            return get(node);
        };
    }

    if (type == "makenode") {
        PutInOp low_op  = parse_put_in_op(j.at("low"),  defs, vars, reg);
        PutInOp high_op = parse_put_in_op(j.at("high"), defs, vars, reg);
        return [low_op, high_op](BDD node, BDD received) -> BDD {
            return bdd_makenode(LEVEL(node),
                                low_op (node, received),
                                high_op(node, received));
        };
    }

    // extension NodeOps may appear inside put_in, wrapping to absorb "received"
    {
        auto it = reg.node_ops.find(type);
        if (it != reg.node_ops.end()) {
            NodeOp inner = it->second(j);
            return [inner](BDD node, BDD /*received*/) -> BDD {
                return inner(node);
            };
        }
    }

    throw std::runtime_error("Unsupported op inside put_in: " + type);
}

/* ---- SwapParam ----------------------------------------------------------- */

SwapParam parse_swap_param(const json& j, const DefMap& defs, const VarMap& vars,
                           const ExtensionRegistry& reg) {
    if (j.contains("$ref"))
        return defs.at(j["$ref"].get<std::string>());

    SwapParam sp;
    sp.put_up = parse_node_op  (j.at("put_up"), defs, vars, reg);
    sp.put_in = parse_put_in_op(j.at("put_in"), defs, vars, reg);
    return sp;
}

/* ---- dispatch ------------------------------------------------------------ */

bool is_binary(const json& j, const ExtensionRegistry& reg) {
    const std::string type = j.at("type").get<std::string>();
    if (type == "lockstep_to")                  return true;
    if (type == "swap" && j.contains("paramL")) return true;
    if (reg.binary_ops.count(type))             return true;
    return false;
}

/* ---- NodeOp parser ------------------------------------------------------- */

NodeOp parse_node_op(const json& j, const DefMap& defs, const VarMap& vars,
                     const ExtensionRegistry& reg) {
    const std::string type = j.at("type").get<std::string>();

    // "group" is a sequencing construct, it cannot appear as an Op argument.
    if (type == "group")
        throw std::runtime_error(
            "\"group\" is a sequencing construct and cannot appear as an Op");

    if (type == "get_side") {
        return mtbdd_get_side(j.at("side").get<std::string>() == "L"
                              ? Side::L : Side::R);
    }

    /* makenode -- each child field accepts either a NodeOp or a BinaryNodeOp.
     *
     * NodeOp child:       child_op(node)           -- same as before
     * BinaryNodeOp child: child_op(LOW(node), HIGH(node))
     *                     -- allows e.g. "plus"/"minus" extension binary ops
     *                        to consume both children of the current node and
     *                        produce one new child value directly.
     */
    if (type == "makenode") {
        const json& low_j  = j.at("low");
        const json& high_j = j.at("high");

        if (is_binary(low_j, reg)) {
            BinaryNodeOp low_bin = parse_binary_op(low_j, defs, vars, reg);
            if (is_binary(high_j, reg)) {
                // both children are BinaryNodeOps
                BinaryNodeOp high_bin = parse_binary_op(high_j, defs, vars, reg);
                return [low_bin, high_bin](BDD node) -> BDD {
                    auto [new_LL, new_LR] = low_bin (LOW(node), HIGH(node));
                    auto [new_HL, new_HR] = high_bin(LOW(node), HIGH(node));
                    // each BinaryNodeOp returns a pair; the "left" result is
                    // the single computed value -- take first of each pair
                    return bdd_makenode(LEVEL(node), new_LL, new_HL);
                };
            } else {
                // low is BinaryNodeOp, high is NodeOp
                NodeOp high_op = parse_node_op(high_j, defs, vars, reg);
                return [low_bin, high_op](BDD node) -> BDD {
                    auto [new_L, _] = low_bin(LOW(node), HIGH(node));
                    return bdd_makenode(LEVEL(node), new_L, high_op(node));
                };
            }
        } else {
            NodeOp low_op = parse_node_op(low_j, defs, vars, reg);
            if (is_binary(high_j, reg)) {
                // low is NodeOp, high is BinaryNodeOp
                BinaryNodeOp high_bin = parse_binary_op(high_j, defs, vars, reg);
                return [low_op, high_bin](BDD node) -> BDD {
                    auto [new_H, _] = high_bin(LOW(node), HIGH(node));
                    return bdd_makenode(LEVEL(node), low_op(node), new_H);
                };
            } else {
                // both children are NodeOps -- original behaviour
                NodeOp high_op = parse_node_op(high_j, defs, vars, reg);
                return [low_op, high_op](BDD node) -> BDD {
                    return bdd_makenode(LEVEL(node), low_op(node), high_op(node));
                };
            }
        }
    }

    if (type == "swap" && !j.contains("paramL")) {
        return mtbdd_make_swap();
    }

    if (type == "traverse_to") {
        int    target    = resolve_target(j.at("target"), vars);
        Branch pref      = j.contains("pref")
                           ? parse_branch(j["pref"].get<std::string>())
                           : Branch::LR;
        Branch action_on = j.contains("action_on")
                           ? parse_branch(j["action_on"].get<std::string>())
                           : Branch::ITSELF;

        const json& action_j = j.at("action");

        if (is_binary(action_j, reg)) {
            BinaryNodeOp bin = parse_binary_op(action_j, defs, vars, reg);
            NodeOp wrapped = [bin](BDD node) -> BDD {
                auto [new_L, new_R] = bin(LOW(node), HIGH(node));
                return bdd_makenode(LEVEL(node), new_L, new_R);
            };
            return mtbdd_with_traverse_to(target, wrapped, pref, action_on);
        } else {
            NodeOp action = parse_node_op(action_j, defs, vars, reg);
            return mtbdd_with_traverse_to(target, action, pref, action_on);
        }
    }

    // extension NodeOp
    {
        auto it = reg.node_ops.find(type);
        if (it != reg.node_ops.end())
            return it->second(j);
    }

    throw std::runtime_error("Unknown NodeOp type: " + type);
}

/* ---- BinaryNodeOp parser ------------------------------------------------- */

BinaryNodeOp parse_binary_op(const json& j, const DefMap& defs, const VarMap& vars,
                              const ExtensionRegistry& reg) {
    const std::string type = j.at("type").get<std::string>();

    if (type == "swap") {
        SwapParam paramL = parse_swap_param(j.at("paramL"), defs, vars, reg);
        SwapParam paramR = parse_swap_param(j.at("paramR"), defs, vars, reg);
        return mtbdd_make_swap(paramL, paramR);
    }

    if (type == "lockstep_to") {
        int    target      = resolve_target(j.at("target"), vars);
        Branch pref_L      = j.contains("pref_L")
                             ? parse_branch(j["pref_L"].get<std::string>())      : Branch::LR;
        Branch action_on_L = j.contains("action_on_L")
                             ? parse_branch(j["action_on_L"].get<std::string>()) : Branch::ITSELF;
        Branch pref_R      = j.contains("pref_R")
                             ? parse_branch(j["pref_R"].get<std::string>())      : Branch::LR;
        Branch action_on_R = j.contains("action_on_R")
                             ? parse_branch(j["action_on_R"].get<std::string>()) : Branch::ITSELF;

        BinaryNodeOp action = parse_binary_op(j.at("action"), defs, vars, reg);
        return mtbdd_with_lockstep_to(target, action,
                                      pref_L, action_on_L,
                                      pref_R, action_on_R);
    }

    // extension BinaryNodeOp
    {
        auto it = reg.binary_ops.find(type);
        if (it != reg.binary_ops.end())
            return it->second(j);
    }

    throw std::runtime_error("Unknown BinaryNodeOp type: " + type);
}

/* ---- defs and vars blocks ------------------------------------------------ */

VarMap parse_vars(const json& j) {
    VarMap vars;
    if (!j.contains("vars")) return vars;
    for (auto& [name, val] : j["vars"].items())
        vars[name] = val.get<int>();
    return vars;
}

DefMap parse_defs(const json& j, const VarMap& vars, const ExtensionRegistry& reg) {
    DefMap defs;
    if (!j.contains("defs")) return defs;
    for (auto& [name, val] : j["defs"].items())
        defs[name] = parse_swap_param(val, defs, vars, reg);
    return defs;
}

/* ---- cursor stack frame --------------------------------------------------
 *
 * rep_cur is used exclusively by next_unrolled() to track how many times
 * the current entry has been dispensed within its repeat window. It is
 * never read or written by next().
 *
 * cached_op holds the parsed NodeOp for the current entry across repeated
 * next_unrolled() calls so the same JSON is not re-parsed on every repeat.
 * Reset to nullptr when cursor advances.
 */

struct Frame {
    const json* ops;
    std::string group_name;
    std::size_t rep_total  = 1;
    std::size_t cursor     = 0;
    std::size_t rep_cur    = 0;       ///< next_unrolled() only
    bool        began      = false;
    NodeOp      cached_op  = nullptr; ///< next_unrolled() only

    bool body_done() const { return cursor >= ops->size(); }
};

} // namespace detail

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

struct MOSFFile {
    std::string                tree;
    detail::VarMap             vars;
    detail::DefMap             defs;
    ExtensionRegistry          reg;    ///< extension registry threaded into parsers
    json                       ops;    ///< raw top-level ops array
    std::vector<detail::Frame> stack;
    std::vector<std::size_t>   unroll_multipliers; ///< mirrors stack; next_unrolled() only

    void _init() {
        stack.clear();
        unroll_multipliers.clear();
        detail::Frame root;
        root.ops       = &ops;
        root.rep_total = 1;
        stack.push_back(root);
        unroll_multipliers.push_back(1); ///< root multiplier is 1
    }

    /* has_next() pops both stacks in sync so that next_unrolled() never
     * encounters a stale unroll_multipliers entry after exhausted frames
     * are silently drained here. */
    bool has_next() {
        while (!stack.empty() && stack.back().body_done()) {
            stack.pop_back();
            if (!unroll_multipliers.empty())
                unroll_multipliers.pop_back();
        }
        return !stack.empty();
    }

    /**
     * @brief Return the next event.
     *
     * EventKind::GROUP_BEGIN -- fired once when a group is entered;
     *                           rep_total carries the symbolic repeat count.
     * EventKind::GROUP_END   -- fired once when the group body is ended;
     *                           body is executed exactly once regardless of
     *                           rep_total (symbolic model).
     * EventKind::OP          -- a parsed NodeOp ready to apply;
     *                           op_repeat carries the symbolic repeat count;
     *                           rep_total is 0 (not meaningful for OP events).
     *
     * Do NOT mix with next_unrolled() on the same MOSFFile without reset().
     */
    Event next() {
        while (true) {
            detail::Frame& f = stack.back();

            // emit GROUP_BEGIN once on entry (skip root frame)
            if (stack.size() > 1 && !f.began) {
                f.began = true;
                return Event{ EventKind::GROUP_BEGIN, {},
                              f.group_name, f.rep_total, /*op_repeat=*/0 };
            }

            // body done -- emit GROUP_END, pop frame
            if (f.body_done()) {
                Event end = Event{ EventKind::GROUP_END, {},
                                   f.group_name, f.rep_total, /*op_repeat=*/0 };
                stack.pop_back();
                return end;
            }

            const json& entry = (*f.ops)[f.cursor];
            const std::string type = entry.at("type").get<std::string>();

            // group entry -- push child frame, loop to emit GROUP_BEGIN
            if (type == "group") {
                f.cursor++;
                detail::Frame child;
                child.ops        = &entry.at("ops");
                child.group_name = entry.value("name", "");
                child.rep_total  = entry.value("repeat", std::size_t(1));
                stack.push_back(child);
                continue;
            }

            // plain op -- symbolic repeat surfaced in op_repeat
            std::size_t op_repeat = entry.value("repeat", std::size_t(1));
            NodeOp op = detail::parse_node_op(entry, defs, vars, reg);
            f.cursor++;

            return Event{ EventKind::OP, op, /*group_name=*/"",
                          /*rep_total=*/0, op_repeat };
        }
    }

    /**
     * @brief Return the next event, with groups fully unrolled.
     *
     * GROUP_BEGIN and GROUP_END events are never emitted.
     * op_repeat on each OP event is the product of all ancestor group
     * repeat counts multiplied by the entry's own repeat count, so the
     * caller applies the op that many times without tracking group structure.
     *
     * The parsed NodeOp is cached across repeated calls for the same entry
     * so the JSON is parsed exactly once per entry regardless of op_repeat.
     *
     * Do NOT mix with next() on the same MOSFFile without reset().
     */
    Event next_unrolled() {
        while (true) {
            detail::Frame& f = stack.back();

            // body done -- pop frame and its multiplier, continue in parent
            if (f.body_done()) {
                stack.pop_back();
                unroll_multipliers.pop_back();
                if (stack.empty())
                    throw std::runtime_error("next_unrolled() called past end");
                continue;
            }

            const json& entry = (*f.ops)[f.cursor];
            const std::string type = entry.at("type").get<std::string>();

            // group entry -- push frame and compound multiplier
            if (type == "group") {
                f.cursor++;
                detail::Frame child;
                child.ops        = &entry.at("ops");
                child.group_name = entry.value("name", "");
                child.rep_total  = entry.value("repeat", std::size_t(1));
                stack.push_back(child);
                unroll_multipliers.push_back(
                    unroll_multipliers.back() * child.rep_total);
                continue;
            }

            // plain op -- parse once, cache across the repeat window
            std::size_t op_repeat = entry.value("repeat", std::size_t(1));
            std::size_t total     = unroll_multipliers.back() * op_repeat;

            if (!f.cached_op)
                f.cached_op = detail::parse_node_op(entry, defs, vars, reg);

            NodeOp op = f.cached_op;

            f.rep_cur++;
            if (f.rep_cur >= total) {
                f.rep_cur   = 0;
                f.cached_op = nullptr;
                f.cursor++;
            }

            return Event{ EventKind::OP, op, /*group_name=*/"",
                          /*rep_total=*/0, total };
        }
    }

    void reset() { _init(); }
};

MOSFFile parse(const json& j, const ExtensionRegistry& reg = {}) {
    MOSFFile f;
    f.tree = j.at("tree").get<std::string>();
    f.reg  = reg;
    f.vars = detail::parse_vars(j);
    f.defs = detail::parse_defs(j, f.vars, reg);
    f.ops  = j.at("ops");
    f._init();
    return f;
}

MOSFFile load(const std::string& path, const ExtensionRegistry& reg = {}) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Cannot open MOSF file: " + path);
    json j;
    file >> j;
    return parse(j, reg);
}

} // namespace mosf

#endif /* __cplusplus */
#endif /* MOSF_PARSER_H */