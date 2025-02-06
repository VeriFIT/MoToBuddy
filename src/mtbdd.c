#include "kernel.h"
#include "bdd.h"
#include "mtbdd.h"
#include <stdlib.h>

int         mtbddTerminalUsed;     /* Number of allocated terminal values */
int         mtbddmaxTerminalSize;  /* Maximum allowed number of terminal values */
int         mtbddLastValueIndex;   /* Index of the "rightmost" free element */
mtbddValues mtbddterminalVals;     /* All of the terminal values */
domainTypes domaintype = -1;       /* Type of value in terminal nodes */

unsigned (*customHash)(void *) = NULL;
int (*customCompare)(void *, void *) = NULL;

#define NODEHASH(lvl,l,h) (TRIPLE(lvl,l,h) % bddnodesize)
#define INITIAL_TERMINAL_SIZE 100


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
int mtbdd_maketerminal(void *value){

   BddNode *node;
   int foundTerminal;

   switch(domaintype){
      case INTVAL:
      case FLOATVAL:
      case UNSIGNEDVAL:
         return bdd_makenode(MAXLEVEL, *(unsigned*)value , *(unsigned*)value); 
      
      case LONGVAL:
         unsigned lowLong  = *(long*)value & 0xFFFFFFFF;
         unsigned highLong = (*(long*)value >> 32) & 0xFFFFFFFF;
         unsigned hashLong = NODEHASH(MAXLEVEL, lowLong, highLong);
         foundTerminal = mtbdd_findterminal(value, hashLong);
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
         foundTerminal       = mtbdd_findterminal(value, hashDouble);
         if(foundTerminal != -1){return foundTerminal;}

         mtbdd_insertvalue(value);

         foundTerminal = bdd_makenode(MAXLEVEL, lowDouble, highDouble); // create new node
         node = &bddnodes[foundTerminal];
         HIGHp(node)   = mtbddTerminalUsed - 1; // set high/index to index of new terminal value
         
         return foundTerminal;

      case CUSTOM:
         unsigned custom = 42; // when no customHash function is set, all terminals map to one list of synonyms
         if(customHash){custom = customHash(value);}
         unsigned hash = NODEHASH(MAXLEVEL, custom, 42); // 42 is a placeholder for now TODO: change to TYPE when support of independed domains will be added
         foundTerminal = mtbdd_findterminal(value, hash);
         if(foundTerminal != -1){return foundTerminal;}
         
         mtbdd_insertvalue(value);

         foundTerminal = bdd_makenode(MAXLEVEL, custom, 42);
         node = &bddnodes[foundTerminal];
         HIGHp(node)   = mtbddTerminalUsed - 1; // set high/index to index of new terminal value
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
            mtbddterminalVals.customPointers = malloc(INITIAL_TERMINAL_SIZE*sizeof(void*));
            if(mtbddterminalVals.customPointers == NULL){
               bdd_error(BDD_MEMORY);
            }
            mtbddmaxTerminalSize = INITIAL_TERMINAL_SIZE;
         }
         if (mtbddTerminalUsed == mtbddmaxTerminalSize) {
            void **newPtr = realloc(mtbddterminalVals.customPointers, mtbddmaxTerminalSize * 2 * sizeof(void*));
            if (newPtr == NULL) {
               bdd_error(BDD_MEMORY); 
            }
            mtbddterminalVals.customPointers = newPtr; 
            mtbddmaxTerminalSize *= 2; 
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
int mtbdd_findterminal(void *value, unsigned hash){
   int res = bddnodes[hash].hash;
   unsigned index = bddnodes[res].index;
   if(index > mtbddTerminalUsed || index < 0){return -1;} // invalid index

   while(res != 0)
   {
      switch(domaintype){
         case LONGVAL:
            if(*(long*)value == mtbddterminalVals.longValues[index]){
               return res;
            }
         case DOUBLEVAL:
            if(*(double*)value == mtbddterminalVals.doubleValues[index]){
               return res;
            }
         case CUSTOM:
            if(customCompare == NULL){
               bdd_error(BDD_OP); // TODO BETTER ERROR
               return bddfalse;
            }
            if(customCompare(value, mtbddterminalVals.customPointers[index])){
               return res;
            }
      }
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
unsigned mtbdd_terminal_hash_gbc(void *value){
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
         return NODEHASH(MAXLEVEL, customHash(value), 42); 
   }
}


/**
 * @brief Function to obtain pointer to the value associated with given terminal.
 * 
 * @param terminal ID of the terminal node
 * 
 * @return Pointer to the value
 */
void *mtbdd_getvalue(BDD terminal){
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
            return &mtbddterminalVals.customPointers[node->index];
        default:
            return NULL;
    }
}

/* APPLY */
/* TODO: REORDER? */

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

BDD mtbdd_apply_rec(BDD l, BDD r, void*(*op)(void*, void*)){

    BddCacheData *entry;
    BDD res;

    entry = BddCache_lookup(&mtbdd_cache_apply, TRIPLE(l,r,(size_t)op)); // hash is calculated from ID of left, right BDD nodes and casted address of the operation
    if(entry->a == l && entry->b == r && entry->c == (size_t)op){
        return entry->r.res;
    }

    if(ISTERMINAL(l) && ISTERMINAL(r)){
        void *resultValue = op(mtbdd_getvalue(l), mtbdd_getvalue(r));
        res = mtbdd_maketerminal(resultValue);
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
   entry->a = l;
   entry->b = r;
   entry->c = (size_t)op;
   entry->r.res = res;
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
         free(mtbddterminalVals.customPointers[terminal->index]);
         mtbddterminalVals.customPointers[terminal->index] = NULL;
         mtbdd_IndexStackPush(&mtbddterminalVals,terminal->index);
         mtbddTerminalUsed--;

      default:
         return;
   }

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
   if(entry->a == g && entry->b == g && entry->c == h){
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

   entry->a = f;
   entry->b = g;
   entry->c = h;
   entry->r.res = res;

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