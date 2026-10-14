/*************************************************************************
  FILE:  parser.cxx
  DESCR: Recursive-descent parser and main program for BDD calculator
  AUTH:  Jorn Lind (original BISON grammar), rewritten in plain C++
  DATE:  (C) may 1999
*************************************************************************/

#include <string>
#include <iostream>
#include <cstdlib>
#include <getopt.h>
#define IMPLEMENTSLIST /* Special for list template handling */
#include "slist.h"
#include "hashtbl.h"
#include "lexer.h"
#include "bdd.h"

   /* Definitions for storing and caching of identifiers */
#define inputTag  0
#define exprTag   1

struct nodeData
{
   nodeData(int t, const std::string &n, bdd v) : tag(t), name(n), val(v) {}
   int tag;
   std::string name;
   bdd val;
};

typedef SList<nodeData> nodeLst;
static nodeLst inputs;
static hashTable names;

static bddgbchandler gbcHandler = bdd_default_gbchandler;

   /* Terminal printer for dot output; plain BDDs only have 0 and 1 */
static void printTerminal(BDD r, FILE *f)
{
   fprintf(f, "%d", r);
}


/*************************************************************************
   Grammar

   calc      : initial inputs actions
   initial   : 'initial' INT INT ';'
   inputs    : 'inputs' ID+ ';'
   actions   : 'actions' (action ';')+
   action    : ID '=' expr
             | 'size' ID
             | 'dot' STR ID
             | 'reorder' method
             | 'autoreorder' INT method
             | 'cache'
             | 'tautology' ID
             | 'print' ID
   method    : 'win2' | 'win2ite' | 'sift' | 'siftite' | 'none'

   Expressions, from loosest to tightest binding. All binary operators
   are left associative; the body of a quantifier extends as far to the
   right as possible.

   expr      : biimp
   biimp     : imp   ( '<>' imp )*
   imp       : orexp ( '=>' orexp )*
   orexp     : xorexp ( ('|' | 'nor') xorexp )*
   xorexp    : andexp ( '^' andexp )*
   andexp    : unary ( ('&' | 'nand') unary )*
   unary     : '~' unary | primary
   primary   : '(' expr ')' | ID | 'true' | 'false'
             | 'exist'  ID+ '.' expr
             | 'forall' ID+ '.' expr
*************************************************************************/

class Parser
{
public:
   explicit Parser(FILE *in) : lex(in) { advance(); }
   void parseCalc(void);

private:
   void advance(void)                { tok = lex.next(); }
   bool accept(TokenKind kind);
   void expect(TokenKind kind);
   std::string expectId(void);
   int expectInt(void);

   void parseInitial(void);
   void parseInputs(void);
   void parseActions(void);
   void parseAction(void);
   int  parseMethod(void);

   bdd parseExpr(void);
   bdd parseBiimp(void);
   bdd parseImp(void);
   bdd parseOr(void);
   bdd parseXor(void);
   bdd parseAnd(void);
   bdd parseUnary(void);
   bdd parsePrimary(void);
   bdd parseVarList(void);

      /* Semantic actions */
   void actInit(int nodes, int cache);
   void actInputs(void);
   void actAddInput(const std::string &id);
   void actAssign(const std::string &id, const bdd &expr);
   bdd  actId(const std::string &id);
   bdd  actQuantVar(const std::string &id);
   void actSize(const std::string &id);
   void actDot(const std::string &fname, const std::string &id);
   void actAutoreorder(int times, int method);
   void actCache(void);
   void actTautology(const std::string &id);
   void actPrint(const std::string &id);

   nodeData *lookup(const std::string &id);

   Lexer lex;
   Token tok;
};


/*************************************************************************
   Token helpers
*************************************************************************/

bool Parser::accept(TokenKind kind)
{
   if (tok.kind != kind)
      return false;
   advance();
   return true;
}


void Parser::expect(TokenKind kind)
{
   if (tok.kind != kind)
      parseError("syntax error, expected %s but found %s",
		 tokenName(kind), tokenName(tok.kind));
   advance();
}


std::string Parser::expectId(void)
{
   if (tok.kind != T_id)
      parseError("syntax error, expected identifier but found %s",
		 tokenName(tok.kind));
   std::string id = tok.text;
   advance();
   return id;
}


int Parser::expectInt(void)
{
   if (tok.kind != T_intval)
      parseError("syntax error, expected integer but found %s",
		 tokenName(tok.kind));
   int v = tok.ival;
   advance();
   return v;
}


/*************************************************************************
   Top level
*************************************************************************/

void Parser::parseCalc(void)
{
   parseInitial();
   parseInputs();
   parseActions();
   expect(T_eof);
}


void Parser::parseInitial(void)
{
   expect(T_initial);
   int nodes = expectInt();
   int cache = expectInt();
   expect(T_semi);
   actInit(nodes, cache);
}


void Parser::parseInputs(void)
{
   expect(T_inputs);
   do
      actAddInput(expectId());
   while (tok.kind == T_id);
   expect(T_semi);
   actInputs();
}


void Parser::parseActions(void)
{
   expect(T_actions);
   do
   {
      parseAction();
      expect(T_semi);
   }
   while (tok.kind != T_eof);
}


void Parser::parseAction(void)
{
   switch (tok.kind)
   {
    case T_id:
    {
       std::string id = expectId();
       expect(T_equal);
       bdd val = parseExpr();
       actAssign(id, val);
       break;
    }

    case T_size:
       advance();
       actSize(expectId());
       break;

    case T_dumpdot:
    {
       advance();
       if (tok.kind != T_str)
	  parseError("syntax error, expected string but found %s",
		     tokenName(tok.kind));
       std::string fname = tok.text;
       advance();
       std::string id = expectId();
       actDot(fname, id);
       break;
    }

    case T_reorder:
       advance();
       bdd_reorder(parseMethod());
       break;

    case T_autoreorder:
    {
       advance();
       int times = expectInt();
       int method = parseMethod();
       actAutoreorder(times, method);
       break;
    }

    case T_cache:
       advance();
       actCache();
       break;

    case T_tautology:
       advance();
       actTautology(expectId());
       break;

    case T_print:
       advance();
       actPrint(expectId());
       break;

    default:
       parseError("syntax error, unexpected %s", tokenName(tok.kind));
   }
}


int Parser::parseMethod(void)
{
   int method;

   switch (tok.kind)
   {
    case T_win2:    method = BDD_REORDER_WIN2;    break;
    case T_win2ite: method = BDD_REORDER_WIN2ITE; break;
    case T_sift:    method = BDD_REORDER_SIFT;    break;
    case T_siftite: method = BDD_REORDER_SIFTITE; break;
    case T_none:    method = BDD_REORDER_NONE;    break;
    default:
       parseError("syntax error, expected reordering method but found %s",
		  tokenName(tok.kind));
       return 0;
   }

   advance();
   return method;
}


/*************************************************************************
   Expressions
*************************************************************************/

bdd Parser::parseExpr(void)
{
   return parseBiimp();
}


bdd Parser::parseBiimp(void)
{
   bdd left = parseImp();
   while (accept(T_biimp))
      left = bdd_apply(left, parseImp(), bddop_biimp);
   return left;
}


bdd Parser::parseImp(void)
{
   bdd left = parseOr();
   while (accept(T_imp))
      left = bdd_apply(left, parseOr(), bddop_imp);
   return left;
}


bdd Parser::parseOr(void)
{
   bdd left = parseXor();
   for (;;)
   {
      if (accept(T_or))
	 left = bdd_apply(left, parseXor(), bddop_or);
      else if (accept(T_nor))
	 left = bdd_apply(left, parseXor(), bddop_nor);
      else
	 return left;
   }
}


bdd Parser::parseXor(void)
{
   bdd left = parseAnd();
   while (accept(T_xor))
      left = bdd_apply(left, parseAnd(), bddop_xor);
   return left;
}


bdd Parser::parseAnd(void)
{
   bdd left = parseUnary();
   for (;;)
   {
      if (accept(T_and))
	 left = bdd_apply(left, parseUnary(), bddop_and);
      else if (accept(T_nand))
	 left = bdd_apply(left, parseUnary(), bddop_nand);
      else
	 return left;
   }
}


bdd Parser::parseUnary(void)
{
   if (accept(T_not))
      return bdd_not(parseUnary());
   return parsePrimary();
}


bdd Parser::parsePrimary(void)
{
   switch (tok.kind)
   {
    case T_lpar:
    {
       advance();
       bdd val = parseExpr();
       expect(T_rpar);
       return val;
    }

    case T_id:
       return actId(expectId());

    case T_true:
       advance();
       return bddtrue;

    case T_false:
       advance();
       return bddfalse;

    case T_exist:
    {
       advance();
       bdd vars = parseVarList();
       expect(T_dot);
       bdd body = parseExpr();
       return bdd_exist(body, vars);
    }

    case T_forall:
    {
       advance();
       bdd vars = parseVarList();
       expect(T_dot);
       bdd body = parseExpr();
       return bdd_forall(body, vars);
    }

    default:
       parseError("syntax error, unexpected %s in expression",
		  tokenName(tok.kind));
       return bddfalse;
   }
}


bdd Parser::parseVarList(void)
{
   bdd vars = actQuantVar(expectId());
   while (tok.kind == T_id)
      vars &= actQuantVar(expectId());
   return vars;
}


/*************************************************************************
   Semantic actions
*************************************************************************/

nodeData *Parser::lookup(const std::string &id)
{
   hashData hd;

   if (names.lookup(id.c_str(), hd) != 0)
      parseError("Unknown variable %s", id.c_str());

   return (nodeData*)hd.def;
}


void Parser::actInit(int nodes, int cache)
{
   bdd_init(nodes, cache);
   bdd_gbc_hook(gbcHandler);
   bdd_reorder_verbose(0);
}


void Parser::actInputs(void)
{
   bdd_setvarnum(inputs.size());

   int vnum=0;
   for (nodeLst::ite i=inputs.first() ; i.more() ; i++, vnum++)
   {
      if (names.exists((*i).name.c_str()))
	 parseError("Redefinition of input %s", (*i).name.c_str());

      (*i).val = bdd_ithvar(vnum);
      hashData hd((*i).name.c_str(), inputTag, &(*i));
      names.add(hd);
   }

   bdd_varblockall();
}


void Parser::actAddInput(const std::string &id)
{
   inputs.append( nodeData(inputTag, id, bddtrue) );
}


void Parser::actAssign(const std::string &id, const bdd &expr)
{
   if (names.exists(id.c_str()))
      parseError("Redefinition of %s", id.c_str());

   nodeData *d = new nodeData(exprTag, id, expr);
   hashData hd(d->name.c_str(), exprTag, d);
   names.add(hd);
}


bdd Parser::actId(const std::string &id)
{
   return lookup(id)->val;
}


bdd Parser::actQuantVar(const std::string &id)
{
   nodeData *d = lookup(id);

   if (d->tag != inputTag)
      parseError("%s is not a variable", id.c_str());

   return d->val;
}


void Parser::actSize(const std::string &id)
{
   using namespace std ;
   cout << "Number of nodes used for " << id << " = "
	<< bdd_nodecount(lookup(id)->val) << endl;
}


void Parser::actDot(const std::string &fname, const std::string &id)
{
   using namespace std ;
   std::string path = fname;
      /* Call the C API directly: the C++ wrapper bdd_fnprintdot(char*,
	 const bdd&) in bdd.h calls itself recursively. */
   if (bdd_fnprintdot(&path[0], lookup(id)->val.id(), printTerminal) < 0)
      cout << "Could not open file: " << fname << endl;
}


void Parser::actAutoreorder(int times, int method)
{
   if (times == 0)
      bdd_autoreorder(method);
   else
      bdd_autoreorder_times(method, times);
}


void Parser::actCache(void)
{
   bdd_printstat();
}


void Parser::actTautology(const std::string &id)
{
   using namespace std ;
   if (lookup(id)->val == bddtrue)
      cout << "Formula " << id << " is a tautology!\n";
   else
      cout << "Formula " << id << " is NOT a tautology!\n";
}


void Parser::actPrint(const std::string &id)
{
   using namespace std ;
   cout << id << " = " << bddset << lookup(id)->val << endl;
}


/*************************************************************************
   Main and more
*************************************************************************/

void usage(void)
{
   using namespace std ;
   cerr << "USAGE: bddcalc [-hg] file\n";
   cerr << " -h : print this message\n";
   cerr << " -g : disable garbage collection info\n";
}


int main(int ac, char **av)
{
   using namespace std ;
   int c;

   while ((c=getopt(ac, av, "hg")) != EOF)
   {
      switch (c)
      {
      case 'h':
	 usage();
	 break;
      case 'g':
	 gbcHandler = bdd_default_gbchandler;
	 break;
      }
   }

   if (optind >= ac)
      usage();

   FILE *in = fopen(av[optind],"r");
   if (!in)
   {
      cerr << "Could not open file: " << av[optind] << endl;
      exit(2);
   }

   linenum = 1;
   bdd_setcacheratio(2);

   {
      Parser parser(in);
      parser.parseCalc();
   }
   fclose(in);

   bdd_printstat();
   bdd_done();

   return 0;
}

/* EOF */
