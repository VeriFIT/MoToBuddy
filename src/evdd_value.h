/**
 * @file evdd_value.h
 * @brief Interned EVDD edge values and CUSTOM-style type registry.
 *
 * Analogous to terminal.h plus mtbddValues, but values sit on edges, not
 * only leaves. Intern row is (type_id, payload). ValueID 0 = absorb,
 * ValueID 1 = identity. Several type ids share one table (CUSTOM pattern).
 * One frozen normalization strategy per type_id. Mixed-type ops error.
 *
 * Algebra callbacks (mul, div, is_zero, …) belong here, not in the DD
 * unique table.
 */

#ifndef EVDD_VALUE_H
#define EVDD_VALUE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Ids
 * ---------------------------------------------------------------------- */

/** Intern table index. 0 = multiplicative absorb, 1 = identity. */
typedef unsigned evdd_value_id;
/** CUSTOM-style type id (several may be live in one intern table). */
typedef unsigned evdd_value_type;

#define EVDD_VALUE_ZERO ((evdd_value_id)0)
#define EVDD_VALUE_ONE  ((evdd_value_id)1)

/* -------------------------------------------------------------------------
 * Callbacks (payloads are user pointers, same contract as mtbdd terminals)
 * ---------------------------------------------------------------------- */

typedef unsigned (*evdd_value_hash_function_t)(void *);
/** Nonzero if equal (same convention as mtbdd_terminal_compare_function_t). */
typedef int (*evdd_value_compare_function_t)(void *, void *);
typedef void (*evdd_value_free_function_t)(void *);
typedef char *(*evdd_value_to_str_function_t)(void *, char *, size_t);
/** Returns a newly allocated payload (intern will take ownership). */
typedef void *(*evdd_value_binary_function_t)(void *, void *);
typedef void *(*evdd_value_unary_function_t)(void *);
typedef int (*evdd_value_pred_function_t)(void *);
/** <0, 0, >0 on |a| vs |b|; used by max-magnitude. */
typedef int (*evdd_value_abs_cmp_function_t)(void *, void *);

/**
 * @brief Result of normalizing one node's two outgoing weights.
 *
 * @c incoming is multiplied into the parent incoming edge.
 */
typedef struct s_evddNormResult {
    evdd_value_id w_low;
    evdd_value_id w_high;
    evdd_value_id incoming;
} evddNormResult;

typedef evddNormResult (*evdd_value_normalize_function_t)(evdd_value_id w_low,
                                                           evdd_value_id w_high);

typedef enum e_evddNormalizeStrategy {
    EVDD_NORMALIZE_FIRST_NONZERO = 0,
    EVDD_NORMALIZE_MAX_MAGNITUDE,
    EVDD_NORMALIZE_L2,
    EVDD_NORMALIZE_CUSTOM
} evddNormalizeStrategy;

typedef struct s_evdd_value_functions {
    evdd_value_hash_function_t hashfun;
    evdd_value_compare_function_t comparefun;
    evdd_value_free_function_t freefun;
    evdd_value_to_str_function_t toStrFun;
    evdd_value_binary_function_t mulfun;
    evdd_value_binary_function_t divfun;
    evdd_value_binary_function_t addfun;
    evdd_value_binary_function_t subfun;
    evdd_value_pred_function_t iszerofun;
    evdd_value_pred_function_t isonefun;
    evdd_value_abs_cmp_function_t abscmpfun;
    evdd_value_unary_function_t absqfun;
    evdd_value_unary_function_t sqrtfun;
    evdd_value_normalize_function_t normalizefun;
    evddNormalizeStrategy strategy;
    int strategy_frozen;
    int normalization_paused;
} evdd_value_functions;

#define EVDDVALUEHASH(type)      evdd_value_functions_list[type].hashfun
#define EVDDVALUECOMPARE(type)   evdd_value_functions_list[type].comparefun
#define EVDDVALUEFREE(type)      evdd_value_functions_list[type].freefun
#define EVDDVALUETOSTR(type)     evdd_value_functions_list[type].toStrFun
#define EVDDVALUEMUL(type)       evdd_value_functions_list[type].mulfun
#define EVDDVALUEDIV(type)       evdd_value_functions_list[type].divfun
#define EVDDVALUEADD(type)       evdd_value_functions_list[type].addfun
#define EVDDVALUESUB(type)       evdd_value_functions_list[type].subfun
#define EVDDVALUEISZERO(type)    evdd_value_functions_list[type].iszerofun
#define EVDDVALUEISONE(type)     evdd_value_functions_list[type].isonefun
#define EVDDVALUEABSCMP(type)    evdd_value_functions_list[type].abscmpfun
#define EVDDVALUEABSSQ(type)     evdd_value_functions_list[type].absqfun
#define EVDDVALUESQRT(type)      evdd_value_functions_list[type].sqrtfun
#define EVDDVALUENORMALIZE(type) evdd_value_functions_list[type].normalizefun

/* -------------------------------------------------------------------------
 * Type registry (cf. terminal.c)
 * ---------------------------------------------------------------------- */

extern evdd_value_functions *evdd_value_functions_list;
extern int evdd_value_type_number;
extern int evdd_value_type_size;

unsigned evdd_new_value_type(void);
void evdd_register_hash_function(evdd_value_type type, evdd_value_hash_function_t hashfun);
void evdd_register_compare_function(evdd_value_type type, evdd_value_compare_function_t comparefun);
void evdd_register_free_function(evdd_value_type type, evdd_value_free_function_t freefun);
void evdd_register_to_str_function(evdd_value_type type, evdd_value_to_str_function_t toStrfun);
void evdd_register_mul_function(evdd_value_type type, evdd_value_binary_function_t mulfun);
void evdd_register_div_function(evdd_value_type type, evdd_value_binary_function_t divfun);
void evdd_register_add_function(evdd_value_type type, evdd_value_binary_function_t addfun);
void evdd_register_sub_function(evdd_value_type type, evdd_value_binary_function_t subfun);
void evdd_register_is_zero_function(evdd_value_type type, evdd_value_pred_function_t iszerofun);
void evdd_register_is_one_function(evdd_value_type type, evdd_value_pred_function_t isonefun);
void evdd_register_abs_cmp_function(evdd_value_type type, evdd_value_abs_cmp_function_t abscmpfun);
void evdd_register_abs_sq_function(evdd_value_type type, evdd_value_unary_function_t absqfun);
void evdd_register_sqrt_function(evdd_value_type type, evdd_value_unary_function_t sqrtfun);
void evdd_register_normalize_function(evdd_value_type type, evdd_value_normalize_function_t normalizefun);
/** Built-in strategy; errors if already frozen or callbacks missing. */
void evdd_set_normalization(evdd_value_type type, evddNormalizeStrategy strategy);
void evdd_value_types_done(void);

/* -------------------------------------------------------------------------
 * Intern table
 * ---------------------------------------------------------------------- */

extern int evddValueUsed;
extern int evddmaxValueSize;
extern int evddLastValueIndex;

evdd_value_id evdd_value_intern(void *value, evdd_value_type type);
void *evdd_value_get(evdd_value_id id);
evdd_value_type evdd_value_get_type(evdd_value_id id);
void evdd_value_done(void);

/* -------------------------------------------------------------------------
 * Algebra on interned ids (hard error on type mismatch; 0/1 are untyped)
 * ---------------------------------------------------------------------- */

evdd_value_id evdd_value_mul(evdd_value_id a, evdd_value_id b);
evdd_value_id evdd_value_div(evdd_value_id a, evdd_value_id b);
evdd_value_id evdd_value_add(evdd_value_id a, evdd_value_id b);
evdd_value_id evdd_value_sub(evdd_value_id a, evdd_value_id b);
int evdd_value_is_zero(evdd_value_id id);
int evdd_value_is_one(evdd_value_id id);
evddNormResult evdd_value_normalize(evdd_value_id w_low, evdd_value_id w_high);

#ifdef __cplusplus
}
#endif

#endif /* EVDD_VALUE_H */
