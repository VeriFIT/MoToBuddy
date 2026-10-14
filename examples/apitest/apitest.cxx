/**
 * @file apitest.cxx
 * @brief Self-checking regression tests for the plain BDD API inherited from
 *        BuDDy: kernel, operators, pairs, reordering, finite domains (fdd),
 *        bit vectors (bvec), file I/O and the C++ wrappers.
 *
 * Every check prints one PASS/FAIL line, so the output is deterministic and
 * can be diffed against `expected`. Where an exact answer is awkward to
 * state by hand (bit-vector arithmetic on variables), the test enumerates
 * all assignments and evaluates the BDD as an oracle.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "bdd.h"
#include "fdd.h"
#include "bvec.h"

/* ------------------------------------------------------------------------=
 * Harness
 * ---------------------------------------------------------------------= */

static int g_failed = 0;
static int g_passed = 0;

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

/* Error hook that records instead of exiting, so error paths can be tested. */
static int g_last_error = 0;
static void record_error(int e) { g_last_error = e; }

static int g_resize_calls = 0;
static void record_resize(int oldsize, int newsize) {
    (void)oldsize;
    (void)newsize;
    ++g_resize_calls;
}

/* Read a whole FILE* (positioned anywhere) into a string. */
static std::string slurp(FILE *f) {
    std::string s;
    rewind(f);
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        s.append(buf, n);
    return s;
}

static std::string slurp_path(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return "";
    std::string s = slurp(f);
    fclose(f);
    return s;
}

static bool contains(const std::string &s, const char *needle) {
    return s.find(needle) != std::string::npos;
}

/* Cube fixing variable `var` to `val` for every (var, val) pair. */
static bdd cube_of(const std::vector<std::pair<int, int>> &lits) {
    bdd c = bddtrue;
    for (auto &l : lits)
        c &= l.second ? bdd_ithvar(l.first) : bdd_nithvar(l.first);
    return c;
}

/* Evaluate f under a (complete enough) assignment: restrict must reach a
 * constant. */
static bdd eval_under(const bdd &f, const bdd &cube) {
    return bdd_restrict(f, cube);
}

/* ------------------------------------------------------------------------=
 * Kernel
 * ---------------------------------------------------------------------= */

static void test_kernel() {
    printf("\n== kernel ==\n");

    EXPECT(!bdd_isrunning(), "not running before bdd_init");
    bdd_init(1000, 100);
    bdd_gbc_hook(NULL); /* keep garbage collection quiet */
    bdd_error_hook(record_error);
    EXPECT(bdd_isrunning(), "running after bdd_init");

    EXPECT(bdd_versionstr() != NULL && strlen(bdd_versionstr()) > 0, "versionstr non-empty");
    EXPECT(bdd_versionnum() > 0, "versionnum positive");

    bdd_setvarnum(4);
    EXPECT(bdd_varnum() == 4, "varnum after setvarnum");
    EXPECT(bdd_extvarnum(2) == 4, "extvarnum returns old count");
    EXPECT(bdd_varnum() == 6, "varnum after extvarnum");

    bdd a = bdd_ithvar(0);
    bdd na = bdd_nithvar(0);
    EXPECT(bdd_var(a) == 0, "bdd_var of ithvar");
    EXPECT(bdd_low(a) == bddfalse && bdd_high(a) == bddtrue, "ithvar children");
    EXPECT(bdd_low(na) == bddtrue && bdd_high(na) == bddfalse, "nithvar children");
    EXPECT(bdd_true() == bddtrue && bdd_false() == bddfalse, "bdd_true/bdd_false");

    /* Tuning knobs return the previous value. */
    int oldinc = bdd_setmaxincrease(50000);
    EXPECT(oldinc > 0, "setmaxincrease returns old value");
    EXPECT(bdd_setmaxincrease(oldinc) == 50000, "setmaxincrease round trip");
    int oldfree = bdd_setminfreenodes(30);
    EXPECT(oldfree == 20, "default minfreenodes is 20");
    EXPECT(bdd_setminfreenodes(oldfree) == 30, "setminfreenodes round trip");
    g_last_error = 0;
    bdd_setminfreenodes(150);
    EXPECT(g_last_error == BDD_RANGE, "minfreenodes above 100 is BDD_RANGE");
    int oldmax = bdd_setmaxnodenum(100000);
    EXPECT(oldmax == 0, "default maxnodenum is 0 (unlimited)");
    EXPECT(bdd_setmaxnodenum(0) == 100000, "setmaxnodenum round trip");
    g_last_error = 0;
    bdd_setmaxnodenum(10);
    EXPECT(g_last_error == BDD_NODES, "maxnodenum below current size is BDD_NODES");
    EXPECT(bdd_setcacheratio(4) == 0, "setcacheratio accepted");
    g_last_error = 0;
    bdd_setcacheratio(0);
    EXPECT(g_last_error == BDD_RANGE, "setcacheratio 0 is BDD_RANGE");

    /* Error paths. */
    g_last_error = 0;
    bdd bad = bdd_ithvar(1000);
    EXPECT(g_last_error == BDD_VAR && bad == bddfalse, "ithvar out of range is BDD_VAR");
    g_last_error = 0;
    bdd_nithvar(-1);
    EXPECT(g_last_error == BDD_VAR, "nithvar out of range is BDD_VAR");
    g_last_error = 0;
    bdd_setvarnum(2);
    EXPECT(g_last_error == BDD_DECVNUM, "shrinking varnum is BDD_DECVNUM");
    bdd_clear_error();
    EXPECT(bdd_errstring(BDD_MEMORY) != NULL, "errstring for a known code");
    EXPECT(bdd_errstring(-1000) == NULL, "errstring for an unknown code is NULL");
    EXPECT(bdd_errstring(0) == NULL, "errstring for success is NULL");

    /* Statistics. */
    bddStat st;
    bdd_stats(&st);
    EXPECT(st.varnum == 6 && st.nodenum >= 1000, "bdd_stats reports varnum and nodenum");
    bddCacheStat cst;
    bdd_cachestats(&cst);
    EXPECT(cst.uniqueAccess >= 0, "bdd_cachestats readable");
    FILE *tmp = tmpfile();
    bdd_fprintstat(tmp);
    EXPECT(contains(slurp(tmp), "Cache statistics"), "fprintstat writes statistics");
    fclose(tmp);
    EXPECT(bdd_getnodenum() > 0, "getnodenum positive");
    EXPECT(bdd_getallocnum() >= bdd_getnodenum(), "getallocnum at least nodenum");

    /* Explicit references and garbage collection. */
    int before_gbc = bdd_getnodenum();
    {
        bdd tmpnode = a & bdd_ithvar(1);
        BDD raw = bdd_addref(tmpnode.id());
        bdd_gbc();
        EXPECT(bdd_getnodenum() >= 1, "nodes remain after gbc");
        bdd_delref(raw);
    }
    bdd_gbc();
    EXPECT(bdd_getnodenum() <= before_gbc, "gbc reclaims the unreferenced node");

    /* Node table growth triggers the resize hook: a 6-bit multiplier
     * relation needs far more than the initial 1000 nodes. */
    bdd_resize_hook(record_resize);
    g_resize_calls = 0;
    int alloc_before = bdd_getallocnum();
    {
        bdd_extvarnum(18);
        bvec x = bvec_var(8, 0, 1);
        bvec y = bvec_var(8, 8, 1);
        bvec z = bvec_var(8, 16, 1);
        bdd big = bvec_coerce(8, x * y) == z;
        EXPECT(big != bddfalse && bdd_nodecount(big) > 1000, "built a large multiplier BDD");
    }
    EXPECT(bdd_getallocnum() > alloc_before, "node table grew");
    EXPECT(g_resize_calls >= 1, "resize hook called when the table grows");
    bdd_resize_hook(NULL);

    bdd_done();
    EXPECT(!bdd_isrunning(), "not running after bdd_done");
}

/* ------------------------------------------------------------------------=
 * Operators
 * ---------------------------------------------------------------------= */

static int g_allsat_calls = 0;
static void count_allsat(char *varset, int size) {
    (void)varset;
    (void)size;
    ++g_allsat_calls;
}

static void test_operators() {
    printf("\n== operators ==\n");
    bdd_init(1000, 100);
    bdd_gbc_hook(NULL);
    bdd_error_hook(record_error);
    bdd_setvarnum(4);

    bdd a = bdd_ithvar(0), b = bdd_ithvar(1), c = bdd_ithvar(2), d = bdd_ithvar(3);

    EXPECT(bdd_not(a) == bdd_nithvar(0), "bdd_not");
    EXPECT(bdd_and(a, b) == bdd_apply(a, b, bddop_and), "bdd_and");
    EXPECT(bdd_or(a, b) == bdd_apply(a, b, bddop_or), "bdd_or");
    EXPECT(bdd_xor(a, b) == bdd_apply(a, b, bddop_xor), "bdd_xor");
    EXPECT(bdd_imp(a, b) == (!a | b), "bdd_imp");
    EXPECT(bdd_biimp(a, b) == !(a ^ b), "bdd_biimp");
    EXPECT(bdd_apply(a, b, bddop_nand) == !(a & b), "bddop_nand");
    EXPECT(bdd_apply(a, b, bddop_nor) == !(a | b), "bddop_nor");
    EXPECT(bdd_apply(a, b, bddop_diff) == (a & !b), "bddop_diff");
    EXPECT(bdd_apply(a, b, bddop_less) == (!a & b), "bddop_less");
    EXPECT(bdd_apply(a, b, bddop_invimp) == (a | !b), "bddop_invimp");
    g_last_error = 0;
    bdd_apply(a, b, 42);
    EXPECT(g_last_error == BDD_OP, "unknown operator is BDD_OP");

    EXPECT(bdd_ite(a, b, c) == ((a & b) | (!a & c)), "bdd_ite");
    EXPECT(bdd_ite(bddtrue, b, c) == b && bdd_ite(bddfalse, b, c) == c, "bdd_ite constants");

    bdd f = (a & b) | c;
    EXPECT(bdd_restrict(f, a) == (b | c), "restrict a=1");
    EXPECT(bdd_restrict(f, bdd_nithvar(0)) == c, "restrict a=0");
    EXPECT(bdd_restrict(f, a & bdd_nithvar(2)) == b, "restrict two literals");
    EXPECT(bdd_restrict(f, bddtrue) == f, "restrict by true is identity");

    EXPECT(bdd_constrain(f, bddtrue) == f, "constrain by true is identity");
    EXPECT((bdd_constrain(f, c | d) & (c | d)) == (f & (c | d)), "constrain agrees on the care set");
    EXPECT(bdd_constrain(f, bddfalse) == bddfalse, "constrain by false");

    EXPECT((bdd_simplify(f, c | d) & (c | d)) == (f & (c | d)), "simplify agrees on the domain");
    EXPECT(bdd_simplify(f, bddtrue) == f, "simplify by true is identity");
    EXPECT(bdd_simplify(f, bddfalse) == bddfalse, "simplify by false");

    EXPECT(bdd_compose(a & b, c | d, 0) == ((c | d) & b), "compose");
    EXPECT(bdd_compose(a & b, c, 3) == (a & b), "compose of absent variable");

    bddPair *p = bdd_newpair();
    bdd_setpair(p, 0, 2);
    EXPECT(bdd_replace(a & b, p) == (c & b), "replace 0->2");
    bdd_resetpair(p);
    EXPECT(bdd_replace(a & b, p) == (a & b), "resetpair makes replace identity");
    bdd_setbddpair(p, 0, c | d);
    EXPECT(bdd_veccompose(a & b, p) == ((c | d) & b), "veccompose");
    bdd_resetpair(p);
    int from[2] = {0, 1}, to[2] = {2, 3};
    bdd_setpairs(p, from, to, 2);
    EXPECT(bdd_replace(a & b, p) == (c & d), "setpairs");
    bdd_resetpair(p);
    bdd tobdd[2] = {c, d ^ c};
    bdd_setbddpairs(p, from, tobdd, 2);
    EXPECT(bdd_veccompose(a & b, p) == (c & (d ^ c)), "setbddpairs");
    g_last_error = 0;
    bdd_setpair(p, 10, 0);
    EXPECT(g_last_error == BDD_VAR, "setpair out of range is BDD_VAR");
    bdd_freepair(p);

    EXPECT(bdd_exist(a & b, a) == b, "exist");
    EXPECT(bdd_forall(a | b, a) == b, "forall");
    EXPECT(bdd_unique(a & b, a) == b, "unique over a&b");
    EXPECT(bdd_unique(a | b, a) == !b, "unique over a|b");
    EXPECT(bdd_exist(f, a & c) == bddtrue, "exist over two variables");
    EXPECT(bdd_forall(f, a & b & c) == bddfalse, "forall over three variables");
    EXPECT(bdd_appex(a & c, b, bddop_and, a) == (c & b), "appex");
    EXPECT(bdd_appall(a | c, b, bddop_or, a) == (c | b), "appall");
    EXPECT(bdd_appuni(a, b, bddop_and, a) == b, "appuni");
    EXPECT(bdd_appex(a & c, b, bddop_and, a) == bdd_exist(bdd_and(a & c, b), a), "appex equals exist of apply");
    EXPECT(bdd_appall(a | c, b, bddop_or, a) == bdd_forall(bdd_or(a | c, b), a), "appall equals forall of apply");
    EXPECT(bdd_appuni(a, b, bddop_and, a) == bdd_unique(bdd_and(a, b), a), "appuni equals unique of apply");
    EXPECT(bdd_exist(f, bddfalse) == f && bdd_forall(f, bddtrue) == f, "quantifying over an empty set is identity");

    EXPECT(bdd_support(a & c) == (a & c), "support is the variable cube");
    EXPECT(bdd_support(f) == (a & b & c), "support of a 3-variable function");
    EXPECT(bdd_support(bddtrue) == bddfalse, "support of a constant is false");

    bdd s1 = bdd_satone(f);
    EXPECT(s1 != bddfalse && (s1 & !f) == bddfalse, "satone implies f");
    bdd s2 = bdd_satoneset(f, a & b & c & d, bddfalse);
    EXPECT((s2 & !f) == bddfalse && bdd_nodecount(s2) == 4, "satoneset covers the whole set");
    bdd s3 = bdd_fullsatone(f);
    EXPECT((s3 & !f) == bddfalse && bdd_nodecount(s3) == 4, "fullsatone assigns every variable");
    EXPECT(bdd_satone(bddfalse) == bddfalse, "satone of false");
    g_allsat_calls = 0;
    bdd_allsat(a | b, count_allsat);
    EXPECT(g_allsat_calls == 2, "allsat visits two paths of a|b");

    EXPECT(bdd_satcount(a & b) == 4.0, "satcount of a&b over 4 variables");
    EXPECT(bdd_satcount(bddtrue) == 16.0 && bdd_satcount(bddfalse) == 0.0, "satcount of constants");
    EXPECT(bdd_satcountset(a & b, a & b & c) == 2.0, "satcountset");
    EXPECT(bdd_satcountln(a & b) == 2.0, "satcountln");
    EXPECT(bdd_satcountlnset(a & b, a & b & c) == 1.0, "satcountlnset");
    EXPECT(bdd_pathcount(a | b) == 2.0 && bdd_pathcount(a & b) == 1.0, "pathcount");

    EXPECT(bdd_nodecount(a & b) == 2, "nodecount");
    EXPECT(bdd_nodecount(bddtrue) == 0, "nodecount of a constant");
    bdd both[2] = {a & b, a | b};
    EXPECT(bdd_anodecount(both, 2) == 3, "anodecount counts shared nodes once");
    int *prof = bdd_varprofile(a & b);
    EXPECT(prof[0] == 1 && prof[1] == 1 && prof[2] == 0, "varprofile");
    free(prof);

    bdd vars[3] = {a, b, c};
    EXPECT(bdd_buildcube(5, 3, vars) == (a & !b & c), "buildcube");
    int ivars[3] = {0, 1, 2};
    EXPECT(bdd_ibuildcube(5, 3, ivars) == (a & !b & c), "ibuildcube");
    int set[2] = {0, 2};
    EXPECT(bdd_makeset(set, 2) == (a & c), "makeset");
    int *scanned = NULL, scanned_n = 0;
    bdd_scanset(a & c, scanned, scanned_n);
    EXPECT(scanned_n == 2 && scanned[0] == 0 && scanned[1] == 2, "scanset");
    free(scanned);
    bdd_scanset(bddtrue, scanned, scanned_n);
    EXPECT(scanned == NULL && scanned_n == 0, "scanset of true is empty");

    bdd_done();
}

/* ------------------------------------------------------------------------=
 * Reordering
 * ---------------------------------------------------------------------= */

static int g_probe_calls = 0;
static int probe_size() {
    ++g_probe_calls;
    return bdd_getnodenum();
}

static int g_reorder_hook_calls = 0;
static void reorder_hook(int prestate) {
    (void)prestate;
    ++g_reorder_hook_calls;
}

static void block_printer(FILE *o, int block) { fprintf(o, "B%d", block); }

static void test_reorder() {
    printf("\n== reordering ==\n");
    bdd_init(10000, 1000);
    bdd_gbc_hook(NULL);
    bdd_error_hook(record_error);
    const int n = 24;
    bdd_setvarnum(n);

    /* An interleaving-sensitive function: x0&y0 | x1&y1 | ... with the x's
     * and y's placed far apart in the initial order. */
    bdd f = bddfalse;
    for (int i = 0; i < 4; ++i)
        f |= bdd_ithvar(i) & bdd_ithvar(i + 4);
    int before = bdd_nodecount(f);
    double count = bdd_satcount(f);

    bdd_varblockall();
    bdd_reorder_verbose(0);
    bdd_reorder_hook(reorder_hook);
    bddsizehandler oldprobe = bdd_reorder_probe(probe_size);
    EXPECT(oldprobe != NULL, "reorder_probe returns the previous handler");

    const int methods[] = {BDD_REORDER_WIN2, BDD_REORDER_WIN2ITE, BDD_REORDER_WIN3,
                           BDD_REORDER_WIN3ITE, BDD_REORDER_SIFT, BDD_REORDER_SIFTITE,
                           BDD_REORDER_RANDOM};
    const char *names[] = {"win2", "win2ite", "win3", "win3ite", "sift", "siftite", "random"};
    for (int m = 0; m < 7; ++m) {
        bdd_reorder(methods[m]);
        bool ok = bdd_satcount(f) == count;
        for (int i = 0; i < n; ++i)
            ok = ok && bdd_level2var(bdd_var2level(i)) == i;
        char msg[64];
        snprintf(msg, sizeof msg, "reorder %s preserves f and the level maps", names[m]);
        EXPECT(ok, msg);
    }
    EXPECT(bdd_nodecount(f) < before, "sifting found a smaller order");
    EXPECT(g_probe_calls > 0, "size probe consulted during reordering");
    EXPECT(bdd_reorder_gain() <= 100, "reorder_gain is a percentage");
    bdd_reorder_probe(oldprobe);

    /* Rebuild f from scratch in the new order and compare. */
    bdd g = bddfalse;
    for (int i = 0; i < 4; ++i)
        g |= bdd_ithvar(i) & bdd_ithvar(i + 4);
    EXPECT(f == g, "same function rebuilt after reordering is identical");

    /* Automatic reordering, triggered from inside bdd_makenode when the
     * node table has to grow; this is the only path that calls the hook. */
    EXPECT(bdd_autoreorder(BDD_REORDER_SIFT) == BDD_REORDER_NONE, "autoreorder returns old method");
    EXPECT(bdd_getreorder_method() == BDD_REORDER_SIFT, "getreorder_method");
    g_reorder_hook_calls = 0;
    {
        bvec x = bvec_var(8, 0, 1);
        bvec y = bvec_var(8, 8, 1);
        bvec z = bvec_var(8, 16, 1);
        bdd big = bvec_coerce(8, x * y) == z;
        EXPECT(big != bddfalse, "multiplier built under automatic reordering");
        EXPECT(bdd_satcount(f) == count, "automatic reordering preserves f");
    }
    EXPECT(g_reorder_hook_calls >= 2, "reorder hook called before and after an automatic reordering");
    bdd_autoreorder_times(BDD_REORDER_WIN2, 3);
    EXPECT(bdd_getreorder_times() == 3, "getreorder_times");
    bdd_disable_reorder();
    bdd_enable_reorder();
    bdd_autoreorder(BDD_REORDER_NONE);

    /* Order printing, with and without a block printer. */
    FILE *tmp = tmpfile();
    bdd_fprintorder(tmp);
    EXPECT(slurp(tmp).size() > 0, "fprintorder writes something");
    fclose(tmp);
    bddfilehandler oldblock = bdd_blockfile_hook(block_printer);
    EXPECT(oldblock == NULL, "blockfile_hook returns previous handler");
    tmp = tmpfile();
    bdd_fprintorder(tmp);
    EXPECT(contains(slurp(tmp), "B0"), "block printer used by fprintorder");
    fclose(tmp);
    bdd_blockfile_hook(NULL);

    /* Explicit blocks. A block must consist of variables that are adjacent
     * in the current order, so first restore the identity order (which
     * bdd_setvarorder only allows while no blocks exist). */
    bdd_clrvarblocks();
    int identity[n];
    for (int i = 0; i < n; ++i)
        identity[i] = i;
    bdd_setvarorder(identity);
    g_last_error = 0;
    bdd_swapvar(0, 5);
    bdd_intaddvarblock(0, 3, BDD_REORDER_FREE);
    EXPECT(g_last_error == BDD_VARBLK, "block of non-adjacent variables is BDD_VARBLK");
    bdd_swapvar(0, 5);
    int blk = bdd_intaddvarblock(0, 3, BDD_REORDER_FIXED);
    EXPECT(blk >= 0, "intaddvarblock");
    EXPECT(bdd_addvarblock(bdd_ithvar(4) & bdd_ithvar(5), BDD_REORDER_FREE) >= 0, "addvarblock from a cube");
    g_last_error = 0;
    bdd_intaddvarblock(2, 5, BDD_REORDER_FREE);
    EXPECT(g_last_error == BDD_VARBLK, "overlapping block is BDD_VARBLK");
    EXPECT(bdd_intaddvarblock(6, n - 1, BDD_REORDER_FREE) >= 0, "block covering the remaining variables");
    bdd_reorder(BDD_REORDER_SIFT);
    EXPECT(bdd_satcount(f) == count, "reorder with explicit blocks preserves f");

    /* Fixed order and single swaps need no blocks. */
    bdd_clrvarblocks();
    int order[n];
    for (int i = 0; i < n; ++i)
        order[i] = n - 1 - i;
    bdd_setvarorder(order);
    bool reversed = true;
    for (int i = 0; i < n; ++i)
        reversed = reversed && bdd_var2level(i) == n - 1 - i;
    EXPECT(reversed, "setvarorder installs the reversed order");
    EXPECT(bdd_satcount(f) == count, "setvarorder preserves f");
    int l0 = bdd_var2level(0), l1 = bdd_var2level(1);
    bdd_swapvar(0, 1);
    EXPECT(bdd_var2level(0) == l1 && bdd_var2level(1) == l0, "swapvar exchanges levels");
    EXPECT(bdd_satcount(f) == count, "swapvar preserves f");
    g_last_error = 0;
    bdd_swapvar(0, 100);
    EXPECT(g_last_error == BDD_VAR, "swapvar out of range is BDD_VAR");

    bdd_done();
}

/* ------------------------------------------------------------------------=
 * Finite domains
 * ---------------------------------------------------------------------= */

static void domain_printer(FILE *o, int dom) { fprintf(o, "D%d", dom); }
static void domain_strm_printer(std::ostream &o, int dom) { o << "S" << dom; }

static void test_fdd() {
    printf("\n== fdd ==\n");
    bdd_init(10000, 1000);
    bdd_gbc_hook(NULL);
    bdd_error_hook(record_error);

    /* Domains 0 and 3 share a size so they can be related by fdd_equals.
     * The boolean variables of domains added together are interleaved bit
     * by bit: 0,1,2,3 are the low bits, 4,5,6,7 the next ones, 8,9 the top
     * bits of the two 3-bit domains. */
    int doms[4] = {4, 8, 5, 4};
    int first = fdd_extdomain(doms, 4);
    EXPECT(first == 0, "first domain index is 0");
    EXPECT(fdd_domainnum() == 4, "four domains");
    EXPECT(fdd_domainsize(0) == 4 && fdd_domainsize(2) == 5, "domainsize");
    EXPECT(fdd_varnum(0) == 2 && fdd_varnum(1) == 3 && fdd_varnum(2) == 3, "varnum per domain");
    int *v0 = fdd_vars(0);
    EXPECT(v0 != NULL && v0[0] == 0 && v0[1] == 4, "vars of domain 0 are interleaved with the others");
    EXPECT(bdd_varnum() == 10, "ten boolean variables in total");

    bdd x3 = fdd_ithvar(0, 3);
    bdd y5 = fdd_ithvar(1, 5);
    EXPECT(fdd_scanvar(x3, 0) == 3, "scanvar");
    EXPECT(fdd_scanvar(x3, 1) >= 0 && fdd_scanvar(bddfalse, 0) == -1, "scanvar of unconstrained domain and of false");
    int *all = fdd_scanallvar(x3 & y5);
    EXPECT(all != NULL && all[0] == 3 && all[1] == 5, "scanallvar");
    free(all);
    g_last_error = 0;
    fdd_ithvar(0, 4);
    EXPECT(g_last_error == BDD_RANGE, "ithvar beyond domain is BDD_RANGE");
    g_last_error = 0;
    fdd_ithvar(7, 0);
    EXPECT(g_last_error == BDD_VAR, "ithvar of unknown domain is BDD_VAR");

    EXPECT(fdd_ithset(0) == (bdd_ithvar(0) & bdd_ithvar(4)), "ithset is the cube of the domain variables");
    EXPECT(fdd_domain(0) == bddtrue, "power-of-two domain is unrestricted");
    EXPECT(bdd_satcountset(fdd_domain(2), fdd_ithset(2)) == 5.0, "domain of size 5 has 5 values");
    EXPECT(bdd_satcountset(fdd_equals(0, 3), fdd_ithset(0) & fdd_ithset(3)) == 4.0, "equals relates 4 value pairs");
    EXPECT(fdd_equals(0, 3) == fdd_equals(3, 0), "equals is symmetric");
    g_last_error = 0;
    fdd_equals(0, 2);
    EXPECT(g_last_error == BDD_RANGE, "equals on different sizes is BDD_RANGE");

    int set[2] = {0, 2};
    EXPECT(fdd_makeset(set, 2) == (fdd_ithset(0) & fdd_ithset(2)), "makeset");
    int *scanned = NULL, scanned_n = 0;
    fdd_scanset(fdd_ithset(0) & fdd_ithset(2), scanned, scanned_n);
    EXPECT(scanned_n == 2 && scanned[0] == 0 && scanned[1] == 2, "scanset");
    free(scanned);

    bddPair *p = bdd_newpair();
    fdd_setpair(p, 0, 3);
    EXPECT(bdd_replace(x3, p) == fdd_ithvar(3, 3), "fdd_setpair renames domain 0 to 3");
    bdd_resetpair(p);
    int from[1] = {3}, to[1] = {0};
    fdd_setpairs(p, from, to, 1);
    EXPECT(bdd_replace(fdd_ithvar(3, 3), p) == x3, "fdd_setpairs");
    g_last_error = 0;
    fdd_setpair(p, 0, 2);
    EXPECT(g_last_error == BDD_VARNUM, "setpair on different sizes is BDD_VARNUM");
    bdd_freepair(p);

    EXPECT(fdd_intaddvarblock(0, 1, BDD_REORDER_FREE) >= 0, "intaddvarblock over domains");
    bdd_clrvarblocks();

    /* Printing, through every channel. */
    FILE *tmp = tmpfile();
    fdd_fprintset(tmp, x3 & y5);
    EXPECT(contains(slurp(tmp), "<0:3, 1:5>"), "fdd_fprintset");
    fclose(tmp);
    printf("fdd_printset: ");
    fdd_printset(x3 & y5);
    printf("\n");
    bddfilehandler oldf = fdd_file_hook(domain_printer);
    EXPECT(oldf == NULL, "fdd_file_hook returns previous handler");
    tmp = tmpfile();
    fdd_fprintset(tmp, x3);
    EXPECT(contains(slurp(tmp), "D0:3"), "fdd_file_hook names the domain");
    fclose(tmp);
    fdd_file_hook(NULL);
    {
        std::ostringstream os;
        os << fddset << (x3 & y5);
        EXPECT(contains(os.str(), "<0:3, 1:5>"), "fddset stream format");
        bddstrmhandler olds = fdd_strm_hook(domain_strm_printer);
        EXPECT(olds == NULL, "fdd_strm_hook returns previous handler");
        std::ostringstream os2;
        os2 << fddset << x3;
        EXPECT(contains(os2.str(), "S0:3"), "fdd_strm_hook names the domain");
        fdd_strm_hook(NULL);
    }

    int ov = fdd_overlapdomain(0, 1);
    EXPECT(ov == 4 && fdd_domainnum() == 5, "overlapdomain adds a fifth domain");
    EXPECT(fdd_varnum(ov) == 5, "overlap domain spans both variable sets");

    fdd_clearall();
    EXPECT(fdd_domainnum() == 0, "clearall removes all domains");
    int again[1] = {16};
    EXPECT(fdd_extdomain(again, 1) == 0, "domains can be added after clearall");

    bdd_done();
}

/* ------------------------------------------------------------------------=
 * Bit vectors
 * ---------------------------------------------------------------------= */

static bdd bv_not(const bdd &x) { return !x; }
static bdd bv_and(const bdd &x, const bdd &y) { return x & y; }
static bdd bv_maj(const bdd &x, const bdd &y, const bdd &z) { return (x & y) | (x & z) | (y & z); }

/* Assignment cube for an interleaved 3-bit vector: bit i at var offset+2i. */
static bdd bv_assign(int offset, int value) {
    std::vector<std::pair<int, int>> lits;
    for (int i = 0; i < 3; ++i)
        lits.push_back({offset + 2 * i, (value >> i) & 1});
    return cube_of(lits);
}

static void test_bvec() {
    printf("\n== bvec ==\n");
    bdd_init(100000, 10000);
    bdd_gbc_hook(NULL);
    bdd_error_hook(record_error);
    bdd_setvarnum(8);

    /* Constants. */
    bvec c5 = bvec_con(4, 5);
    EXPECT(bvec_isconst(c5) && bvec_val(c5) == 5 && c5.bitnum() == 4, "bvec_con");
    EXPECT(bvec_val(bvec_true(3)) == 7, "bvec_true");
    EXPECT(bvec_val(bvec_false(3)) == 0, "bvec_false");
    EXPECT(bvec_val(bvec_coerce(2, c5)) == 1, "coerce down truncates");
    EXPECT(bvec_val(bvec_coerce(6, c5)) == 5 && bvec_coerce(6, c5).bitnum() == 6, "coerce up zero-extends");
    bvec cc = bvec_copy(c5);
    EXPECT(bvec_val(cc) == 5, "bvec_copy");
    bvec assigned;
    EXPECT(assigned.empty(), "default bvec is empty");
    assigned = c5;
    EXPECT(!assigned.empty() && bvec_val(assigned) == 5, "bvec assignment");
    bvec c5b(c5);
    EXPECT(bvec_val(c5b) == 5, "bvec copy constructor");
    bvec sized(3);
    EXPECT(bvec_val(sized) == 0, "bvec(bitnum) is zero");
    bvec sizedval(3, 6);
    EXPECT(bvec_val(sizedval) == 6, "bvec(bitnum, val)");
    sizedval.set(0, bddtrue);
    EXPECT(bvec_val(sizedval) == 7 && sizedval[0] == bddtrue, "bvec::set and operator[]");

    EXPECT(bvec_val(bvec_con(4, 3) + bvec_con(4, 4)) == 7, "add constants");
    EXPECT(bvec_val(bvec_con(4, 4) - bvec_con(4, 3)) == 1, "sub constants");
    EXPECT(bvec_val(bvec_con(4, 3) * 2) == 6, "mulfixed constants");
    EXPECT(bvec_val(bvec_con(4, 3) * bvec_con(4, 2)) == 6, "mul constants");
    bvec q, r;
    EXPECT(bvec_divfixed(bvec_con(4, 7), 2, q, r) == 0 && bvec_val(q) == 3 && bvec_val(r) == 1, "divfixed constants");
    EXPECT(bvec_div(bvec_con(4, 7), bvec_con(4, 2), q, r) == 0 && bvec_val(q) == 3 && bvec_val(r) == 1, "div constants");
    g_last_error = 0;
    bvec_divfixed(bvec_con(4, 7), 0, q, r);
    EXPECT(g_last_error == BVEC_DIVZERO, "divfixed by zero is BVEC_DIVZERO");
    g_last_error = 0;
    bvec_add(bvec_con(4, 1), bvec_con(3, 1));
    EXPECT(g_last_error == BVEC_SIZE, "add with mismatched widths is BVEC_SIZE");
    EXPECT(bvec_val(bvec_con(4, 1) << 2) == 4, "shlfixed constants");
    EXPECT(bvec_val(bvec_con(4, 8) >> 3) == 1, "shrfixed constants");
    EXPECT(bvec_val(bvec_con(4, 1) << bvec_con(2, 2)) == 4, "shl constants");
    EXPECT(bvec_val(bvec_con(4, 8) >> bvec_con(2, 3)) == 1, "shr constants");
    EXPECT(bvec_val(bvec_shlfixed(bvec_con(4, 1), 2, bddtrue)) == 7, "shlfixed fills with the carry-in");
    EXPECT(bvec_val(bvec_ite(bddtrue, bvec_con(4, 1), bvec_con(4, 2))) == 1, "ite true");
    EXPECT(bvec_val(bvec_ite(bddfalse, bvec_con(4, 1), bvec_con(4, 2))) == 2, "ite false");
    EXPECT((bvec_con(4, 1) < bvec_con(4, 2)) == bddtrue, "lth constants");
    EXPECT((bvec_con(4, 2) <= bvec_con(4, 2)) == bddtrue, "lte constants");
    EXPECT((bvec_con(4, 2) > bvec_con(4, 2)) == bddfalse, "gth constants");
    EXPECT((bvec_con(4, 2) >= bvec_con(4, 2)) == bddtrue, "gte constants");
    EXPECT((bvec_con(4, 2) == bvec_con(4, 2)) == bddtrue, "equ constants");
    EXPECT((bvec_con(4, 2) != bvec_con(4, 3)) == bddtrue, "neq constants");

    /* Variables: x on even variables, y on odd ones, 3 bits each. */
    bvec x = bvec_var(3, 0, 2);
    bvec y = bvec_var(3, 1, 2);
    EXPECT(!bvec_isconst(x), "variable vector is not constant");
    EXPECT(x[1] == bdd_ithvar(2), "bvec_var places bit i at offset+i*step");
    int vv[3] = {0, 2, 4};
    EXPECT(bvec_varvec(3, vv)[2] == bdd_ithvar(4), "bvec_varvec");
    EXPECT((!x)[0] == bdd_nithvar(0), "operator! maps bdd_not");
    EXPECT((x & y)[0] == (bdd_ithvar(0) & bdd_ithvar(1)), "operator& maps bdd_and");
    EXPECT((x | y)[0] == (bdd_ithvar(0) | bdd_ithvar(1)), "operator| maps bdd_or");
    EXPECT((x ^ y)[0] == (bdd_ithvar(0) ^ bdd_ithvar(1)), "operator^ maps bdd_xor");
    EXPECT(bvec_map1(x, bv_not)[1] == bdd_nithvar(2), "bvec_map1");
    EXPECT(bvec_map2(x, y, bv_and)[1] == (bdd_ithvar(2) & bdd_ithvar(3)), "bvec_map2");
    EXPECT(bvec_map3(x, y, x, bv_maj)[0] == bdd_ithvar(0), "bvec_map3");
    {
        std::ostringstream os;
        os << x;
        EXPECT(os.str().size() > 0, "bvec stream output");
    }

    /* Oracle: every arithmetic and relational result agrees with integers
     * modulo 8 for every pair of assignments. */
    bvec sum = x + y, diff = x - y, prod = x * y, triple = x * 3;
    bvec shl1 = x << 1, shr1 = x >> 1, shl = x << y, shr = x >> y;
    bvec qf, rf, qv, rv;
    bvec_divfixed(x, 3, qf, rf);
    bvec_div(x, y, qv, rv);
    bdd sel = bdd_ithvar(6);
    bvec chosen = bvec_ite(sel, x, y);
    bdd lth = x < y, lte = x <= y, gth = x > y, gte = x >= y, equ = x == y, neq = x != y;
    int failures = 0;
    for (int xv = 0; xv < 8; ++xv) {
        for (int yv = 0; yv < 8; ++yv) {
            bdd cube = bv_assign(0, xv) & bv_assign(1, yv);
            auto is = [&](const bvec &v, int value) {
                return eval_under(v == bvec_con(3, value), cube) == bddtrue;
            };
            auto truth = [&](const bdd &rel, bool expect) {
                return eval_under(rel, cube) == (expect ? bddtrue : bddfalse);
            };
            if (!is(sum, (xv + yv) & 7)) ++failures;
            if (!is(diff, (xv - yv) & 7)) ++failures;
            if (!is(prod, (xv * yv) & 7)) ++failures;
            if (!is(triple, (xv * 3) & 7)) ++failures;
            if (!is(shl1, (xv << 1) & 7)) ++failures;
            if (!is(shr1, xv >> 1)) ++failures;
            if (!is(shl, yv < 3 ? (xv << yv) & 7 : 0)) ++failures;
            if (!is(shr, yv < 3 ? xv >> yv : 0)) ++failures;
            if (!is(qf, xv / 3) || !is(rf, xv % 3)) ++failures;
            if (yv > 0 && (!is(qv, xv / yv) || !is(rv, xv % yv))) ++failures;
            if (eval_under(chosen == bvec_con(3, xv), cube & sel) != bddtrue) ++failures;
            if (eval_under(chosen == bvec_con(3, yv), cube & !sel) != bddtrue) ++failures;
            if (!truth(lth, xv < yv) || !truth(lte, xv <= yv) || !truth(gth, xv > yv) ||
                !truth(gte, xv >= yv) || !truth(equ, xv == yv) || !truth(neq, xv != yv))
                ++failures;
        }
    }
    EXPECT(failures == 0, "arithmetic and relations agree with integers on all 64 assignments");

    /* bvec over a finite domain. */
    int dom[1] = {8};
    int d = fdd_extdomain(dom, 1);
    bvec z = bvec_varfdd(d);
    EXPECT(z.bitnum() == 3, "bvec_varfdd has the domain's width");
    EXPECT(bdd_restrict(z == bvec_con(3, 6), fdd_ithvar(d, 6)) == bddtrue, "bvec_varfdd matches fdd_ithvar");
    g_last_error = 0;
    bvec_varfdd(99);
    EXPECT(g_last_error == BDD_VAR, "bvec_varfdd of unknown domain is BDD_VAR");

    bdd_done();
}

/* ------------------------------------------------------------------------=
 * File I/O and stream output
 * ---------------------------------------------------------------------= */

static void var_printer(FILE *o, int var) { fprintf(o, "v%d", var); }
static void var_strm_printer(std::ostream &o, int var) { o << "w" << var; }

static void test_io() {
    printf("\n== io ==\n");
    bdd_init(1000, 100);
    bdd_gbc_hook(NULL);
    bdd_error_hook(record_error);
    bdd_setvarnum(3);
    bdd a = bdd_ithvar(0), b = bdd_ithvar(1), c = bdd_ithvar(2);
    bdd f = (a & b) | (!a & c);

    FILE *tmp = tmpfile();
    bdd_fprintall(tmp);
    EXPECT(slurp(tmp).size() > 0, "fprintall");
    fclose(tmp);
    tmp = tmpfile();
    bdd_fprinttable(tmp, f);
    EXPECT(contains(slurp(tmp), "ROOT"), "fprinttable mentions the root");
    fclose(tmp);
    tmp = tmpfile();
    bdd_fprintset(tmp, a & !b);
    EXPECT(contains(slurp(tmp), "<0:1, 1:0>"), "fprintset");
    fclose(tmp);
    tmp = tmpfile();
    bdd_fprintset(tmp, bddtrue);
    EXPECT(contains(slurp(tmp), "T"), "fprintset of true");
    fclose(tmp);
    tmp = tmpfile();
    bdd_fprintset(tmp, bddfalse);
    EXPECT(contains(slurp(tmp), "F"), "fprintset of false");
    fclose(tmp);
    printf("printset: ");
    bdd_printset(a & !b);
    printf("\n");
    bddfilehandler oldf = bdd_file_hook(var_printer);
    EXPECT(oldf == NULL, "file_hook returns previous handler");
    tmp = tmpfile();
    bdd_fprintset(tmp, a);
    EXPECT(contains(slurp(tmp), "v0:1"), "file_hook names variables");
    fclose(tmp);
    bdd_file_hook(NULL);

    tmp = tmpfile();
    bdd_fprintdot(tmp, f);
    EXPECT(contains(slurp(tmp), "digraph G"), "fprintdot writes a digraph");
    fclose(tmp);
    char dotname[] = "apitest_graph.dot";
    EXPECT(bdd_fnprintdot(dotname, f) == 0, "fnprintdot returns 0");
    EXPECT(contains(slurp_path(dotname), "digraph G"), "fnprintdot wrote the file");
    remove(dotname);
    char baddot[] = "no_such_dir/graph.dot";
    g_last_error = 0;
    bdd_fnprintdot(baddot, f);
    EXPECT(g_last_error == BDD_FILE, "fnprintdot to an unwritable path is BDD_FILE");

    /* Save and load round trip. */
    char savename[] = "apitest_saved.bdd";
    EXPECT(bdd_fnsave(savename, f, "f", "BDD") == 0, "fnsave returns 0");
    std::string saved = slurp_path(savename);
    EXPECT(contains(saved, "@BDD") && contains(saved, "%Name f") && contains(saved, "%Vars 3"),
           "fnsave writes the header");
    bdd loaded;
    EXPECT(bdd_fnload(savename, loaded) == 0, "fnload returns 0");
    EXPECT(loaded == f, "loaded BDD equals the saved one");
    remove(savename);
    EXPECT(bdd_fnsave(savename, bddtrue) == 0, "fnsave of a constant with default name");
    EXPECT(bdd_fnload(savename, loaded) == 0 && loaded == bddtrue, "fnload of a constant");
    remove(savename);
    tmp = tmpfile();
    EXPECT(bdd_save(tmp, f) == 0, "save to a stream");
    EXPECT(contains(slurp(tmp), "%Root"), "save writes the root");
    rewind(tmp);
    g_last_error = 0;
    bdd_load(tmp, loaded);
    EXPECT(g_last_error == BDD_FORMAT, "load of a headerless stream is BDD_FORMAT");
    fclose(tmp);
    g_last_error = 0;
    bdd_fnload(baddot, loaded);
    EXPECT(g_last_error == BDD_FILE, "fnload of a missing file is BDD_FILE");
    tmp = tmpfile();
    fputs("@BDD\n%Name g\n%Vars x\n", tmp);
    rewind(tmp);
    g_last_error = 0;
    bdd_load(tmp, loaded);
    EXPECT(g_last_error == BDD_FORMAT, "load with a bad %Vars line is BDD_FORMAT");
    fclose(tmp);
    tmp = tmpfile();
    fputs("@BDD\n%Name g\n%Vars 3\n%Nodes 1\n%Root 7\n%Ordering 0 1 2\n7[0] 0 1\n", tmp);
    rewind(tmp);
    EXPECT(bdd_load(tmp, loaded) == 0 && loaded == a, "load of a hand-written single node");
    fclose(tmp);
    tmp = tmpfile();
    fputs("@BDD\n%Name g\n%Vars 5\n%Nodes 1\n%Root 9\n%Ordering 0 1 2 3 4\n9[4] 0 1\n", tmp);
    rewind(tmp);
    EXPECT(bdd_load(tmp, loaded) == 0 && bdd_varnum() == 5 && loaded == bdd_ithvar(4),
           "load grows the variable count");
    fclose(tmp);

    /* Stream formats. */
    {
        std::ostringstream os;
        os << bddset << (a & !b);
        EXPECT(contains(os.str(), "<0:1, 1:0>"), "bddset stream format");
        std::ostringstream ot;
        ot << bddtable << f;
        EXPECT(contains(ot.str(), "ROOT"), "bddtable stream format");
        std::ostringstream od;
        od << bdddot << f;
        EXPECT(contains(od.str(), "digraph G"), "bdddot stream format");
        std::ostringstream oa;
        oa << bddall << f;
        EXPECT(oa.str().size() > 0, "bddall stream format");
        std::ostringstream oc;
        oc << bddset << bddtrue << bddfalse;
        EXPECT(oc.str() == "TF", "stream output of constants");
        bddstrmhandler olds = bdd_strm_hook(var_strm_printer);
        EXPECT(olds == NULL, "strm_hook returns previous handler");
        std::ostringstream oh;
        oh << bddset << a;
        EXPECT(contains(oh.str(), "w0:1"), "strm_hook names variables");
        bdd_strm_hook(NULL);
    }

    bdd_done();
}

/* ------------------------------------------------------------------------=
 * C++ wrapper class
 * ---------------------------------------------------------------------= */

static void test_cpp() {
    printf("\n== C++ wrappers ==\n");
    bdd_init(1000, 100);
    bdd_gbc_hook(NULL);
    bdd_setvarnum(3);
    bdd a = bdd_ithvar(0), b = bdd_ithvar(1), c = bdd_ithvar(2);

    EXPECT((a & b) == bdd_and(a, b), "operator&");
    EXPECT((a | b) == bdd_or(a, b), "operator|");
    EXPECT((a ^ b) == bdd_xor(a, b), "operator^");
    EXPECT(!a == bdd_not(a) && ~a == bdd_not(a), "operator! and operator~");
    EXPECT((a >> b) == bdd_imp(a, b), "operator>> is implication");
    EXPECT((a - b) == bdd_apply(a, b, bddop_diff), "operator- is difference");
    EXPECT((a > b) == bdd_apply(a, b, bddop_diff), "operator> is difference");
    EXPECT((a < b) == bdd_apply(a, b, bddop_less), "operator< is less");
    EXPECT((a << b) == bdd_apply(a, b, bddop_invimp), "operator<< is inverse implication");
    bdd t = a;
    t &= b;
    EXPECT(t == (a & b), "operator&=");
    t |= c;
    EXPECT(t == ((a & b) | c), "operator|=");
    t ^= c;
    EXPECT(t == (((a & b) | c) ^ c), "operator^=");
    t = a;
    t >>= b;
    EXPECT(t == (a >> b), "operator>>=");
    t = a;
    t -= b;
    EXPECT(t == (a - b), "operator-=");
    t = a;
    t <<= b;
    EXPECT(t == (a << b), "operator<<=");
    EXPECT(a != b && !(a != a), "operator!=");
    EXPECT(a.id() == bdd_ithvar(0).id(), "id of equal BDDs");
    bdd copy(a);
    EXPECT(copy == a, "copy constructor");
    bdd empty;
    EXPECT(empty == bddfalse, "default constructed bdd is false");
    EXPECT(bdd_var(a) == 0 && bdd_low(a) == bddfalse && bdd_high(a) == bddtrue, "node accessors");

    bdd_done();
}

/* ------------------------------------------------------------------------=
 * Main
 * ---------------------------------------------------------------------= */

int main() {
    printf("MoToBuddy BDD API test suite\n");

    test_kernel();
    test_operators();
    test_reorder();
    test_fdd();
    test_bvec();
    test_io();
    test_cpp();

    printf("\nSummary: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
