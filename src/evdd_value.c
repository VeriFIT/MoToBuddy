/**
 * @file evdd_value.c
 * @brief Intern table for EVDD edge values (hash by payload, not via DD nodes).
 *
 * Counterpart of terminal.c and mtbdd_insertvalue / mtbdd_findterminal.
 * Uniquing must not create EvddNodes. GC recycles intern slots when no
 * live edge still holds the ValueID.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bdd.h"
#include "evdd_value.h"

extern int bdd_error(int);

evdd_value_functions *evdd_value_functions_list = NULL;
int evdd_value_type_number = 0;
int evdd_value_type_size = 0;

int evddValueUsed = 0;
int evddmaxValueSize = 0;
int evddLastValueIndex = 0;

#define EVDD_VALUE_TYPE_ALLOC_CHUNK 3
#define EVDD_VALUE_INITIAL_SIZE 16
#define EVDD_VALUE_UNTYPED ((evdd_value_type) - 1)

typedef struct s_EvddValueRow {
    void *payload;
    evdd_value_type type;
    unsigned hash;
} EvddValueRow;

static EvddValueRow *evddValues = NULL;

static evddNormResult evdd_norm_first_nonzero(evdd_value_id w_low,
                                              evdd_value_id w_high);
static evddNormResult evdd_norm_low(evdd_value_id w_low, evdd_value_id w_high);
static evddNormResult evdd_norm_max_magnitude(evdd_value_id w_low,
                                              evdd_value_id w_high);
static evddNormResult evdd_norm_l2(evdd_value_id w_low, evdd_value_id w_high);

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

static int evdd_value_check_type(evdd_value_type type, const char *fn)
{
    if (type >= (unsigned)evdd_value_type_number) {
        printf("Error: invalid value type %u passed to %s.\n", type, fn);
        bdd_error(BDD_OP);
        return 0;
    }
    return 1;
}

static void evdd_value_release(void *value, evdd_value_type type)
{
    if (value == NULL)
        return;
    if (type < (unsigned)evdd_value_type_number) {
        evdd_value_free_function_t freefun = EVDDVALUEFREE(type);
        if (freefun)
            freefun(value);
    }
    free(value);
}

static int evdd_value_is_untyped(evdd_value_id id)
{
    return id == EVDD_VALUE_ZERO;
}

static evdd_value_type evdd_value_type_of(evdd_value_id id)
{
    if (evdd_value_is_untyped(id))
        return EVDD_VALUE_UNTYPED;
    if (evddValues == NULL || (int)id >= evddLastValueIndex ||
        evddValues[id].payload == NULL)
        return EVDD_VALUE_UNTYPED;
    return evddValues[id].type;
}

static int evdd_value_resolve_type(evdd_value_id a, evdd_value_id b,
                                   evdd_value_type *out)
{
    evdd_value_type ta = evdd_value_type_of(a);
    evdd_value_type tb = evdd_value_type_of(b);
    int a_typed = (ta != EVDD_VALUE_UNTYPED);
    int b_typed = (tb != EVDD_VALUE_UNTYPED);

    if (a_typed && b_typed && ta != tb) {
        printf("Error: mixed EVDD value types %u and %u\n", ta, tb);
        bdd_error(BDD_OP);
        return 0;
    }
    if (a_typed)
        *out = ta;
    else if (b_typed)
        *out = tb;
    else
        *out = EVDD_VALUE_UNTYPED;
    return 1;
}

static int evdd_value_ensure_table(void)
{
    if (evddmaxValueSize == 0) {
        evddValues = (EvddValueRow *)calloc(EVDD_VALUE_INITIAL_SIZE,
                                            sizeof(EvddValueRow));
        if (evddValues == NULL) {
            printf("Memory allocation fail in evdd_value_intern\n");
            bdd_error(BDD_MEMORY);
            return 0;
        }
        evddmaxValueSize = EVDD_VALUE_INITIAL_SIZE;
        /* Slot 0 reserved for bare ZERO; first payload at index 1. */
        evddLastValueIndex = 1;
        return 1;
    }
    if (evddLastValueIndex >= evddmaxValueSize) {
        int old_size = evddmaxValueSize;
        int new_size = evddmaxValueSize * 2;
        EvddValueRow *grown =
            (EvddValueRow *)realloc(evddValues, (size_t)new_size * sizeof(EvddValueRow));
        if (grown == NULL) {
            printf("Memory allocation fail in evdd_value_intern\n");
            bdd_error(BDD_MEMORY);
            return 0;
        }
        memset(grown + old_size, 0,
               (size_t)(new_size - old_size) * sizeof(EvddValueRow));
        evddValues = grown;
        evddmaxValueSize = new_size;
    }
    return 1;
}

/* -------------------------------------------------------------------------
 * Type registry (cf. terminal.c)
 * ---------------------------------------------------------------------- */

unsigned evdd_new_value_type(void)
{
    if (evdd_value_type_number >= evdd_value_type_size) {
        int new_size = evdd_value_type_size + EVDD_VALUE_TYPE_ALLOC_CHUNK;
        evdd_value_functions *new_list = realloc(
            evdd_value_functions_list, (size_t)new_size * sizeof(evdd_value_functions));
        if (!new_list) {
            printf("Memory allocation fail in evdd_new_value_type\n");
            bdd_error(BDD_MEMORY);
            return (unsigned)-1;
        }
        memset(new_list + evdd_value_type_size, 0,
               (size_t)(new_size - evdd_value_type_size) * sizeof(evdd_value_functions));
        evdd_value_functions_list = new_list;
        evdd_value_type_size = new_size;
    }
    return (unsigned)evdd_value_type_number++;
}

void evdd_register_hash_function(evdd_value_type type,
                                 evdd_value_hash_function_t hashfun)
{
    if (!evdd_value_check_type(type, "evdd_register_hash_function"))
        return;
    evdd_value_functions_list[type].hashfun = hashfun;
}

void evdd_register_compare_function(evdd_value_type type,
                                    evdd_value_compare_function_t comparefun)
{
    if (!evdd_value_check_type(type, "evdd_register_compare_function"))
        return;
    evdd_value_functions_list[type].comparefun = comparefun;
}

void evdd_register_free_function(evdd_value_type type,
                                 evdd_value_free_function_t freefun)
{
    if (!evdd_value_check_type(type, "evdd_register_free_function"))
        return;
    evdd_value_functions_list[type].freefun = freefun;
}

void evdd_register_to_str_function(evdd_value_type type,
                                   evdd_value_to_str_function_t toStrfun)
{
    if (!evdd_value_check_type(type, "evdd_register_to_str_function"))
        return;
    evdd_value_functions_list[type].toStrFun = toStrfun;
}

void evdd_register_mul_function(evdd_value_type type,
                                evdd_value_binary_function_t mulfun)
{
    if (!evdd_value_check_type(type, "evdd_register_mul_function"))
        return;
    evdd_value_functions_list[type].mulfun = mulfun;
}

void evdd_register_div_function(evdd_value_type type,
                                evdd_value_binary_function_t divfun)
{
    if (!evdd_value_check_type(type, "evdd_register_div_function"))
        return;
    evdd_value_functions_list[type].divfun = divfun;
}

void evdd_register_add_function(evdd_value_type type,
                                evdd_value_binary_function_t addfun)
{
    if (!evdd_value_check_type(type, "evdd_register_add_function"))
        return;
    evdd_value_functions_list[type].addfun = addfun;
}

void evdd_register_sub_function(evdd_value_type type,
                                evdd_value_binary_function_t subfun)
{
    if (!evdd_value_check_type(type, "evdd_register_sub_function"))
        return;
    evdd_value_functions_list[type].subfun = subfun;
}

void evdd_register_is_zero_function(evdd_value_type type,
                                    evdd_value_pred_function_t iszerofun)
{
    if (!evdd_value_check_type(type, "evdd_register_is_zero_function"))
        return;
    evdd_value_functions_list[type].iszerofun = iszerofun;
}

void evdd_register_is_one_function(evdd_value_type type,
                                   evdd_value_pred_function_t isonefun)
{
    if (!evdd_value_check_type(type, "evdd_register_is_one_function"))
        return;
    evdd_value_functions_list[type].isonefun = isonefun;
}

void evdd_register_abs_cmp_function(evdd_value_type type,
                                    evdd_value_abs_cmp_function_t abscmpfun)
{
    if (!evdd_value_check_type(type, "evdd_register_abs_cmp_function"))
        return;
    evdd_value_functions_list[type].abscmpfun = abscmpfun;
}

void evdd_register_abs_sq_function(evdd_value_type type,
                                   evdd_value_unary_function_t absqfun)
{
    if (!evdd_value_check_type(type, "evdd_register_abs_sq_function"))
        return;
    evdd_value_functions_list[type].absqfun = absqfun;
}

void evdd_register_sqrt_function(evdd_value_type type,
                                 evdd_value_unary_function_t sqrtfun)
{
    if (!evdd_value_check_type(type, "evdd_register_sqrt_function"))
        return;
    evdd_value_functions_list[type].sqrtfun = sqrtfun;
}

void evdd_register_normalize_function(evdd_value_type type,
                                      evdd_value_normalize_function_t normalizefun)
{
    if (!evdd_value_check_type(type, "evdd_register_normalize_function"))
        return;
    evdd_value_functions_list[type].normalizefun = normalizefun;
}

void evdd_set_normalization(evdd_value_type type, evddNormalizeStrategy strategy)
{
    evdd_value_functions *fns;
    int ok = 0;

    if (!evdd_value_check_type(type, "evdd_set_normalization"))
        return;

    fns = &evdd_value_functions_list[type];
    if (fns->strategy_frozen) {
        printf("Error: normalization already frozen for type %u\n", type);
        bdd_error(BDD_OP);
        return;
    }

    switch (strategy) {
    case EVDD_NORMALIZE_FIRST_NONZERO:
        ok = fns->iszerofun && fns->mulfun && fns->divfun;
        if (ok)
            fns->normalizefun = evdd_norm_first_nonzero;
        break;
    case EVDD_NORMALIZE_LOW:
        ok = fns->iszerofun && fns->mulfun && fns->divfun;
        if (ok)
            fns->normalizefun = evdd_norm_low;
        break;
    case EVDD_NORMALIZE_MAX_MAGNITUDE:
        ok = fns->iszerofun && fns->abscmpfun && fns->mulfun && fns->divfun;
        if (ok)
            fns->normalizefun = evdd_norm_max_magnitude;
        break;
    case EVDD_NORMALIZE_L2:
        ok = fns->addfun && fns->absqfun && fns->sqrtfun && fns->divfun;
        if (ok)
            fns->normalizefun = evdd_norm_l2;
        break;
    case EVDD_NORMALIZE_CUSTOM:
        ok = fns->normalizefun != NULL;
        break;
    default:
        ok = 0;
        break;
    }

    if (!ok) {
        printf("Error: missing callbacks for normalization strategy on type %u\n",
               type);
        bdd_error(BDD_OP);
        return;
    }

    fns->strategy = strategy;
    fns->strategy_frozen = 1;
}

void evdd_value_types_done(void)
{
    free(evdd_value_functions_list);
    evdd_value_functions_list = NULL;
    evdd_value_type_number = 0;
    evdd_value_type_size = 0;
}

/* -------------------------------------------------------------------------
 * Intern table
 * ---------------------------------------------------------------------- */

/*
 * Open-addressing index over evddValues, keyed by (payload hash, type).
 * Rows are append-only and wiped wholesale in evdd_value_done, so the index
 * never deletes. Slot 0 means empty; row indices start at 1 (bare ZERO).
 */
static int *evdd_value_idx = NULL;       /* open-address table; 0 = empty slot */
static unsigned evdd_value_idx_cap = 0;  /* number of slots (power of two) */
static unsigned evdd_value_idx_cnt = 0;  /* occupied slots, grows when 2*(cnt+1) > cap */

/**
    @brief Hash function for the open-address index.
    @param h The hash of the payload.
    @param type The type of the payload.
    @return The index in the open-address table.
*/
static unsigned evdd_value_idx_key(unsigned h, evdd_value_type type)
{
    unsigned k = h ^ (type * 0x9E3779B9u);
    k ^= k >> 16;
    k *= 0x85EBCA6Bu;
    k ^= k >> 13;
    return k;
}

/**
    @brief Clear the open-address index.
    Frees the index and resets the capacity and count.
*/
static void evdd_value_idx_clear(void)
{
    free(evdd_value_idx);
    evdd_value_idx = NULL;
    evdd_value_idx_cap = 0;
    evdd_value_idx_cnt = 0;
}

/**
    @brief Insert ValueID @p id into the open-address index (id indexes evddValues).
    @param id The ValueID to insert.
    @return 1 if the insertion was successful, 0 if the insertion failed.
*/
static int evdd_value_idx_put(int id)
{
    unsigned index;

    // grow if needed
    if (2u * (evdd_value_idx_cnt + 1u) > evdd_value_idx_cap) {
        unsigned new_cap = evdd_value_idx_cap ? evdd_value_idx_cap * 2u : 1024u;
        int *grown = (int *)calloc(new_cap, sizeof(int));
        unsigned j;

        if (grown == NULL) {
            printf("Memory allocation fail in evdd_value_intern index\n");
            bdd_error(BDD_MEMORY);
            return 0;
        }

        // copy the old index to the new one
        for (j = 0; j < evdd_value_idx_cap; j++) {
            if (evdd_value_idx[j] == 0)
                continue;
            index = evdd_value_idx_key(evddValues[evdd_value_idx[j]].hash,
                                   evddValues[evdd_value_idx[j]].type) &
                (new_cap - 1u);

            // linear probe until empty slot is found
            while (grown[index] != 0)
                index = (index + 1u) & (new_cap - 1u);
            grown[index] = evdd_value_idx[j];
        }
        free(evdd_value_idx);
        evdd_value_idx = grown;
        evdd_value_idx_cap = new_cap;
    }


    // cap is power of two, so we can use bitwise AND to get the index
    index = evdd_value_idx_key(evddValues[id].hash, evddValues[id].type) &
        (evdd_value_idx_cap - 1u);
    
    // linear probe until empty slot is found
    while (evdd_value_idx[index] != 0)
        index = (index + 1u) & (evdd_value_idx_cap - 1u);
    
    evdd_value_idx[index] = id;
    evdd_value_idx_cnt++;
    return 1;
}

/**
    @brief Intern a value into the evddValues table and the open-address index.
    @param value The value to intern.
    @param type The type of the value.
    @return The ValueID of the interned value.
*/
evdd_value_id evdd_value_intern(void *value, evdd_value_type type)
{
    unsigned h = 0;
    int have_hash = 0;
    int i;
    evdd_value_compare_function_t cmp;

    if (value == NULL)
        return EVDD_VALUE_ZERO;

    if (!evdd_value_check_type(type, "evdd_value_intern")) {
        evdd_value_release(value, type);
        return EVDD_VALUE_ZERO;
    }

    if (EVDDVALUEISZERO(type) && EVDDVALUEISZERO(type)(value)) {
        evdd_value_release(value, type);
        return EVDD_VALUE_ZERO;
    }

    if (EVDDVALUEHASH(type)) {
        h = EVDDVALUEHASH(type)(value);
        have_hash = 1;
    }

    cmp = EVDDVALUECOMPARE(type);

    if (evddValues != NULL && have_hash && evdd_value_idx_cap != 0) {
        unsigned k =
            evdd_value_idx_key(h, type) & (evdd_value_idx_cap - 1u);
        
        // linear probe until empty slot is found
        for (; evdd_value_idx[k] != 0;
             k = (k + 1u) & (evdd_value_idx_cap - 1u)) {
            int j = evdd_value_idx[k];

            if (evddValues[j].type != type || evddValues[j].hash != h)
                continue;
            
            // if cmp is set, use it, else bitwise compare
            if (cmp ? cmp(value, evddValues[j].payload)
                    : value == evddValues[j].payload) {
                // already found stored value, release the new one
                evdd_value_release(value, type);
                return (evdd_value_id)j;
            }
        }
    } else if (evddValues != NULL && !have_hash) { // fallback to linear search
        for (i = 1; i < evddLastValueIndex; i++) {
            if (evddValues[i].payload == NULL || evddValues[i].type != type)
                continue;
            if (cmp ? cmp(value, evddValues[i].payload)
                    : value == evddValues[i].payload) {
                evdd_value_release(value, type);
                return (evdd_value_id)i;
            }
        }
    }

    if (!evdd_value_ensure_table()) {
        evdd_value_release(value, type);
        return EVDD_VALUE_ZERO;
    }

    // store new value
    i = evddLastValueIndex++;
    evddValues[i].payload = value;
    evddValues[i].type = type;
    evddValues[i].hash = h;
    evddValueUsed++;

    if (have_hash && !evdd_value_idx_put(i)) {
        evddValues[i].payload = NULL;
        evddLastValueIndex--;
        evddValueUsed--;
        evdd_value_release(value, type);
        return EVDD_VALUE_ZERO;
    }
    return (evdd_value_id)i;
}

/**
    @brief Get the value from a ValueID.
    @param id The ValueID to get the payload of.
    @return The payload of the ValueID.
*/
void *evdd_value_get(evdd_value_id id)
{
    if (evdd_value_is_untyped(id) || evddValues == NULL ||
        (int)id >= evddLastValueIndex)
        return NULL;
    return evddValues[id].payload;
}

/**
    @brief Get the type of a ValueID.
    @param id The ValueID to get the type of.
    @return The type of the ValueID.
*/
evdd_value_type evdd_value_get_type(evdd_value_id id)
{
    evdd_value_type t = evdd_value_type_of(id);
    return t == EVDD_VALUE_UNTYPED ? 0 : t;
}

/**
    @brief Free all values and clear the evddValues table and the open-address index.
*/
void evdd_value_done(void)
{
    int i;
    if (evddValues != NULL) {
        for (i = 1; i < evddLastValueIndex; i++) {
            if (evddValues[i].payload != NULL)
                evdd_value_release(evddValues[i].payload, evddValues[i].type);
        }
        free(evddValues);
        evddValues = NULL;
    }
    evdd_value_idx_clear();
    evddValueUsed = 0;
    evddmaxValueSize = 0;
    evddLastValueIndex = 0;
}

/* -------------------------------------------------------------------------
 * Algebra on interned ids
 * ---------------------------------------------------------------------- */

evdd_value_id evdd_value_div(evdd_value_id a, evdd_value_id b);

int evdd_value_is_zero(evdd_value_id id)
{
    evdd_value_pred_function_t pred;
    evdd_value_type t;

    if (id == EVDD_VALUE_ZERO)
        return 1;
    t = evdd_value_type_of(id);
    if (t == EVDD_VALUE_UNTYPED)
        return 0;
    pred = EVDDVALUEISZERO(t);
    return pred ? pred(evdd_value_get(id)) : 0;
}

int evdd_value_is_one(evdd_value_id id)
{
    evdd_value_pred_function_t pred;
    evdd_value_type t;

    if (id == EVDD_VALUE_ZERO)
        return 0;
    t = evdd_value_type_of(id);
    if (t == EVDD_VALUE_UNTYPED)
        return 0;
    pred = EVDDVALUEISONE(t);
    return pred ? pred(evdd_value_get(id)) : 0;
}

/**
 * Call the type's binary op. Bare ZERO: get() is NULL; callback treats as 0.
 */
static evdd_value_id evdd_value_apply_binary(evdd_value_id a, evdd_value_id b,
                                             evdd_value_binary_function_t fun,
                                             evdd_value_type type)
{
    void *res;

    if (fun == NULL) {
        bdd_error(BDD_OP);
        return EVDD_VALUE_ZERO;
    }

    res = fun(evdd_value_get(a), evdd_value_get(b));
    return evdd_value_intern(res, type);
}

evdd_value_id evdd_value_unit(evdd_value_id w)
{
    if (evdd_value_is_zero(w))
        return EVDD_VALUE_ZERO;
    return evdd_value_div(w, w);
}

evdd_value_id evdd_value_unit_from_pair(evdd_value_id a, evdd_value_id b)
{
    if (!evdd_value_is_zero(a))
        return evdd_value_unit(a);
    if (!evdd_value_is_zero(b))
        return evdd_value_unit(b);
    return EVDD_VALUE_ZERO;
}

evdd_value_id evdd_value_mul(evdd_value_id a, evdd_value_id b)
{
    evdd_value_type t;

    if (evdd_value_is_zero(a) || evdd_value_is_zero(b))
        return EVDD_VALUE_ZERO;
    if (evdd_value_is_one(a))
        return b;
    if (evdd_value_is_one(b))
        return a;
    if (!evdd_value_resolve_type(a, b, &t) || t == EVDD_VALUE_UNTYPED)
        return EVDD_VALUE_ZERO;
    return evdd_value_apply_binary(a, b, EVDDVALUEMUL(t), t);
}

evdd_value_id evdd_value_div(evdd_value_id a, evdd_value_id b)
{
    evdd_value_type t;

    if (evdd_value_is_zero(b)) {
        printf("Error: EVDD value division by zero\n");
        bdd_error(BDD_OP);
        return EVDD_VALUE_ZERO;
    }
    if (evdd_value_is_zero(a))
        return EVDD_VALUE_ZERO;
    if (evdd_value_is_one(b))
        return a;
    if (!evdd_value_resolve_type(a, b, &t) || t == EVDD_VALUE_UNTYPED)
        return EVDD_VALUE_ZERO;
    return evdd_value_apply_binary(a, b, EVDDVALUEDIV(t), t);
}

evdd_value_id evdd_value_add(evdd_value_id a, evdd_value_id b)
{
    evdd_value_type t;

    if (evdd_value_is_zero(a))
        return b;
    if (evdd_value_is_zero(b))
        return a;
    if (!evdd_value_resolve_type(a, b, &t) || t == EVDD_VALUE_UNTYPED)
        return EVDD_VALUE_ZERO;
    return evdd_value_apply_binary(a, b, EVDDVALUEADD(t), t);
}

evdd_value_id evdd_value_sub(evdd_value_id a, evdd_value_id b)
{
    evdd_value_type t;

    if (evdd_value_is_zero(b))
        return a;
    if (evdd_value_is_one(a) && evdd_value_is_one(b))
        return EVDD_VALUE_ZERO;
    if (!evdd_value_resolve_type(a, b, &t) || t == EVDD_VALUE_UNTYPED)
        return EVDD_VALUE_ZERO;
    return evdd_value_apply_binary(a, b, EVDDVALUESUB(t), t);
}

/* -------------------------------------------------------------------------
 * Built-in normalization
 * ---------------------------------------------------------------------- */

static evddNormResult evdd_norm_zeros(void)
{
    evddNormResult r;
    r.w_low = EVDD_VALUE_ZERO;
    r.w_high = EVDD_VALUE_ZERO;
    r.incoming = EVDD_VALUE_ZERO;
    return r;
}

static evddNormResult evdd_norm_first_nonzero(evdd_value_id w_low,
                                              evdd_value_id w_high)
{
    evddNormResult r;
    if (evdd_value_is_zero(w_low) && evdd_value_is_zero(w_high))
        return evdd_norm_zeros();
    if (!evdd_value_is_zero(w_low)) {
        r.incoming = w_low;
        r.w_low = evdd_value_unit(w_low);
        r.w_high = evdd_value_div(w_high, w_low);
        return r;
    }
    r.incoming = w_high;
    r.w_low = EVDD_VALUE_ZERO;
    r.w_high = evdd_value_unit(w_high);
    return r;
}

/** Always normalize by the low edge; never pull a factor from high alone. */
static evddNormResult evdd_norm_low(evdd_value_id w_low, evdd_value_id w_high)
{
    evddNormResult r;
    if (evdd_value_is_zero(w_low) && evdd_value_is_zero(w_high))
        return evdd_norm_zeros();
    if (evdd_value_is_zero(w_low)) {
        r.incoming = evdd_value_unit(w_high);
        r.w_low = EVDD_VALUE_ZERO;
        r.w_high = w_high;
        return r;
    }
    r.incoming = w_low;
    r.w_low = evdd_value_unit(w_low);
    r.w_high = evdd_value_div(w_high, w_low);
    return r;
}

static evddNormResult evdd_norm_max_magnitude(evdd_value_id w_low,
                                              evdd_value_id w_high)
{
    evddNormResult r;
    evdd_value_id factor;
    evdd_value_type t;
    int cmp;

    if (evdd_value_is_zero(w_low) && evdd_value_is_zero(w_high))
        return evdd_norm_zeros();
    if (evdd_value_is_zero(w_low))
        factor = w_high;
    else if (evdd_value_is_zero(w_high))
        factor = w_low;
    else if (!evdd_value_resolve_type(w_low, w_high, &t) ||
             t == EVDD_VALUE_UNTYPED || EVDDVALUEABSCMP(t) == NULL)
        factor = w_low;
    else {
        cmp = EVDDVALUEABSCMP(t)(evdd_value_get(w_low), evdd_value_get(w_high));
        factor = (cmp >= 0) ? w_low : w_high;
    }

    r.incoming = factor;
    r.w_low = evdd_value_div(w_low, factor);
    r.w_high = evdd_value_div(w_high, factor);
    return r;
}

static evddNormResult evdd_norm_l2(evdd_value_id w_low, evdd_value_id w_high)
{
    evddNormResult r;
    evdd_value_type t;
    evdd_value_id sq0, sq1, sum, n;

    if (evdd_value_is_zero(w_low) && evdd_value_is_zero(w_high))
        return evdd_norm_zeros();
    if (!evdd_value_resolve_type(w_low, w_high, &t) || t == EVDD_VALUE_UNTYPED) {
        r.w_low = w_low;
        r.w_high = w_high;
        r.incoming = evdd_value_unit_from_pair(w_low, w_high);
        return r;
    }

    sq0 = evdd_value_is_zero(w_low)
              ? EVDD_VALUE_ZERO
              : evdd_value_intern(EVDDVALUEABSSQ(t)(evdd_value_get(w_low)), t);
    sq1 = evdd_value_is_zero(w_high)
              ? EVDD_VALUE_ZERO
              : evdd_value_intern(EVDDVALUEABSSQ(t)(evdd_value_get(w_high)), t);
    sum = evdd_value_add(sq0, sq1);
    n = evdd_value_intern(EVDDVALUESQRT(t)(evdd_value_get(sum)), t);

    r.incoming = n;
    r.w_low = evdd_value_div(w_low, n);
    r.w_high = evdd_value_div(w_high, n);
    return r;
}

evddNormResult evdd_value_normalize(evdd_value_id w_low, evdd_value_id w_high)
{
    evddNormResult r;
    evdd_value_type t;
    evdd_value_normalize_function_t fn;

    if (evdd_value_is_zero(w_low) && evdd_value_is_zero(w_high))
        return evdd_norm_zeros();

    r.w_low = w_low;
    r.w_high = w_high;
    r.incoming = evdd_value_unit_from_pair(w_low, w_high);

    if (!evdd_value_resolve_type(w_low, w_high, &t) || t == EVDD_VALUE_UNTYPED)
        return r;

    fn = EVDDVALUENORMALIZE(t);
    if (fn == NULL) {
        printf("Error: no normalize function for value type %u\n", t);
        bdd_error(BDD_OP);
        return r;
    }
    return fn(w_low, w_high);
}