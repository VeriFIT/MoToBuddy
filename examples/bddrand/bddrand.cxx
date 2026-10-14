/**
 * @file bddrand.cxx
 * @brief Differential tests of the BDD operators against truth tables.
 *
 * Every boolean function over NVARS = 6 variables is represented twice: as
 * a BDD and as a 64-bit truth table (bit a is the value under the
 * assignment whose bit i is variable i). Random functions are generated
 * from a private, seeded generator, each BDD operator is applied, and the
 * result is converted back to a truth table by walking the BDD and
 * compared with the same operation computed with plain bit arithmetic.
 *
 * Random operands have arbitrary supports, so the recursive cases where
 * the operands' top variables differ, where one side is constant, and
 * where results come from the caches are all exercised.
 *
 * The suite runs twice: once in a roomy node table, and once in a tiny one
 * with automatic sifting enabled. In the second phase the kernel runs out
 * of nodes in the middle of operations and restarts them after
 * reordering, which covers the reorder-and-retry path in every operator.
 * The truth-table walk reads variables, not levels, so it is independent
 * of the variable order.
 *
 * Output is one PASS/FAIL line per operator and phase, so it can be diffed
 * against `expected`.
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "bdd.h"

typedef uint64_t TT; /* truth table */

static const int NVARS = 6;
static const int NASSIGN = 1 << NVARS;
static const int ITER = 150;

static int g_failed = 0;
static int g_passed = 0;

/* ------------------------------------------------------------------------=
 * Deterministic generator (independent of rand(), which the library uses)
 * ---------------------------------------------------------------------= */

static uint64_t g_rng = 0x9E3779B97F4A7C15ULL;

static uint64_t next_u64() {
    /* xorshift64* */
    g_rng ^= g_rng >> 12;
    g_rng ^= g_rng << 25;
    g_rng ^= g_rng >> 27;
    return g_rng * 0x2545F4914F6CDD1DULL;
}

static int next_int(int n) { return (int)(next_u64() % (uint64_t)n); }

/* A random function with a random support: sparse, dense and functions
 * that ignore some variables all occur. */
static TT random_tt() {
    TT t;
    switch (next_int(4)) {
    case 0: t = next_u64(); break;
    case 1: t = next_u64() & next_u64(); break;
    case 2: t = next_u64() | next_u64(); break;
    default: t = next_u64() & next_u64() & next_u64(); break;
    }
    /* Make the function independent of a random subset of variables. */
    for (int v = 0; v < NVARS; ++v) {
        if (next_int(3) == 0) {
            TT r = 0;
            for (int a = 0; a < NASSIGN; ++a)
                if ((t >> (a & ~(1 << v))) & 1)
                    r |= (TT)1 << a;
            t = r;
        }
    }
    return t;
}

/* ------------------------------------------------------------------------=
 * Truth-table helpers
 * ---------------------------------------------------------------------= */

static TT var_tt(int v) {
    TT t = 0;
    for (int a = 0; a < NASSIGN; ++a)
        if ((a >> v) & 1)
            t |= (TT)1 << a;
    return t;
}

static int bit(TT t, int a) { return (int)((t >> a) & 1); }

static TT apply_tt(TT l, TT r, int op) {
    switch (op) {
    case bddop_and: return l & r;
    case bddop_xor: return l ^ r;
    case bddop_or: return l | r;
    case bddop_nand: return ~(l & r);
    case bddop_nor: return ~(l | r);
    case bddop_imp: return ~l | r;
    case bddop_biimp: return ~(l ^ r);
    case bddop_diff: return l & ~r;
    case bddop_less: return ~l & r;
    default: return l | ~r; /* bddop_invimp */
    }
}

/* Quantify variable v: combine the two cofactors with op. */
static TT quant1_tt(TT t, int v, int op) {
    TT r = 0;
    for (int a = 0; a < NASSIGN; ++a) {
        int c0 = bit(t, a & ~(1 << v));
        int c1 = bit(t, a | (1 << v));
        int x = op == bddop_or ? (c0 | c1) : op == bddop_and ? (c0 & c1) : (c0 ^ c1);
        if (x)
            r |= (TT)1 << a;
    }
    return r;
}

static TT quant_tt(TT t, unsigned varset, int op) {
    for (int v = 0; v < NVARS; ++v)
        if ((varset >> v) & 1)
            t = quant1_tt(t, v, op);
    return t;
}

static int popcount(TT t) {
    int n = 0;
    while (t) {
        t &= t - 1;
        ++n;
    }
    return n;
}

/* Unique (xor) quantification as BuDDy implements it. The recursion only
 * quantifies variables it meets: on a path where the remaining function no
 * longer depends on a quantified variable, that variable is left alone
 * instead of giving f xor f = false. Which variables are met depends on the
 * variable order, so the oracle walks variables in the current level order. */
static TT unique_tt_rec(TT t, unsigned varset, int fromlevel) {
    for (int lv = fromlevel; lv < NVARS; ++lv) {
        int v = bdd_level2var(lv);
        if (quant1_tt(t, v, bddop_xor) == 0)
            continue; /* t does not depend on v: v is not met here */
        TT lo = 0, hi = 0;
        for (int a = 0; a < NASSIGN; ++a) {
            if ((t >> (a & ~(1 << v))) & 1)
                lo |= (TT)1 << a;
            if ((t >> (a | (1 << v))) & 1)
                hi |= (TT)1 << a;
        }
        lo = unique_tt_rec(lo, varset, lv + 1);
        hi = unique_tt_rec(hi, varset, lv + 1);
        if ((varset >> v) & 1)
            return lo ^ hi;
        TT vt = 0;
        for (int a = 0; a < NASSIGN; ++a)
            if ((a >> v) & 1)
                vt |= (TT)1 << a;
        return (vt & hi) | (~vt & lo);
    }
    return t;
}

static TT unique_tt(TT t, unsigned varset) { return unique_tt_rec(t, varset, 0); }

/* Top level of a truth table in the current order (NVARS for constants). */
static int top_level(TT t) {
    for (int lv = 0; lv < NVARS; ++lv)
        if (quant1_tt(t, bdd_level2var(lv), bddop_xor) != 0)
            return lv;
    return NVARS;
}

static TT cofactor(TT t, int v, int val) {
    TT r = 0;
    for (int a = 0; a < NASSIGN; ++a) {
        int b = val ? (a | (1 << v)) : (a & ~(1 << v));
        if ((t >> b) & 1)
            r |= (TT)1 << a;
    }
    return r;
}

/* bdd_appuni as BuDDy implements it (appquant_rec). Its documentation says
 * it equals bdd_unique(bdd_apply(l, r, op), var), but the recursion
 * quantifies the variables met while walking l and r, not those of their
 * combination, and its constant shortcuts stop early. This oracle follows
 * the same recursion on truth tables. */
static TT appuni_tt(TT l, TT r, int op, unsigned varset) {
    const TT ONE = ~(TT)0;
    switch (op) {
    case bddop_and:
        if (l == 0 || r == 0) return 0;
        if (l == r) return unique_tt(l, varset);
        if (l == ONE) return unique_tt(r, varset);
        if (r == ONE) return unique_tt(l, varset);
        break;
    case bddop_or:
        if (l == ONE || r == ONE) return ONE;
        if (l == r) return unique_tt(l, varset);
        if (l == 0) return unique_tt(r, varset);
        if (r == 0) return unique_tt(l, varset);
        break;
    case bddop_xor:
        if (l == r) return 0;
        if (l == 0) return unique_tt(r, varset);
        if (r == 0) return unique_tt(l, varset);
        break;
    case bddop_nand:
        if (l == 0 || r == 0) return ONE;
        break;
    case bddop_nor:
        if (l == ONE || r == ONE) return 0;
        break;
    }
    int ll = top_level(l), lr = top_level(r);
    int quantlast = -1;
    for (int v = 0; v < NVARS; ++v)
        if (((varset >> v) & 1) && bdd_var2level(v) > quantlast)
            quantlast = bdd_var2level(v);
    if ((ll == NVARS && lr == NVARS) || (ll > quantlast && lr > quantlast))
        return apply_tt(l, r, op);
    int lv = ll < lr ? ll : lr;
    int v = bdd_level2var(lv);
    TT lo = appuni_tt(cofactor(l, v, 0), cofactor(r, v, 0), op, varset);
    TT hi = appuni_tt(cofactor(l, v, 1), cofactor(r, v, 1), op, varset);
    if ((varset >> v) & 1)
        return lo ^ hi;
    TT vt = var_tt(v);
    return (vt & hi) | (~vt & lo);
}

static unsigned support_tt(TT t) {
    unsigned s = 0;
    for (int v = 0; v < NVARS; ++v)
        if (quant1_tt(t, v, bddop_xor) != 0)
            s |= 1u << v;
    return s;
}

/* ------------------------------------------------------------------------=
 * Conversion between truth tables and BDDs
 * ---------------------------------------------------------------------= */

static bdd from_tt_rec(TT t, int v, int base) {
    if (v < 0)
        return bit(t, base) ? bddtrue : bddfalse;
    bdd lo = from_tt_rec(t, v - 1, base);
    bdd hi = from_tt_rec(t, v - 1, base | (1 << v));
    return (bdd_ithvar(v) & hi) | (bdd_nithvar(v) & lo);
}

static bdd from_tt(TT t) { return from_tt_rec(t, NVARS - 1, 0); }

/* Walks the BDD for every assignment. Uses only bdd_var/low/high, so it is
 * independent of both the operators and the variable order. */
static TT to_tt(const bdd &f) {
    TT t = 0;
    for (int a = 0; a < NASSIGN; ++a) {
        bdd n = f;
        while (n != bddtrue && n != bddfalse)
            n = ((a >> bdd_var(n)) & 1) ? bdd_high(n) : bdd_low(n);
        if (n == bddtrue)
            t |= (TT)1 << a;
    }
    return t;
}

static bdd varset_bdd(unsigned s) {
    bdd c = bddtrue;
    for (int v = 0; v < NVARS; ++v)
        if ((s >> v) & 1)
            c &= bdd_ithvar(v);
    return c;
}

/* A cube fixing the variables in `vars` to the bits of `vals`. */
static bdd cube_bdd(unsigned vars, unsigned vals) {
    bdd c = bddtrue;
    for (int v = 0; v < NVARS; ++v)
        if ((vars >> v) & 1)
            c &= ((vals >> v) & 1) ? bdd_ithvar(v) : bdd_nithvar(v);
    return c;
}

static unsigned random_nonempty_set() {
    unsigned s;
    do
        s = (unsigned)next_int(NASSIGN);
    while (s == 0);
    return s;
}

/* ------------------------------------------------------------------------=
 * Per-operator bookkeeping
 * ---------------------------------------------------------------------= */

enum Check {
    C_APPLY, C_NOT, C_ITE, C_EXIST, C_FORALL, C_UNIQUE, C_APPEX, C_APPALL,
    C_APPUNI, C_RESTRICT, C_CONSTRAIN, C_SIMPLIFY, C_COMPOSE, C_VECCOMPOSE,
    C_REPLACE, C_SATCOUNT, C_SATCOUNTSET, C_SATONE, C_SATONESET, C_FULLSATONE,
    C_ALLSAT, C_SUPPORT, C_NUM
};

static const char *check_names[C_NUM] = {
    "apply (all 10 operators)", "not", "ite", "exist", "forall", "unique",
    "appex", "appall", "appuni", "restrict", "constrain", "simplify",
    "compose", "veccompose", "replace", "satcount", "satcountset", "satone",
    "satoneset", "fullsatone", "allsat", "support"};

static int g_bad[C_NUM];
static int g_done[C_NUM];

static void record(Check c, bool ok) {
    ++g_done[c];
    if (!ok)
        ++g_bad[c];
}

/* allsat handler: accumulates the covered truth table. */
static TT g_allsat_tt;
static bool g_allsat_ok;
static void allsat_handler(char *varset, int size) {
    if (size < NVARS) {
        g_allsat_ok = false;
        return;
    }
    for (int a = 0; a < NASSIGN; ++a) {
        bool match = true;
        for (int v = 0; v < NVARS && match; ++v)
            if (varset[v] >= 0 && varset[v] != ((a >> v) & 1))
                match = false;
        if (match) {
            if ((g_allsat_tt >> a) & 1)
                g_allsat_ok = false; /* cubes must be disjoint */
            g_allsat_tt |= (TT)1 << a;
        }
    }
}

/* ------------------------------------------------------------------------=
 * One round of every operator on fresh random operands
 * ---------------------------------------------------------------------= */

static void round_of_checks() {
    TT tl = random_tt(), tr = random_tt(), th = random_tt();
    bdd l = from_tt(tl), r = from_tt(tr), h = from_tt(th);

    /* apply, every operator; each called twice so the second is a cache hit */
    bool ok = true;
    for (int op = bddop_and; op <= bddop_invimp; ++op) {
        bdd res = bdd_apply(l, r, op);
        ok = ok && to_tt(res) == apply_tt(tl, tr, op) && bdd_apply(l, r, op) == res;
    }
    ok = ok && to_tt(bdd_apply(l, bddtrue, bddop_and)) == tl;
    ok = ok && to_tt(bdd_apply(bddfalse, r, bddop_or)) == tr;
    record(C_APPLY, ok);

    record(C_NOT, to_tt(bdd_not(l)) == ~tl);

    record(C_ITE, to_tt(bdd_ite(l, r, h)) == ((tl & tr) | (~tl & th)) &&
                      to_tt(bdd_ite(l, r, bddfalse)) == (tl & tr) &&
                      to_tt(bdd_ite(l, bddtrue, h)) == (tl | th));

    unsigned qs = random_nonempty_set();
    bdd qv = varset_bdd(qs);
    record(C_EXIST, to_tt(bdd_exist(l, qv)) == quant_tt(tl, qs, bddop_or));
    record(C_FORALL, to_tt(bdd_forall(l, qv)) == quant_tt(tl, qs, bddop_and));
    record(C_UNIQUE, to_tt(bdd_unique(l, qv)) == unique_tt(tl, qs));

    int op = next_int(bddop_invimp + 1);
    TT applied = apply_tt(tl, tr, op);
    record(C_APPEX, to_tt(bdd_appex(l, r, op, qv)) == quant_tt(applied, qs, bddop_or));
    record(C_APPALL, to_tt(bdd_appall(l, r, op, qv)) == quant_tt(applied, qs, bddop_and));
    record(C_APPUNI, to_tt(bdd_appuni(l, r, op, qv)) == appuni_tt(tl, tr, op, qs));

    /* restrict by a random cube */
    unsigned rv = random_nonempty_set(), rval = (unsigned)next_int(NASSIGN);
    TT rexp = 0;
    for (int a = 0; a < NASSIGN; ++a) {
        int b = (a & ~(int)rv) | (int)(rval & rv);
        if (bit(tl, b))
            rexp |= (TT)1 << a;
    }
    record(C_RESTRICT, to_tt(bdd_restrict(l, cube_bdd(rv, rval))) == rexp);

    /* constrain and simplify agree with f on the care set */
    TT care = tr | (tr == 0 ? 1 : 0); /* never empty */
    bdd careb = from_tt(care);
    /* With an empty care set any result is allowed, so only the
     * defining property on a non-empty care set is checked. */
    record(C_CONSTRAIN, (to_tt(bdd_constrain(l, careb)) & care) == (tl & care));
    record(C_SIMPLIFY, (to_tt(bdd_simplify(l, careb)) & care) == (tl & care));

    /* compose: substitute h for variable cv */
    int cv = next_int(NVARS);
    TT cexp = 0;
    for (int a = 0; a < NASSIGN; ++a) {
        int b = bit(th, a) ? (a | (1 << cv)) : (a & ~(1 << cv));
        if (bit(tl, b))
            cexp |= (TT)1 << a;
    }
    record(C_COMPOSE, to_tt(bdd_compose(l, h, cv)) == cexp);

    /* veccompose: simultaneously substitute random functions for a subset */
    {
        bddPair *p = bdd_newpair();
        unsigned vs = random_nonempty_set();
        TT subst[NVARS];
        for (int v = 0; v < NVARS; ++v) {
            subst[v] = var_tt(v);
            if ((vs >> v) & 1) {
                subst[v] = random_tt();
                bdd_setbddpair(p, v, from_tt(subst[v]));
            }
        }
        TT vexp = 0;
        for (int a = 0; a < NASSIGN; ++a) {
            int b = 0;
            for (int v = 0; v < NVARS; ++v)
                if (bit(subst[v], a))
                    b |= 1 << v;
            if (bit(tl, b))
                vexp |= (TT)1 << a;
        }
        record(C_VECCOMPOSE, to_tt(bdd_veccompose(l, p)) == vexp);
        bdd_freepair(p);
    }

    /* replace: rename variables by a random permutation */
    {
        int perm[NVARS];
        for (int v = 0; v < NVARS; ++v)
            perm[v] = v;
        for (int v = NVARS - 1; v > 0; --v) {
            int w = next_int(v + 1);
            int tmp = perm[v];
            perm[v] = perm[w];
            perm[w] = tmp;
        }
        bddPair *p = bdd_newpair();
        for (int v = 0; v < NVARS; ++v)
            bdd_setpair(p, v, perm[v]);
        /* result(x) = l(y) with y_v = x_perm[v] */
        TT pexp = 0;
        for (int a = 0; a < NASSIGN; ++a) {
            int b = 0;
            for (int v = 0; v < NVARS; ++v)
                if ((a >> perm[v]) & 1)
                    b |= 1 << v;
            if (bit(tl, b))
                pexp |= (TT)1 << a;
        }
        record(C_REPLACE, to_tt(bdd_replace(l, p)) == pexp);
        bdd_freepair(p);
    }

    /* counting */
    int pc = popcount(tl);
    record(C_SATCOUNT, bdd_satcount(l) == (double)pc &&
                           (pc == 0 || bdd_satcountln(l) > -1.0));
    {
        /* satcountset over the support plus any extra variables */
        unsigned sup = support_tt(tl) | random_nonempty_set();
        int k = popcount(sup);
        double expect = (double)pc / (double)(1 << (NVARS - k));
        record(C_SATCOUNTSET, bdd_satcountset(l, varset_bdd(sup)) == expect);
    }

    /* satisfying assignments */
    {
        bdd s = bdd_satone(l);
        TT ts = to_tt(s);
        record(C_SATONE, tl == 0 ? s == bddfalse : (ts != 0 && (ts & ~tl) == 0));
        unsigned set = random_nonempty_set();
        bdd so = bdd_satoneset(l, varset_bdd(set), bddtrue);
        TT tso = to_tt(so);
        bool ok2 = tl == 0 ? so == bddfalse : (tso != 0 && (tso & ~tl) == 0);
        /* every variable in the set is fixed by the result */
        if (tl != 0)
            for (int v = 0; v < NVARS; ++v)
                if ((set >> v) & 1)
                    ok2 = ok2 && (quant1_tt(tso, v, bddop_xor) != 0 || tso == 0);
        record(C_SATONESET, ok2);
        bdd fs = bdd_fullsatone(l);
        TT tfs = to_tt(fs);
        record(C_FULLSATONE, tl == 0 ? fs == bddfalse : (popcount(tfs) == 1 && (tfs & ~tl) == 0));
        g_allsat_tt = 0;
        g_allsat_ok = true;
        bdd_allsat(l, allsat_handler);
        record(C_ALLSAT, g_allsat_ok && g_allsat_tt == tl);
    }

    record(C_SUPPORT, bdd_support(l) == (support_tt(tl) == 0 ? bddfalse : varset_bdd(support_tt(tl))));
}

/* ------------------------------------------------------------------------=
 * Argument checks on every entry point
 * ---------------------------------------------------------------------= */

static int g_last_error = 0;
static void record_error(int e) { g_last_error = e; }

/* Calls through the C API with node ids that are out of range or freed. */
static void test_argument_checks() {
    printf("\n== argument checks ==\n");
    bdd_init(1000, 100);
    bdd_gbc_hook(NULL);
    bdd_error_hook(record_error);
    bdd_setvarnum(NVARS);

    /* A node that has been freed: build it, drop it, collect. */
    BDD freed;
    {
        bdd tmp = bdd_ithvar(0) & bdd_ithvar(1);
        freed = tmp.id();
    }
    bdd_gbc();

    const BDD bad_ids[3] = {-5, 1 << 30, freed};
    const char *bad_names[3] = {"negative id", "id beyond the table", "freed node"};
    const BDD ok = bdd_ithvar(2).id();
    bddPair *p = bdd_newpair();

    for (int k = 0; k < 3; ++k) {
        BDD b = bad_ids[k];
        int caught = 0, calls = 0;
#define TRY(expr)                                                              \
    do {                                                                       \
        g_last_error = 0;                                                      \
        (void)(expr);                                                          \
        ++calls;                                                               \
        if (g_last_error == BDD_ILLBDD)                                        \
            ++caught;                                                          \
    } while (0)
        TRY(bdd_not(b));
        TRY(bdd_apply(b, ok, bddop_and));
        TRY(bdd_apply(ok, b, bddop_and));
        TRY(bdd_ite(b, ok, ok));
        TRY(bdd_ite(ok, b, ok));
        TRY(bdd_ite(ok, ok, b));
        TRY(bdd_restrict(b, ok));
        TRY(bdd_restrict(ok, b));
        TRY(bdd_constrain(b, ok));
        TRY(bdd_constrain(ok, b));
        TRY(bdd_replace(b, p));
        TRY(bdd_compose(b, ok, 0));
        TRY(bdd_compose(ok, b, 0));
        TRY(bdd_veccompose(b, p));
        TRY(bdd_simplify(b, ok));
        TRY(bdd_simplify(ok, b));
        TRY(bdd_exist(b, ok));
        TRY(bdd_exist(ok, b));
        TRY(bdd_forall(b, ok));
        TRY(bdd_unique(b, ok));
        TRY(bdd_appex(b, ok, bddop_and, ok));
        TRY(bdd_appex(ok, b, bddop_and, ok));
        TRY(bdd_appex(ok, ok, bddop_and, b));
        TRY(bdd_appall(b, ok, bddop_and, ok));
        TRY(bdd_appuni(b, ok, bddop_and, ok));
        TRY(bdd_support(b));
        TRY(bdd_satone(b));
        TRY(bdd_satoneset(b, ok, ok));
        TRY(bdd_fullsatone(b));
        TRY(bdd_satcount(b));
        TRY(bdd_satcountset(b, ok));
        TRY(bdd_satcountln(b));
        TRY(bdd_satcountlnset(b, ok));
        TRY(bdd_nodecount(b));
        TRY(bdd_varprofile(b));
        TRY(bdd_pathcount(b));
        TRY(bdd_var(b));
        TRY(bdd_low(b));
        TRY(bdd_high(b));
        if (b >= 2) { /* ids below 2 are constants to addref/delref */
            TRY(bdd_addref(b));
            TRY(bdd_delref(b));
        }
#undef TRY
        char msg[96];
        snprintf(msg, sizeof msg, "every entry point rejects a %s with BDD_ILLBDD", bad_names[k]);
        if (caught == calls) {
            printf("PASS: %s\n", msg);
            ++g_passed;
        } else {
            printf("FAIL: %s (%d of %d)\n", msg, caught, calls);
            ++g_failed;
        }
    }

    /* Out-of-range operator codes. */
    g_last_error = 0;
    bdd_apply(ok, ok, -1);
    int e1 = g_last_error;
    g_last_error = 0;
    bdd_appex(ok, ok, 99, ok);
    int e2 = g_last_error;
    g_last_error = 0;
    bdd_appall(ok, ok, 99, ok);
    int e3 = g_last_error;
    g_last_error = 0;
    bdd_appuni(ok, ok, 99, ok);
    int e4 = g_last_error;
    if (e1 == BDD_OP && e2 == BDD_OP && e3 == BDD_OP && e4 == BDD_OP) {
        printf("PASS: unknown operator codes are BDD_OP\n");
        ++g_passed;
    } else {
        printf("FAIL: unknown operator codes are BDD_OP\n");
        ++g_failed;
    }

    bdd_freepair(p);
    bdd_done();

    /* Every entry point refuses to run before bdd_init. */
    int caught = 0, calls = 0;
#define TRY(expr)                                                              \
    do {                                                                       \
        g_last_error = 0;                                                      \
        (void)(expr);                                                          \
        ++calls;                                                               \
        if (g_last_error == BDD_RUNNING)                                       \
            ++caught;                                                          \
    } while (0)
    bdd_error_hook(record_error);
    TRY(bdd_not(2));
    TRY(bdd_apply(2, 2, bddop_and));
    TRY(bdd_ite(2, 2, 2));
    TRY(bdd_restrict(2, 2));
    TRY(bdd_constrain(2, 2));
    TRY(bdd_compose(2, 2, 0));
    TRY(bdd_simplify(2, 2));
    TRY(bdd_exist(2, 2));
    TRY(bdd_forall(2, 2));
    TRY(bdd_unique(2, 2));
    TRY(bdd_appex(2, 2, bddop_and, 2));
    TRY(bdd_support(2));
    TRY(bdd_satone(2));
    TRY(bdd_fullsatone(2));
    TRY(bdd_satcount(2));
    TRY(bdd_nodecount(2));
    TRY(bdd_setvarnum(4));
#undef TRY
    /* Without any variables these report an unknown variable instead. */
    g_last_error = 0;
    bdd_ithvar(0);
    int e_ith = g_last_error;
    g_last_error = 0;
    bdd_nithvar(0);
    if (e_ith == BDD_VAR && g_last_error == BDD_VAR)
        ++caught;
    ++calls;
    if (caught == calls) {
        printf("PASS: entry points before bdd_init report BDD_RUNNING\n");
        ++g_passed;
    } else {
        printf("FAIL: entry points before bdd_init report BDD_RUNNING (%d of %d)\n", caught, calls);
        ++g_failed;
    }
}

/* ------------------------------------------------------------------------=
 * Phases
 * ---------------------------------------------------------------------= */

static int g_reorders = 0;
static void count_reorders(int prestate) {
    if (prestate)
        ++g_reorders;
}

static void report_phase(const char *phase) {
    for (int c = 0; c < C_NUM; ++c) {
        char msg[128];
        snprintf(msg, sizeof msg, "%s: %s agrees with truth tables on %d random cases",
                 phase, check_names[c], g_done[c]);
        if (g_bad[c] == 0) {
            printf("PASS: %s\n", msg);
            ++g_passed;
        } else {
            printf("FAIL: %s (%d wrong)\n", msg, g_bad[c]);
            ++g_failed;
        }
    }
}

static void run_session(int nodes, int cache, bool autoreorder, int iterations) {
    bdd_init(nodes, cache);
    bdd_gbc_hook(NULL);
    bdd_error_hook(record_error);
    bdd_setvarnum(NVARS);
    if (autoreorder) {
        bdd_varblockall();
        bdd_reorder_verbose(0);
        bdd_reorder_hook(count_reorders);
        bdd_autoreorder(BDD_REORDER_SIFT);
    }

    /* Keep a pool of live functions so the table stays under pressure. */
    std::vector<bdd> pool;
    for (int i = 0; i < iterations; ++i) {
        round_of_checks();
        if (autoreorder) {
            pool.push_back(from_tt(random_tt()));
            if (pool.size() > 40)
                pool.erase(pool.begin());
        }
    }
    if (autoreorder)
        bdd_reorder_hook(NULL);
    pool.clear();
    bdd_done();
}

/* The automatic-reordering threshold starts at the table size and doubles
 * after every reordering, so the stress phase is split into short sessions,
 * each starting from a fresh bdd_init. */
static void run_phase(const char *phase, int nodes, int cache, bool autoreorder) {
    printf("\n== %s ==\n", phase);
    memset(g_bad, 0, sizeof g_bad);
    memset(g_done, 0, sizeof g_done);
    g_reorders = 0;
    if (autoreorder)
        for (int s = 0; s < ITER / 10; ++s)
            run_session(nodes, cache, true, 10);
    else
        run_session(nodes, cache, false, ITER);
    report_phase(phase);
    if (autoreorder) {
        bool reordered = g_reorders >= ITER / 10;
        printf("%s: automatic reordering ran during the operations\n", reordered ? "PASS" : "FAIL");
        reordered ? ++g_passed : ++g_failed;
        if (getenv("BDDRAND_DEBUG"))
            fprintf(stderr, "reorders: %d\n", g_reorders);
    }
}

int main() {
    printf("MoToBuddy BDD differential test suite\n");
    run_phase("default table", 10000, 1000, false);
    run_phase("tiny table with automatic sifting", 50, 10, true);
    test_argument_checks();
    printf("\nSummary: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
