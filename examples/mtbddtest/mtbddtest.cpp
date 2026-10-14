/**
 * @file mtbddtest.cpp
 * @brief Unit/regression tests for MTBDD terminals, apply, ITE, operation,
 *        and C++ combinators (not FDD).
 * @author Filip Novak / MoToBuddy test harness
 */

#ifdef CPLUSPLUS
#undef CPLUSPLUS
#endif

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "bdd.h"
#include "mtbddop.h"

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

/* Uses high 32 bits of param so entries that share low 32 bits differ. */
static void *op_add_param_hi(void *l, void *r, size_t p) {
    int lv = l ? *(int *)l : 0;
    int rv = r ? *(int *)r : 0;
    int hi = (int)((p >> 16) >> 16);
    return box_int(lv + rv + hi);
}

static void *op_add_unary_param_hi(void *v, size_t p) {
    int x = v ? *(int *)v : 0;
    int hi = (int)((p >> 16) >> 16);
    return box_int(x + hi);
}

/* operation_param: encode hi half into LOW leaf only (HIGH unchanged). */
static BDD op_tag_lo_with_param_hi(size_t ctrl, BDD lo, BDD hi, size_t param) {
    int add = (int)((param >> 16) >> 16);
    unsigned tid = mtbdd_get_terminal_type(lo);
    int *lp = (int *)malloc(sizeof(int));
    *lp = (ISTERMINAL(lo) ? *(int *)mtbdd_getTerminalValue(lo) : 0) + add;
    BDD nlo = mtbdd_maketerminal(lp, tid);
    return bdd_makenode((int)ctrl, nlo, hi);
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

/* Params that share low 32 bits must not false-hit the operation cache.
 * (MEDUSA packs IEEE-754 doubles into size_t; 0.0 and 1.0 both have lo==0.) */
static void test_param_cache_full_sizet() {
    printf("\n== param cache full size_t ==\n");
    Fixture f = setup_custom(2);

    const size_t p_lo0_hi0 = (size_t)0;
    const size_t p_lo0_hi1 = ((size_t)1) << 32; /* same low 32 bits as 0 */

    BDD t3 = make_int_leaf(f, 3);
    BDD t5 = make_int_leaf(f, 5);

    BDD a0 = mtbdd_apply_param(t3, t5, op_add_param_hi, p_lo0_hi0);
    BDD a1 = mtbdd_apply_param(t3, t5, op_add_param_hi, p_lo0_hi1);
    EXPECT(ISTERMINAL(a0) && leaf_int(a0) == 8, "apply_param hi=0 yields 3+5+0");
    EXPECT(ISTERMINAL(a1) && leaf_int(a1) == 9,
          "apply_param cache misses when only high 32 bits differ");

    BDD u0 = mtbdd_apply_unary_param(t5, op_add_unary_param_hi, p_lo0_hi0);
    BDD u1 = mtbdd_apply_unary_param(t5, op_add_unary_param_hi, p_lo0_hi1);
    EXPECT(ISTERMINAL(u0) && leaf_int(u0) == 5, "unary_param hi=0 yields 5+0");
    EXPECT(ISTERMINAL(u1) && leaf_int(u1) == 6,
          "unary_param cache misses when only high 32 bits differ");

    BDD node = bdd_makenode(0, make_int_leaf(f, 1), make_int_leaf(f, 7));
    size_t controls[] = {0};
    BDD o0 = mtbdd_operation_param(node, controls, 0, op_tag_lo_with_param_hi, p_lo0_hi0);
    BDD o1 = mtbdd_operation_param(node, controls, 0, op_tag_lo_with_param_hi, p_lo0_hi1);
    EXPECT(leaf_int(LOW(o0)) == 1 && leaf_int(HIGH(o0)) == 7,
          "operation_param hi=0 leaves LOW=1");
    EXPECT(leaf_int(LOW(o1)) == 2 && leaf_int(HIGH(o1)) == 7,
          "operation_param cache misses when only high 32 bits differ");

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

/* ------------------------------------------------------------------------=
 * Tests: guarded variants with parameters, unary guarded, operation guarded,
 * leaf counting and dot output
 * ---------------------------------------------------------------------= */

static Fixture *g_guard_fixture = nullptr;

/* Binary guard with a parameter: at two leaves, returns a leaf holding
 * l + r + param; above that, asks for recursion. */
static BDD guarded_sum_param(BDD l, BDD r, size_t p) {
    if ((ISTERMINAL(l) || ISCONST(l)) && (ISTERMINAL(r) || ISCONST(r))) {
        FLAG_VALID_APPLY();
        int lv = ISTERMINAL(l) ? leaf_int(l) : 0;
        int rv = ISTERMINAL(r) ? leaf_int(r) : 0;
        return make_int_leaf(*g_guard_fixture, lv + rv + (int)p);
    }
    FLAG_INVALID_APPLY();
    return 0;
}

/* Unary guard: at a leaf, returns a leaf holding value * arg. */
static BDD guarded_scale(BDD l, void *arg) {
    if (ISTERMINAL(l)) {
        FLAG_VALID_APPLY();
        return make_int_leaf(*g_guard_fixture, leaf_int(l) * (int)(size_t)arg);
    }
    if (ISCONST(l)) {
        FLAG_VALID_APPLY();
        return l;
    }
    FLAG_INVALID_APPLY();
    return 0;
}

/* Operation guard: at the target level swaps the children, elsewhere asks
 * the library to keep descending. The guard is also consulted at control
 * levels, so it must check for the target explicitly. */
static size_t g_swap_target = 0;
static BDD guarded_swap(size_t ctrl, BDD node) {
    if (ctrl == g_swap_target && !ISCONST(node) && !ISTERMINAL(node) &&
        (size_t)LEVEL(node) == ctrl) {
        FLAG_VALID_OPERATION();
        return bdd_makenode((int)ctrl, HIGH(node), LOW(node));
    }
    FLAG_INVALID_OPERATION();
    return node;
}

static void test_guarded_variants() {
    printf("\n== guarded variants ==\n");
    Fixture f = setup_custom(3);
    g_guard_fixture = &f;

    BDD left = build_int_tree(f, {1, 2, 3, 4});
    BDD right = bdd_makenode(1, make_int_leaf(f, 10), make_int_leaf(f, 20));

    BDD sum = mtbdd_apply_guarded_param(left, right, guarded_sum_param, 100);
    EXPECT(leaf_int(LOW(LOW(sum))) == 111 && leaf_int(HIGH(LOW(sum))) == 122 &&
               leaf_int(LOW(HIGH(sum))) == 113 && leaf_int(HIGH(HIGH(sum))) == 124,
           "apply_guarded_param adds param at the leaves");
    BDD sum2 = mtbdd_apply_guarded_param(left, right, guarded_sum_param, 100);
    EXPECT(sum2 == sum, "apply_guarded_param result is stable (cache hit)");
    BDD sum3 = mtbdd_apply_guarded_param(left, right, guarded_sum_param, 7);
    EXPECT(leaf_int(LOW(LOW(sum3))) == 18, "apply_guarded_param distinguishes params");

    BDD scaled = mtbdd_apply_unary_guarded(left, guarded_scale, 3);
    EXPECT(leaf_int(LOW(LOW(scaled))) == 3 && leaf_int(HIGH(HIGH(scaled))) == 12,
           "apply_unary_guarded scales every leaf");
    EXPECT(mtbdd_apply_unary_guarded(left, guarded_scale, 3) == scaled,
           "apply_unary_guarded result is stable (cache hit)");
    EXPECT(mtbdd_apply_unary_guarded(0, guarded_scale, 3) == 0,
           "apply_unary_guarded of a constant");

    size_t controls0[] = {0};
    g_swap_target = 0;
    BDD swapped = mtbdd_operation_guarded(left, controls0, 0, guarded_swap);
    EXPECT(leaf_int(LOW(LOW(swapped))) == 3 && leaf_int(LOW(HIGH(swapped))) == 1,
           "operation_guarded swaps level-0 children");
    size_t controls1[] = {1};
    g_swap_target = 1;
    BDD swapped1 = mtbdd_operation_guarded(left, controls1, 0, guarded_swap);
    EXPECT(leaf_int(LOW(LOW(swapped1))) == 2 && leaf_int(HIGH(LOW(swapped1))) == 1 &&
               leaf_int(LOW(HIGH(swapped1))) == 4,
           "operation_guarded descends to level 1 and swaps there");
    size_t controls_cx[] = {0, 1};
    g_swap_target = 1;
    BDD cx = mtbdd_operation_guarded(left, controls_cx, 1, guarded_swap);
    EXPECT(LOW(cx) == LOW(left) && leaf_int(LOW(HIGH(cx))) == 4 && leaf_int(HIGH(HIGH(cx))) == 3,
           "operation_guarded with a control only transforms the HIGH branch");
    g_swap_target = 0;
    EXPECT(mtbdd_operation_guarded(0, controls0, 0, guarded_swap) == 0,
           "operation_guarded of false");

    EXPECT(mtbdd_leaf_count(left) == 4 && mtbdd_leaf_count(left) == 4,
           "leaf_count unmarks, so a second count agrees");

    FILE *tmp = tmpfile();
    buddy_mtbdd_fprintdot(tmp, left);
    rewind(tmp);
    char buf[4096];
    size_t n = fread(buf, 1, sizeof buf - 1, tmp);
    buf[n] = 0;
    fclose(tmp);
    EXPECT(strstr(buf, "digraph") != nullptr && strstr(buf, "\"3\"") != nullptr,
           "mtbdd dot output contains leaf values");

    g_guard_fixture = nullptr;
    teardown();
}

/* ------------------------------------------------------------------------=
 * Differential tests: random MTBDDs against explicit value tables
 *
 * An MTBDD over levels 0..DV-1 is also kept as a table of 2^DV int values
 * (index bit i = value of level i). Random tables with few distinct values
 * reduce to diagrams with skipped levels and shared subgraphs, and value 0
 * is sometimes represented by the constant-0 node, so the recursions meet
 * operands at different levels and constant operands. Each result is read
 * back by evaluating every path and compared with the same operation on
 * the tables. Every operation is repeated (cache hit) and repeated again
 * after a garbage collection (cache reset).
 * ---------------------------------------------------------------------= */

static const int DV = 4;
static const int DN = 1 << DV;

static uint64_t g_mrng = 0x243F6A8885A308D3ULL;
static int mrand(int n) {
    g_mrng ^= g_mrng >> 12;
    g_mrng ^= g_mrng << 25;
    g_mrng ^= g_mrng >> 27;
    return (int)((g_mrng * 0x2545F4914F6CDD1DULL) % (uint64_t)n);
}

typedef std::vector<int> Table;

static Table random_table() {
    Table t(DN);
    int distinct = 1 + mrand(4);
    for (int a = 0; a < DN; ++a)
        t[a] = mrand(distinct);
    /* Make the table independent of some levels. */
    for (int v = 0; v < DV; ++v)
        if (mrand(3) == 0)
            for (int a = 0; a < DN; ++a)
                t[a] = t[a & ~(1 << v)];
    return t;
}

static BDD build_rec(Fixture &f, const Table &t, int level, int base, bool zero_as_const) {
    if (level == DV) {
        int v = t[base];
        if (v == 0 && zero_as_const)
            return 0;
        return make_int_leaf(f, v);
    }
    BDD lo = build_rec(f, t, level + 1, base, zero_as_const);
    PUSHREF(lo);
    BDD hi = build_rec(f, t, level + 1, base | (1 << level), zero_as_const);
    PUSHREF(hi);
    BDD r = bdd_makenode(level, lo, hi);
    POPREF(2);
    return r;
}

static BDD build_table(Fixture &f, const Table &t) {
    return bdd_addref(build_rec(f, t, 0, 0, mrand(2) == 0));
}

static Table eval_all(BDD n0) {
    Table t(DN);
    for (int a = 0; a < DN; ++a) {
        BDD n = n0;
        while (!ISCONST(n) && !ISTERMINAL(n))
            n = ((a >> LEVEL(n)) & 1) ? HIGH(n) : LOW(n);
        t[a] = leaf_int(n);
    }
    return t;
}

/* Guards for the guarded variants: act at leaves, recurse elsewhere. */
static BDD guard_sum(BDD l, BDD r) {
    if ((ISTERMINAL(l) || ISCONST(l)) && (ISTERMINAL(r) || ISCONST(r))) {
        FLAG_VALID_APPLY();
        return make_int_leaf(*g_guard_fixture, leaf_int(l) + leaf_int(r));
    }
    FLAG_INVALID_APPLY();
    return 0;
}

static void *op_scale_param(void *v, size_t p) {
    int x = v ? *(int *)v : 0;
    return box_int(x * (int)p);
}

static BDD guard_cx_swap(size_t ctrl, BDD node) {
    if (ctrl == g_swap_target && !ISCONST(node) && !ISTERMINAL(node) &&
        (size_t)LEVEL(node) == ctrl) {
        FLAG_VALID_OPERATION();
        return bdd_makenode((int)ctrl, HIGH(node), LOW(node));
    }
    FLAG_INVALID_OPERATION();
    return node;
}

struct DiffCheck {
    const char *name;
    int done = 0, bad = 0;
};

static void diff_record(DiffCheck &c, bool ok) {
    ++c.done;
    if (!ok)
        ++c.bad;
}

/* Runs `compute` three times (fresh, cache hit, after gbc) and compares
 * every result with `expect`. */
template <class F>
static bool stable_and_equal(F compute, const Table &expect) {
    BDD r1 = bdd_addref(compute());
    BDD r2 = bdd_addref(compute());
    bdd_gbc();
    BDD r3 = bdd_addref(compute());
    bool ok = eval_all(r1) == expect && r1 == r2 && eval_all(r3) == expect;
    bdd_delref(r1);
    bdd_delref(r2);
    bdd_delref(r3);
    return ok;
}

static void test_differential() {
    printf("\n== differential: random MTBDDs ==\n");
    Fixture f = setup_custom(DV);
    bdd_gbc_hook(NULL);
    g_guard_fixture = &f;

    DiffCheck c_apply{"apply (add and mul)"}, c_param{"apply_param"},
        c_unary{"apply_unary"}, c_uparam{"apply_unary_param"},
        c_guarded{"apply_guarded"}, c_gparam{"apply_guarded_param"},
        c_ugarded{"apply_unary_guarded"}, c_op{"operation (controlled swap)"},
        c_opg{"operation_guarded (controlled swap)"}, c_leaves{"leaf_count"};
    DiffCheck *all[] = {&c_apply, &c_param, &c_unary, &c_uparam, &c_guarded,
                        &c_gparam, &c_ugarded, &c_op, &c_opg, &c_leaves};

    for (int it = 0; it < 120; ++it) {
        Table tl = random_table(), tr = random_table();
        BDD l = build_table(f, tl), r = build_table(f, tr);
        Table sum(DN), prod(DN), neg(DN), sump(DN), scaled(DN);
        size_t p = (size_t)mrand(5);
        for (int a = 0; a < DN; ++a) {
            sum[a] = tl[a] + tr[a];
            prod[a] = tl[a] * tr[a];
            neg[a] = -tl[a];
            sump[a] = tl[a] + tr[a] + (int)p;
            scaled[a] = tl[a] * (int)(p + 1);
        }

        diff_record(c_apply, stable_and_equal([&] { return mtbdd_apply(l, r, op_add); }, sum) &&
                                 stable_and_equal([&] { return mtbdd_apply(l, r, op_mul); }, prod));
        diff_record(c_param, stable_and_equal([&] { return mtbdd_apply_param(l, r, op_add_param, p); }, sump));
        diff_record(c_unary, stable_and_equal([&] { return mtbdd_apply_unary(l, op_neg); }, neg));
        diff_record(c_uparam, stable_and_equal([&] { return mtbdd_apply_unary_param(l, op_scale_param, p + 1); }, scaled));
        diff_record(c_guarded, stable_and_equal([&] { return mtbdd_apply_guarded(l, r, guard_sum); }, sum));
        diff_record(c_gparam, stable_and_equal([&] { return mtbdd_apply_guarded_param(l, r, guarded_sum_param, p); }, sump));
        diff_record(c_ugarded, stable_and_equal([&] { return mtbdd_apply_unary_guarded(l, guarded_scale, p + 1); }, scaled));

        /* Controlled swap: ascending controls, target below all of them. */
        size_t target = (size_t)(1 + mrand(DV - 1));
        std::vector<size_t> ctl;
        for (size_t v = 0; v < target; ++v)
            if (mrand(2) == 0)
                ctl.push_back(v);
        std::vector<size_t> controls = ctl;
        controls.push_back(target);
        Table cx(DN);
        for (int a = 0; a < DN; ++a) {
            bool all_set = true;
            for (size_t c : ctl)
                all_set = all_set && ((a >> c) & 1);
            cx[a] = all_set ? tl[a ^ (1 << target)] : tl[a];
        }
        diff_record(c_op, stable_and_equal([&] {
            return mtbdd_operation(l, controls.data(), ctl.size(), op_swap_at_target);
        }, cx));
        g_swap_target = target;
        diff_record(c_opg, stable_and_equal([&] {
            return mtbdd_operation_guarded(l, controls.data(), ctl.size(), guard_cx_swap);
        }, cx));

        std::vector<int> distinct;
        BDD lr = bdd_addref(build_rec(f, tl, 0, 0, false));
        for (int a = 0; a < DN; ++a) {
            bool seen = false;
            for (int d : distinct)
                seen = seen || d == tl[a];
            if (!seen)
                distinct.push_back(tl[a]);
        }
        diff_record(c_leaves, mtbdd_leaf_count(lr) == distinct.size());
        bdd_delref(lr);

        bdd_delref(l);
        bdd_delref(r);
        if (it % 20 == 19)
            bdd_gbc();
    }

    for (DiffCheck *c : all) {
        char msg[128];
        snprintf(msg, sizeof msg, "%s agrees with value tables on %d random cases", c->name, c->done);
        EXPECT(c->bad == 0, msg);
        if (c->bad)
            printf("      (%d wrong)\n", c->bad);
    }

    g_guard_fixture = nullptr;
    teardown();
}

/* Argument checks: invalid nodes, NULL operations, and use before init. */
static int g_mtbdd_error = 0;
static void record_mtbdd_error(int e) { g_mtbdd_error = e; }

static void test_mtbdd_argument_checks() {
    printf("\n== argument checks ==\n");
    Fixture f = setup_custom(2);
    bdd_error_hook(record_mtbdd_error);
    BDD leaf = make_int_leaf(f, 1);
    BDD node = bdd_makenode(0, leaf, make_int_leaf(f, 2));
    size_t controls[] = {0};

    EXPECT(mtbdd_apply(node, node, nullptr) == 0, "apply with a NULL operation returns false");
    EXPECT(mtbdd_apply_param(node, node, nullptr, 1) == 0, "apply_param with a NULL operation returns false");
    EXPECT(mtbdd_apply_unary(node, nullptr) == 0, "apply_unary with a NULL operation returns false");
    EXPECT(mtbdd_apply_unary_param(node, nullptr, 1) == 0, "apply_unary_param with a NULL operation returns false");
    EXPECT(mtbdd_apply_guarded(node, node, nullptr) == 0, "apply_guarded with a NULL operation returns false");
    EXPECT(mtbdd_apply_guarded_param(node, node, nullptr, 1) == 0, "apply_guarded_param with a NULL operation returns false");
    EXPECT(mtbdd_apply_unary_guarded(node, nullptr, 1) == 0, "apply_unary_guarded with a NULL operation returns false");
    EXPECT(mtbdd_operation(node, controls, 0, nullptr) == 0, "operation with a NULL operation returns false");
    EXPECT(mtbdd_operation_guarded(node, controls, 0, nullptr) == 0, "operation_guarded with a NULL operation returns false");

    const BDD bad = 1 << 30;
    EXPECT(mtbdd_apply(bad, node, op_add) == 0 && mtbdd_apply(node, bad, op_add) == 0,
           "apply rejects an id beyond the table");
    EXPECT(mtbdd_apply_param(bad, node, op_add_param, 1) == 0, "apply_param rejects an id beyond the table");
    EXPECT(mtbdd_apply_unary(bad, op_neg) == 0, "apply_unary rejects an id beyond the table");
    EXPECT(mtbdd_apply_unary_param(bad, op_scale_param, 1) == 0, "apply_unary_param rejects an id beyond the table");
    EXPECT(mtbdd_ite(bad, node, node) == 0 && mtbdd_ite(node, bad, node) == 0 && mtbdd_ite(node, node, bad) == 0,
           "ite rejects an id beyond the table");
    EXPECT(mtbdd_operation(bad, controls, 0, op_swap_at_target) == 0, "operation rejects an id beyond the table");
    g_mtbdd_error = 0;
    mtbdd_apply(node, node, nullptr);
    EXPECT(g_mtbdd_error == BDD_OP, "a NULL operation is reported as BDD_OP");
    g_mtbdd_error = 0;
    mtbdd_apply(bad, node, op_add);
    EXPECT(g_mtbdd_error == BDD_ILLBDD, "an invalid node is reported as BDD_ILLBDD");
    bdd_error_hook(bdd_default_errhandler);
    teardown();
}

/* ------------------------------------------------------------------------=
 * Differential tests of the C++ combinators
 *
 * Oracles on value tables (bit i of an index = value of level i):
 *  - traverse_to(T, swap, pref, ITSELF) swaps level T on the paths it
 *    visits; traverse_to(T, const K, pref, L or R) replaces the LOW or HIGH
 *    child at level T by leaf K on those paths. Descent LR and RL visit
 *    every path; L (R) visits the paths whose bits above T are all 0 (1).
 *    L and R are checked on full trees, where no level is skipped.
 *  - lockstep_to(T, exchange, LR/RL, ...) exchanges the whole subtrees
 *    (ITSELF on both sides) or one chosen child of each side.
 * ---------------------------------------------------------------------= */

static BDD build_full(Fixture &f, const Table &t) {
    /* Unreduced: every level present on every path. */
    checkSameChildren = 0;
    BDD r = bdd_addref(build_rec(f, t, 0, 0, false));
    checkSameChildren = 1;
    return r;
}

static bool path_visited(int a, int target, Branch pref) {
    for (int v = 0; v < target; ++v) {
        int b = (a >> v) & 1;
        if (pref == Branch::L && b != 0) return false;
        if (pref == Branch::R && b != 1) return false;
    }
    return true;
}

static void test_combinator_differential() {
    printf("\n== differential: combinators ==\n");
    Fixture f = setup_custom(DV);
    bdd_gbc_hook(NULL);

    const Branch prefs[] = {Branch::LR, Branch::RL, Branch::L, Branch::R};
    const char *pref_names[] = {"LR", "RL", "L", "R"};
    int bad_swap[4] = {0}, bad_child[4] = {0}, runs = 0;
    int bad_lock = 0, bad_lock_child = 0, bad_make_swap = 0, lock_runs = 0;

    for (int it = 0; it < 40; ++it) {
        Table tl = random_table(), tr = random_table();
        int target = mrand(DV);
        int K = 7 + mrand(3);
        BDD leafK = bdd_addref(make_int_leaf(f, K));
        ++runs;

        for (int pi = 0; pi < 4; ++pi) {
            Branch pref = prefs[pi];
            /* Reduced operands for LR/RL, full trees for L/R. */
            BDD l = (pi < 2 && mrand(2)) ? build_table(f, tl) : build_full(f, tl);

            Table exp_swap(DN), exp_lo(DN), exp_hi(DN);
            for (int a = 0; a < DN; ++a) {
                bool vis = path_visited(a, target, pref);
                exp_swap[a] = vis ? tl[a ^ (1 << target)] : tl[a];
                bool t1 = (a >> target) & 1;
                exp_lo[a] = (vis && !t1) ? K : tl[a];
                exp_hi[a] = (vis && t1) ? K : tl[a];
            }

            NodeOp sw = mtbdd_with_traverse_to(target, mtbdd_make_swap(), pref, Branch::ITSELF);
            BDD r1 = bdd_addref(sw(l));
            if (eval_all(r1) != exp_swap) ++bad_swap[pi];
            /* Swapping twice restores the function. Only for LR/RL: with L
             * or R descent on the reduced first result, the levels above its
             * root are unconstrained, so another set of paths is visited. */
            if (pi < 2) {
                BDD r2 = bdd_addref(sw(r1));
                if (eval_all(r2) != tl) ++bad_swap[pi];
                bdd_delref(r2);
            }
            bdd_delref(r1);

            NodeOp toK = [leafK](BDD) -> BDD { return leafK; };
            BDD c1 = bdd_addref(mtbdd_with_traverse_to(target, toK, pref, Branch::L)(l));
            BDD c2 = bdd_addref(mtbdd_with_traverse_to(target, toK, pref, Branch::R)(l));
            if (eval_all(c1) != exp_lo || eval_all(c2) != exp_hi) ++bad_child[pi];
            bdd_delref(c1);
            bdd_delref(c2);
            bdd_delref(l);
        }

        /* Lockstep with LR and RL descent on both sides. */
        for (int pi = 0; pi < 2; ++pi) {
            ++lock_runs;
            Branch pref = prefs[pi];
            BDD l = mrand(2) ? build_table(f, tl) : build_full(f, tl);
            BDD r = mrand(2) ? build_table(f, tr) : build_full(f, tr);
            BinaryNodeOp exchange = [](BDD x, BDD y) -> BDDPair { return BDDPair(y, x); };

            BDDPair whole = mtbdd_with_lockstep_to(target, exchange, pref, Branch::ITSELF, pref, Branch::ITSELF)(l, r);
            bdd_addref(whole.first);
            bdd_addref(whole.second);
            if (eval_all(whole.first) != tr || eval_all(whole.second) != tl) ++bad_lock;
            bdd_delref(whole.first);
            bdd_delref(whole.second);

            const Branch sides[] = {Branch::L, Branch::R};
            for (Branch sl : sides) {
                for (Branch sr : sides) {
                    int bl = sl == Branch::R, br = sr == Branch::R;
                    Table el(DN), er(DN);
                    for (int a = 0; a < DN; ++a) {
                        int t = (a >> target) & 1;
                        int base = a & ~(1 << target);
                        el[a] = t == bl ? tr[base | (br << target)] : tl[a];
                        er[a] = t == br ? tl[base | (bl << target)] : tr[a];
                    }
                    BDDPair ch = mtbdd_with_lockstep_to(target, exchange, pref, sl, pref, sr)(l, r);
                    bdd_addref(ch.first);
                    bdd_addref(ch.second);
                    if (eval_all(ch.first) != el || eval_all(ch.second) != er) ++bad_lock_child;
                    bdd_delref(ch.first);
                    bdd_delref(ch.second);
                }
            }

            /* mtbdd_make_swap(paramL, paramR) exchanging LOW children is the
             * (L, L) case above, expressed through put_up/put_in. */
            SwapParam lowside{mtbdd_get_side(Side::L), [](BDD node, BDD v) -> BDD {
                                  return bdd_makenode(LEVEL(node), v, HIGH(node));
                              }};
            BDDPair ms = mtbdd_with_lockstep_to(target, mtbdd_make_swap(lowside, lowside), pref,
                                                Branch::ITSELF, pref, Branch::ITSELF)(l, r);
            bdd_addref(ms.first);
            bdd_addref(ms.second);
            Table el(DN), er(DN);
            for (int a = 0; a < DN; ++a) {
                bool t = (a >> target) & 1;
                el[a] = t ? tl[a] : tr[a];
                er[a] = t ? tr[a] : tl[a];
            }
            if (eval_all(ms.first) != el || eval_all(ms.second) != er) ++bad_make_swap;
            bdd_delref(ms.first);
            bdd_delref(ms.second);
            bdd_delref(l);
            bdd_delref(r);
        }
        bdd_delref(leafK);
        bdd_gbc();
    }

    for (int pi = 0; pi < 4; ++pi) {
        char msg[128];
        snprintf(msg, sizeof msg, "traverse_to with descent %s swaps the target level (%d cases)", pref_names[pi], runs);
        EXPECT(bad_swap[pi] == 0, msg);
        snprintf(msg, sizeof msg, "traverse_to with descent %s rewrites the LOW and HIGH child (%d cases)", pref_names[pi], runs);
        EXPECT(bad_child[pi] == 0, msg);
    }
    char msg[128];
    snprintf(msg, sizeof msg, "lockstep_to exchanges whole subtrees (%d cases)", lock_runs);
    EXPECT(bad_lock == 0, msg);
    snprintf(msg, sizeof msg, "lockstep_to exchanges the chosen children (%d cases)", lock_runs * 4);
    EXPECT(bad_lock_child == 0, msg);
    snprintf(msg, sizeof msg, "make_swap(paramL, paramR) exchanges LOW children (%d cases)", lock_runs);
    EXPECT(bad_make_swap == 0, msg);
    teardown();
}

/* ------------------------------------------------------------------------=
 * Parametrised operation, and every terminal domain at scale
 * ---------------------------------------------------------------------= */

/* Swap at the target when the parameter is odd, keep the node otherwise. */
static BDD op_swap_if_odd(size_t ctrl, BDD lo, BDD hi, size_t p) {
    return (p & 1) ? bdd_makenode((int)ctrl, hi, lo) : bdd_makenode((int)ctrl, lo, hi);
}

static void test_operation_param_differential() {
    printf("\n== differential: operation_param ==\n");
    Fixture f = setup_custom(DV);
    bdd_gbc_hook(NULL);
    int bad = 0, n = 0;
    for (int it = 0; it < 80; ++it) {
        Table tl = random_table();
        BDD l = build_table(f, tl);
        size_t target = (size_t)(1 + mrand(DV - 1));
        std::vector<size_t> ctl;
        for (size_t v = 0; v < target; ++v)
            if (mrand(2) == 0)
                ctl.push_back(v);
        std::vector<size_t> controls = ctl;
        controls.push_back(target);
        for (size_t p = 0; p < 4; ++p) {
            Table expect(DN);
            for (int a = 0; a < DN; ++a) {
                bool all_set = true;
                for (size_t c : ctl)
                    all_set = all_set && ((a >> c) & 1);
                expect[a] = (all_set && (p & 1)) ? tl[a ^ (1 << target)] : tl[a];
            }
            /* The parameter's high half takes part in the cache key too. */
            size_t param = p | ((size_t)(it & 3) << 32);
            ++n;
            if (!stable_and_equal([&] {
                    return mtbdd_operation_param(l, controls.data(), ctl.size(), op_swap_if_odd, param);
                }, expect))
                ++bad;
        }
        bdd_delref(l);
    }
    char msg[96];
    snprintf(msg, sizeof msg, "operation_param agrees with value tables on %d random cases", n);
    EXPECT(bad == 0, msg);
    teardown();
}

template <class T> static void *add_values(void *a, void *b) {
    T *r = (T *)malloc(sizeof(T));
    *r = (a ? *(T *)a : T(0)) + (b ? *(T *)b : T(0));
    return r;
}

/* Builds COUNT distinct terminals in one domain (forcing the value tables
 * to grow past their initial 10000 entries), checks interning and value
 * round trips, drops them all, collects, and builds a fresh set to reuse
 * the reclaimed slots. Then applies an addition over a small diagram. */
template <class T>
static void check_domain(domainTypes dom, const char *name, T scale, T offset) {
    const int COUNT = 25000;
    mtbdd_init(200000, 10000);
    bdd_gbc_hook(NULL);
    bdd_setvarnum(2);
    SETDOMAIN(dom);

    bool ok = true;
    std::vector<BDD> leaves(COUNT);
    for (int i = 0; i < COUNT; ++i) {
        T v = (T)i * scale + offset;
        leaves[i] = bdd_addref(mtbdd_maketerminal(&v, 0));
    }
    for (int i = 0; i < COUNT && ok; i += 97) {
        T v = (T)i * scale + offset;
        ok = ISTERMINAL(leaves[i]) && mtbdd_maketerminal(&v, 0) == leaves[i] &&
             *(T *)mtbdd_getTerminalValue(leaves[i]) == v &&
             *(T *)mtbdd_getvaluep(&bddnodes[leaves[i]]) == v;
    }
    bool distinct = leaves[0] != leaves[1] && leaves[COUNT - 1] != leaves[COUNT - 2];
    char msg[128];
    snprintf(msg, sizeof msg, "%s: %d distinct terminals intern and round-trip", name, COUNT);
    EXPECT(ok && distinct, msg);

    for (BDD t : leaves)
        bdd_delref(t);
    bdd_gbc();
    ok = true;
    for (int i = 0; i < COUNT; ++i) {
        T v = (T)(i + COUNT) * scale + offset;
        leaves[i] = bdd_addref(mtbdd_maketerminal(&v, 0));
    }
    for (int i = 0; i < COUNT && ok; i += 89) {
        T v = (T)(i + COUNT) * scale + offset;
        ok = *(T *)mtbdd_getTerminalValue(leaves[i]) == v;
    }
    snprintf(msg, sizeof msg, "%s: terminals rebuilt after garbage collection keep their values", name);
    EXPECT(ok, msg);
    for (BDD t : leaves)
        bdd_delref(t);

    T a = (T)3, b = (T)4, c = (T)10;
    BDD ta = mtbdd_maketerminal(&a, 0), tb = mtbdd_maketerminal(&b, 0), tc = mtbdd_maketerminal(&c, 0);
    BDD l = bdd_addref(bdd_makenode(0, ta, tb));
    BDD r = bdd_addref(bdd_makenode(1, tc, tc == tb ? ta : tb));
    BDD sum = bdd_addref(mtbdd_apply(l, r, add_values<T>));
    bool sums = true;
    for (int x = 0; x < 4; ++x) {
        BDD n0 = sum;
        while (!ISTERMINAL(n0) && !ISCONST(n0))
            n0 = ((x >> LEVEL(n0)) & 1) ? HIGH(n0) : LOW(n0);
        T lv = (x & 1) ? b : a, rv = (x & 2) ? b : c;
        sums = sums && ISTERMINAL(n0) && *(T *)mtbdd_getTerminalValue(n0) == (T)(lv + rv);
    }
    snprintf(msg, sizeof msg, "%s: apply adds values along every path", name);
    EXPECT(sums, msg);
    bdd_delref(l);
    bdd_delref(r);
    bdd_delref(sum);
    bdd_done();
}

static void test_all_domains() {
    printf("\n== terminal domains at scale ==\n");
    check_domain<int>(INTVAL, "INTVAL", 1, -12000);
    check_domain<unsigned>(UNSIGNEDVAL, "UNSIGNEDVAL", 3u, 42u);
    check_domain<float>(FLOATVAL, "FLOATVAL", 0.5f, -1.0f);
    check_domain<long>(LONGVAL, "LONGVAL", 4294967297L, -7L); /* equal 32-bit halves occur */
    check_domain<double>(DOUBLEVAL, "DOUBLEVAL", 0.25, -0.0);
}

int main() {
    printf("MoToBuddy MTBDD test suite\n");

    test_terminal_types_and_intern();
    test_long_double_domains();
    test_apply_binary_unary_param();
    test_param_cache_full_sizet();
    test_apply_guarded();
    test_ite();
    test_operation();
    test_combinators();
    test_constants_vs_terminal();
    test_freefun_contract();
    test_cube2();
    test_reinit();
    test_guarded_variants();
    test_differential();
    test_mtbdd_argument_checks();
    test_combinator_differential();
    test_operation_param_differential();
    test_all_domains();

    printf("\n----------------------------------------\n");
    printf("passed=%d failed=%d xfailed=%d\n", g_passed, g_failed, g_xfailed);
    if (g_failed == 0) {
        printf("All tests passed.\n");
        return 0;
    }
    printf("SOME TESTS FAILED.\n");
    return 1;
}
