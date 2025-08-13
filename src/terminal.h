#ifndef TERMINAL_H
#define TERMINAL_H
typedef unsigned (*mtbdd_terminal_hash_function_t)(void *);
typedef int (*mtbdd_terminal_compare_function_t)(void *, void *);
typedef void (*mtbdd_terminal_free_function_t)(void *);

typedef struct mtbdd_terminal_functions {
    mtbdd_terminal_hash_function_t hashfun;
    mtbdd_terminal_compare_function_t comparefun;
    mtbdd_terminal_free_function_t freefun;
} mtbdd_terminal_functions;

typedef unsigned mtbdd_terminal_type;

extern mtbdd_terminal_functions *mtbdd_terminal_functions_list;
extern int mtbdd_terminal_type_number; // number of registered terminal types
extern int mtbdd_terminal_type_size; // allocated size of mtbdd_terminal_functions_list

#define CUSTOMCOMPARE(type) mtbdd_terminal_functions_list[type].comparefun
#define CUSTOMHASH(type) mtbdd_terminal_functions_list[type].hashfun
#define CUSTOMFREE(type) mtbdd_terminal_functions_list[type].freefun


unsigned mtbdd_new_terminal_type(void);
void mtbdd_register_hash_function(mtbdd_terminal_type type, mtbdd_terminal_hash_function_t hashfun);
void mtbdd_register_compare_function(mtbdd_terminal_type type, mtbdd_terminal_compare_function_t comparefun);
void mtbdd_register_free_function(mtbdd_terminal_type type, mtbdd_terminal_free_function_t freefun);
#endif