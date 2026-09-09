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
        r.incoming = EVDD_VALUE_ONE;
        return r;
    }
    r.incoming = w_high;
    r.w_low = evdd_value_div(w_low, w_high);
    r.w_high = EVDD_VALUE_ONE;
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

static int get_int(evdd_value_id id) {
    if (id == EVDD_VALUE_ZERO)
        return 0;
    if (id == EVDD_VALUE_ONE)
        return 1;
    return int_val(evdd_value_get(id));
}

static double get_dbl(evdd_value_id id) {
    if (id == EVDD_VALUE_ZERO)
        return 0.0;
    if (id == EVDD_VALUE_ONE)
        return 1.0;
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

    EXPECT(a1 != EVDD_VALUE_ZERO && a1 != EVDD_VALUE_ONE && get_int(a1) == 42,
           "interned payload is not reserved 0/1");
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
    EXPECT(get_int(five) == 5 && intern_int(t, 1) == EVDD_VALUE_ONE,
           "is_one payload collapses to ONE");

    EXPECT(evdd_value_get(EVDD_VALUE_ZERO) == NULL, "get(ZERO) is NULL");
    EXPECT(evdd_value_get(EVDD_VALUE_ONE) == NULL, "get(ONE) is NULL");

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

static unsigned const_hash(void *a) {
    (void)a;
    return 1u;
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
    EXPECT(evdd_value_is_one(EVDD_VALUE_ONE), "ONE is one");
    EXPECT(!evdd_value_is_zero(EVDD_VALUE_ONE), "ONE is not zero");

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

    EXPECT(get_int(evdd_value_mul(two, three)) == 6, "mul 2*3 interned as 6");
    EXPECT(get_int(evdd_value_mul(two, EVDD_VALUE_ONE)) == 2, "mul by ONE is identity");
    EXPECT(get_int(evdd_value_mul(EVDD_VALUE_ONE, three)) == 3, "ONE*x is x");
    EXPECT(get_int(two) == 2 &&
               evdd_value_mul(two, EVDD_VALUE_ZERO) == EVDD_VALUE_ZERO,
           "mul by ZERO absorbs");
    EXPECT(get_int(three) == 3 &&
               evdd_value_mul(EVDD_VALUE_ZERO, three) == EVDD_VALUE_ZERO,
           "ZERO*x absorbs");
    EXPECT(evdd_value_mul(EVDD_VALUE_ZERO, EVDD_VALUE_ONE) == EVDD_VALUE_ZERO,
           "ZERO*ONE is ZERO");

    EXPECT(get_int(evdd_value_add(two, three)) == 5, "add 2+3");
    EXPECT(get_int(evdd_value_add(two, EVDD_VALUE_ZERO)) == 2, "add ZERO is identity");
    EXPECT(get_int(evdd_value_sub(six, two)) == 4, "sub 6-2");
    EXPECT(get_int(two) == 2 && evdd_value_sub(two, two) == EVDD_VALUE_ZERO,
           "sub x-x collapses to ZERO");
    EXPECT(get_int(evdd_value_div(six, two)) == 3, "div 6/2");
    EXPECT(get_int(evdd_value_div(six, EVDD_VALUE_ONE)) == 6, "div by ONE is identity");
    EXPECT(get_int(two) == 2 &&
               evdd_value_div(EVDD_VALUE_ZERO, two) == EVDD_VALUE_ZERO,
           "ZERO / x is ZERO");

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
    EXPECT(r.w_low == EVDD_VALUE_ONE, "nonzero low becomes ONE");
    EXPECT(nearly(get_dbl(r.w_high), 2.0), "high divided by low");
    EXPECT(nearly(get_dbl(r.incoming), 3.0), "incoming is the low factor");

    r = evdd_value_normalize(EVDD_VALUE_ZERO, intern_dbl(t, 4.0));
    EXPECT(r.w_low == EVDD_VALUE_ZERO && r.w_high == EVDD_VALUE_ONE,
           "zero low: high becomes ONE");
    EXPECT(nearly(get_dbl(r.incoming), 4.0), "incoming is the high factor");

    r = evdd_value_normalize(intern_dbl(t, -2.0), intern_dbl(t, 4.0));
    EXPECT(r.w_low == EVDD_VALUE_ONE, "negative low still factors");
    EXPECT(nearly(get_dbl(r.w_high), -2.0), "high / low keeps sign");
    EXPECT(nearly(get_dbl(r.incoming), -2.0), "incoming keeps the low value");

    r = evdd_value_normalize(EVDD_VALUE_ZERO, EVDD_VALUE_ZERO);
    EXPECT(r.w_low == EVDD_VALUE_ZERO && r.w_high == EVDD_VALUE_ZERO &&
               r.incoming == EVDD_VALUE_ZERO,
           "both zero: all ZERO");

    teardown_values();
}

static void test_normalize_max_magnitude(void) {
    printf("\n== normalize max-magnitude ==\n");
    evdd_value_type t = register_dbl_type();
    evdd_set_normalization(t, EVDD_NORMALIZE_MAX_MAGNITUDE);

    evddNormResult r = evdd_value_normalize(intern_dbl(t, 3.0), intern_dbl(t, 4.0));
    EXPECT(r.w_high == EVDD_VALUE_ONE, "larger |w| edge becomes ONE");
    EXPECT(nearly(get_dbl(r.w_low), 0.75), "other edge divided by max");
    EXPECT(nearly(get_dbl(r.incoming), 4.0), "incoming is the max-magnitude value");

    r = evdd_value_normalize(intern_dbl(t, -5.0), intern_dbl(t, 3.0));
    EXPECT(r.w_low == EVDD_VALUE_ONE, "divide by the actual max-mag edge (keep sign)");
    EXPECT(nearly(get_dbl(r.w_high), -0.6), "other / -5");
    EXPECT(nearly(get_dbl(r.incoming), -5.0), "incoming is -5");

    r = evdd_value_normalize(intern_dbl(t, 3.0), intern_dbl(t, -3.0));
    EXPECT(r.w_low == EVDD_VALUE_ONE, "tie: prefer low");
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
    EXPECT(nearly(get_dbl(r.w_low), 0.6) && r.w_low != EVDD_VALUE_ONE &&
               r.w_high != EVDD_VALUE_ONE,
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
    EXPECT(r.w_high == EVDD_VALUE_ONE, "custom: high becomes ONE");
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
    EXPECT(a.w_low == EVDD_VALUE_ONE && nearly(get_dbl(a.w_high), 2.0),
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
    test_intern_types_dont_alias();
    test_intern_hash_collision();
    test_intern_dedup_freefun();
    test_value_done_resets();
    test_is_zero_is_one();
    test_algebra();
    test_mixed_type_error();
    test_normalize_first_nonzero();
    test_normalize_max_magnitude();
    test_normalize_l2();
    test_normalize_custom_and_freeze();
    test_set_normalization_missing_callbacks();
    test_two_types_two_norms();
    test_independent_of_bdd_init();

    printf("\n----------------------------------------\n");
    printf("passed=%d failed=%d\n", g_passed, g_failed);
    if (g_failed == 0) {
        printf("All tests passed.\n");
        return 0;
    }
    printf("SOME TESTS FAILED.\n");
    return 1;
}
