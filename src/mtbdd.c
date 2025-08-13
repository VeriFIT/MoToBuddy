#include "mtbdd.h"
#include <stdlib.h>

int         mtbddTerminalUsed;     /* Number of allocated terminal values */
int         mtbddmaxTerminalSize;  /* Maximum allowed number of terminal values */
int         mtbddLastValueIndex;   /* Index of the "rightmost" free element */
mtbddValues mtbddterminalVals;     /* All of the terminal values */
domainTypes domaintype = -1;       /* Type of value in terminal nodes */
extern int  checkSameChildren;
int         mtbdd_apply_invalid_value_flag;
int mtbdd_operation_invalid_value_flag;

unsigned  (*customHash)(void *) = NULL;
int       (*customCompare)(void *, void *) = NULL;
void      (*customFree)(void *);

#define NODEHASH(lvl,l,h) (TRIPLE(lvl,l,h) % bddnodesize)



/**
 * @brief Function creating terminal node.
 * 
 * @param value Pointer to the value to store in the terminal node
 * 
 * @warning All work is based on correctly set domaintype and when using CUSTOM,
 *          customCompare must be defined. Defining customHash will prevent 
 *          terminal nodes mapping to one list of synonyms.
 * 
 * @return ID of the created terminal node, or one of already existing and found.
 */
int mtbdd_maketerminal(void *value, mtbdd_terminal_type type) {
   if(value == NULL) {return 0;}
   BddNode *node;
   int foundTerminal;

   switch(domaintype){
      case INTVAL:
      case FLOATVAL:
      case UNSIGNEDVAL:
         return bdd_makenode(MAXLEVEL,  42 , *(unsigned*)value); // some placeholder for the high
      
      case LONGVAL:
         unsigned lowLong  = *(long*)value & 0xFFFFFFFF;
         unsigned highLong = (*(long*)value >> 32) & 0xFFFFFFFF;
         unsigned hashLong = NODEHASH(MAXLEVEL, lowLong, highLong);
         foundTerminal = mtbdd_findterminal(value, hashLong, type);
         if(foundTerminal != -1){return foundTerminal;}

         mtbdd_insertvalue(value);

         foundTerminal = bdd_makenode(MAXLEVEL, lowLong, highLong); // create new node
         node = &bddnodes[foundTerminal];
         HIGHp(node)    = mtbddTerminalUsed - 1; // set high/index to index of new terminal value

         return foundTerminal;

      case DOUBLEVAL:
         unsigned long doubleBits = *(unsigned long*)value;
         unsigned lowDouble  = (unsigned)(doubleBits & 0xFFFFFFFF);
         unsigned highDouble = (unsigned)((doubleBits >> 32) & 0xFFFFFFFF);
         unsigned hashDouble = NODEHASH(MAXLEVEL, lowDouble, highDouble);
         foundTerminal       = mtbdd_findterminal(value, hashDouble, type);
         if(foundTerminal != -1){return foundTerminal;}

         mtbdd_insertvalue(value);

         foundTerminal = bdd_makenode(MAXLEVEL, lowDouble, highDouble); // create new node
         node = &bddnodes[foundTerminal];
         HIGHp(node)   = mtbddTerminalUsed - 1; // set high/index to index of new terminal value
         
         return foundTerminal;
      case CUSTOM:
         unsigned custom = 42; // when no custom hash function is set, all terminals map to one list of synonyms
         mtbdd_terminal_hash_function_t hashfun = CUSTOMHASH(type);
         if(hashfun){custom = hashfun(value);}
         unsigned hash = NODEHASH(MAXLEVEL, custom, 42); // 42 is a placeholder
         foundTerminal = mtbdd_findterminal(value, hash, type);
         if(foundTerminal != -1){return foundTerminal;}
         
         mtbdd_insertvalue(value);

         foundTerminal = bdd_makenode(MAXLEVEL, custom, 42);
         node = &bddnodes[foundTerminal];
         HIGHp(node)   = mtbddTerminalUsed - 1; // set high/index to index of new terminal value
         TERMINALTYPEp(node) = type;
         return foundTerminal;
      default:
         return -1;
   } 
   
}

/**
 * @brief Inserts value into table of values for terminal nodes.
 * 
 * @param value Pointer to the value to store.
 * 
 */
int mtbdd_insertvalue(void *value){

   if(mtbddmaxTerminalSize == 0){
      mtbddLastValueIndex   = 0;
      mtbddterminalVals.top = NULL;
   }

   int index;

   switch(domaintype){

      case DOUBLEVAL:
         if(mtbddmaxTerminalSize == 0){
            mtbddterminalVals.doubleValues = malloc(INITIAL_TERMINAL_SIZE*sizeof(double));
            if(mtbddterminalVals.doubleValues == NULL){
               bdd_error(BDD_MEMORY);
            }
            mtbddmaxTerminalSize = INITIAL_TERMINAL_SIZE;
         }
         if (mtbddTerminalUsed == mtbddmaxTerminalSize) {
            double* newPtr = realloc(mtbddterminalVals.doubleValues, mtbddmaxTerminalSize * 2 * sizeof(double));
            if (newPtr == NULL) {
               bdd_error(BDD_MEMORY); 
            }
            mtbddterminalVals.doubleValues = newPtr; 
            mtbddmaxTerminalSize *= 2; 
         }

         index = mtbdd_IndexStackPop(&mtbddterminalVals);
         if(index == -1){index = mtbddLastValueIndex++;}
         mtbddterminalVals.doubleValues[index] = *(double*)value;
         mtbddTerminalUsed++;
         break;

      case LONGVAL:
         if(mtbddmaxTerminalSize == 0){
            mtbddterminalVals.longValues = malloc(INITIAL_TERMINAL_SIZE*sizeof(long));
            if(mtbddterminalVals.longValues == NULL){
               bdd_error(BDD_MEMORY);
            }
            mtbddmaxTerminalSize = INITIAL_TERMINAL_SIZE;
         }
         if (mtbddTerminalUsed == mtbddmaxTerminalSize) {
            long* newPtr = realloc(mtbddterminalVals.longValues, mtbddmaxTerminalSize * 2 * sizeof(long));
            if (newPtr == NULL) {
               bdd_error(BDD_MEMORY); 
            }
            mtbddterminalVals.longValues = newPtr; 
            mtbddmaxTerminalSize *= 2; 
         }

         index = mtbdd_IndexStackPop(&mtbddterminalVals);
         if(index == -1){index = mtbddLastValueIndex++;}
         mtbddterminalVals.longValues[index] = *(long*)value;
         mtbddTerminalUsed++;
         break;

      case CUSTOM:
         if(mtbddmaxTerminalSize == 0){
            mtbddterminalVals.customPointers = calloc(INITIAL_TERMINAL_SIZE,sizeof(void*));
            if(mtbddterminalVals.customPointers == NULL){
               bdd_error(BDD_MEMORY);
            }
            mtbddmaxTerminalSize = INITIAL_TERMINAL_SIZE;
         }
         if (mtbddTerminalUsed == mtbddmaxTerminalSize) {
            printf("reallocating terminals");
            size_t old_size = mtbddmaxTerminalSize;

            void **newPtr = realloc(mtbddterminalVals.customPointers, mtbddmaxTerminalSize * 2 * sizeof(void*));
            if (newPtr == NULL) {
               bdd_error(BDD_MEMORY);
               return 0;
            }

            // Update the pointer and size AFTER successful realloc
            mtbddterminalVals.customPointers = newPtr;
            mtbddmaxTerminalSize *= 2;

            // Initialize the newly added portion to NULL
            // The new elements start from the 'old_size' index up to 'mtbddmaxTerminalSize - 1'
            for (size_t i = old_size; i < mtbddmaxTerminalSize; ++i) {
               mtbddterminalVals.customPointers[i] = NULL;
            } 
         }
         index = mtbdd_IndexStackPop(&mtbddterminalVals);
         if(index == -1){index = mtbddLastValueIndex++;}
         mtbddterminalVals.customPointers[index] = value;
         mtbddTerminalUsed++;
         break;
      default:
         break;
   }

   return 0;
}

/**
 * @brief Looks for terminal node with the hash.
 * 
 * @param value Value to validate the node
 * @param hash  Calculated hash for the node
 * 
 * @return ID of the node representing the terminal if found, -1 otherwise
 */
int mtbdd_findterminal(void *value, unsigned hash, mtbdd_terminal_type type){
   int res = bddnodes[hash].hash;
   unsigned index = bddnodes[res].index;
   if(index > mtbddTerminalUsed || index < 0){return -1;} // invalid index
   int count = 0;
   while(res != 0)
   {
      unsigned index = bddnodes[res].index;
      switch(domaintype){
         case LONGVAL:
            if (*(long*)value == mtbddterminalVals.longValues[index]) {
               return res;
            }
            break;

         case DOUBLEVAL:
            if (*(double*)value == mtbddterminalVals.doubleValues[index]) {
               return res;
            }
            break;

         case CUSTOM:
            mtbdd_terminal_compare_function_t cmp = CUSTOMCOMPARE(type);
            if (cmp == NULL) {
               bdd_error(BDD_OP);
               return bddfalse;
            }
            if (index < mtbddmaxTerminalSize && 
               type == bddnodes[res].type &&
               cmp(value, mtbddterminalVals.customPointers[index])) {
               return res;
            }
            break;
      }
      count++;
      res = bddnodes[res].next;
   }
   return -1; // not found

}

/**
 * @brief Hashes of terminal nodes during rehash in gbc
 * 
 * As terminal nodes with values stored not directly in the node have a different way
 * of calculating hash for bddnodes table, this function encapsulates the logic.
 * 
 * @warning If the domaintype is CUSTOM, customHash must be defined
 * 
 * @param value Pointer to the value associated with the terminal
 * 
 * @return Calculated hash
 */
unsigned mtbdd_terminal_hash_gbc(void *value, mtbdd_terminal_type type){
   switch(domaintype){
      case LONGVAL:
         unsigned lowLong  = *(long*)value & 0xFFFFFFFF;
         unsigned highLong = (*(long*)value >> 32) & 0xFFFFFFFF;
         return NODEHASH(MAXLEVEL, lowLong, highLong);

      case DOUBLEVAL:
         unsigned long doubleBits = *(unsigned long*)value;
         unsigned lowDouble  = (unsigned)(doubleBits & 0xFFFFFFFF);
         unsigned highDouble = (unsigned)((doubleBits >> 32) & 0xFFFFFFFF);
         return NODEHASH(MAXLEVEL, lowDouble, highDouble);

      case CUSTOM:
         mtbdd_terminal_hash_function_t hashfun = CUSTOMHASH(type);
         return NODEHASH(MAXLEVEL, hashfun(value), 42); 
   }
}


/* APPLY */

/**
 * @brief Function to obtain pointer to the value associated with given terminal.
 * 
 * @param terminal ID of the terminal node
 * 
 * @return Pointer to the value
 */
void *mtbdd_getTerminalValue(BDD terminal){
   if(ISZERO(terminal)) {return NULL;}
   if(!ISTERMINAL(terminal)){
        bdd_error(BDD_OP); // TODO add better error
    }
    switch(domaintype){
        case INTVAL:
        case FLOATVAL:
        case UNSIGNEDVAL:
            return &bddnodes[terminal].intVal;
        case LONGVAL:
            return &mtbddterminalVals.longValues[bddnodes[terminal].index];
        case DOUBLEVAL:
            return &mtbddterminalVals.doubleValues[bddnodes[terminal].index];
        case CUSTOM:
            return mtbddterminalVals.customPointers[bddnodes[terminal].index];
        default:
            return NULL;
    }
}


/**
 * @brief Function to obtain pointer to the value associated with given terminal.
 * 
 * @param node Pointer to the terminal node
 * 
 * @return Pointer to the value 
 */
void *mtbdd_getvaluep(BddNode* node){

       if(LEVELp(node) != MAXLEVEL){
        bdd_error(BDD_OP); // TODO add better error
    }
    switch(domaintype){
        case INTVAL:
        case FLOATVAL:
        case UNSIGNEDVAL:
            return &(node->intVal);
        case LONGVAL:
            return &mtbddterminalVals.longValues[node->index];
        case DOUBLEVAL:
            return &mtbddterminalVals.doubleValues[node->index];
        case CUSTOM:
            return mtbddterminalVals.customPointers[node->index];
        default:
            return NULL;
    }
}

mtbdd_terminal_type mtbdd_get_terminal_type(BDD terminal) {
   return bddnodes[terminal].type;
}


BDD mtbdd_apply(BDD l, BDD r, void*(*op)(void*, void*)){
   CHECKa(l, bddfalse);
   CHECKa(r, bddfalse);

   if(op == NULL){
      bdd_error(BDD_OP);
      return bddfalse;
   }
   BDD res = mtbdd_apply_rec(l,r,op);

   return res;
}

BDD mtbdd_apply_rec(BDD l, BDD r, void*(*op)(void*, void*)) {

    BddCacheData *entry;
    BDD res;

    entry = BddCache_lookup(&mtbdd_cache_apply, TRIPLE(l,r,(int)(size_t)op)); // hash is calculated from ID of left, right BDD nodes and casted address of the operation
    if( BddCache_is_valid(&mtbdd_cache_apply, entry)
       && entry->a == l && entry->b == r && entry->c == (int)(size_t)op){
        return entry->r.res;
    }

    if((ISTERMINAL(l) || ISZERO(l)) && (ISZERO(r) || ISTERMINAL(r))){
        void *terminalValueL = mtbdd_getTerminalValue(l);
        void *terminalValueR = mtbdd_getTerminalValue(r);
        mtbdd_terminal_type terminalTypeL = mtbdd_get_terminal_type(l);
        mtbdd_terminal_type terminalTypeR = mtbdd_get_terminal_type(r);
        if (terminalTypeL != terminalTypeR) {
         printf("Mismatch of terminal types in APPLY.\n"); // TODO CUSTOM ERROR
         bdd_error(BDD_ILLBDD);
        }
        void *resultValue = op(terminalValueL,terminalValueR );
        if (resultValue == NULL) {return bdd_false();}
        mtbdd_terminal_compare_function_t cmp = CUSTOMCOMPARE(terminalTypeL);
        mtbdd_terminal_free_function_t freeterminal = CUSTOMFREE(terminalTypeL);
        if (cmp == NULL) {
         printf("Missing custom compare for type %d.\n", terminalTypeL);
         bdd_error(BDD_OP);
        }
        if (cmp(resultValue, terminalValueL)) {
            if (freeterminal) freeterminal(resultValue);
            free(resultValue);
            return l;
        }
        if(cmp(resultValue, terminalValueR)) {
            if (freeterminal) freeterminal(resultValue);
            free(resultValue);
          return r;
        }
        res = (mtbdd_maketerminal(resultValue, terminalTypeL));
        if(!DOMAIN_NOT_SHORT){ // value will be directly in the node
            free(resultValue); // we don't need the allocated one
        }
    }
    else{
      if(LEVEL(l) == LEVEL(r)){
         PUSHREF(mtbdd_apply_rec(LOW(l), LOW(r), op));
         PUSHREF(mtbdd_apply_rec(HIGH(l), HIGH(r), op));
         res = bdd_makenode(LEVEL(l), READREF(2), READREF(1));
      }
      else if(LEVEL(l) < LEVEL(r)){
            PUSHREF(mtbdd_apply_rec(LOW(l), r, op));
            PUSHREF(mtbdd_apply_rec(HIGH(l), r, op));
            res = bdd_makenode(LEVEL(l), READREF(2), READREF(1));
      }
      else{
            PUSHREF(mtbdd_apply_rec(l, LOW(r), op));
            PUSHREF(mtbdd_apply_rec(l, HIGH(r), op));
            res = bdd_makenode(LEVEL(r), READREF(2), READREF(1));
      }

      POPREF(2);
   }
   // add reference to cache
   BddCache_store(entry, &mtbdd_cache_apply, l, r, (int)(size_t)op, res);
   return res;
}

/**
 * @brief User-guarded binary apply operation for MTBDDs.
 *
 * This version of apply allows a user-defined operation `op` to decide whether
 * to short-circuit the recursion and return a custom result. This is useful
 * when recursion should be overridden based on context.
 *
 * @param l Left-hand side MTBDD.
 * @param r Right-hand side MTBDD.
 * @param op User-defined binary operation. Must also set FLAG_VALID_APPLY / FLAG_INVALID_APPLY.
 *
 * @warning The `op` function must properly handle terminals and construct valid terminal nodes.
 * @warning If both operands are terminals, the result of `op(l, r)` is used unconditionally.
 * 
 * @see mtbdd_apply_guarded_rec
 * @see mtbdd_apply
 * 
 * @return Result of guarded apply.
 */
BDD mtbdd_apply_guarded(BDD l, BDD r, BDD(*op)(BDD, BDD)) {
   CHECKa(l, bddfalse);
   CHECKa(r, bddfalse);

   if(op == NULL){
      bdd_error(BDD_OP);
      return bddfalse;
   }
   INITREF;
   BDD res = mtbdd_apply_guarded_rec(l,r,op);

   return res;
}


BDD mtbdd_apply_guarded_rec(BDD l, BDD r, BDD(*op)(BDD, BDD)) {
   BddCacheData *entry;
   BDD res = op(l,r);
   if (APPLY_RESULT_VALID) {
      return res;
   }

   entry = BddCache_lookup(&mtbdd_cache_apply, TRIPLE(l,r,(int)(size_t)op)); // hash is calculated from ID of left, right BDD nodes and casted address of the operation
   if( BddCache_is_valid(&mtbdd_cache_apply, entry)
      && entry->a == l && entry->b == r && entry->c == (int)(size_t)op){
      return entry->r.res;
   }

   if((ISTERMINAL(l) || ISZERO(l)) && (ISZERO(r) || ISTERMINAL(r))){
      res = op(l, r);
   }
   else {
      if(LEVEL(l) == LEVEL(r)){
         PUSHREF(mtbdd_apply_guarded_rec(LOW(l), LOW(r), op));
         PUSHREF(mtbdd_apply_guarded_rec(HIGH(l), HIGH(r), op));
         res = bdd_makenode(LEVEL(l), READREF(2), READREF(1));
      }
      else if(LEVEL(l) < LEVEL(r)){
            PUSHREF(mtbdd_apply_guarded_rec(LOW(l), r, op));
            PUSHREF(mtbdd_apply_guarded_rec(HIGH(l), r, op));
            res = bdd_makenode(LEVEL(l), READREF(2), READREF(1));
      }
      else{
            PUSHREF(mtbdd_apply_guarded_rec(l, LOW(r), op));
            PUSHREF(mtbdd_apply_guarded_rec(l, HIGH(r), op));
            res = bdd_makenode(LEVEL(r), READREF(2), READREF(1));
      }

      POPREF(2);
   }
   // add reference to cache
   BddCache_store(entry, &mtbdd_cache_apply, l, r, (int)(size_t)op, res);
   return res;
}


BDD mtbdd_apply_unary(BDD l, void*(*op)(void*)) {
   CHECKa(l, bddfalse);

   if (op == NULL) {
      bdd_error(BDD_OP);
      return bddfalse;
   }
   INITREF;
   BDD res = mtbdd_apply_unary_rec(l, op);
   return res;
}

BDD mtbdd_apply_unary_rec(BDD l, void*(*op)(void*)) {
    BddCacheData *entry;
    BDD res;

    entry = BddCache_lookup(&mtbdd_cache_apply, PAIR(l, (int)(size_t)op));
    if ( BddCache_is_valid(&mtbdd_cache_apply, entry) && 
         entry->a == l && entry->b == -1 && entry->c == (int)(size_t)op) {
        return entry->r.res;
    }

    if (ISTERMINAL(l) || ISZERO(l)) {
        void *terminalValue = mtbdd_getTerminalValue(l);
        mtbdd_terminal_type terminalType = mtbdd_get_terminal_type(l);
        void *resultValue = op(terminalValue);
        mtbdd_terminal_compare_function_t cmp = CUSTOMCOMPARE(terminalType);
        mtbdd_terminal_free_function_t freeterminal = CUSTOMFREE(terminalType);
        if (cmp == NULL) {
         printf("Custom compare missing for type %d.\n", terminalType);
         bdd_error(BDD_OP);
        }
        if (cmp(resultValue, terminalValue)) {
            if (freeterminal) freeterminal(resultValue);
            free(resultValue);
            return l;
        }
        res = (mtbdd_maketerminal(resultValue, terminalType));
        if (!DOMAIN_NOT_SHORT) {
            free(resultValue);
        }
    } else {
        PUSHREF(mtbdd_apply_unary_rec(LOW(l), op));
        PUSHREF(mtbdd_apply_unary_rec(HIGH(l), op));
        res = bdd_makenode(LEVEL(l), READREF(2), READREF(1));
        POPREF(2);
    }

    BddCache_store(entry, &mtbdd_cache_apply, l, -1, (int)(size_t)op, res);
    return res;
}

BDD mtbdd_apply_unary_param(BDD l, void*(*op)(void*, size_t), size_t param) {
   CHECKa(l, bddfalse);

   if (op == NULL) {
      bdd_error(BDD_OP);
      return bddfalse;
   }
   INITREF;
   BDD res = mtbdd_apply_unary_param_rec(l, op, param);
   return res;
}

BDD mtbdd_apply_unary_param_rec(BDD l, void*(*op)(void*, size_t), size_t param) {
    BddCacheData *entry;
    BDD res;

    entry = BddCache_lookup(&mtbdd_cache_apply, TRIPLE(l, (int)param, (int)(size_t)op));
    if ( BddCache_is_valid(&mtbdd_cache_apply, entry) && 
         entry->a == l && entry->b == (int)param && entry->c == (int)(size_t)op) {
        return entry->r.res;
    }

    if (ISTERMINAL(l) || ISZERO(l)) {
        void *terminalValue = mtbdd_getTerminalValue(l);
        mtbdd_terminal_type terminalType = mtbdd_get_terminal_type(l);
        void *resultValue = op(terminalValue, param);
        mtbdd_terminal_compare_function_t cmp = CUSTOMCOMPARE(terminalType);
        mtbdd_terminal_free_function_t freeterminal = CUSTOMFREE(terminalType);
        if (cmp == NULL) {
         printf("Custom compare missing for type %d.\n", terminalType);
         bdd_error(BDD_OP);
        }
        if (cmp(resultValue, terminalValue)) {
            if (freeterminal) freeterminal(resultValue);
            free(resultValue);
            return l;
        }
        res = (mtbdd_maketerminal(resultValue, terminalType));
        if (!DOMAIN_NOT_SHORT) {
            free(resultValue);
        }
    } else {
        PUSHREF(mtbdd_apply_unary_param_rec(LOW(l), op, param));
        PUSHREF(mtbdd_apply_unary_param_rec(HIGH(l), op, param));
        res = bdd_makenode(LEVEL(l), READREF(2), READREF(1));
        POPREF(2);
    }

    BddCache_store(entry, &mtbdd_cache_apply, l, (int)param, (int)(size_t)op, res);
    return res;
}

void mtbdd_delete_terminal(BddNode *terminal){
   switch(domaintype){

      case LONGVAL:
      case DOUBLEVAL:
         mtbdd_IndexStackPush(&mtbddterminalVals,terminal->index);
         mtbddTerminalUsed--;
         return;

      case CUSTOM:
         mtbdd_terminal_free_function_t freeterminal = CUSTOMFREE(terminal->type);
         if (freeterminal) {
            freeterminal(mtbddterminalVals.customPointers[terminal->index]);
         }
         free(mtbddterminalVals.customPointers[terminal->index]);
         mtbddterminalVals.customPointers[terminal->index] = NULL;
         mtbdd_IndexStackPush(&mtbddterminalVals,terminal->index);
         mtbddTerminalUsed--;

      default:
         return;
   }
}

/**
 * @brief Unary apply guarded by user with an additional argument.
 * 
 * Recursively applies a user-defined operation to each node of the MTBDD.
 * The user decides whether recursion should continue or stop by setting
 * the appropriate flag using FLAG_VALID_APPLY / FLAG_INVALID_APPLY.
 * 
 * @note This version is slightly less performant than mtbdd_apply_unary
 *       due to calls of `op` to every node and not checking cache first.
 * 
 * @warning The user MUST set the apply validity flag inside `op` using FLAG_VALID_APPLY / FLAG_INVALID_APPLY.
 * @warning The `arg` parameter is NOT included in cache key — different calls
 *          with the same MTBDD and op but different arg will reuse cached results.
 * @warning When `op` is called on a terminal node, the returned result is always
 *          used and returned - even if the result is flagged as invalid. * 
 * @param l The MTBDD root.
 * @param op Unary operation function with recursion control.
 * @param arg User-defined optional argument passed to `op`.
 * 
 * @return Resulting MTBDD.
 * 
 * @see mtbdd_apply_unary_guarded_rec
 * @see mtbdd_apply_unary
 * 
 * @todo arg cache
 */
BDD mtbdd_apply_unary_guarded(BDD l, BDD(*op)(BDD, void*), size_t arg) {
   CHECKa(l, bddfalse);

   if (op == NULL) {
      bdd_error(BDD_OP);
      return bddfalse;
   }
   INITREF;
   BDD res = mtbdd_apply_unary_guarded_rec(l, op, arg);
   return res;
}

/**
 * @brief Recursive logic of guarded unary apply with argument.
 *
 * @see mtbdd_apply_unary_guarded
 * 
 * @param l MTBDD to perform apply on.
 * @param op User-provided operation to apply.
 * @param arg Optional argument passed to op.
 * 
 * @return Root of a subtree after apply.
 */
BDD mtbdd_apply_unary_guarded_rec(BDD l, BDD(*op)(BDD, void*), size_t arg) {

   BDD res = op(l, arg);

   // terminal case signalised by user
   if (APPLY_RESULT_VALID) {
      return res;
   }

   BddCacheData *entry;

   entry = BddCache_lookup(&mtbdd_cache_apply, TRIPLE(l, (int)(size_t)op, arg));
   if ( BddCache_is_valid(&mtbdd_cache_apply, entry) && 
      entry->a == l && entry->b == arg && entry->c == (int)(size_t)op) {
      return entry->r.res;
   }

   // terminal case even if user signals invalid
   // cannot call apply on LOW or HIGH of terminals
   if (ISTERMINAL(l) || ISCONST(l)) {
      return res;
   } else {
      PUSHREF(mtbdd_apply_unary_guarded_rec(LOW(l), op, arg));
      PUSHREF(mtbdd_apply_unary_guarded_rec(HIGH(l), op, arg));
      res = bdd_makenode(LEVEL(l), READREF(2), READREF(1));
      POPREF(2);
   }

   BddCache_store(entry, &mtbdd_cache_apply, l, arg, (int)(size_t)op, res);
   return res;
}

BDD mtbdd_set_decision(BDD f, BDD g, BDD h){
   LOW(f) = g;
   HIGH(f) = h;
   return f;
}

BDD mtbdd_ite(BDD f, BDD g, BDD h){
   BDD res;

   CHECKa(f, bddfalse);
   CHECKa(g, bddfalse);
   CHECKa(h, bddfalse);


   res = mtbdd_ite_rec(f,g,h);
   return res;
}

BDD mtbdd_ite_rec(BDD f, BDD g, BDD h){

   BddCacheData *entry;
   BDD res;
   
   if(ISTERMINAL(f)){
      return f;
   }
   if(g == h){
      return g;
   }
   if(ISTERMINAL(g) && ISTERMINAL(h)){
      if(LOW(f) != g || HIGH(f) != h){
         mtbdd_set_decision(f,g,h);
      }   
      return f;
      
   }

   entry = BddCache_lookup(&mtbdd_cache_ite, TRIPLE(f,g,h));
   if(BddCache_is_valid(&mtbdd_cache_ite, entry) &&
      entry->a == g && entry->b == g && entry->c == h){
      return entry->r.res;
   }

   // ite is recursively called in lows/highs of the nodes, that have the lowest level out of three -> 
   // therefore it will never be called on a terminal node with low/high -> no need to consider the problems
   if (LEVEL(f) == LEVEL(g)){
      if (LEVEL(f) == LEVEL(h)){ // f,g,h have the same level, so they cannot be terminals
         PUSHREF(mtbdd_ite_rec(LOW(f), LOW(g), LOW(h)) );
         PUSHREF(mtbdd_ite_rec(HIGH(f), HIGH(g), HIGH(h)) );
         res = bdd_makenode(LEVEL(f), READREF(2), READREF(1));
      }
      else if (LEVEL(f) < LEVEL(h)){
         PUSHREF( mtbdd_ite_rec(LOW(f), LOW(g), h) );
         PUSHREF( mtbdd_ite_rec(HIGH(f), HIGH(g), h) );
         res = bdd_makenode(LEVEL(f), READREF(2), READREF(1));
      }
      else{ /* f > h */
         PUSHREF( mtbdd_ite_rec(f, g, LOW(h)) );
         PUSHREF( mtbdd_ite_rec(f, g, HIGH(h)) );
         res = bdd_makenode(LEVEL(h), READREF(2), READREF(1));
      }
   }
   else if (LEVEL(f) < LEVEL(g)){
      if (LEVEL(f) == LEVEL(h)){
         PUSHREF( mtbdd_ite_rec(LOW(f), g, LOW(h)) );
         PUSHREF( mtbdd_ite_rec(HIGH(f), g, HIGH(h)) );
         res = bdd_makenode(LEVEL(f), READREF(2), READREF(1));
      }
      else if (LEVEL(f) < LEVEL(h)){
         PUSHREF( mtbdd_ite_rec(LOW(f), g, h) );
         PUSHREF( mtbdd_ite_rec(HIGH(f), g, h) );
         res = bdd_makenode(LEVEL(f), READREF(2), READREF(1));
      }
      else{ /* f > h */
         PUSHREF( mtbdd_ite_rec(f, g, LOW(h)) );
         PUSHREF( mtbdd_ite_rec(f, g, HIGH(h)) );
         res = bdd_makenode(LEVEL(h), READREF(2), READREF(1));
      }
   }
   else{ /* f > g */
      if (LEVEL(g) == LEVEL(h)){
         PUSHREF( mtbdd_ite_rec(f, LOW(g), LOW(h)) );
         PUSHREF( mtbdd_ite_rec(f, HIGH(g), HIGH(h)) );
         res = bdd_makenode(LEVEL(g), READREF(2), READREF(1));
      }
      else if (LEVEL(g) < LEVEL(h)){
         PUSHREF( mtbdd_ite_rec(f, LOW(g), h) );
         PUSHREF( mtbdd_ite_rec(f, HIGH(g), h) );
         res = bdd_makenode(LEVEL(g), READREF(2), READREF(1));
      }
      else{ /* g > h */
         PUSHREF( mtbdd_ite_rec(f, g, LOW(h)) );
         PUSHREF( mtbdd_ite_rec(f, g, HIGH(h)) );
         res = bdd_makenode(LEVEL(h), READREF(2), READREF(1));
      }
   }

   POPREF(2);

   BddCache_store(entry, &mtbdd_cache_ite, f, g, h, res);
   return res;

}

/* INDEXSTACK */

void mtbdd_IndexStackPush(mtbddValues *vals, int index){
   mtbddIndexStackEl *new = (mtbddIndexStackEl*)malloc(sizeof(mtbddIndexStackEl));
   if(new == NULL){bdd_error(BDD_MEMORY);}

   new->index = index;
   new->next  = vals->top;
   vals->top  = new;
}

int mtbdd_IndexStackPop(mtbddValues *vals){
   
   if(vals->top == NULL){return -1;} // no extra index

   mtbddIndexStackEl *tmp = vals->top;
   int index = tmp->index;
   vals->top = tmp->next;
   free(tmp);
   return index;

}

void mtbdd_IndexStackFree(mtbddValues *vals){
   while(vals->top != NULL){
      mtbdd_IndexStackPop(vals);
   }
}

BDD mtbdd_operation(BDD operand, size_t *controls, size_t controlNum, BDD(*op)(size_t, BDD, BDD)) {
   CHECKa(operand, bddfalse);
   return mtbdd_operation_rec(operand, controls, controlNum, op);
}


BDD mtbdd_operation_rec(BDD operand, size_t* controls, size_t controlNum, BDD(*op)(size_t, BDD, BDD)) {
   CHECKa(operand, bddfalse); // sanity check
   if (operand == bdd_false()) { return bdd_false(); }
   if (op == NULL) {
      bdd_error(BDD_OP);
      return bddfalse;
   }

   size_t control = controls[0];
   
   BddCacheData *entry = BddCache_lookup(&mtbdd_cache_operation, TRIPLE(operand, control, (int)(size_t)op));
   size_t hash = TRIPLE(operand, control, (int)(size_t)op);
   if (BddCache_is_valid(&mtbdd_cache_operation, entry) &&
      entry->a == operand && entry->b == control && entry->c == (int)(size_t)op) {
      return entry->r.res;
   }

   BDD targetDD = operand;
   if (LEVEL(targetDD) > control || ISTERMINAL(targetDD) || ISCONST(targetDD)) {
      checkSameChildren = 0;
      PUSHREF(operand);
      targetDD = bdd_makenode(control, READREF(1), READREF(1));
      checkSameChildren = 1;
      POPREF(1);
   }

   BDD res;

   if (LEVEL(targetDD) == control && controlNum == 0) {
      res = op(control, LOW(targetDD), HIGH(targetDD));
      if (HIGH(res) == LOW(res)) {
         res = LOW(res);
      }
   }
   else if (LEVEL(targetDD) == control) {
      PUSHREF(mtbdd_operation_rec(HIGH(targetDD), controls + 1, controlNum - 1, op));
      res = bdd_makenode(control, LOW(targetDD), READREF(1));
      POPREF(1);
   }
   else if (LEVEL(targetDD) < control) {
      BDD oldLow = LOW(targetDD);
      BDD oldHigh = HIGH(targetDD);
      PUSHREF(mtbdd_operation_rec(LOW(targetDD), controls, controlNum, op)); // low
      PUSHREF(mtbdd_operation_rec(HIGH(targetDD), controls, controlNum, op)); // high
      if (READREF(2) == oldLow &&
      READREF(1) == oldHigh) {
         res = targetDD; // no change
      } else {
         res = bdd_makenode(LEVEL(targetDD), READREF(2), 
         READREF(1));
      }
      POPREF(2);

   } else {
      res = targetDD;
   }
   BddCache_store(entry, &mtbdd_cache_operation, operand, control, (int)(size_t)op, res);
   return res;
}


/**
 * @brief Extended version of mtbdd_operation allowing user-defined guarded recursion.
 * 
 * This function performs a recursive operation on an MTBDD, but first gives control
 * to a user-specified function `op` to decide whether to override the recursion.
 * 
 * If `op` sets the global flag `FLAG_VALID_OPERATION()`, the returned node is assumed
 * valid and returned immediately. Otherwise, normal recursion and caching proceed.
 * 
 * @param operand    The BDD to apply the operation to.
 * @param controls   Array of control variables, with the last entry being the target.
 * @param controlNum Number of control variables. If only the target is present, set to 0.
 * @param op         User-defined operation. Signature: BDD op(size_t var, BDD node)
 *                   The function receives the control/target variable index and the current node.
 *                   It must also explicitly set validity using FLAG_VALID_OPERATION() or FLAG_INVALID_OPERATION().
 * 
 * @warning Terminal or early-exit behavior must be handled within the user function.
 * @warning If FLAG_INVALID_OPERATION() is set, recursion proceeds regardless of returned node.
 * 
 * @return The resulting BDD after applying the guarded operation.
 */
BDD mtbdd_operation_guarded(BDD operand, size_t* controls, size_t controlNum, BDD(*op)(size_t, BDD)) {
   CHECKa(operand, bddfalse); // sanity check
   if (operand == bdd_false()) { return bdd_false(); }
   if (op == NULL) {
      bdd_error(BDD_OP);
      return bddfalse;
   }

   size_t control = controls[0];
   
   BDD res = op(control, operand); // check if recursion needed
   
   if (OPERATION_RESULT_VALID) { // recursion not needed
      return res; // return valid result
   }

   // continue recursion and general operation
   BddCacheData *entry = BddCache_lookup(&mtbdd_cache_operation, TRIPLE(operand, control, (int)(size_t)op));
   size_t hash = TRIPLE(operand, control, (int)(size_t)op);
   if (BddCache_is_valid(&mtbdd_cache_operation, entry) &&
      entry->a == operand && entry->b == control && entry->c == (int)(size_t)op) {
      return entry->r.res;
   }

   BDD targetDD = operand;
   if (LEVEL(targetDD) > control || ISTERMINAL(targetDD) || ISCONST(targetDD)) {
      checkSameChildren = 0;
      targetDD = bdd_makenode(control, operand, operand);
      checkSameChildren = 1;
   }
   
   if (LEVEL(targetDD) == control && controlNum == 0) {
      res = op(control, targetDD);
      if (HIGH(res) == LOW(res)) {
         res = LOW(res);
      }
   }
   else if (LEVEL(targetDD) == control) {
      targetDD;
      BDD high = bdd_addref(mtbdd_operation_guarded(HIGH(targetDD), controls + 1, controlNum - 1, op));
      res = bdd_makenode(control, LOW(targetDD), high);
      bdd_delref(high);
   }
   else if (LEVEL(targetDD) < control) {
      BDD oldLow = LOW(targetDD);
      BDD oldHigh = HIGH(targetDD);

      BDD low = bdd_addref(mtbdd_operation_guarded(LOW(targetDD), controls, controlNum, op));
      BDD high = bdd_addref(mtbdd_operation_guarded(HIGH(targetDD), controls, controlNum, op));

      if (low == oldLow && high == oldHigh) {
         res = targetDD; // no change
      } else {
         res = bdd_makenode(LEVEL(targetDD), low, high);
      }
      bdd_delref(low);
      bdd_delref(high);
   } else {
      res = targetDD;
   }

   BddCache_store(entry, &mtbdd_cache_operation, operand, control, (int)(size_t)op, res);
   return res;
}

BDD mtbdd_cube2(int value, int width, BDD *variables, BDD leaf1, BDD leaf0)
{
    BDD result = leaf1; 

    for (int i = width - 1; i >= 0; i--) {
        BDD node = variables[i];

        if (value & (1 << i)) {

            result = mtbdd_set_decision(node, bdd_false(), result);
        } else {

            result = mtbdd_set_decision(node, result, bdd_false());
        }
    }
    return result;
}

size_t mtbdd_leaf_count(BDD mtbdd) {
   size_t count = mtbdd_leaf_count_fn(mtbdd);
   bdd_unmark(mtbdd);
   return count;
}

size_t mtbdd_leaf_count_fn(BDD mtbdd) {
   if (ISCONST(mtbdd)) return 0;
   if (MARKED(mtbdd)) return 0;
   SETMARK(mtbdd);
   if (ISTERMINAL(mtbdd)) return 1;
   return (mtbdd_leaf_count_fn(LOW(mtbdd)) + mtbdd_leaf_count_fn(HIGH(mtbdd)));
}