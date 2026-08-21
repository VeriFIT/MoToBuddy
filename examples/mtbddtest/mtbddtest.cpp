/**
 * @file mtbddtest.cpp
 * @brief Unit/regression tests for MTBDD terminals, apply, ITE, operation,
 *        and C++ combinators (not FDD).
 * @author Filip Novak / MoToBuddy test harness
 */

#ifdef CPLUSPLUS
#undef CPLUSPLUS
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../src/bdd.h"
#include "../src/mtbddop.h"

/* ------------------------------------------------------------------------=
 * Minimal test harness
 * ---------------------------------------------------------------------= */

static int g_failed = 0;
static int g_passed = 0;
static int g_xfailed = 0; /* known bugs: expected to fail today */

#define EXPECT(cond, msg)                                                      \
    do {                                                                       \
        if (cond) {                                                            \
            printf("PASS: %s\n", msg);                                         \
            ++g_passed;                                                        \
        } else {                                                               \
            printf("FAIL: %s\n", msg);                                         \
            ++g_failed;                                                        \
        }                                                                      \
    } while (0)

/* Assert correct behavior that is currently broken. Counts as XFAIL if it
 * fails (documents the bug); if it unexpectedly passes, report as PASS. */
#define XFAIL_EXPECT(cond, msg)                                                \
    do {                                                                       \
        if (cond) {                                                            \
            printf("PASS (was XFAIL): %s\n", msg);                             \
            ++g_passed;                                                        \
        } else {                                                               \
            printf("XFAIL: %s\n", msg);                                        \
            ++g_xfailed;                                                       \
        }                                                                      \
    } while (0)

/* ------------------------------------------------------------------------=
 * Int terminal helpers (heap-allocated ints; freefun frees them)
 * ---------------------------------------------------------------------= */

static int int_cmp(void *a, void *b) {
    if (a == nullptr && b == nullptr)
        return 1;
    if (a == nullptr || b == nullptr)
        return 0;
    return *(int *)a == *(int *)b;
}

static unsigned int_hash(void *a) {
    return (unsigned)(*(int *)a);
}

static char *int_tostr(void *a, char *buf, size_t bufsz) {
    snprintf(buf, bufsz, "%d", *(int *)a);
    return buf;
}

static int *box_int(int v) {
    int *p = (int *)malloc(sizeof(int));
    *p = v;
    return p;
}

static void *op_add(void *l, void *r) {
    int lv = l ? *(int *)l : 0;
    int rv = r ? *(int *)r : 0;
    return box_int(lv + rv);
}

static void *op_mul(void *l, void *r) {
    int lv = l ? *(int *)l : 0;
    int rv = r ? *(int *)r : 0;
    return box_int(lv * rv);
}

static void *op_neg(void *v) {
    int x = v ? *(int *)v : 0;
    return box_int(-x);
}

static void *op_add_param(void *l, void *r, size_t p) {
    int lv = l ? *(int *)l : 0;
    int rv = r ? *(int *)r : 0;
    return box_int(lv + rv + (int)p);
}

static int leaf_int(BDD t) {
    void *p = mtbdd_getTerminalValue(t);
    return p ? *(int *)p : 0;
}

/* ------------------------------------------------------------------------=
 * String terminal helpers (no freefun — matches quantum_gates ownership)
 * ---------------------------------------------------------------------= */

static int str_cmp(void *a, void *b) {
    if (a == nullptr && b == nullptr)
        return 1;
    if (a == nullptr || b == nullptr)
        return 0;
    return !strcmp((char *)a, (char *)b);
}

static unsigned str_hash(void *a) {
    unsigned h = 5381;
    for (unsigned char *p = (unsigned char *)a; *p; ++p)
        h = ((h << 5) + h) + *p;
    return h;
}

/* ------------------------------------------------------------------------=
 * Setup / teardown
 * ---------------------------------------------------------------------= */

struct Fixture {
    unsigned int_type;
    unsigned str_type;
};

static Fixture setup_custom(int varnum) {
    mtbdd_init(10000, 10000);
    bdd_setvarnum(varnum);
    SETDOMAIN(CUSTOM);

    Fixture f{};
    f.int_type = mtbdd_new_terminal_type();
    mtbdd_register_compare_function(f.int_type, int_cmp);
    mtbdd_register_hash_function(f.int_type, int_hash);
    mtbdd_register_to_str_function(f.int_type, int_tostr);
    /* Intentionally no freefun by default: freefun must only free internals;
       MoToBuddy always free()s the outer pointer (MoToMedusa contract). */

    f.str_type = mtbdd_new_terminal_type();
    mtbdd_register_compare_function(f.str_type, str_cmp);
    mtbdd_register_hash_function(f.str_type, str_hash);
    return f;
}

static void teardown() { bdd_done(); }

static BDD make_int_leaf(Fixture &f, int v) {
    return mtbdd_maketerminal(box_int(v), f.int_type);
}

/* Perfect binary tree of int leaves over levels [0 .. depth-1]. */
static BDD build_int_tree(Fixture &f, const std::vector<int> &leaves,
                          int root_level = 0) {
    int n = (int)leaves.size();
    int padded = 1;
    while (padded < n)
        padded <<= 1;
    int num_levels = 0;
    for (int p = padded; p > 1; p >>= 1)
        ++num_levels;

    std::vector<BDD> cur(padded);
    for (int i = 0; i < padded; ++i)
        cur[i] = (i < n) ? make_int_leaf(f, leaves[i]) : 0;

    int level = root_level + num_levels - 1;
    while (padded > 1) {
        int next_size = padded / 2;
        std::vector<BDD> next(next_size);
        for (int i = 0; i < next_size; ++i) {
            BDD lo = cur[2 * i], hi = cur[2 * i + 1];
            next[i] = (lo == 0 && hi == 0) ? 0 : bdd_makenode(level, lo, hi);
        }
        cur.swap(next);
        padded = next_size;
        --level;
    }
    return cur[0];
}

/* ------------------------------------------------------------------------=
 * Tests: terminal registration & interning
 * ---------------------------------------------------------------------= */

static void test_terminal_types_and_intern() {
    printf("\n== terminal types & interning ==\n");
    Fixture f = setup_custom(2);

    EXPECT(f.int_type != f.str_type, "distinct terminal type ids");
    EXPECT(mtbdd_terminal_type_number >= 2, "at least two types registered");

    BDD a1 = make_int_leaf(f, 42);
    BDD a2 = make_int_leaf(f, 42);
    BDD b = make_int_leaf(f, 7);

    EXPECT(ISTERMINAL(a1), "maketerminal produces ISTERMINAL node");
    EXPECT(a1 == a2, "equal values intern to same node");
    EXPECT(a1 != b, "distinct values are distinct nodes");
    EXPECT(leaf_int(a1) == 42, "getTerminalValue returns stored int");
    EXPECT(mtbdd_get_terminal_type(a1) == f.int_type, "terminal type preserved");

    BDD null_term = mtbdd_maketerminal(nullptr, f.int_type);
    EXPECT(null_term == 0, "NULL value maps to constant 0");

    teardown();
}

static void test_long_double_domains() {
    printf("\n== LONG / DOUBLE domains ==\n");

    mtbdd_init(10000, 10000);
    bdd_setvarnum(1);
    SETDOMAIN(LONGVAL);
    long lv = 1234567890123L;
    BDD t1 = mtbdd_maketerminal(&lv, 0);
    BDD t2 = mtbdd_maketerminal(&lv, 0);
    EXPECT(ISTERMINAL(t1) && t1 == t2, "LONGVAL interns equal values");
    EXPECT(*(long *)mtbdd_getTerminalValue(t1) == lv, "LONGVAL value roundtrip");
    bdd_done();

    mtbdd_init(10000, 10000);
    bdd_setvarnum(1);
    SETDOMAIN(DOUBLEVAL);
    double dv = 3.141592653589793;
    BDD d1 = mtbdd_maketerminal(&dv, 0);
    BDD d2 = mtbdd_maketerminal(&dv, 0);
    EXPECT(ISTERMINAL(d1) && d1 == d2, "DOUBLEVAL interns equal values");
    EXPECT(*(double *)mtbdd_getTerminalValue(d1) == dv, "DOUBLEVAL value roundtrip");
    bdd_done();
}

/* ------------------------------------------------------------------------=
 * Tests: apply family
 * ---------------------------------------------------------------------= */

static void test_apply_binary_unary_param() {
    printf("\n== apply (binary / unary / param) ==\n");
    Fixture f = setup_custom(2);

    BDD t3 = make_int_leaf(f, 3);
    BDD t5 = make_int_leaf(f, 5);

    BDD sum = mtbdd_apply(t3, t5, op_add);
    EXPECT(ISTERMINAL(sum) && leaf_int(sum) == 8, "apply add on leaves");

    BDD prod = mtbdd_apply(t3, t5, op_mul);
    EXPECT(ISTERMINAL(prod) && leaf_int(prod) == 15, "apply mul on leaves");

    /* Identity-ish: add with 0 should reuse left when result equals left */
    BDD z = make_int_leaf(f, 0);
    BDD same = mtbdd_apply(t3, z, op_add);
    EXPECT(same == t3, "apply reuses left when result equals left");

    BDD neg = mtbdd_apply_unary(t5, op_neg);
    EXPECT(ISTERMINAL(neg) && leaf_int(neg) == -5, "apply_unary neg");

    BDD sum_p = mtbdd_apply_param(t3, t5, op_add_param, 10);
    EXPECT(ISTERMINAL(sum_p) && leaf_int(sum_p) == 18, "apply_param add+10");

    /* Structural apply on a small tree: (1,2) + (10,20) -> (11,22) */
    BDD left = bdd_makenode(0, make_int_leaf(f, 1), make_int_leaf(f, 2));
    BDD right = bdd_makenode(0, make_int_leaf(f, 10), make_int_leaf(f, 20));
    BDD both = mtbdd_apply(left, right, op_add);
    EXPECT(!ISTERMINAL(both) && LEVEL(both) == 0, "apply preserves internal level");
    EXPECT(leaf_int(LOW(both)) == 11 && leaf_int(HIGH(both)) == 22,
          "apply recurses on children");

    EXPECT(mtbdd_leaf_count(both) == 2, "leaf_count on apply result");

    teardown();
}

static BDD guarded_prefer_left(BDD l, BDD r) {
    (void)r;
    if (ISTERMINAL(l) || ISZERO(l)) {
        FLAG_VALID_APPLY();
        return l;
    }
    FLAG_INVALID_APPLY();
    return 0;
}

static void test_apply_guarded() {
    printf("\n== apply_guarded ==\n");
    Fixture f = setup_custom(2);

    BDD left = bdd_makenode(0, make_int_leaf(f, 1), make_int_leaf(f, 2));
    BDD right = bdd_makenode(0, make_int_leaf(f, 9), make_int_leaf(f, 9));
    BDD res = mtbdd_apply_guarded(left, right, guarded_prefer_left);
    /* At each leaf, guard returns left leaf → whole tree equals left */
    EXPECT(res == left, "apply_guarded short-circuits to left leaves");

    teardown();
}

/* ------------------------------------------------------------------------=
 * Tests: ITE
 * ---------------------------------------------------------------------= */

static void test_ite() {
    printf("\n== mtbdd_ite ==\n");
    Fixture f = setup_custom(3);

    BDD t0 = make_int_leaf(f, 0);
    BDD t1 = make_int_leaf(f, 1);
    BDD t2 = make_int_leaf(f, 2);

    /* g == h → result is g (independent of f) */
    BDD fnode = bdd_makenode(0, t0, t1);
    BDD same = mtbdd_ite(fnode, t2, t2);
    EXPECT(same == t2, "ite(f,g,g) returns g");

    /* ite(f,g,h) builds a new node and must not mutate f */
    BDD a = make_int_leaf(f, 10);
    BDD b = make_int_leaf(f, 20);
    BDD x = bdd_makenode(1, t0, t1);
    BDD x_low_before = LOW(x);
    BDD x_high_before = HIGH(x);
    BDD ite_res = mtbdd_ite(x, a, b);

    bool children_ok =
        ISTERMINAL(LOW(ite_res)) && ISTERMINAL(HIGH(ite_res)) &&
        leaf_int(LOW(ite_res)) == 10 && leaf_int(HIGH(ite_res)) == 20;
    bool x_untouched = (LOW(x) == x_low_before && HIGH(x) == x_high_before);

    EXPECT(children_ok && x_untouched && ite_res != x,
           "ite(f,g,h) builds new node and does not mutate f");

    EXPECT(mtbdd_ite(0, a, b) == b, "ite(false,g,h) returns h");
    EXPECT(mtbdd_ite(1, a, b) == a, "ite(true,g,h) returns g");

    /* Cache keyed on (f,g,h): same call twice returns identical node.
     * Use non-terminal g,h so different f yield different results. */
    BDD ga = bdd_makenode(1, a, b);
    BDD ha = bdd_makenode(1, b, a);
    BDD y = bdd_makenode(0, t0, t1);
    BDD z = bdd_makenode(0, t1, t2);
    BDD r1 = mtbdd_ite(y, ga, ha);
    BDD r2 = mtbdd_ite(y, ga, ha);
    EXPECT(r1 == r2, "ite cache hit returns same node for identical (f,g,h)");
    BDD r3 = mtbdd_ite(z, ga, ha);
    EXPECT(r3 != r1, "ite with different f does not reuse wrong cache entry");

    teardown();
}

/* ------------------------------------------------------------------------=
 * Tests: mtbdd_operation (controlled swap-like op)
 * ---------------------------------------------------------------------= */

/* At target with controlNum==0, op receives LOW/HIGH of the target node.
 * Swap them at the target level. */
static BDD op_swap_at_target(size_t ctrl, BDD lo, BDD hi) {
    return bdd_makenode((int)ctrl, hi, lo);
}

static void test_operation() {
    printf("\n== mtbdd_operation ==\n");
    Fixture f = setup_custom(3);

    /* Tree: level0 -> (level1 -> (1,2), level1 -> (3,4)) */
    BDD tree = build_int_tree(f, {1, 2, 3, 4});
    EXPECT(mtbdd_leaf_count(tree) == 4, "built 4-leaf tree");

    /* Target only (controlNum==0): swap children at level 0 */
    size_t controls0[] = {0};
    BDD swapped = mtbdd_operation(tree, controls0, 0, op_swap_at_target);
    EXPECT(leaf_int(LOW(LOW(swapped))) == 3 && leaf_int(HIGH(LOW(swapped))) == 4 &&
              leaf_int(LOW(HIGH(swapped))) == 1 && leaf_int(HIGH(HIGH(swapped))) == 2,
          "operation swaps level-0 children (controlNum==0)");

    /* CX-style: control=0, target=1 — only HIGH of control is transformed.
     * Same session as the controlNum==0 call above; cache must distinguish. */
    size_t controls_cx[] = {0, 1};
    BDD cx = mtbdd_operation(tree, controls_cx, 1, op_swap_at_target);
    EXPECT(leaf_int(LOW(LOW(cx))) == 1 && leaf_int(HIGH(LOW(cx))) == 2,
          "operation leaves inactive control branch unchanged");
    EXPECT(leaf_int(LOW(HIGH(cx))) == 4 && leaf_int(HIGH(HIGH(cx))) == 3,
          "operation transforms HIGH branch at target");
    EXPECT(cx != swapped,
          "operation cache distinguishes controlNum / full controls[]");

    teardown();
}

/* ------------------------------------------------------------------------=
 * Tests: C++ combinators (traverse / swap)
 * ---------------------------------------------------------------------= */

static void test_combinators() {
    printf("\n== mtbddop combinators ==\n");
    Fixture f = setup_custom(3);

    BDD tree = build_int_tree(f, {1, 2, 3, 4});

    auto swap = mtbdd_make_swap();
    BDD s0 = swap(tree);
    EXPECT(leaf_int(LOW(LOW(s0))) == 3 && leaf_int(HIGH(HIGH(s0))) == 2,
          "mtbdd_make_swap exchanges LOW/HIGH");

    /* CX: traverse to control=0 on HIGH only, then swap at target=1 */
    auto cx = mtbdd_with_traverse_to(
        0, mtbdd_with_traverse_to(1, mtbdd_make_swap()), Branch::LR, Branch::R);
    BDD cx_res = cx(tree);
    EXPECT(leaf_int(LOW(LOW(cx_res))) == 1 && leaf_int(HIGH(LOW(cx_res))) == 2,
          "traverse_to CX keeps inactive branch");
    EXPECT(leaf_int(LOW(HIGH(cx_res))) == 4 && leaf_int(HIGH(HIGH(cx_res))) == 3,
          "traverse_to CX swaps under active control");

    auto get_high = mtbdd_get_side(Side::R);
    EXPECT(get_high(tree) == HIGH(tree), "mtbdd_get_side(R) returns HIGH");

    teardown();
}

/* ------------------------------------------------------------------------=
 * Tests: constants vs ISTERMINAL after setvarnum
 * ---------------------------------------------------------------------= */

static void test_constants_vs_terminal() {
    printf("\n== constants vs ISTERMINAL ==\n");
    Fixture f = setup_custom(2);

    EXPECT(!ISTERMINAL(0), "bddfalse is not ISTERMINAL after setvarnum");
    EXPECT(!ISTERMINAL(1), "bddtrue is not ISTERMINAL after setvarnum");
    EXPECT(ISZERO(0) && !ISZERO(1), "ISZERO only for bddfalse");

    BDD t = make_int_leaf(f, 5);
    BDD with0 = mtbdd_apply(t, 0, op_add);
    EXPECT(ISTERMINAL(with0) && leaf_int(with0) == 5,
          "apply treats bddfalse as zero leaf");

    BDD with1 = mtbdd_apply(t, 1, op_add);
    EXPECT(ISTERMINAL(with1) && leaf_int(with1) == 5,
          "apply treats bddtrue as leaf (NULL value → 0 in op_add)");

    teardown();
}

/* ------------------------------------------------------------------------=
 * Tests: freefun frees internals only; MoToBuddy free()s the outer pointer
 * ---------------------------------------------------------------------= */

static int g_freefun_calls = 0;
static void int_freefun_internals_only(void *a) {
    (void)a;
    ++g_freefun_calls;
    /* Do not free(a): outer pointer is free()'d by MoToBuddy (MEDUSA contract). */
}

static void test_freefun_contract() {
    printf("\n== freefun ownership contract ==\n");
    mtbdd_init(10000, 10000);
    bdd_setvarnum(1);
    SETDOMAIN(CUSTOM);

    unsigned tid = mtbdd_new_terminal_type();
    mtbdd_register_compare_function(tid, int_cmp);
    mtbdd_register_hash_function(tid, int_hash);
    mtbdd_register_free_function(tid, int_freefun_internals_only);

    g_freefun_calls = 0;
    BDD a = mtbdd_maketerminal(box_int(4), tid);
    BDD z = mtbdd_maketerminal(box_int(0), tid);
    BDD r = mtbdd_apply(a, z, op_add);
    EXPECT(r == a, "reuse path with freefun does not crash");
    EXPECT(g_freefun_calls >= 1, "freefun called on unused apply result");

    /* Dedup: second equal value is freed (freefun + free), live terminal kept */
    g_freefun_calls = 0;
    BDD a2 = mtbdd_maketerminal(box_int(4), tid);
    EXPECT(a2 == a, "maketerminal dedups equal CUSTOM values");
    EXPECT(g_freefun_calls == 1, "dedup frees unused new value via freefun");

    bdd_done();
}

/* ------------------------------------------------------------------------=
 * Tests: mtbdd_cube2
 * ---------------------------------------------------------------------= */

static void test_cube2() {
    printf("\n== mtbdd_cube2 ==\n");
    Fixture f = setup_custom(3);

    BDD vars[2];
    vars[0] = bdd_makenode(0, 0, 1); /* x0 */
    vars[1] = bdd_makenode(1, 0, 1); /* x1 */
    BDD leaf1 = make_int_leaf(f, 1);
    BDD leaf0 = make_int_leaf(f, 0);

    BDD v0_low = LOW(vars[0]);
    BDD v0_high = HIGH(vars[0]);

    BDD cube = mtbdd_cube2(0b10, 2, vars, leaf1, leaf0);
    EXPECT(LOW(vars[0]) == v0_low && HIGH(vars[0]) == v0_high,
          "cube2 does not mutate variable nodes");
    EXPECT(!ISTERMINAL(cube) && LEVEL(cube) == 0,
          "cube2 returns an internal node at first var level");

    teardown();
}

/* ------------------------------------------------------------------------=
 * Tests: done/re-init
 * ---------------------------------------------------------------------= */

static void test_reinit() {
    printf("\n== re-init / done ==\n");
    Fixture f = setup_custom(1);
    BDD a = make_int_leaf(f, 1);
    EXPECT(ISTERMINAL(a), "terminal before done");
    bdd_done();

    /* Re-init should work after done (no double-free of mtbdd caches) */
    mtbdd_init(10000, 10000);
    bdd_setvarnum(1);
    SETDOMAIN(CUSTOM);
    unsigned tid = mtbdd_new_terminal_type();
    mtbdd_register_compare_function(tid, int_cmp);
    mtbdd_register_hash_function(tid, int_hash);
    BDD b = mtbdd_maketerminal(box_int(9), tid);
    EXPECT(ISTERMINAL(b) && leaf_int(b) == 9, "re-init after done works");
    bdd_done();
}

/* ------------------------------------------------------------------------=
 * main
 * ---------------------------------------------------------------------= */

int main() {
    printf("MoToBuddy MTBDD test suite\n");

    test_terminal_types_and_intern();
    test_long_double_domains();
    test_apply_binary_unary_param();
    test_apply_guarded();
    test_ite();
    test_operation();
    test_combinators();
    test_constants_vs_terminal();
    test_freefun_contract();
    test_cube2();
    test_reinit();

    printf("\n----------------------------------------\n");
    printf("passed=%d failed=%d xfailed=%d\n", g_passed, g_failed, g_xfailed);
    if (g_failed == 0) {
        printf("All tests passed.\n");
        return 0;
    }
    printf("SOME TESTS FAILED.\n");
    return 1;
}
