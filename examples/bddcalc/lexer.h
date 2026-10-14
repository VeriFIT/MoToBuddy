/*************************************************************************
  FILE:  lexer.h
  DESCR: Hand-written tokenizer for the BDD calculator
  AUTH:  Jorn Lind (original FLEX rules), rewritten in plain C++
  DATE:  (C) may 1999
*************************************************************************/

#ifndef _LEXER_H
#define _LEXER_H

#include <cstdio>
#include <string>

enum TokenKind
{
   T_eof,

      /* Identifiers and constant values */
   T_id, T_str, T_intval, T_true, T_false,

      /* Keywords */
   T_initial, T_inputs, T_actions,
   T_size, T_dumpdot,
   T_autoreorder, T_reorder, T_win2, T_win2ite, T_sift, T_siftite, T_none,
   T_cache, T_tautology, T_print,
   T_exist, T_forall,

      /* Symbols and operators */
   T_lpar, T_rpar, T_equal, T_semi, T_dot,
   T_biimp, T_imp, T_or, T_nor, T_xor, T_nand, T_and, T_not
};

struct Token
{
   TokenKind   kind;
   std::string text;   /* Identifier name or string literal contents */
   int         ival;   /* Value of an integer literal */
};

class Lexer
{
public:
   explicit Lexer(FILE *in);
   Token next(void);

private:
   int  peek(void) const { return cur; }
   void advance(void);
   void skipBlockComment(void);

   FILE *in;
   int   cur;   /* Current (not yet consumed) character, or EOF */
};

const char *tokenName(TokenKind kind);

   /* Current line number, maintained by the lexer and used for errors */
extern int linenum;

   /* Print a parse error with the current line number and exit(3) */
void parseError(const char *fmt, ...);

#endif /* _LEXER_H */

/* EOF */
