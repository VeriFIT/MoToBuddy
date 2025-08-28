#include <stdlib.h>
#include <string.h>
#include "terminal.h"
#include "bdd.h"
#include "kernel.h"

mtbdd_terminal_functions *mtbdd_terminal_functions_list = NULL;
int mtbdd_terminal_type_number = 0;
int mtbdd_terminal_type_size = 0;

// how many terminal types to allocate for at once
#define TERMINAL_TYPE_ALLOC_CHUNK 3

unsigned mtbdd_new_terminal_type(void) {
    if (mtbdd_terminal_type_number >= mtbdd_terminal_type_size) {
        int new_size = mtbdd_terminal_type_size + TERMINAL_TYPE_ALLOC_CHUNK;
        mtbdd_terminal_functions *new_list = realloc(
            mtbdd_terminal_functions_list,
            new_size * sizeof(mtbdd_terminal_functions)
        );
        if (!new_list) {
            printf("Memory allocation fail in mtbdd_new_terminal_type\n");
            bdd_error(BDD_MEMORY);
            return (unsigned)-1;
        }
        memset(new_list + mtbdd_terminal_type_size, 0,
               (new_size - mtbdd_terminal_type_size) * sizeof(mtbdd_terminal_functions));
        mtbdd_terminal_functions_list = new_list;
        mtbdd_terminal_type_size = new_size;
    }
    return (unsigned)mtbdd_terminal_type_number++;
}

void mtbdd_register_hash_function(mtbdd_terminal_type type, mtbdd_terminal_hash_function_t hashfun) {
    if (type >= (unsigned)mtbdd_terminal_type_number) {
        bdd_error(BDD_OP); // invalid type
        return;
    }
    mtbdd_terminal_functions_list[type].hashfun = hashfun;
}

void mtbdd_register_compare_function(mtbdd_terminal_type type, mtbdd_terminal_compare_function_t comparefun) {
    if (type >= (unsigned)mtbdd_terminal_type_number) {
        bdd_error(BDD_OP); // invalid type
        return;
    }
    mtbdd_terminal_functions_list[type].comparefun = comparefun;
}

void mtbdd_register_free_function(mtbdd_terminal_type type, mtbdd_terminal_free_function_t freefun) {
    if (type >= (unsigned)mtbdd_terminal_type_number) {
        bdd_error(BDD_OP); // invalid type
        return;
    }
    mtbdd_terminal_functions_list[type].freefun = freefun;
}


void mtbdd_register_to_str_function(mtbdd_terminal_type type, mtbdd_terminal_to_str_function_t toStrFun) {
    if (type >= (unsigned)mtbdd_terminal_type_number) {
        bdd_error(BDD_OP); // invalid type
        return;
    }
    mtbdd_terminal_functions_list[type].toStrFun = toStrFun;
}
