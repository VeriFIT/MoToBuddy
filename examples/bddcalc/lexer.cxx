/*************************************************************************
  FILE:  lexer.cxx
  DESCR: Hand-written tokenizer for the BDD calculator
  AUTH:  Jorn Lind (original FLEX rules), rewritten in plain C++
  DATE:  (C) may 1999
*************************************************************************/

#include <cctype>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include "lexer.h"

int linenum = 1;


void parseError(const char *fmt, ...)
{
   va_list argp;
   va_start(argp,fmt);
   fprintf(stderr, "Parse error in (or before) line %d: ", linenum);
   vfprintf(stderr, fmt, argp);
   fputc('\n', stderr);
   va_end(argp);
   exit(3);
}


/*************************************************************************
   Keyword table
*************************************************************************/

struct Keyword
{
   const char *name;
   TokenKind   kind;
};

   /* Keywords take precedence over identifiers of the same spelling,
      exactly as the FLEX rules did. */
static const Keyword keywords[] =
{
   { "initial",     T_initial },
   { "inputs",      T_inputs },
   { "actions",     T_actions },
   { "exist",       T_exist },
   { "forall",      T_forall },
   { "size",        T_size },
   { "dot",         T_dumpdot },
   { "autoreorder", T_autoreorder },
   { "reorder",     T_reorder },
   { "win2",        T_win2 },
   { "win2ite",     T_win2ite },
   { "sift",        T_sift },
   { "siftite",     T_siftite },
   { "none",        T_none },
   { "cache",       T_cache },
   { "tautology",   T_tautology },
   { "true",        T_true },
   { "false",       T_false },
   { "print",       T_print },

      /* Word forms of the operators */
   { "biimp",       T_biimp },
   { "imp",         T_imp },
   { "or",          T_or },
   { "xor",         T_xor },
   { "and",         T_and },
   { "not",         T_not },
   { "nand",        T_nand },
   { "nor",         T_nor },
};


const char *tokenName(TokenKind kind)
{
   switch (kind)
   {
    case T_eof:     return "end of file";
    case T_id:      return "identifier";
    case T_str:     return "string";
    case T_intval:  return "integer";
    case T_lpar:    return "'('";
    case T_rpar:    return "')'";
    case T_equal:   return "'='";
    case T_semi:    return "';'";
    case T_dot:     return "'.'";
    case T_biimp:   return "'<>'";
    case T_imp:     return "'=>'";
    case T_or:      return "'|'";
    case T_nor:     return "'nor'";
    case T_xor:     return "'^'";
    case T_nand:    return "'nand'";
    case T_and:     return "'&'";
    case T_not:     return "'~'";
    default:
       for (const Keyword &k : keywords)
	  if (k.kind == kind)
	     return k.name;
       return "token";
   }
}


/*************************************************************************
   Lexer
*************************************************************************/

Lexer::Lexer(FILE *f) : in(f)
{
   cur = fgetc(in);
}


void Lexer::advance(void)
{
   if (cur == '\n')
      linenum++;
   cur = fgetc(in);
}


   /* Called after the opening "/*" has been consumed. An unterminated
      comment simply runs to end of file, as with the FLEX rules. */
void Lexer::skipBlockComment(void)
{
   while (cur != EOF)
   {
      if (cur == '*')
      {
	 advance();
	 if (cur == '/')
	 {
	    advance();
	    return;
	 }
      }
      else
	 advance();
   }
}


Token Lexer::next(void)
{
   Token tok;
   tok.ival = 0;

   for (;;)
   {
      int c = peek();

      if (c == EOF)
      {
	 tok.kind = T_eof;
	 return tok;
      }

	 /* Whitespace */
      if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\n')
      {
	 advance();
	 continue;
      }

	 /* Comments */
      if (c == '/')
      {
	 advance();
	 if (peek() == '/')
	 {
	    while (peek() != EOF && peek() != '\n')
	       advance();
	    continue;
	 }
	 if (peek() == '*')
	 {
	    advance();
	    skipBlockComment();
	    continue;
	 }
	 parseError("Unknown symbol");
      }

	 /* Keywords and identifiers */
      if (isalpha(c) || c == '_')
      {
	 tok.text.clear();
	 while (isalnum(peek()) || peek() == '_')
	 {
	    tok.text += (char)peek();
	    advance();
	 }
	 for (const Keyword &k : keywords)
	 {
	    if (tok.text == k.name)
	    {
	       tok.kind = k.kind;
	       return tok;
	    }
	 }
	 tok.kind = T_id;
	 return tok;
      }

	 /* Integer constants */
      if (isdigit(c))
      {
	 tok.text.clear();
	 while (isdigit(peek()))
	 {
	    tok.text += (char)peek();
	    advance();
	 }
	 tok.kind = T_intval;
	 tok.ival = atol(tok.text.c_str());
	 return tok;
      }

	 /* Strings: no escapes, everything up to the closing quote */
      if (c == '"')
      {
	 advance();
	 tok.text.clear();
	 while (peek() != EOF && peek() != '"')
	 {
	    tok.text += (char)peek();
	    advance();
	 }
	 if (peek() != '"')
	    parseError("Unterminated string");
	 advance();
	 tok.kind = T_str;
	 return tok;
      }

	 /* Symbols and operators */
      advance();
      switch (c)
      {
       case '.': tok.kind = T_dot;   return tok;
       case '(': tok.kind = T_lpar;  return tok;
       case ')': tok.kind = T_rpar;  return tok;
       case ';': tok.kind = T_semi;  return tok;
       case '|': tok.kind = T_or;    return tok;
       case '^': tok.kind = T_xor;   return tok;
       case '&': tok.kind = T_and;   return tok;
       case '~': tok.kind = T_not;   return tok;
       case '=':
	  if (peek() == '>')
	  {
	     advance();
	     tok.kind = T_imp;
	  }
	  else
	     tok.kind = T_equal;
	  return tok;
       case '<':
	  if (peek() == '>')
	  {
	     advance();
	     tok.kind = T_biimp;
	     return tok;
	  }
	  break;
      }

      parseError("Unknown symbol");
   }
}

/* EOF */
