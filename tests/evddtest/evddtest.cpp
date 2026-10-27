/**
 * @file evddtest.cpp
 * @brief Unit tests for evdd_value (registry, intern, algebra, normalize).
 *
 * Spec for test-driven implementation of src/evdd_value.c. Run:
 *   ./evddtest
 * A test is done when it prints PASS. Mixed-type and freeze errors use a
 * non-exiting bdd_error_hook so the suite can keep going.
 */

#ifdef CPLUSPLUS
#undef CPLUSPLUS
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bdd.h"
#include "evdd.h"
#include "evdd_kernel.h"
#include "evdd_op.h"
#include "evdd_value.h"

/* ------------------------------------------------------------------------=
 * Harness (same idea as mtbddtest.cpp)
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

static int g_last_error = 0;
static int g_error_count = 0;
static int g_freefun_calls = 0;

static void test_errhandler(int e) {
    g_last_error = e;
    ++g_error_count;
}

static void reset_errors(void) {
    g_last_error = 0;
    g_error_count = 0;
}

static int nearly(double a, double b) { return std::fabs(a - b) < 1e-9; }

/* ------------------------------------------------------------------------=
 * Int payloads
 * ---------------------------------------------------------------------= */

static int *box_int(int v) {
    int *p = (int *)malloc(sizeof(int));
    *p = v;
    return p;
}

static int int_val(void *p) { return p ? *(int *)p : 0; }

static unsigned int_hash(void *a) { return (unsigned)int_val(a); }

static int int_cmp(void *a, void *b) { return int_val(a) == int_val(b); }

static void int_free_internals(void *a) {
    (void)a;
    ++g_freefun_calls;
}

static char *int_tostr(void *a, char *buf, size_t n) {
    snprintf(buf, n, "%d", int_val(a));
    return buf;
}

static void *int_mul(void *a, void *b) { return box_int(int_val(a) * int_val(b)); }
static void *int_div(void *a, void *b) { return box_int(int_val(a) / int_val(b)); }
static void *int_add(void *a, void *b) { return box_int(int_val(a) + int_val(b)); }
static void *int_sub(void *a, void *b) { return box_int(int_val(a) - int_val(b)); }
static int int_is_zero(void *a) { return int_val(a) == 0; }
static int int_is_one(void *a) { return int_val(a) == 1; }
static int int_abs_cmp(void *a, void *b) {
    int aa = std::abs(int_val(a));
    int bb = std::abs(int_val(b));
    return (aa > bb) - (aa < bb);
}
static void *int_abs_sq(void *a) {
    int v = int_val(a);
    return box_int(v * v);
}

/* ------------------------------------------------------------------------=
 * Double payloads
 * ---------------------------------------------------------------------= */

static double *box_dbl(double v) {
    double *p = (double *)malloc(sizeof(double));
    *p = v;
    return p;
}

static double dbl_val(void *p) { return p ? *(double *)p : 0.0; }

static unsigned dbl_hash(void *a) {
    double v = dbl_val(a);
    unsigned h;
    memcpy(&h, &v, sizeof(h));
    return h;
}

static int dbl_cmp(void *a, void *b) { return dbl_val(a) == dbl_val(b); }

static char *dbl_tostr(void *a, char *buf, size_t n) {
    snprintf(buf, n, "%g", dbl_val(a));
    return buf;
}

static void *dbl_mul(void *a, void *b) { return box_dbl(dbl_val(a) * dbl_val(b)); }
static void *dbl_div(void *a, void *b) { return box_dbl(dbl_val(a) / dbl_val(b)); }
static void *dbl_add(void *a, void *b) { return box_dbl(dbl_val(a) + dbl_val(b)); }
static void *dbl_sub(void *a, void *b) { return box_dbl(dbl_val(a) - dbl_val(b)); }
static int dbl_is_zero(void *a) { return dbl_val(a) == 0.0; }
static int dbl_is_one(void *a) { return dbl_val(a) == 1.0; }
static int dbl_abs_cmp(void *a, void *b) {
    double aa = std::fabs(dbl_val(a));
    double bb = std::fabs(dbl_val(b));
    return (aa > bb) - (aa < bb);
}
static void *dbl_abs_sq(void *a) {
    double v = dbl_val(a);
    return box_dbl(v * v);
}
static void *dbl_sqrt(void *a) { return box_dbl(std::sqrt(dbl_val(a))); }

/* ------------------------------------------------------------------------=
 * Custom normalize: always factor the high edge when it is nonzero.
 * ---------------------------------------------------------------------= */

static evddNormResult high_unit_norm(evdd_value_id w_low, evdd_value_id w_high) {
    evddNormResult r;
    if (evdd_value_is_zero(w_high)) {
        r.w_low = w_low;
        r.w_high = w_high;
        r.incoming = evdd_value_unit_from_pair(w_low, w_high);
        return r;
    }
    r.incoming = w_high;
    r.w_low = evdd_value_div(w_low, w_high);
    r.w_high = evdd_value_unit(w_high);
    return r;
}

/* ------------------------------------------------------------------------=
 * Setup / teardown
 * ---------------------------------------------------------------------= */

static evdd_value_type register_int_type(void) {
    evdd_value_type t = evdd_new_value_type();
    evdd_register_hash_function(t, int_hash);
    evdd_register_compare_function(t, int_cmp);
    evdd_register_free_function(t, int_free_internals);
    evdd_register_to_str_function(t, int_tostr);
    evdd_register_mul_function(t, int_mul);
    evdd_register_div_function(t, int_div);
    evdd_register_add_function(t, int_add);
    evdd_register_sub_function(t, int_sub);
    evdd_register_is_zero_function(t, int_is_zero);
    evdd_register_is_one_function(t, int_is_one);
    evdd_register_abs_cmp_function(t, int_abs_cmp);
    evdd_register_abs_sq_function(t, int_abs_sq);
    return t;
}

static evdd_value_type register_dbl_type(void) {
    evdd_value_type t = evdd_new_value_type();
    evdd_register_hash_function(t, dbl_hash);
    evdd_register_compare_function(t, dbl_cmp);
    evdd_register_free_function(t, int_free_internals);
    evdd_register_to_str_function(t, dbl_tostr);
    evdd_register_mul_function(t, dbl_mul);
    evdd_register_div_function(t, dbl_div);
    evdd_register_add_function(t, dbl_add);
    evdd_register_sub_function(t, dbl_sub);
    evdd_register_is_zero_function(t, dbl_is_zero);
    evdd_register_is_one_function(t, dbl_is_one);
    evdd_register_abs_cmp_function(t, dbl_abs_cmp);
    evdd_register_abs_sq_function(t, dbl_abs_sq);
    evdd_register_sqrt_function(t, dbl_sqrt);
    return t;
}

static evdd_value_id intern_int(evdd_value_type t, int v) {
    return evdd_value_intern(box_int(v), t);
}

static evdd_value_id intern_dbl(evdd_value_type t, double v) {
    return evdd_value_intern(box_dbl(v), t);
}

static evdd_value_id one_int(evdd_value_type t) { return intern_int(t, 1); }
static evdd_value_id one_dbl(evdd_value_type t) { return intern_dbl(t, 1.0); }
static EVDD unit_const_int(evdd_value_type t) { return evdd_const(one_int(t)); }
static EVDD unit_const_dbl(evdd_value_type t) { return evdd_const(one_dbl(t)); }

static int get_int(evdd_value_id id) {
    if (id == EVDD_VALUE_ZERO)
        return 0;
    return int_val(evdd_value_get(id));
}

static double get_dbl(evdd_value_id id) {
    if (id == EVDD_VALUE_ZERO)
        return 0.0;
    return dbl_val(evdd_value_get(id));
}

static void teardown_values(void) {
    evdd_value_done();
    evdd_value_types_done();
}

/* ------------------------------------------------------------------------=
 * Tests
 * ---------------------------------------------------------------------= */

static void test_type_registry(void) {
    printf("\n== type registry ==\n");

    EXPECT(evdd_value_type_number == 0 && evdd_value_functions_list == NULL,
           "registry starts empty");

    evdd_value_type t0 = evdd_new_value_type();
    evdd_value_type t1 = evdd_new_value_type();
    EXPECT(t0 == 0 && t1 == 1 && t0 != t1, "new types are 0, 1, …");
    EXPECT(evdd_value_type_number == 2, "type_number is live count");
    EXPECT(evdd_value_type_size >= evdd_value_type_number,
           "type_size is allocated capacity");
    EXPECT(evdd_value_functions_list != NULL, "functions list allocated");

    evdd_new_value_type();
    evdd_new_value_type();
    EXPECT(evdd_value_type_number == 4, "four types registered");
    EXPECT(evdd_value_type_size >= 4, "capacity grew for fourth type");

    teardown_values();
}

static void test_register_callbacks(void) {
    printf("\n== register callbacks ==\n");
    evdd_value_type t = register_int_type();

    EXPECT(EVDDVALUEHASH(t) == int_hash, "hash registered");
    EXPECT(EVDDVALUECOMPARE(t) == int_cmp, "compare registered");
    EXPECT(EVDDVALUEFREE(t) == int_free_internals, "free registered");
    EXPECT(EVDDVALUETOSTR(t) == int_tostr, "to_str registered");
    EXPECT(EVDDVALUEMUL(t) == int_mul, "mul registered");
    EXPECT(EVDDVALUEDIV(t) == int_div, "div registered");
    EXPECT(EVDDVALUEADD(t) == int_add, "add registered");
    EXPECT(EVDDVALUESUB(t) == int_sub, "sub registered");
    EXPECT(EVDDVALUEISZERO(t) == int_is_zero, "is_zero registered");
    EXPECT(EVDDVALUEISONE(t) == int_is_one, "is_one registered");
    EXPECT(EVDDVALUEABSCMP(t) == int_abs_cmp, "abs_cmp registered");
    EXPECT(EVDDVALUEABSSQ(t) == int_abs_sq, "abs_sq registered");

    evdd_value_type d = register_dbl_type();
    EXPECT(EVDDVALUESQRT(d) == dbl_sqrt, "sqrt registered");

    reset_errors();
    evdd_register_hash_function((evdd_value_type)evdd_value_type_number, int_hash);
    EXPECT(g_error_count >= 1, "register rejects type == type_number");

    teardown_values();
}

static void test_types_done_reinit(void) {
    printf("\n== types_done / reinit ==\n");
    evdd_new_value_type();
    evdd_value_types_done();
    EXPECT(evdd_value_functions_list == NULL, "types_done frees the list");
    EXPECT(evdd_value_type_number == 0 && evdd_value_type_size == 0,
           "types_done resets counters");

    evdd_value_type t = evdd_new_value_type();
    EXPECT(t == 0 && evdd_value_type_number == 1, "new_type works after types_done");
    teardown_values();
}

static void test_intern_uniquing(void) {
    printf("\n== intern uniquing ==\n");
    (void)register_int_type(); /* type 0: get_type stub returns 0, so use type 1 */
    evdd_value_type t = register_int_type();

    evdd_value_id a1 = intern_int(t, 42);
    evdd_value_id a2 = intern_int(t, 42);
    evdd_value_id b = intern_int(t, 7);

    EXPECT(a1 != EVDD_VALUE_ZERO && get_int(a1) == 42,
           "interned payload is not bare ZERO");
    EXPECT(a1 == a2 && get_int(a2) == 42, "equal values intern to the same id");
    EXPECT(a1 != b && get_int(b) == 7, "distinct values get distinct ids");
    EXPECT(get_int(a1) == 42, "get returns stored int");
    EXPECT(evdd_value_get_type(a1) == t, "get_type preserves type");
    EXPECT(evdd_value_get_type(b) == t, "get_type on second value");

    evdd_value_id five = intern_int(t, 5);
    evdd_value_id z = evdd_value_intern(NULL, t);
    EXPECT(get_int(five) == 5 && z == EVDD_VALUE_ZERO, "NULL intern maps to ZERO");

    EXPECT(get_int(five) == 5 && intern_int(t, 0) == EVDD_VALUE_ZERO,
           "is_zero payload collapses to ZERO");
    evdd_value_id one = intern_int(t, 1);
    EXPECT(get_int(five) == 5 && one != EVDD_VALUE_ZERO && evdd_value_is_one(one),
           "is_one payload stays typed and is_one");
    EXPECT(intern_int(t, 1) == one, "typed ones uniquify");

    EXPECT(evdd_value_get(EVDD_VALUE_ZERO) == NULL, "get(ZERO) is NULL");
    EXPECT(evdd_value_get(one) != NULL, "get(typed one) is non-NULL");

    teardown_values();
}

/* Bit pattern vs a scaled integer. Equal payloads land in different buckets. */
static unsigned hash_bits(void *a)
{
    double v = dbl_val(a);
    unsigned long long u;
    memcpy(&u, &v, 8);
    return (unsigned)(u ^ (u >> 32));
}

static unsigned hash_scaled(void *a)
{
    return (unsigned)(dbl_val(a) * 1e6);
}

static int cmp_near(void *a, void *b)
{
    double d = dbl_val(a) - dbl_val(b);
    if (d < 0)
        d = -d;
    return d <= 1e-3;
}

/* Stands in for a late arithmetic change: indexing must not care. */
static void *dbl_add_as_mul(void *a, void *b)
{
    return box_dbl(dbl_val(a) + dbl_val(b));
}

/* Hash, compare, and is-zero pick the id. A change after the first stored
 * row would give one value two ids. Arithmetic callbacks stay mutable. */
static void test_intern_index_callback_freeze(void)
{
    evdd_value_id a, b, c, d, e, zid, two, three;

    printf("\n== intern index callback freeze ==\n");

    evdd_value_type t = evdd_new_value_type();
    evdd_register_hash_function(t, hash_scaled);
    evdd_register_hash_function(t, hash_bits);
    evdd_register_compare_function(t, dbl_cmp);
    evdd_register_free_function(t, int_free_internals);
    EXPECT(EVDDVALUEHASH(t) == hash_bits,
           "hash freeze: register before intern replaces the callback");
    a = intern_dbl(t, 0.5);
    reset_errors();
    evdd_register_hash_function(t, hash_scaled);
    b = intern_dbl(t, 0.5);
    EXPECT(g_error_count >= 1 && g_last_error == BDD_OP &&
               EVDDVALUEHASH(t) == hash_bits,
           "hash freeze: register after intern is rejected");
    EXPECT(a == b && a != EVDD_VALUE_ZERO,
           "hash freeze: equal value keeps the first id");

    evdd_value_type u = evdd_new_value_type();
    evdd_register_hash_function(u, hash_scaled);
    evdd_register_compare_function(u, dbl_cmp);
    evdd_register_free_function(u, int_free_internals);
    c = intern_dbl(u, 0.5);
    d = intern_dbl(u, 0.5000001);
    reset_errors();
    evdd_register_compare_function(u, cmp_near);
    e = intern_dbl(u, 0.5000001);
    EXPECT(g_error_count >= 1 && g_last_error == BDD_OP &&
               EVDDVALUECOMPARE(u) == dbl_cmp,
           "compare freeze: register after intern is rejected");
    EXPECT(c != d && e == d,
           "compare freeze: near values stay on the ids exact compare gave them");

    evdd_value_type z = evdd_new_value_type();
    evdd_register_hash_function(z, hash_bits);
    evdd_register_compare_function(z, dbl_cmp);
    evdd_register_free_function(z, int_free_internals);
    zid = intern_dbl(z, 0.0);
    reset_errors();
    evdd_register_is_zero_function(z, dbl_is_zero);
    EXPECT(g_error_count >= 1 && g_last_error == BDD_OP &&
               EVDDVALUEISZERO(z) == NULL,
           "is_zero freeze: register after intern is rejected");
    EXPECT(zid != EVDD_VALUE_ZERO && intern_dbl(z, 0.0) == zid,
           "is_zero freeze: a later zero stays on the typed id");

    evdd_value_type m = register_dbl_type();
    two = intern_dbl(m, 2.0);
    three = intern_dbl(m, 3.0);
    reset_errors();
    evdd_register_mul_function(m, dbl_add_as_mul);
    EXPECT(g_error_count == 0 && EVDDVALUEMUL(m) == dbl_add_as_mul,
           "mul stays mutable after intern");
    /* Add gives 5, mul would give 6. */
    EXPECT(nearly(get_dbl(evdd_value_mul(two, three)), 5.0),
           "mul after intern uses the new callback");

    evdd_value_done();
    evdd_register_hash_function(t, hash_scaled);
    EXPECT(EVDDVALUEHASH(t) == hash_scaled,
           "hash freeze: value_done allows a new hash");
    a = intern_dbl(t, 0.5);
    b = intern_dbl(t, 0.5);
    EXPECT(a == b, "hash freeze: the new hash still uniquifies");

    teardown_values();
}

static void test_intern_hash_index_scale(void) {
    printf("\n== intern hash index scale ==\n");
    evdd_value_type t = register_int_type();
    /* Past initial idx cap (1024) and at least one grow (2048+). */
    const int n = 20000;
    evdd_value_id *ids = (evdd_value_id *)malloc((size_t)(n + 1) * sizeof(*ids));
    int i;
    int ok_unique = 1;
    int ok_roundtrip = 1;
    int ok_rehit = 1;

    EXPECT(ids != NULL, "allocate id scratch for scale test");
    if (ids == NULL) {
        teardown_values();
        return;
    }

    EXPECT(intern_int(t, 0) == EVDD_VALUE_ZERO,
           "zero still collapses with hash index");

    for (i = 1; i <= n; i++)
        ids[i] = intern_int(t, i);

    for (i = 1; i <= n; i++) {
        if (get_int(ids[i]) != i)
            ok_roundtrip = 0;
        if (ids[i] == EVDD_VALUE_ZERO)
            ok_unique = 0;
    }
    for (i = 1; i < n; i++) {
        if (ids[i] == ids[i + 1])
            ok_unique = 0;
    }

    EXPECT(ok_roundtrip, "all 20k payloads round-trip after index grows");
    EXPECT(ok_unique, "all 20k non-zero values got distinct ValueIDs");
    EXPECT(evddValueUsed == n, "evddValueUsed counts every non-zero intern");

    for (i = 1; i <= n; i += 97) {
        if (intern_int(t, i) != ids[i])
            ok_rehit = 0;
    }
    EXPECT(ok_rehit, "sparse re-intern hits index after multiple grows");
    EXPECT(intern_int(t, 1) == ids[1] && intern_int(t, n) == ids[n],
           "first and last re-intern hit after scale");

    free(ids);
    teardown_values();
}

static void test_intern_hash_index_grow_boundary(void) {
    printf("\n== intern hash index grow boundary ==\n");
    evdd_value_type t = register_int_type();
    /* Load factor triggers grow when 2*(cnt+1) > cap; first grow at cnt=512
     * with cap=1024. Cross 512, 1024, 2048 and re-check early ids. */
    const int checkpoints[] = {500, 512, 513, 1000, 1024, 1025, 2000, 2048, 2049};
    evdd_value_id early[3];
    int i, c;
    int ok = 1;

    early[0] = intern_int(t, 1);
    early[1] = intern_int(t, 2);
    early[2] = intern_int(t, 3);

    for (i = 4; i <= 2100; i++)
        (void)intern_int(t, i);

    for (c = 0; c < (int)(sizeof(checkpoints) / sizeof(checkpoints[0])); c++) {
        int v = checkpoints[c];
        if (get_int(intern_int(t, v)) != v)
            ok = 0;
    }
    EXPECT(ok, "values around grow thresholds still resolve");
    EXPECT(intern_int(t, 1) == early[0] && intern_int(t, 2) == early[1] &&
               intern_int(t, 3) == early[2],
           "pre-grow ids stay stable and findable after grows");
    teardown_values();
}

static unsigned const_hash(void *a) {
    (void)a;
    return 1u;
}

static unsigned low_bits_hash(void *a) {
    /* Force many values into a few start slots (stress linear probe). */
    return (unsigned)int_val(a) & 0xfu;
}

static void test_intern_hash_collision(void) {
    printf("\n== intern hash collision ==\n");
    evdd_value_type t = evdd_new_value_type();
    evdd_register_hash_function(t, const_hash);
    evdd_register_compare_function(t, int_cmp);
    evdd_register_free_function(t, int_free_internals);
    evdd_register_is_zero_function(t, int_is_zero);
    evdd_register_is_one_function(t, int_is_one);

    evdd_value_id a = intern_int(t, 10);
    evdd_value_id b = intern_int(t, 20);
    EXPECT(a != b && get_int(a) == 10 && get_int(b) == 20,
           "equal hashes still intern separately when compare differs");
    EXPECT(intern_int(t, 10) == a && intern_int(t, 20) == b,
           "collision chain re-intern finds the right payload");
    teardown_values();
}

static void test_intern_long_collision_chain(void) {
    printf("\n== intern long collision chain ==\n");
    evdd_value_type t = evdd_new_value_type();
    const int n = 800;
    evdd_value_id *ids = (evdd_value_id *)malloc((size_t)(n + 1) * sizeof(*ids));
    int i;
    int ok = 1;

    evdd_register_hash_function(t, const_hash);
    evdd_register_compare_function(t, int_cmp);
    evdd_register_free_function(t, int_free_internals);
    evdd_register_is_zero_function(t, int_is_zero);
    evdd_register_is_one_function(t, int_is_one);

    EXPECT(ids != NULL, "allocate scratch for collision-chain test");
    if (ids == NULL) {
        teardown_values();
        return;
    }

    for (i = 1; i <= n; i++)
        ids[i] = intern_int(t, i);

    for (i = 1; i <= n; i++) {
        if (intern_int(t, i) != ids[i] || get_int(ids[i]) != i)
            ok = 0;
    }
    EXPECT(ok, "800 same-hash values distinct and re-findable via probe");

    g_freefun_calls = 0;
    EXPECT(intern_int(t, n / 2) == ids[n / 2], "mid-chain dedup returns live id");
    EXPECT(g_freefun_calls == 1, "mid-chain dedup frees the unused payload");

    free(ids);
    teardown_values();
}

static void test_intern_clustered_hash(void) {
    printf("\n== intern clustered hash ==\n");
    evdd_value_type t = evdd_new_value_type();
    const int n = 4000;
    int i;
    int ok = 1;

    evdd_register_hash_function(t, low_bits_hash);
    evdd_register_compare_function(t, int_cmp);
    evdd_register_free_function(t, int_free_internals);
    evdd_register_is_zero_function(t, int_is_zero);
    evdd_register_is_one_function(t, int_is_one);

    for (i = 1; i <= n; i++)
        (void)intern_int(t, i);
    for (i = 1; i <= n; i += 16) {
        if (get_int(intern_int(t, i)) != i)
            ok = 0;
    }
    EXPECT(ok, "low-bit hash clusters still resolve after grow");
    teardown_values();
}

static void test_intern_no_hash_fallback(void) {
    printf("\n== intern no-hash linear fallback ==\n");
    evdd_value_type t = evdd_new_value_type();
    evdd_value_id a, b, a2;

    /* No hash callback: must use linear scan of evddValues. */
    evdd_register_compare_function(t, int_cmp);
    evdd_register_free_function(t, int_free_internals);
    evdd_register_is_zero_function(t, int_is_zero);
    evdd_register_is_one_function(t, int_is_one);

    a = intern_int(t, 11);
    b = intern_int(t, 22);
    a2 = intern_int(t, 11);
    EXPECT(a != EVDD_VALUE_ZERO && b != a && get_int(b) == 22,
           "no-hash interns distinct values");
    EXPECT(a2 == a && get_int(a) == 11, "no-hash re-intern dedups via linear scan");
    teardown_values();
}

static void test_intern_two_types_index(void) {
    printf("\n== intern two types share index ==\n");
    evdd_value_type ta = register_int_type();
    evdd_value_type tb = register_int_type();
    const int n = 3000;
    evdd_value_id *ida = (evdd_value_id *)malloc((size_t)(n + 1) * sizeof(*ida));
    evdd_value_id *idb = (evdd_value_id *)malloc((size_t)(n + 1) * sizeof(*idb));
    int i;
    int ok = 1;

    EXPECT(ida != NULL && idb != NULL, "allocate scratch for two-type index test");
    if (ida == NULL || idb == NULL) {
        free(ida);
        free(idb);
        teardown_values();
        return;
    }

    for (i = 1; i <= n; i++) {
        ida[i] = intern_int(ta, i);
        idb[i] = intern_int(tb, i);
    }
    for (i = 1; i <= n; i++) {
        if (ida[i] == idb[i])
            ok = 0;
        if (intern_int(ta, i) != ida[i] || intern_int(tb, i) != idb[i])
            ok = 0;
        if (evdd_value_get_type(ida[i]) != ta || evdd_value_get_type(idb[i]) != tb)
            ok = 0;
    }
    EXPECT(ok, "same payloads on two types never alias; both index-hits");
    free(ida);
    free(idb);
    teardown_values();
}

static void test_intern_index_survives_value_done(void) {
    printf("\n== intern index cleared by value_done ==\n");
    evdd_value_type t = register_int_type();
    evdd_value_id a, b;
    int i;

    for (i = 1; i <= 1500; i++)
        (void)intern_int(t, i);
    a = intern_int(t, 42);

    evdd_value_done();
    EXPECT(evddValueUsed == 0 && evddLastValueIndex == 0,
           "value_done resets intern counters (index wiped too)");

    b = intern_int(t, 42);
    EXPECT(b != EVDD_VALUE_ZERO && get_int(b) == 42,
           "intern after value_done rebuilds index");
    EXPECT(intern_int(t, 42) == b, "re-intern after rebuild hits new index");
    (void)a; /* pre-done id is dangling; do not use */
    teardown_values();
}

static void test_intern_types_dont_alias(void) {
    printf("\n== intern types do not alias ==\n");
    evdd_value_type ta = register_int_type();
    evdd_value_type tb = register_int_type();

    evdd_value_id a = intern_int(ta, 5);
    evdd_value_id b = intern_int(tb, 5);
    EXPECT(a != b, "same payload, different type_ids, distinct ValueIDs");
    EXPECT(evdd_value_get_type(a) == ta && evdd_value_get_type(b) == tb,
           "each id keeps its type");
    teardown_values();
}

static void test_intern_dedup_freefun(void) {
    printf("\n== intern dedup / freefun ==\n");
    evdd_value_type t = register_int_type();

    g_freefun_calls = 0;
    evdd_value_id a = intern_int(t, 4);
    EXPECT(g_freefun_calls == 0, "first intern does not free the live payload");

    g_freefun_calls = 0;
    evdd_value_id a2 = intern_int(t, 4);
    EXPECT(a2 == a && get_int(a) == 4, "dedup returns the live id");
    EXPECT(g_freefun_calls == 1, "dedup calls freefun on the unused payload");

    g_freefun_calls = 0;
    evdd_value_done();
    EXPECT(g_freefun_calls >= 1, "value_done calls freefun on remaining rows");
    evdd_value_types_done();
}

static void test_value_done_resets(void) {
    printf("\n== value_done resets intern ==\n");
    evdd_value_type t = register_int_type();
    intern_int(t, 9);
    EXPECT(evddValueUsed >= 1, "intern increments evddValueUsed");
    evdd_value_done();
    EXPECT(evddValueUsed == 0, "value_done resets evddValueUsed");

    evdd_value_id a = intern_int(t, 9);
    EXPECT(a != EVDD_VALUE_ZERO && get_int(a) == 9,
           "intern works again after value_done");
    teardown_values();
}

static void test_is_zero_is_one(void) {
    printf("\n== is_zero / is_one ==\n");
    evdd_value_type t = register_int_type();

    EXPECT(evdd_value_is_zero(EVDD_VALUE_ZERO), "ZERO is zero");
    EXPECT(!evdd_value_is_one(EVDD_VALUE_ZERO), "ZERO is not one");
    evdd_value_id one = one_int(t);
    EXPECT(evdd_value_is_one(one), "typed ONE is one");
    EXPECT(!evdd_value_is_zero(one), "typed ONE is not zero");

    evdd_value_id five = intern_int(t, 5);
    EXPECT(get_int(five) == 5 && !evdd_value_is_zero(five) &&
               !evdd_value_is_one(five),
           "typed 5 is neither zero nor one");
    teardown_values();
}

static void test_algebra(void) {
    printf("\n== algebra on ids ==\n");
    evdd_value_type t = register_int_type();
    evdd_value_id two = intern_int(t, 2);
    evdd_value_id three = intern_int(t, 3);
    evdd_value_id six = intern_int(t, 6);
    EXPECT(get_int(two) == 2 && get_int(three) == 3 && get_int(six) == 6,
           "algebra operands interned");

    evdd_value_id one = one_int(t);
    EXPECT(get_int(evdd_value_mul(two, three)) == 6, "mul 2*3 interned as 6");
    EXPECT(get_int(evdd_value_mul(two, one)) == 2, "mul by ONE is identity");
    EXPECT(get_int(evdd_value_mul(one, three)) == 3, "ONE*x is x");
    EXPECT(get_int(two) == 2 &&
               evdd_value_mul(two, EVDD_VALUE_ZERO) == EVDD_VALUE_ZERO,
           "mul by ZERO absorbs");
    EXPECT(get_int(three) == 3 &&
               evdd_value_mul(EVDD_VALUE_ZERO, three) == EVDD_VALUE_ZERO,
           "ZERO*x absorbs");
    EXPECT(evdd_value_mul(EVDD_VALUE_ZERO, one) == EVDD_VALUE_ZERO,
           "ZERO*ONE is ZERO");

    EXPECT(get_int(evdd_value_add(two, three)) == 5, "add 2+3");
    EXPECT(get_int(evdd_value_add(two, EVDD_VALUE_ZERO)) == 2, "add ZERO is identity");
    EXPECT(get_int(evdd_value_add(one, three)) == 4, "add ONE+typed");
    EXPECT(get_int(evdd_value_add(one, one)) == 2, "add ONE+ONE");
    EXPECT(get_int(evdd_value_sub(six, two)) == 4, "sub 6-2");
    EXPECT(get_int(two) == 2 && evdd_value_sub(two, two) == EVDD_VALUE_ZERO,
           "sub x-x collapses to ZERO");
    EXPECT(get_int(evdd_value_sub(EVDD_VALUE_ZERO, three)) == -3, "sub ZERO-typed");
    EXPECT(evdd_value_sub(one, one) == EVDD_VALUE_ZERO, "sub ONE-ONE is ZERO");
    EXPECT(get_int(evdd_value_div(six, two)) == 3, "div 6/2");
    EXPECT(get_int(evdd_value_div(six, one)) == 6, "div by ONE is identity");
    EXPECT(get_int(two) == 2 &&
               evdd_value_div(EVDD_VALUE_ZERO, two) == EVDD_VALUE_ZERO,
           "ZERO / x is ZERO");
    EXPECT(evdd_value_is_one(evdd_value_div(one, one)), "div ONE/ONE is ONE");

    reset_errors();
    (void)evdd_value_div(two, EVDD_VALUE_ZERO);
    EXPECT(g_error_count >= 1, "div by ZERO is a hard error");

    teardown_values();
}

static void test_mixed_type_error(void) {
    printf("\n== mixed-type ops error ==\n");
    evdd_value_type ta = register_int_type();
    evdd_value_type tb = register_int_type();
    evdd_value_id a = intern_int(ta, 2);
    evdd_value_id b = intern_int(tb, 3);

    reset_errors();
    evdd_value_id r = evdd_value_mul(a, b);
    EXPECT(g_error_count >= 1, "mul mixed types is a hard error");
    EXPECT(get_int(a) == 2 && get_int(b) == 3 && get_int(r) != 6,
           "mixed mul does not intern a product");

    reset_errors();
    (void)evdd_value_add(a, b);
    EXPECT(g_error_count >= 1, "add mixed types is a hard error");

    reset_errors();
    evddNormResult n = evdd_value_normalize(a, b);
    EXPECT(g_error_count >= 1, "normalize mixed types is a hard error");
    (void)n;

    teardown_values();
}

static void test_normalize_first_nonzero(void) {
    printf("\n== normalize first-nonzero ==\n");
    evdd_value_type t = register_dbl_type();
    reset_errors();
    evdd_set_normalization(t, EVDD_NORMALIZE_FIRST_NONZERO);
    EXPECT(g_error_count == 0 &&
               evdd_value_functions_list[t].strategy_frozen &&
               evdd_value_functions_list[t].strategy ==
                   EVDD_NORMALIZE_FIRST_NONZERO,
           "first-nonzero accepted with mul/div/is_zero");
    EXPECT(EVDDVALUENORMALIZE(t) != NULL, "built-in normalizefun installed");

    evddNormResult r = evdd_value_normalize(intern_dbl(t, 3.0), intern_dbl(t, 6.0));
    EXPECT(evdd_value_is_one(r.w_low), "nonzero low becomes ONE");
    EXPECT(nearly(get_dbl(r.w_high), 2.0), "high divided by low");
    EXPECT(nearly(get_dbl(r.incoming), 3.0), "incoming is the low factor");

    r = evdd_value_normalize(EVDD_VALUE_ZERO, intern_dbl(t, 4.0));
    EXPECT(r.w_low == EVDD_VALUE_ZERO && evdd_value_is_one(r.w_high),
           "zero low: high becomes ONE");
    EXPECT(nearly(get_dbl(r.incoming), 4.0), "incoming is the high factor");

    r = evdd_value_normalize(intern_dbl(t, -2.0), intern_dbl(t, 4.0));
    EXPECT(evdd_value_is_one(r.w_low), "negative low still factors");
    EXPECT(nearly(get_dbl(r.w_high), -2.0), "high / low keeps sign");
    EXPECT(nearly(get_dbl(r.incoming), -2.0), "incoming keeps the low value");

    r = evdd_value_normalize(EVDD_VALUE_ZERO, EVDD_VALUE_ZERO);
    EXPECT(r.w_low == EVDD_VALUE_ZERO && r.w_high == EVDD_VALUE_ZERO &&
               r.incoming == EVDD_VALUE_ZERO,
           "both zero: all ZERO");

    evdd_value_normalize_function_t builtin = EVDDVALUENORMALIZE(t);
    reset_errors();
    evdd_register_normalize_function(t, high_unit_norm);
    EXPECT(g_error_count >= 1 && g_last_error == BDD_OP &&
               EVDDVALUENORMALIZE(t) == builtin,
           "normalizefun stays put once the strategy is frozen");
    r = evdd_value_normalize(intern_dbl(t, 3.0), intern_dbl(t, 6.0));
    EXPECT(evdd_value_is_one(r.w_low) && nearly(get_dbl(r.w_high), 2.0),
           "frozen strategy still divides high by low");

    teardown_values();
}

static void test_normalize_low(void) {
    printf("\n== normalize low-edge ==\n");
    evdd_value_type t = register_dbl_type();
    reset_errors();
    evdd_set_normalization(t, EVDD_NORMALIZE_LOW);
    EXPECT(g_error_count == 0 &&
               evdd_value_functions_list[t].strategy == EVDD_NORMALIZE_LOW,
           "low-edge strategy accepted");

    evddNormResult r = evdd_value_normalize(intern_dbl(t, 3.0), intern_dbl(t, 6.0));
    EXPECT(evdd_value_is_one(r.w_low) && nearly(get_dbl(r.w_high), 2.0) &&
               nearly(get_dbl(r.incoming), 3.0),
           "nonzero low: same as first-nonzero");

    r = evdd_value_normalize(EVDD_VALUE_ZERO, intern_dbl(t, 4.0));
    EXPECT(r.w_low == EVDD_VALUE_ZERO && nearly(get_dbl(r.w_high), 4.0) &&
               evdd_value_is_one(r.incoming),
           "zero low: keep high, incoming ONE (no high fallback)");

    teardown_values();
}

static void test_normalize_max_magnitude(void) {
    printf("\n== normalize max-magnitude ==\n");
    evdd_value_type t = register_dbl_type();
    evdd_set_normalization(t, EVDD_NORMALIZE_MAX_MAGNITUDE);

    evddNormResult r = evdd_value_normalize(intern_dbl(t, 3.0), intern_dbl(t, 4.0));
    EXPECT(evdd_value_is_one(r.w_high), "larger |w| edge becomes ONE");
    EXPECT(nearly(get_dbl(r.w_low), 0.75), "other edge divided by max");
    EXPECT(nearly(get_dbl(r.incoming), 4.0), "incoming is the max-magnitude value");

    r = evdd_value_normalize(intern_dbl(t, -5.0), intern_dbl(t, 3.0));
    EXPECT(evdd_value_is_one(r.w_low), "divide by the actual max-mag edge (keep sign)");
    EXPECT(nearly(get_dbl(r.w_high), -0.6), "other / -5");
    EXPECT(nearly(get_dbl(r.incoming), -5.0), "incoming is -5");

    r = evdd_value_normalize(intern_dbl(t, 3.0), intern_dbl(t, -3.0));
    EXPECT(evdd_value_is_one(r.w_low), "tie: prefer low");
    EXPECT(nearly(get_dbl(r.w_high), -1.0), "tie: high / low");
    EXPECT(nearly(get_dbl(r.incoming), 3.0), "tie: incoming is low");

    teardown_values();
}

static void test_normalize_l2(void) {
    printf("\n== normalize L2 ==\n");
    evdd_value_type t = register_dbl_type();
    evdd_set_normalization(t, EVDD_NORMALIZE_L2);

    evddNormResult r = evdd_value_normalize(intern_dbl(t, 3.0), intern_dbl(t, 4.0));
    EXPECT(nearly(get_dbl(r.w_low), 0.6) && nearly(get_dbl(r.w_high), 0.8),
           "L2 (3,4) -> (0.6, 0.8)");
    EXPECT(nearly(get_dbl(r.incoming), 5.0), "L2 incoming is the norm");
    EXPECT(nearly(get_dbl(r.w_low), 0.6) && !evdd_value_is_one(r.w_low) &&
               !evdd_value_is_one(r.w_high),
           "L2 need not produce a unit edge");

    teardown_values();
}

static void test_normalize_custom_and_freeze(void) {
    printf("\n== normalize custom + freeze ==\n");
    evdd_value_type t = register_dbl_type();
    evdd_register_normalize_function(t, high_unit_norm);
    EXPECT(EVDDVALUENORMALIZE(t) == high_unit_norm, "custom normalizefun registered");

    reset_errors();
    evdd_set_normalization(t, EVDD_NORMALIZE_CUSTOM);
    EXPECT(g_error_count == 0 &&
               evdd_value_functions_list[t].strategy == EVDD_NORMALIZE_CUSTOM &&
               evdd_value_functions_list[t].strategy_frozen,
           "CUSTOM accepted when normalizefun is set");
    EXPECT(evdd_value_functions_list[t].strategy_frozen, "CUSTOM freezes");

    evddNormResult r = evdd_value_normalize(intern_dbl(t, 3.0), intern_dbl(t, 6.0));
    EXPECT(evdd_value_is_one(r.w_high), "custom: high becomes ONE");
    EXPECT(nearly(get_dbl(r.w_low), 0.5), "custom: low / high");
    EXPECT(nearly(get_dbl(r.incoming), 6.0), "custom: incoming is high");

    reset_errors();
    evdd_set_normalization(t, EVDD_NORMALIZE_FIRST_NONZERO);
    EXPECT(g_error_count >= 1, "second set_normalization is a hard error");
    EXPECT(evdd_value_functions_list[t].strategy == EVDD_NORMALIZE_CUSTOM,
           "frozen strategy is unchanged");

    teardown_values();
}

static void test_set_normalization_missing_callbacks(void) {
    printf("\n== set_normalization missing callbacks ==\n");
    evdd_value_type t = evdd_new_value_type();
    evdd_register_hash_function(t, int_hash);
    evdd_register_compare_function(t, int_cmp);

    reset_errors();
    evdd_set_normalization(t, EVDD_NORMALIZE_FIRST_NONZERO);
    EXPECT(g_error_count >= 1, "first-nonzero without mul/div/is_zero errors");
    EXPECT(!evdd_value_functions_list[t].strategy_frozen,
           "failed set does not freeze");

    evdd_value_type d = register_int_type(); /* no sqrt */
    reset_errors();
    evdd_set_normalization(d, EVDD_NORMALIZE_L2);
    EXPECT(g_error_count >= 1, "L2 without sqrt/abs_sq/add errors");
    EXPECT(!evdd_value_functions_list[d].strategy_frozen, "failed L2 does not freeze");

    evdd_value_type c = register_dbl_type();
    reset_errors();
    evdd_set_normalization(c, EVDD_NORMALIZE_CUSTOM);
    EXPECT(g_error_count >= 1, "CUSTOM without normalizefun errors");

    teardown_values();
}

static void test_two_types_two_norms(void) {
    printf("\n== two types, two norms ==\n");
    evdd_value_type ints = register_dbl_type();
    evdd_value_type dbls = register_dbl_type();
    evdd_set_normalization(ints, EVDD_NORMALIZE_FIRST_NONZERO);
    evdd_set_normalization(dbls, EVDD_NORMALIZE_L2);
    EXPECT(evdd_value_functions_list[ints].strategy == EVDD_NORMALIZE_FIRST_NONZERO &&
               evdd_value_functions_list[dbls].strategy == EVDD_NORMALIZE_L2,
           "different types may freeze different strategies");

    evddNormResult a =
        evdd_value_normalize(intern_dbl(ints, 3.0), intern_dbl(ints, 6.0));
    evddNormResult b =
        evdd_value_normalize(intern_dbl(dbls, 3.0), intern_dbl(dbls, 4.0));
    EXPECT(evdd_value_is_one(a.w_low) && nearly(get_dbl(a.w_high), 2.0),
           "int-typed diagram uses first-nonzero");
    EXPECT(nearly(get_dbl(b.w_low), 0.6) && nearly(get_dbl(b.w_high), 0.8),
           "double-typed diagram uses L2");
    teardown_values();
}

static void test_independent_of_bdd_init(void) {
    printf("\n== independent of bdd_init ==\n");
    EXPECT(!bdd_isrunning(), "bdd is not running");
    evdd_value_type t = register_int_type();
    evdd_value_id a = intern_int(t, 11);
    EXPECT(get_int(a) == 11, "intern works without bdd_init");
    teardown_values();
}

static void test_node_table_init(void) {
    printf("\n== node table init ==\n");

    EXPECT(sizeof(EvddNode) == 28, "EvddNode is 28 bytes");
    EXPECT(!evdd_isrunning() && evddnodes == NULL, "evdd starts idle");

    reset_errors();
    EXPECT(evdd_init(1000) == 0, "evdd_init succeeds");
    EXPECT(evdd_isrunning(), "evdd_isrunning after init");
    EXPECT(evddnodes != NULL && evddnodesize >= 1000, "table allocated at prime size");
    EXPECT(evddfreenum == evddnodesize - 1 && evddfreepos == 1,
           "freelist starts at 1 with size-1 free nodes");

    EXPECT(evddnodes[EVDD_TRUE_NODE].refcou == EVDD_MAXREF,
           "1-terminal is pinned MAXREF");
    EXPECT(EVDDLEVEL(EVDD_TRUE_NODE) == EVDD_MAXLEVEL,
           "1-terminal sits at MAXLEVEL");
    EXPECT(EVDDLOW(EVDD_TRUE_NODE) == EVDD_TRUE_NODE &&
               EVDDHIGH(EVDD_TRUE_NODE) == EVDD_TRUE_NODE,
           "1-terminal points to itself");
    EXPECT(EVDDISFREE(1), "first free slot has low==-1");

    EVDD z = evdd_false();
    EXPECT(z.node == EVDD_TRUE_NODE && z.w == EVDD_VALUE_ZERO,
           "evdd_false is (TRUE, ZERO)");
    evdd_value_type ti = register_int_type();
    EVDD o = evdd_const(one_int(ti));
    EXPECT(o.node == EVDD_TRUE_NODE && evdd_value_is_one(o.w),
           "evdd_const(typed 1) is (TRUE, one)");
    teardown_values();

    reset_errors();
    EXPECT(evdd_init(100) != 0 && g_error_count >= 1,
           "second evdd_init without done errors");

    evdd_done();
    EXPECT(!evdd_isrunning() && evddnodes == NULL, "evdd_done frees the table");

    EXPECT(evdd_init(64) == 0 && evdd_isrunning(), "re-init after done works");
    EXPECT(!bdd_isrunning(), "evdd_init does not start bdd");
    evdd_done();
}

static void test_makenode_raw(void) {
    printf("\n== makenode_raw ==\n");

    EXPECT(evdd_init(128) == 0, "makenode_raw: evdd_init");

    evdd_value_type ti = register_int_type();
    EVDD f = evdd_false();
    EVDD t = unit_const_int(ti);
    EVDD a = evdd_makenode_raw(0, f, t);
    EXPECT(evdd_value_is_one(a.w) && a.node >= 1, "raw returns (unit, node>=1)");
    EXPECT(EVDDLEVEL(a.node) == 0 && EVDDLOW(a.node) == EVDD_TRUE_NODE &&
               EVDDHIGH(a.node) == EVDD_TRUE_NODE,
           "raw stores level and child node ids");
    EXPECT(EVDDWLOW(a.node) == EVDD_VALUE_ZERO &&
               evdd_value_is_one(EVDDWHIGH(a.node)),
           "raw stores child edge weights on the node");

    EVDD b = evdd_makenode_raw(0, f, t);
    EXPECT(a.node == b.node && evddproduced == 1, "raw unique hit shares node");

    EVDD same = evdd_makenode_raw(3, t, t);
    EXPECT(same.node == EVDD_TRUE_NODE && evdd_value_is_one(same.w),
           "raw skip-reduces equal children to that edge");
    evdd_set_normalization(ti, EVDD_NORMALIZE_FIRST_NONZERO);
    EVDD same_e = evdd_makenode(3, t, t);
    EXPECT(same_e.node == EVDD_TRUE_NODE && evdd_value_is_one(same_e.w),
           "eager skip-reduces equal children to that edge");

    EVDD tw = t;
    tw.w = intern_int(ti, 2);
    EVDD fold = evdd_makenode_raw(1, tw, tw);
    EXPECT(fold.node == EVDD_TRUE_NODE && get_int(fold.w) == 2,
           "raw skip-reduce keeps common edge weight");
    EVDD fold_e = evdd_makenode(1, tw, tw);
    /* normalize (2,2) -> incoming 2, edges (1,1) -> skip to TRUE with w=1
       then makenode multiplies incoming => (TRUE, 2) */
    EXPECT(fold_e.node == EVDD_TRUE_NODE && get_int(fold_e.w) == 2,
           "eager skip-reduce folds normalized incoming onto edge");

    EVDD zedge = f;
    zedge.w = EVDD_VALUE_ZERO;
    EVDD skip_f = evdd_makenode_raw(2, zedge, zedge);
    EXPECT(EVDDISFALSE(skip_f),
           "raw skip-reduce to false is canonical evdd_false");

    EVDD low = f;
    EVDD high = t;
    low.w = intern_int(ti, 2);
    high.w = intern_int(ti, 4); /* 4/2=2; avoid int 3/2→1 which skip-reduces */
    EVDD r1 = evdd_makenode_raw(2, low, high);
    EVDD r2 = evdd_makenode_raw(2, low, high);
    EXPECT(r1.node == r2.node && EVDDWLOW(r1.node) == low.w &&
               EVDDWHIGH(r1.node) == high.w,
           "raw keeps unnormalized weights and uniquifies them");

    EVDD other = f;
    other.w = intern_int(ti, 5);
    EVDD r3 = evdd_makenode_raw(2, other, high);
    EXPECT(r3.node != r1.node, "different outgoing weight => different node");

    EVDD e = evdd_makenode(2, low, high);
    EXPECT(e.w == low.w && evdd_value_is_one(EVDDWLOW(e.node)) &&
               get_int(EVDDWHIGH(e.node)) == 2,
           "makenode weight-normalizes then calls raw");
    EXPECT(get_int(EVDDWHIGH(e.node)) == get_int(evdd_value_div(high.w, low.w)),
           "makenode high weight is w_high/incoming");

    /* Eager result shares with raw built on the normalized key. */
    EVDD nlow = low;
    EVDD nhigh = high;
    nlow.w = one_int(ti);
    nhigh.w = EVDDWHIGH(e.node);
    EVDD raw_norm = evdd_makenode_raw(2, nlow, nhigh);
    EXPECT(raw_norm.node == e.node, "makenode and raw agree on normalized key");

    evdd_done();
    teardown_values();
}

static void test_evdd_normalize(void) {
    printf("\n== evdd_normalize ==\n");

    EXPECT(evdd_init(128) == 0, "normalize: evdd_init");

    evdd_value_type ti = register_int_type();
    evdd_set_normalization(ti, EVDD_NORMALIZE_FIRST_NONZERO);

    EVDD f = evdd_false();
    EVDD t = unit_const_int(ti);
    EVDD cf = evdd_normalize(f);
    EVDD ct = evdd_normalize(t);
    EXPECT(EVDDISFALSE(cf), "normalize false is false");
    EXPECT(ct.node == EVDD_TRUE_NODE && evdd_value_is_one(ct.w),
           "normalize unit const is unit");

    EVDD low = f;
    EVDD high = t;
    low.w = intern_int(ti, 2);
    high.w = intern_int(ti, 4);

    EVDD raw = evdd_makenode_raw(1, low, high);
    EVDD eager = evdd_makenode(1, low, high);
    EVDD norm = evdd_normalize(raw);
    EXPECT(norm.node == eager.node && norm.w == eager.w,
           "normalize(raw) matches eager makenode (single node)");

    /* Multi-level: raw then normalize == makenode throughout. */
    EVDD mid_raw = evdd_makenode_raw(0, f, t); /* identity weights */
    EVDD up_low = mid_raw;
    EVDD up_high = t;
    up_low.w = intern_int(ti, 6);
    up_high.w = intern_int(ti, 3);
    EVDD top_raw = evdd_makenode_raw(2, up_low, up_high);

    EVDD mid_e = evdd_makenode(0, f, t);
    EVDD up_low_e = mid_e;
    EVDD up_high_e = t;
    up_low_e.w = intern_int(ti, 6);
    up_high_e.w = intern_int(ti, 3);
    EVDD top_e = evdd_makenode(2, up_low_e, up_high_e);

    EVDD top_n = evdd_normalize(top_raw);
    EXPECT(top_n.node == top_e.node && top_n.w == top_e.w,
           "normalize(raw DAG) matches eager makenode throughout");

    /* Root incoming weight scales the result factor. */
    EVDD scaled = top_raw;
    scaled.w = intern_int(ti, 5);
    EVDD scaled_n = evdd_normalize(scaled);
    EXPECT(scaled_n.node == top_n.node &&
               scaled_n.w == evdd_value_mul(intern_int(ti, 5), top_n.w),
           "normalize multiplies root incoming weight");

    /*
     * Diamond: left and right both point at the same mid. After normalize,
     * the shared child must still be one node (memo + unique table).
     *
     * Mid edges both go to the 1-terminal (zero is a weight, not a sink).
     * Weights (1,2) keep FIRST_NONZERO incoming at 1 so left's (mid,true)
     * pair does not hit integer truncation (2,1) -> (1,0).
     *
     *        top
     *       /   \
     *    left   right
     *       \   /
     *        mid
     */
    EVDD mid_l = t;
    EVDD mid_h = t;
    mid_l.w = one_int(ti);
    mid_h.w = intern_int(ti, 2);
    EVDD mid = evdd_makenode_raw(0, mid_l, mid_h);

    EVDD left_l = mid;
    EVDD left_h = t;
    left_l.w = one_int(ti);
    left_h.w = one_int(ti);
    EVDD left = evdd_makenode_raw(1, left_l, left_h);

    EVDD right_l = mid;
    EVDD right_h = f; /* (TRUE, ZERO) */
    right_l.w = one_int(ti);
    EVDD right = evdd_makenode_raw(1, right_l, right_h);

    EXPECT(EVDDLOW(left.node) == mid.node && EVDDLOW(right.node) == mid.node,
           "diamond raw: left and right share mid");

    EVDD dtop_l = left;
    EVDD dtop_r = right;
    dtop_l.w = one_int(ti);
    dtop_r.w = one_int(ti);
    EVDD diamond_raw = evdd_makenode_raw(2, dtop_l, dtop_r);

    EVDD mid_e2 = evdd_makenode(0, mid_l, mid_h);
    EVDD left_e2 = evdd_makenode(1, mid_e2, t);
    EVDD right_e2 = evdd_makenode(1, mid_e2, f);
    EVDD diamond_e = evdd_makenode(2, left_e2, right_e2);

    EVDD diamond_n = evdd_normalize(diamond_raw);
    EXPECT(diamond_n.node == diamond_e.node && diamond_n.w == diamond_e.w,
           "normalize(diamond) matches eager makenode");

    int left_n = EVDDLOW(diamond_n.node);
    int right_n = EVDDHIGH(diamond_n.node);
    EXPECT(left_n != right_n, "diamond: left and right stay distinct nodes");
    EXPECT(EVDDLOW(left_n) == EVDDLOW(right_n),
           "diamond: shared mid remains one node after normalize");
    EXPECT(EVDDLOW(left_n) == mid_e2.node,
           "diamond: shared child is the eager-normalized mid");

    evdd_done();
    teardown_values();
}

/* Full binary tree of raw nodes. Bottom edges carry distinct positive weights
 * so normalize cannot collapse the tree. Ancestors are referenced across the
 * sibling build, same as a caller that keeps the path alive. */
static EVDD normgc_build(unsigned lvl, int nvars, evdd_value_type t, int *seq)
{
    if ((int)lvl + 1 == nvars) {
        EVDD lo = evdd_const(intern_dbl(t, (double)(*seq)++));
        EVDD hi = evdd_const(intern_dbl(t, (double)(*seq)++));
        return evdd_makenode_raw(lvl, lo, hi);
    }

    EVDD lo = evdd_addref(normgc_build(lvl + 1, nvars, t, seq));
    EVDD hi = evdd_addref(normgc_build(lvl + 1, nvars, t, seq));
    lo.w = one_dbl(t);
    hi.w = one_dbl(t);
    EVDD r = evdd_makenode_raw(lvl, lo, hi);
    evdd_delref(lo);
    evdd_delref(hi);
    return r;
}

static double normgc_eval(EVDD e, unsigned x, int nvars)
{
    double w = get_dbl(e.w);
    int nd = e.node;
    int guard = 0;

    while (nd != EVDD_TRUE_NODE) {
        int bit;
        if (++guard > nvars || nd < 0 || nd >= evddnodesize || EVDDISFREE(nd))
            return -1.0;
        bit = (int)((x >> (nvars - 1 - (int)EVDDLEVEL(nd))) & 1u);
        w *= get_dbl(bit ? EVDDWHIGH(nd) : EVDDWLOW(nd));
        nd = bit ? EVDDHIGH(nd) : EVDDLOW(nd);
    }
    return w;
}

static int normgc_count(int node, unsigned char *seen)
{
    if (node == EVDD_TRUE_NODE || node < 0 || node >= evddnodesize || seen[node])
        return 0;
    seen[node] = 1;
    if (EVDDISFREE(node))
        return 0;
    return 1 + normgc_count(EVDDLOW(node), seen) +
           normgc_count(EVDDHIGH(node), seen);
}

static int normgc_levels_ok(void)
{
    int i;
    for (i = 1; i < evddnodesize; i++) {
        int c;
        if (EVDDISFREE(i))
            continue;
        c = EVDDLOW(i);
        if (c != EVDD_TRUE_NODE &&
            (c < 0 || c >= evddnodesize || EVDDISFREE(c) ||
             (unsigned)EVDDLEVEL(c) <= EVDDLEVEL(i)))
            return 0;
        c = EVDDHIGH(i);
        if (c != EVDD_TRUE_NODE &&
            (c < 0 || c >= evddnodesize || EVDDISFREE(c) ||
             (unsigned)EVDDLEVEL(c) <= EVDDLEVEL(i)))
            return 0;
    }
    return 1;
}

static void test_evdd_normalize_under_gc(void)
{
    printf("\n== evdd_normalize under GC ==\n");

    /* 6 variables: 63 raw nodes. init(80) lands on prime 83, so 19 free slots
     * remain and normalize must collect or resize before it finishes. */
    const int nvars = 6;
    const int nodes = (1 << nvars) - 1;
    int seq = 1;
    int x;
    int match = 1;
    unsigned char *seen;

    EXPECT(evdd_init(80) == 0, "normalize GC: evdd_init");

    evdd_value_type t = register_dbl_type();
    evdd_set_normalization(t, EVDD_NORMALIZE_FIRST_NONZERO);

    EVDD raw = evdd_addref(normgc_build(0, nvars, t, &seq));
    EXPECT(evddfreenum < nodes,
           "normalize GC: free list cannot hold a second copy");

    EVDD norm = evdd_normalize(raw);
    EXPECT(evddnodes[raw.node].refcou == 1,
           "normalize GC: input refcou restored");
    EXPECT(norm.node != raw.node && norm.node != EVDD_TRUE_NODE &&
               evddnodes[norm.node].refcou == 0,
           "normalize GC: result is a new unreferenced node");
    EXPECT(normgc_levels_ok(), "normalize GC: child level is below its parent");

    seen = (unsigned char *)calloc((size_t)evddnodesize, 1);
    EXPECT(seen != NULL && normgc_count(norm.node, seen) == nodes,
           "normalize GC: normalized tree keeps every node");
    free(seen);

    for (x = 0; x < (1 << nvars); x++) {
        if (!nearly(normgc_eval(raw, (unsigned)x, nvars),
                    normgc_eval(norm, (unsigned)x, nvars)))
            match = 0;
    }
    EXPECT(match, "normalize GC: raw and normalized agree on every assignment");

    evdd_delref(raw);
    evdd_done();
    teardown_values();
}

/* Bottom-level pairs (s, s/2) with a distinct s per pair: every bottom raw
 * node normalizes to the same node (1, 1/2). */
static EVDD build_shared_pairs(evdd_value_type t, unsigned level, int off, int len) {
    if (len == 2) {
        double s = 1.0 + off / 2;
        return evdd_makenode_raw(level, evdd_const(intern_dbl(t, s)),
                                 evdd_const(intern_dbl(t, s / 2)));
    }
    EVDD lo = evdd_addref(build_shared_pairs(t, level + 1, off, len / 2));
    EVDD hi = evdd_addref(build_shared_pairs(t, level + 1, off + len / 2, len / 2));
    EVDD r = evdd_makenode_raw(level, lo, hi);
    evdd_delref(lo);
    evdd_delref(hi);
    return r;
}

/* #18: more than EVDD_MAXREF memo entries share one result node. */
static void test_evdd_normalize_shared_result(void) {
    printf("\n== evdd_normalize shared result ==\n");
    teardown_values();
    EXPECT(evdd_init(1 << 13) == 0, "shared result: evdd_init");
    evdd_value_type t = register_dbl_type();
    evdd_set_normalization(t, EVDD_NORMALIZE_FIRST_NONZERO);

    /* 11 variables: 1024 bottom raw nodes, one normalized bottom node. */
    EVDD raw = evdd_addref(build_shared_pairs(t, 0, 0, 1 << 11));
    EVDD norm = evdd_normalize(raw);
    EXPECT(norm.node != EVDD_TRUE_NODE, "shared result: normalize returns a node");
    EXPECT(evddrefstacktop == 0, "shared result: reference stack unwinds");

    evdd_delref(raw);
    evdd_gbc();
    int live = 0, pinned = 0;
    for (int i = 1; i < evddnodesize; i++) {
        if (EVDDISFREE(i))
            continue;
        live++;
        if (evddnodes[i].refcou > 0)
            pinned++;
    }
    EXPECT(pinned == 0, "shared result: no node keeps a reference after normalize");
    EXPECT(live == 0, "shared result: everything is collected once the roots go");

    evdd_done();
    teardown_values();
}

static void test_evdd_refs_gbc(void) {
    printf("\n== refs / gbc ==\n");

    EXPECT(evdd_init(64) == 0, "refs_gbc: evdd_init");

    evdd_value_type ti = register_int_type();
    EVDD f = evdd_false();
    EVDD t = unit_const_int(ti);
    EVDD keep = evdd_makenode_raw(0, f, t);
    EVDD drop = evdd_makenode_raw(1, f, t);
    int drop_node = drop.node;
    int free_before;

    EXPECT(evddnodes[keep.node].refcou == 0 && evddnodes[drop_node].refcou == 0,
           "new nodes start with refcou 0");

    evdd_addref(keep);
    EXPECT(evddnodes[keep.node].refcou == 1, "addref bumps root refcou");

    EVDD still_false = evdd_addref(f);
    EXPECT(EVDDISFALSE(still_false) &&
               evddnodes[EVDD_TRUE_NODE].refcou == EVDD_MAXREF,
           "addref on false edge is a no-op (terminal stays MAXREF)");

    free_before = evddfreenum;
    evdd_gbc();
    EXPECT(EVDDISFREE(drop_node), "gbc frees unreachable node");
    EXPECT(!EVDDISFREE(keep.node) && evddnodes[keep.node].refcou == 1,
           "gbc keeps addref'd root");
    EXPECT(evddfreenum > free_before, "gbc increases freelist");

    EVDD again = evdd_makenode_raw(0, f, t);
    EXPECT(again.node == keep.node, "unique hit still works after gbc");

    evdd_delref(keep);
    EXPECT(evddnodes[keep.node].refcou == 0, "delref drops refcou to 0");
    evdd_gbc();
    EXPECT(EVDDISFREE(keep.node), "gbc frees node after last delref");

    evdd_done();
    teardown_values();
}

/** Path product at a constant edge: zero weight → 0, else w. */
static int path_value_int(EVDD e) {
    if (!EVDDISCONST(e.node))
        return -1;
    if (evdd_value_is_zero(e.w))
        return 0;
    return get_int(e.w);
}

static void test_evdd_apply(void) {
    printf("\n== evdd_apply ==\n");

    EXPECT(evdd_init(128) == 0, "apply: evdd_init");
    evdd_value_type ti = register_int_type();
    evdd_set_normalization(ti, EVDD_NORMALIZE_FIRST_NONZERO);

    EVDD f = evdd_false();
    EVDD t = unit_const_int(ti);

    EVDD tw = t;
    tw.w = intern_int(ti, 2);
    EVDD th = t;
    th.w = intern_int(ti, 3);
    EVDD mul_c = evdd_apply(tw, th, evdd_value_mul);
    EXPECT(mul_c.node == EVDD_TRUE_NODE && get_int(mul_c.w) == 6,
           "apply mul on TRUE edges multiplies weights");

    EVDD add_c = evdd_apply(tw, th, evdd_value_add);
    EXPECT(add_c.node == EVDD_TRUE_NODE && get_int(add_c.w) == 5,
           "apply add on TRUE edges adds weights");

    EVDD add_tt = evdd_apply(t, t, evdd_value_add);
    EXPECT(add_tt.node == EVDD_TRUE_NODE && get_int(add_tt.w) == 2,
           "apply(unit,unit,add) is true with weight 2");

    EVDD z = evdd_apply(f, tw, evdd_value_mul);
    EXPECT(EVDDISFALSE(z), "apply mul with FALSE is FALSE");

    /* a: low=0, high=2; b: low=2, high=4 (divides cleanly under first-nonzero).
       mul → low=0, high=8. */
    EVDD a_l = f;
    EVDD a_h = t;
    a_h.w = intern_int(ti, 2);
    EVDD a = evdd_makenode(0, a_l, a_h);

    EVDD b_l = t;
    EVDD b_h = t;
    b_l.w = intern_int(ti, 2);
    b_h.w = intern_int(ti, 4);
    EVDD b = evdd_makenode(0, b_l, b_h);

    EVDD prod = evdd_apply(a, b, evdd_value_mul);
    EVDD again = evdd_apply(a, b, evdd_value_mul);
    EXPECT(prod.node == again.node && prod.w == again.w,
           "apply cache returns same edge on second call");

    EVDD pl;
    pl.node = EVDDLOW(prod.node);
    pl.w = evdd_value_mul(prod.w, EVDDWLOW(prod.node));
    EVDD ph;
    ph.node = EVDDHIGH(prod.node);
    ph.w = evdd_value_mul(prod.w, EVDDWHIGH(prod.node));
    EXPECT(path_value_int(pl) == 0, "apply mul: low path value is 0");
    EXPECT(path_value_int(ph) == 8, "apply mul: high path value is 8");

    /* Level-1 diagram times constant 2: high 5 → 10. */
    EVDD c_l = f;
    EVDD c_h = t;
    c_h.w = intern_int(ti, 5);
    EVDD c = evdd_makenode(1, c_l, c_h);
    EVDD scaled = evdd_apply(c, tw, evdd_value_mul);
    EVDD sh;
    sh.node = EVDDHIGH(scaled.node);
    sh.w = evdd_value_mul(scaled.w, EVDDWHIGH(scaled.node));
    EVDD sl;
    sl.node = EVDDLOW(scaled.node);
    sl.w = evdd_value_mul(scaled.w, EVDDWLOW(scaled.node));
    EXPECT(path_value_int(sl) == 0 && path_value_int(sh) == 10,
           "apply mul: scale level-1 diagram by constant 2");

    evdd_done();
    teardown_values();
}

/* ---- Gate-style helpers (test-local; not library API yet) ---- */

static EVDD evdd_const_dbl(evdd_value_type td, double v) {
    if (v == 0.0)
        return evdd_false();
    return evdd_const(intern_dbl(td, v));
}

static EVDD test_scalar_mul_dbl(EVDD f, evdd_value_type td, double c) {
    return evdd_apply(f, evdd_const_dbl(td, c), evdd_value_mul);
}

/** Pauli-X on a state whose top node is exactly @p level (swap cofactors). */
static EVDD test_gate_x_level(unsigned level, EVDD state) {
    if (EVDDISCONST(state.node) || EVDDLEVEL(state.node) != level)
        return state;
    EVDD low;
    low.node = EVDDLOW(state.node);
    low.w = evdd_value_mul(state.w, EVDDWLOW(state.node));
    EVDD high;
    high.node = EVDDHIGH(state.node);
    high.w = evdd_value_mul(state.w, EVDDWHIGH(state.node));
    return evdd_makenode(level, high, low);
}

static double path_value_dbl(EVDD e) {
    if (!EVDDISCONST(e.node))
        return -1.0;
    if (evdd_value_is_zero(e.w))
        return 0.0;
    return get_dbl(e.w);
}

static void amps_level0_dbl(EVDD s, double *a0, double *a1) {
    if (EVDDISCONST(s.node)) {
        *a0 = path_value_dbl(s);
        *a1 = 0.0;
        return;
    }
    EVDD e0;
    e0.node = EVDDLOW(s.node);
    e0.w = evdd_value_mul(s.w, EVDDWLOW(s.node));
    EVDD e1;
    e1.node = EVDDHIGH(s.node);
    e1.w = evdd_value_mul(s.w, EVDDWHIGH(s.node));
    *a0 = path_value_dbl(e0);
    *a1 = path_value_dbl(e1);
}

static int dbl_eq(double a, double b) {
    return fabs(a - b) < 1e-9;
}

static void test_evdd_gates(void) {
    printf("\n== evdd gate-style ==\n");

    EXPECT(evdd_init(128) == 0, "gates: evdd_init");
    /* Doubles + max-magnitude: ratios stay exact under X swap (unlike int
       first-nonzero truncation). */
    evdd_value_type td = register_dbl_type();
    evdd_set_normalization(td, EVDD_NORMALIZE_MAX_MAGNITUDE);

    EVDD f = evdd_false();

    EVDD ket0 = evdd_makenode(0, evdd_const_dbl(td, 1.0), f);
    EVDD ket1 = evdd_makenode(0, f, evdd_const_dbl(td, 1.0));

    double a0, a1;
    amps_level0_dbl(ket0, &a0, &a1);
    EXPECT(dbl_eq(a0, 1.0) && dbl_eq(a1, 0.0), "ket0 amplitudes (1,0)");
    amps_level0_dbl(ket1, &a0, &a1);
    EXPECT(dbl_eq(a0, 0.0) && dbl_eq(a1, 1.0), "ket1 amplitudes (0,1)");

    EVDD x0 = test_gate_x_level(0, ket0);
    EVDD x1 = test_gate_x_level(0, ket1);
    amps_level0_dbl(x0, &a0, &a1);
    EXPECT(dbl_eq(a0, 0.0) && dbl_eq(a1, 1.0), "X|0> = |1>");
    amps_level0_dbl(x1, &a0, &a1);
    EXPECT(dbl_eq(a0, 1.0) && dbl_eq(a1, 0.0), "X|1> = |0>");
    EXPECT(x0.node == ket1.node && x1.node == ket0.node,
           "X maps basis states onto each other (unique table)");

    EVDD scaled1 = test_scalar_mul_dbl(ket1, td, 3.0);
    amps_level0_dbl(scaled1, &a0, &a1);
    EXPECT(dbl_eq(a0, 0.0) && dbl_eq(a1, 3.0), "scalar mul 3|1> amplitudes (0,3)");

    EVDD psi = evdd_makenode(0, evdd_const_dbl(td, 2.0), evdd_const_dbl(td, 4.0));
    amps_level0_dbl(psi, &a0, &a1);
    EXPECT(dbl_eq(a0, 2.0) && dbl_eq(a1, 4.0), "psi amplitudes (2,4)");
    EVDD xpsi = test_gate_x_level(0, psi);
    amps_level0_dbl(xpsi, &a0, &a1);
    EXPECT(dbl_eq(a0, 4.0) && dbl_eq(a1, 2.0), "X|psi> swaps amplitudes");

    EVDD q1_ket0 = evdd_makenode(1, evdd_const_dbl(td, 1.0), f);
    EVDD ket10 = evdd_makenode(0, f, q1_ket0);
    EVDD after_x0 = test_gate_x_level(0, ket10);
    EXPECT(EVDDLEVEL(after_x0.node) == 0 &&
               EVDDHIGH(after_x0.node) == EVDD_TRUE_NODE &&
               evdd_value_is_zero(EVDDWHIGH(after_x0.node)),
           "X on q0 sends |10> to |00|-shaped node (high weight 0)");
    {
        EVDD low;
        low.node = EVDDLOW(after_x0.node);
        low.w = evdd_value_mul(after_x0.w, EVDDWLOW(after_x0.node));
        EVDD q1_low;
        q1_low.node = EVDDLOW(low.node);
        q1_low.w = evdd_value_mul(low.w, EVDDWLOW(low.node));
        EXPECT(EVDDLEVEL(low.node) == 1 && dbl_eq(path_value_dbl(q1_low), 1.0),
               "X|10> low cofactor is |0> on q1 with amp 1");
    }

    EVDD s0 = test_scalar_mul_dbl(ket0, td, 2.0);
    EVDD s1 = test_scalar_mul_dbl(ket1, td, 3.0);
    EVDD sum = evdd_apply(s0, s1, evdd_value_add);
    amps_level0_dbl(sum, &a0, &a1);
    EXPECT(dbl_eq(a0, 2.0) && dbl_eq(a1, 3.0), "apply-add builds 2|0>+3|1>");

    evdd_done();
    teardown_values();
}

static int buf_contains(const char *buf, const char *needle) {
    return buf != NULL && needle != NULL && strstr(buf, needle) != NULL;
}

static void test_evdd_dot(void) {
    printf("\n== evdd_printdot ==\n");

    EXPECT(evdd_init(64) == 0, "dot: evdd_init");
    evdd_value_type ti = register_int_type();
    evdd_set_normalization(ti, EVDD_NORMALIZE_FIRST_NONZERO);

    EVDD f = evdd_false();
    EVDD t = unit_const_int(ti);
    EVDD hi = t;
    hi.w = intern_int(ti, 4);
    EVDD n = evdd_makenode(0, f, hi);
    /* first-nonzero: low is (TRUE, ZERO), high weight 4 → factor from high. */

    char *buf = NULL;
    size_t buflen = 0;
    FILE *mem = open_memstream(&buf, &buflen);
    EXPECT(mem != NULL, "dot: open_memstream");
    if (mem) {
        evdd_fprintdot(mem, n, 0);
        fclose(mem);
    }

    EXPECT(buf_contains(buf, "digraph G"), "dot: digraph header");
    EXPECT(buf_contains(buf, "root ->"), "dot: root incoming edge");
    EXPECT(buf_contains(buf, "style=dashed"), "dot: low edge dashed");
    EXPECT(buf_contains(buf, "style=solid"), "dot: high edge solid");
    EXPECT(buf_contains(buf, "label=\"0\""), "dot: level label on node");
    /* high weight 4 appears as edge label (to_str from int type) */
    EXPECT(buf_contains(buf, "4"), "dot: prints weight 4 on an edge");
    EXPECT(buf_contains(buf, "shape=box, label=\"1\""), "dot: sole 1-terminal shown");

    /* Marks cleared: second print still works and produces digraph. */
    char *buf2 = NULL;
    size_t buflen2 = 0;
    FILE *mem2 = open_memstream(&buf2, &buflen2);
    if (mem2) {
        evdd_fprintdot(mem2, n, 0);
        fclose(mem2);
    }
    EXPECT(buf_contains(buf2, "digraph G"), "dot: second print after unmark");

    /* hide_zeros: omit zero-weight edges */
    char *buf3 = NULL;
    size_t buflen3 = 0;
    FILE *mem3 = open_memstream(&buf3, &buflen3);
    if (mem3) {
        evdd_fprintdot(mem3, n, 1);
        fclose(mem3);
    }
    EXPECT(buf_contains(buf3, "digraph G"), "dot hide0: digraph header");
    EXPECT(buf_contains(buf3, "style=solid"), "dot hide0: high edge kept");
    EXPECT(buf_contains(buf3, "4"), "dot hide0: weight 4 kept");
    EXPECT(!buf_contains(buf3, "style=dashed"), "dot hide0: no zero-weight low edge");
    EXPECT(buf_contains(buf3, "shape=box, label=\"1\""), "dot hide0: 1-terminal kept");

    free(buf);
    free(buf2);
    free(buf3);
    evdd_done();
    teardown_values();
}

/* Cell width 0.1. 0.10 bins to cell 1, 0.16 bins to cell 2 (llround(1.6)). */
static const double k_cell_width = 0.1;

static void cell_real(void *p, long long *cell)
{
    cell[0] = llround(dbl_val(p) / k_cell_width);
}

static int cmp_tol(void *a, void *b)
{
    return std::fabs(dbl_val(a) - dbl_val(b)) <= k_cell_width;
}

typedef struct {
    double re;
    double im;
} cdouble;

static cdouble *box_cd(double re, double im)
{
    cdouble *p = (cdouble *)malloc(sizeof(cdouble));
    p->re = re;
    p->im = im;
    return p;
}

static void cell_reim(void *p, long long *cell)
{
    cdouble *z = (cdouble *)p;
    cell[0] = llround(z->re / k_cell_width);
    cell[1] = llround(z->im / k_cell_width);
}

static int cmp_reim(void *a, void *b)
{
    cdouble *x = (cdouble *)a;
    cdouble *y = (cdouble *)b;
    double dre = x->re - y->re;
    double dim = x->im - y->im;
    return std::hypot(dre, dim) <= k_cell_width;
}

static void test_intern_cell_walk(void)
{
    evdd_value_id a, b, far, hash_id, cell_id;
    int frees;

    printf("\n== intern cell walk ==\n");

    /* Bad axis counts on a type with no interned rows. */
    evdd_value_type empty = evdd_new_value_type();
    reset_errors();
    evdd_register_cell_function(empty, cell_real, 0);
    EXPECT(g_error_count >= 1, "cell walk: naxes 0 is rejected");
    reset_errors();
    evdd_register_cell_function(empty, cell_real, EVDD_VALUE_MAX_AXES + 1);
    EXPECT(g_error_count >= 1, "cell walk: naxes above max is rejected");

    /* Clear before any intern: lookup goes back to the payload hash. */
    evdd_value_type cleared = evdd_new_value_type();
    evdd_register_hash_function(cleared, dbl_hash);
    evdd_register_compare_function(cleared, cmp_tol);
    evdd_register_free_function(cleared, int_free_internals);
    evdd_register_cell_function(cleared, cell_real, 1);
    evdd_register_cell_function(cleared, NULL, 0);
    EXPECT(evdd_value_functions_list[cleared].cellfun == NULL &&
               evdd_value_functions_list[cleared].naxes == 0,
           "cell walk: NULL clears the callback");
    a = intern_dbl(cleared, 0.10);
    b = intern_dbl(cleared, 0.16);
    EXPECT(a != b, "cell walk: after clear, the hash index misses the neighbor");

    /* Hash + tolerance compare never looks in the next bucket. */
    evdd_value_type exact = evdd_new_value_type();
    evdd_register_hash_function(exact, dbl_hash);
    evdd_register_compare_function(exact, cmp_tol);
    evdd_register_free_function(exact, int_free_internals);
    hash_id = intern_dbl(exact, 0.10);
    b = intern_dbl(exact, 0.16);
    EXPECT(hash_id != b, "cell walk: hash index misses the neighbor bucket");

    reset_errors();
    evdd_register_cell_function(exact, cell_real, 1);
    EXPECT(g_error_count >= 1 &&
               evdd_value_functions_list[exact].cellfun == NULL,
           "cell walk: register after intern is rejected");
    EXPECT(intern_dbl(exact, 0.10) == hash_id,
           "cell walk: rejected register keeps the hash id");

    reset_errors();
    evdd_register_cell_function(exact, NULL, 0);
    EXPECT(g_error_count >= 1 &&
               evdd_value_functions_list[exact].cellfun == NULL,
           "cell walk: clear after intern is rejected");
    EXPECT(intern_dbl(exact, 0.10) == hash_id,
           "cell walk: rejected clear keeps the hash id");

    evdd_value_type real = evdd_new_value_type();
    evdd_register_compare_function(real, cmp_tol);
    evdd_register_free_function(real, int_free_internals);
    evdd_register_cell_function(real, cell_real, 1);
    frees = g_freefun_calls;
    cell_id = intern_dbl(real, 0.10);
    b = intern_dbl(real, 0.16);
    EXPECT(cell_id == b && nearly(get_dbl(cell_id), 0.10),
           "cell walk: 1 axis merges the next cell and keeps the first payload");
    EXPECT(g_freefun_calls == frees + 1, "cell walk: neighbor hit frees the new payload");
    far = intern_dbl(real, 0.40);
    EXPECT(far != cell_id, "cell walk: 1 axis leaves a value two cells away distinct");
    a = intern_dbl(real, -0.10);
    b = intern_dbl(real, -0.16);
    EXPECT(a == b, "cell walk: 1 axis merges a negative neighbor");

    reset_errors();
    evdd_register_cell_function(real, NULL, 0);
    EXPECT(g_error_count >= 1 &&
               evdd_value_functions_list[real].cellfun == cell_real,
           "cell walk: clear after a cell intern is rejected");
    EXPECT(intern_dbl(real, 0.16) == cell_id,
           "cell walk: rejected clear still merges the neighbor");

    evdd_value_type plane = evdd_new_value_type();
    evdd_register_compare_function(plane, cmp_reim);
    evdd_register_free_function(plane, int_free_internals);
    evdd_register_cell_function(plane, cell_reim, 2);
    a = evdd_value_intern(box_cd(0.10, 0.10), plane);
    b = evdd_value_intern(box_cd(0.16, 0.16), plane);
    EXPECT(a == b, "cell walk: 2 axes merge the corner bucket");
    far = evdd_value_intern(box_cd(0.40, 0.10), plane);
    EXPECT(far != a, "cell walk: 2 axes leave a far re cell distinct");

    /* The interned flag clears with the table, so registration works again. */
    evdd_value_done();
    evdd_register_cell_function(exact, cell_real, 1);
    EXPECT(evdd_value_functions_list[exact].cellfun == cell_real,
           "cell walk: value_done allows a cell callback again");
    a = intern_dbl(exact, 0.10);
    b = intern_dbl(exact, 0.16);
    EXPECT(a == b, "cell walk: after value_done the new callback merges neighbors");

    teardown_values();
}

/* Nearby high weights share a node because intern merges them first. */
static void test_cell_diagram(void)
{
    printf("\n== cell callback on a diagram ==\n");

    EXPECT(evdd_init(64) == 0, "cell diagram: evdd_init");

    evdd_value_type t = register_dbl_type();
    evdd_register_compare_function(t, cmp_tol);
    evdd_register_cell_function(t, cell_real, 1);
    evdd_set_normalization(t, EVDD_NORMALIZE_FIRST_NONZERO);

    EVDD f = evdd_false();
    EVDD hi = unit_const_dbl(t);
    hi.w = intern_dbl(t, 0.10);
    EVDD n1 = evdd_makenode(0, f, hi);

    hi.w = intern_dbl(t, 0.16);
    EVDD n2 = evdd_makenode(0, f, hi);
    EXPECT(n1.node == n2.node && n1.node != EVDD_TRUE_NODE && n1.w == n2.w,
           "cell diagram: nearby weights share one node");
    EXPECT(nearly(get_dbl(n1.w), 0.10),
           "cell diagram: incoming weight keeps the first payload");

    hi.w = intern_dbl(t, 0.40);
    EVDD n3 = evdd_makenode(0, f, hi);
    EXPECT(n3.node == n1.node && n3.w != n1.w,
           "cell diagram: a weight two cells away is a different edge");

    evdd_done();
    teardown_values();
}

/* ------------------------------------------------------------------------=
 * main
 * ---------------------------------------------------------------------= */

int main(void) {
    printf("MoToBuddy EVDD value test suite\n");
    bdd_error_hook(test_errhandler);

    test_type_registry();
    test_register_callbacks();
    test_types_done_reinit();
    test_intern_uniquing();
    test_intern_index_callback_freeze();
    test_intern_cell_walk();
    test_cell_diagram();
    test_intern_hash_index_scale();
    test_intern_hash_index_grow_boundary();
    test_intern_types_dont_alias();
    test_intern_hash_collision();
    test_intern_long_collision_chain();
    test_intern_clustered_hash();
    test_intern_no_hash_fallback();
    test_intern_two_types_index();
    test_intern_index_survives_value_done();
    test_intern_dedup_freefun();
    test_value_done_resets();
    test_is_zero_is_one();
    test_algebra();
    test_mixed_type_error();
    test_normalize_first_nonzero();
    test_normalize_low();
    test_normalize_max_magnitude();
    test_normalize_l2();
    test_normalize_custom_and_freeze();
    test_set_normalization_missing_callbacks();
    test_two_types_two_norms();
    test_independent_of_bdd_init();
    test_node_table_init();
    test_makenode_raw();
    test_evdd_normalize();
    test_evdd_normalize_under_gc();
    test_evdd_refs_gbc();
    test_evdd_normalize_shared_result();
    test_evdd_apply();
    test_evdd_gates();
    test_evdd_dot();

    printf("\n----------------------------------------\n");
    printf("passed=%d failed=%d\n", g_passed, g_failed);
    if (g_failed == 0) {
        printf("All tests passed.\n");
        return 0;
    }
    printf("SOME TESTS FAILED.\n");
    return 1;
}
