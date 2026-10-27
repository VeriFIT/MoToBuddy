#ifndef TERMINAL_H
#define TERMINAL_H
/**
 * Hash of a CUSTOM terminal payload.
 *
 * Payloads that compare equal must hash equal. A numeric compare that
 * treats -0.0 and +0.0 as the same value, paired with a hash of the
 * object bytes, stores both. Padding bytes, such as those in an x87
 * long double, do the same. The diagram then stops being canonical.
 */
typedef unsigned (*mtbdd_terminal_hash_function_t)(void *);
/**
 * Nonzero if the two payloads are equal.
 *
 * Must be reflexive: compare(x, x) is nonzero for every payload that
 * can be interned. A NaN under a plain == compare is not. Each copy
 * misses the existing row, they share one hash bucket, and lookup
 * becomes quadratic. Must be symmetric. An exact compare must be
 * transitive. A tolerance compare is not, and MTBDD lookup has no
 * neighbor probe, so it can miss an equal terminal.
 */
typedef int (*mtbdd_terminal_compare_function_t)(void *, void *);
typedef void (*mtbdd_terminal_free_function_t)(void *);
typedef char *(*mtbdd_terminal_to_str_function_t)(void *, char *, size_t);


typedef struct mtbdd_terminal_functions {
    mtbdd_terminal_hash_function_t hashfun;
    mtbdd_terminal_compare_function_t comparefun;
    mtbdd_terminal_free_function_t freefun;
    mtbdd_terminal_to_str_function_t toStrFun;
} mtbdd_terminal_functions;

typedef unsigned mtbdd_terminal_type;

extern mtbdd_terminal_functions *mtbdd_terminal_functions_list;
extern int mtbdd_terminal_type_number; // number of registered terminal types
extern int mtbdd_terminal_type_size; // allocated size of mtbdd_terminal_functions_list

#define CUSTOMCOMPARE(type) mtbdd_terminal_functions_list[type].comparefun
#define CUSTOMHASH(type) mtbdd_terminal_functions_list[type].hashfun
#define CUSTOMFREE(type) mtbdd_terminal_functions_list[type].freefun
#define CUSTOMTOSTR(type) mtbdd_terminal_functions_list[type].toStrFun


unsigned mtbdd_new_terminal_type(void);
void mtbdd_register_hash_function(mtbdd_terminal_type type, mtbdd_terminal_hash_function_t hashfun);
void mtbdd_register_compare_function(mtbdd_terminal_type type, mtbdd_terminal_compare_function_t comparefun);
void mtbdd_register_free_function(mtbdd_terminal_type type, mtbdd_terminal_free_function_t freefun);
void mtbdd_register_to_str_function(mtbdd_terminal_type type, mtbdd_terminal_to_str_function_t toStrfun);
/** Free the terminal-type function table (call from bdd_done). */
void mtbdd_terminal_types_done(void);

#endif