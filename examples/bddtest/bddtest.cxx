#include <iomanip>
#include <stdlib.h>
#include <bdd.h>

static const int varnum = 5;

/**************************************************************************
  Example of allsat print handler.
**************************************************************************/

void allsatHandlerPrint(char* varset, int size)
{
  using namespace std ;
  for (int v=0; v<size ; ++v)
  {
    cout << (varset[v] < 0 ? 'X' : (char)('0' + varset[v]));
  }
  cout << endl;
}


/**************************************************************************
  allsat handler for checking that all assignments are detected.
**************************************************************************/

static bdd allsatBDD;
static bdd allsatSumBDD;

void allsatHandler(char* varset, int size)
{
  bdd x = bddtrue;
  for (int v=0 ; v<size ; ++v)
    if (varset[v] == 0)
      x &= bdd_nithvar(v);
    else if (varset[v] == 1)
      x &= bdd_ithvar(v);

  // Sum up all assignments
  allsatSumBDD |= x;

  // Remove assignment from initial set
  allsatBDD -= x;
}


void test1_check(bdd x)
{
  using namespace std ;
  double anum = bdd_satcount(x);
  
  cout << "Checking bdd with " << setw(4) << anum << " assignments: ";
  
  allsatBDD = x;
  allsatSumBDD = bddfalse;

  // Calculate whole set of asignments and remove all assignments
  // from original set
  bdd_allsat(x, allsatHandler);

  // Now the summed set should be equal to the original set
  if (allsatSumBDD == x)
    cout << "  Sum-OK. ";
  else
    cout << "  Sum-ERROR. ";

  // The subtracted set should be empty
  if (allsatBDD == bddfalse)
    cout << "Sub-OK.\n";
  else
    cout << "Sub-ERROR.\n";
}


/* A private generator, so that the sequence of random sets does not shift
 * whenever the library itself consumes rand() (bdd_prime_gte uses rand() in
 * its primality test, and the number of such calls depends on how many
 * caches the kernel allocates). */
static unsigned long random_state = 12345;

static int next_random()
{
  random_state = random_state * 1103515245UL + 12345UL;
  return (int)((random_state / 65536UL) % 32768UL);
}


void test1()
{
  bdd a = bdd_ithvar(0);
  bdd b = bdd_ithvar(1);
  bdd c = bdd_ithvar(2);
  bdd d = bdd_ithvar(3);
  bdd x;

  x = bddtrue;
  test1_check(x);

  x = bddfalse;
  test1_check(x);

  x = a & b | !a & !b;
  test1_check(x);

  x = a & b | c & d;
  test1_check(x);

  x = a & !b | a & !d | a & b & !c;
  test1_check(x);

  int i;
  for (i=0 ; i<varnum ; ++i)
  {
    test1_check(bdd_ithvar(i));
    test1_check(bdd_nithvar(i));
  }

  bdd set = bddtrue;
  for (i=0 ; i<50 ; ++i)
  {
    int v = next_random() % varnum;
    int s = next_random() % 2;
    int o = next_random() % 2;
    
    if (o == 0)
    {
      if (s == 0)
	set &= bdd_ithvar(v);
      else
	set &= bdd_nithvar(v);
    }
    else
    {
      if (s == 0)
	set |= bdd_ithvar(v);
      else
	set |= bdd_nithvar(v);
    }

    test1_check(set);
  }
}


/**************************************************************************
  Main
**************************************************************************/

int main()
{
  bdd_init(1000,1000);
  bdd_setvarnum(varnum);
  
  test1();
  
  bdd_done();
  
  return 0;
}
