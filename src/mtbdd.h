#ifndef MTBDD_H
#define MTBDD_H
#include "kernel.h"
#include "bdd.h"
#include "cache.h"

#define SETDOMAIN(d) domaintype = d
#define ISTERMINAL(a) (bddnodes[a].level == MAXLEVEL)
#define ISTERMINALp(p) ((p)->level == MAXLEVEL)
#define NODEHASH(lvl,l,h) (TRIPLE(lvl,l,h) % bddnodesize)
#define INITIAL_TERMINAL_SIZE 10000

#define DOMAIN_NOT_SHORT ((domaintype != INTVAL) && (domaintype != FLOATVAL) && (domaintype != UNSIGNEDVAL))

#define CUSTOM_HASH_DECLARE(name) unsigned name(void*);

#define CUSTOM_HASH_DEFINE_START(name, paramName) unsigned name(void* paramName){ 
#define CUSTOM_HASH_DEFINE_END }

#define CUSTOM_COMPARE_DECLARE(name) int name(void*, void*);

#define CUSTOM_COMPARE_DEFINE_START(name, param1Name, param2Name) int name(void* param1Name, void* param2Name){
#define CUSTOM_COMPARE_DEFINE_END }

typedef enum e_domainTypes{
   INTVAL,
   FLOATVAL,
   CHARVAL,
   UNSIGNEDVAL,
   DOUBLEVAL,
   LDOUBLEVAL,
   LONGVAL,
   ULONGVAL,
   CUSTOM
} domainTypes;

typedef struct s_mtbddIndexStackEl{
   int index;
   struct s_mtbddIndexStackEl *next;
} mtbddIndexStackEl;

typedef struct s_mtbddValues{

   union{
      double        *doubleValues;
      long          *longValues;
      unsigned long *uLongValues;
      void         **customPointers;
   };

   mtbddIndexStackEl *top;

} mtbddValues;


extern mtbddValues mtbddterminalVals;     /* All of the terminal values */
extern domainTypes domaintype;  
extern unsigned (*customHash)(void *);
extern int (*customCompare)(void *, void *);
extern void (*customFree)(void *);

int mtbdd_findterminal(void *value, unsigned hash);
int mtbdd_maketerminal(void *value);
int mtbdd_insertvalue(void *value);
void *mtbdd_getTerminalValue(BDD terminal);
void *mtbdd_getvaluep(BddNode* node);
unsigned mtbdd_terminal_hash_gbc(void *value);
BDD mtbdd_apply(BDD l, BDD r, void*(*op)(void*, void*));
BDD mtbdd_apply_rec(BDD l, BDD r, void*(*op)(void*, void*));
BDD mtbdd_apply_unary(BDD l, void*(*op)(void*));
BDD mtbdd_apply_unary_rec(BDD l, void*(*op)(void*));
BDD mtbdd_ite(BDD f, BDD g, BDD h);
BDD mtbdd_ite_rec(BDD f, BDD g, BDD h);
extern BddCache mtbdd_cache_apply;
extern BddCache mtbdd_cache_ite;
extern BddCache mtbdd_cache_operation;
BDD mtbdd_set_decision(BDD f, BDD g, BDD h);
void mtbdd_delete_terminal(BddNode *terminal);
BDD mtbdd_cube2(int value, int width, BDD *variables, BDD leaf1, BDD leaf0);
BDD mtbdd_operation(BDD operand, size_t* controls, size_t controlNum, BDD(*op)(size_t, BDD, BDD));
void mtbdd_IndexStackPush(mtbddValues *vals, int index);
int  mtbdd_IndexStackPop(mtbddValues *vals);
void mtbdd_IndexStackFree(mtbddValues *vals);

#endif