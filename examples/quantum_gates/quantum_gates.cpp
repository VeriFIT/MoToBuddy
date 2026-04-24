/**
 * @file quantum_gates.cpp
 * @brief Demonstrates CX, CCX, and MOSF-driven quantum gate simulation on an
 *        alphabet MTBDD. Builds a perfect binary tree over A–Z leaves, then
 *        applies a chosen gate by composing traverse-to and lockstep-to
 *        primitives, or by loading an arbitrary gate from a MOSF file.
 *        The resulting MTBDD is printed to stdout in Graphviz DOT format.
 * @author Filip Novak
 */

#ifdef CPLUSPLUS
#undef CPLUSPLUS
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <functional>
#include <utility>

#include "../src/bdd.h"
#include "../src/mtbddop.h"
#include "../src/mosf_parser.h"
#include <nlohmann/json.hpp>

/* ------------------------------------------------------------------------=
 * String terminal helpers
 * ---------------------------------------------------------------------= */

static int str_cmp(void *a, void *b) {
    if (a == nullptr && b == nullptr) return 1;
    if (a == nullptr || b == nullptr) return 0;
    return !strcmp(static_cast<char*>(a), static_cast<char*>(b));
}

static unsigned str_hash(void *a) {
    unsigned hash = 5381;
    for (unsigned char *p = reinterpret_cast<unsigned char*>(a); *p; ++p)
        hash = ((hash << 5) + hash) + *p;
    return hash;
}

static char* str_tostr(void *a, char *buf, size_t bufsz) {
    const char *str = static_cast<char*>(a);
    size_t len = strlen(str);
    if (len < bufsz) {
        memcpy(buf, str, len);
        buf[len] = '\0';
        return buf;
    }
    char *out = static_cast<char*>(malloc(len + 1));
    if (!out) return nullptr;
    memcpy(out, str, len + 1);
    return out;
}

/* ------------------------------------------------------------------------=
 * MTBDD apply operations for H gate (symbolic string terminals)
 * ---------------------------------------------------------------------= */

static void* op_plus(void *l, void *r) {
    if (l == nullptr && r == nullptr) return nullptr;
    const char *sl = l ? static_cast<const char*>(l) : "0";
    const char *sr = r ? static_cast<const char*>(r) : "0";
    size_t len = strlen("1/sqrt(2) * (") + strlen(sl) + 1 + strlen(sr) + 2;
    char *result = static_cast<char*>(malloc(len));
    snprintf(result, len, "1/sqrt(2) * (%s+%s)", sl, sr);
    return result;
}

static void* op_minus(void *l, void *r) {
    if (l == nullptr && r == nullptr) return nullptr;
    const char *sl = l ? static_cast<const char*>(l) : "0";
    const char *sr = r ? static_cast<const char*>(r) : "0";
    size_t len = strlen("1/sqrt(2) * (") + strlen(sl) + 1 + strlen(sr) + 2;
    char *result = static_cast<char*>(malloc(len));
    snprintf(result, len, "1/sqrt(2) * (%s-%s)", sl, sr);
    return result;
}

static BDD mtbdd_plus (BDD l, BDD r) { return mtbdd_apply(l, r, op_plus);  }
static BDD mtbdd_minus(BDD l, BDD r) { return mtbdd_apply(l, r, op_minus); }

/* ------------------------------------------------------------------------=
 * Alphabet tree builder
 *
 * Constructs a perfect binary MTBDD over the first 'count' letters.
 * Leaf slots beyond 'count' are filled with bdd_false (0).
 * 'root_level' offsets variable indices so the tree fits into the
 * global variable ordering.
 * ---------------------------------------------------------------------= */

static BDD build_alphabet_tree(unsigned leaf_id, const char **letters,
                                int count, int root_level = 0) {
    int padded = 1;
    while (padded < count) padded <<= 1;
    int num_levels = __builtin_ctz(padded);

    BDD *current = static_cast<BDD*>(malloc(padded * sizeof(BDD)));
    for (int i = 0; i < padded; ++i)
        current[i] = (i < count) ? mtbdd_maketerminal((void*)letters[i], leaf_id) : 0;

    int level = root_level + num_levels - 1;
    while (padded > 1) {
        int next_size = padded / 2;
        BDD *next = static_cast<BDD*>(malloc(next_size * sizeof(BDD)));
        for (int i = 0; i < next_size; ++i) {
            BDD lo = current[2*i], hi = current[2*i + 1];
            next[i] = (lo == 0 && hi == 0) ? 0 : bdd_makenode(level, lo, hi);
        }
        free(current);
        current = next;
        padded = next_size;
        --level;
    }

    BDD result = current[0];
    free(current);
    return result;
}

/* ------------------------------------------------------------------------=
 * Usage
 * ---------------------------------------------------------------------= */

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage:\n"
        "  %s <num_letters> cx  <c> <t>\n"
        "  %s <num_letters> ccx <c1> <c2> <t>\n"
        "  %s <num_letters> <gate.mosf>\n"
        "\n"
        "  num_letters  number of alphabet letters (A..Z); padded to next power of 2\n"
        "  cx           single-control CX gate: control c, target t\n"
        "  ccx          double-control CCX gate: controls c1 c2 (auto-sorted), target t\n"
        "  gate.mosf    path to a MOSF file describing an arbitrary gate\n"
        "\n"
        "Semantics:\n"
        "  Level 0 is the root (most significant bit).\n"
        "  A control is active when the HIGH (1) branch is taken at that level.\n"
        "  The target subtree is transformed when all controls are active.\n",
        prog, prog, prog);
}

/* ------------------------------------------------------------------------=
 * main
 * ---------------------------------------------------------------------= */

int main(int argc, char **argv) {
    if (argc < 3) { usage(argv[0]); return 1; }

    /* ---- parse num_letters ---- */
    int num_letters = atoi(argv[1]);
    if (num_letters < 1 || num_letters > 26) {
        fprintf(stderr, "error: num_letters must be 1..26\n");
        return 1;
    }

    int padded     = 1;
    while (padded < num_letters) padded <<= 1;
    int num_levels = __builtin_ctz(padded);
    int var_num    = num_levels;

    const char *gate = argv[2];

    /* ---- MTBDD initialisation ---- */
    mtbdd_init(10000, 10000);
    bdd_setvarnum(var_num);

    unsigned leaf_id = mtbdd_new_terminal_type();
    mtbdd_register_compare_function(leaf_id, str_cmp);
    mtbdd_register_hash_function  (leaf_id, str_hash);
    mtbdd_register_to_str_function(leaf_id, str_tostr);
    SETDOMAIN(CUSTOM);

    /* ---- build alphabet tree ---- */
    const char *letters[] = {
        "A","B","C","D","E","F","G","H","I","J","K","L","M",
        "N","O","P","Q","R","S","T","U","V","W","X","Y","Z"
    };
    BDD alphabet_tree = build_alphabet_tree(leaf_id, letters, num_letters, 0);

    /* SwapParam targeting the HIGH child of each node */
    SwapParam high_swap_param = {
        .put_up = +[](BDD node) -> BDD { return HIGH(node); },
        .put_in = +[](BDD node, BDD received) -> BDD {
            return bdd_makenode(LEVEL(node), LOW(node), received);
        }
    };

    BDD result = 0;

    /* ------------------------------------------------------------------
     * CX gate
     * ------------------------------------------------------------------ */
    if (strcmp(gate, "cx") == 0) {
        if (argc < 5) { usage(argv[0]); return 1; }
        int c = atoi(argv[3]);
        int t = atoi(argv[4]);

        if (c < 0 || c >= var_num) { fprintf(stderr, "error: c=%d out of range\n", c); return 1; }
        if (t < 0 || t >= var_num) { fprintf(stderr, "error: t=%d out of range\n", t); return 1; }
        if (t == c) { fprintf(stderr, "error: c and t must differ\n"); return 1; }

        if (t < c) {
            /* target above control: lockstep from t down to c */
            auto swap_action = mtbdd_make_swap(high_swap_param, high_swap_param);
            auto do_lockstep = mtbdd_with_lockstep_to(c, swap_action);
            auto cx = mtbdd_with_traverse_to(t, [=](BDD node) -> BDD {
                auto [new_L, new_R] = do_lockstep(LOW(node), HIGH(node));
                return bdd_makenode(LEVEL(node), new_L, new_R);
            });
            result = cx(alphabet_tree);
        } else {
            /* control above target: descend HIGH at c, swap at t */
            auto cx = mtbdd_with_traverse_to(
                c,
                mtbdd_with_traverse_to(t, mtbdd_make_swap()),
                Branch::LR, Branch::R
            );
            result = cx(alphabet_tree);
        }

    /* ------------------------------------------------------------------
     * CCX gate
     * ------------------------------------------------------------------ */
    } else if (strcmp(gate, "ccx") == 0) {
        if (argc < 6) { usage(argv[0]); return 1; }
        int c1 = atoi(argv[3]);
        int c2 = atoi(argv[4]);
        int t  = atoi(argv[5]);

        if (c1 < 0 || c1 >= var_num) { fprintf(stderr, "error: c1=%d out of range\n", c1); return 1; }
        if (c2 < 0 || c2 >= var_num) { fprintf(stderr, "error: c2=%d out of range\n", c2); return 1; }
        if (t  < 0 || t  >= var_num) { fprintf(stderr, "error: t=%d out of range\n",  t);  return 1; }
        if (t == c1 || t == c2 || c1 == c2) {
            fprintf(stderr, "error: c1, c2, t must all differ\n"); return 1;
        }
        if (c1 > c2) { int tmp = c1; c1 = c2; c2 = tmp; }

        if (t < c1) {
            /* t < c1 < c2: two nested locksteps from t */
            auto swap_action    = mtbdd_make_swap(high_swap_param, high_swap_param);
            auto inner_lockstep = mtbdd_with_lockstep_to(c2, swap_action,
                                      Branch::LR, Branch::R, Branch::LR, Branch::R);
            auto outer_lockstep = mtbdd_with_lockstep_to(c1, inner_lockstep,
                                      Branch::LR, Branch::R, Branch::LR, Branch::R);
            auto ccx = mtbdd_with_traverse_to(t, [=](BDD node) -> BDD {
                auto [new_L, new_R] = outer_lockstep(LOW(node), HIGH(node));
                return bdd_makenode(LEVEL(node), new_L, new_R);
            });
            result = ccx(alphabet_tree);

        } else if (c1 < t && t < c2) {
            /* c1 < t < c2: descend HIGH at c1, lockstep at t down to c2 */
            auto swap_action2 = mtbdd_make_swap(high_swap_param, high_swap_param);
            auto at_t = [=](BDD node) -> BDD {
                auto do_lockstep = mtbdd_with_lockstep_to(c2, swap_action2,
                                       Branch::LR, Branch::R, Branch::LR, Branch::R);
                auto [new_L, new_R] = do_lockstep(LOW(node), HIGH(node));
                return bdd_makenode(LEVEL(node), new_L, new_R);
            };
            auto ccx = mtbdd_with_traverse_to(
                c1,
                mtbdd_with_traverse_to(t, at_t),
                Branch::LR, Branch::R
            );
            result = ccx(alphabet_tree);

        } else {
            /* c1 < c2 < t: descend HIGH at c1, HIGH at c2, swap at t */
            auto ccx = mtbdd_with_traverse_to(
                c1,
                mtbdd_with_traverse_to(
                    c2,
                    mtbdd_with_traverse_to(t, mtbdd_make_swap()),
                    Branch::LR, Branch::R
                ),
                Branch::LR, Branch::R
            );
            result = ccx(alphabet_tree);
        }

    /* ------------------------------------------------------------------
     * MOSF-driven gate
     * ------------------------------------------------------------------ */
    } else {
        using json = nlohmann::json;

        mosf::ExtensionRegistry reg;

        reg.binary_ops["plus_s"] = [](const json&) -> BinaryNodeOp {
            return [](BDD low, BDD high) -> BDDPair {
                return { mtbdd_plus(low, high), 0 };
            };
        };

        reg.binary_ops["minus_s"] = [](const json&) -> BinaryNodeOp {
            return [](BDD low, BDD high) -> BDDPair {
                return { mtbdd_minus(low, high), 0 };
            };
        };

        mosf::MOSFFile circuit = mosf::load(gate, reg);
        result = alphabet_tree;
        while (circuit.has_next()) {
            mosf::Event ev = circuit.next_unrolled();
            for (std::size_t i = 0; i < ev.op_repeat; ++i)
                result = ev.op(result);
        }
    }

    /* ---- output ---- */
    buddy_mtbdd_fprintdot(stdout, result);
    return 0;
}

/* EOF cx_ccx_alphabet.cpp */