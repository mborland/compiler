#include "p.h"
extern "C" int printf(const char *, ...);
int main() {
  D d; d.p = 1; d.q = 2; d.c = 3; d.r = 4; d.d = 5.5; d.own = 6;
  printf("run %ld\n", run(&d));
  for (int i = 0; i < 5; ++i) printf("%ld ", *(long *)&g_di[i]);
  printf("\n%ld %ld %ld\n", *(long *)&g_dl[0], *(long *)&g_dl[1], *(long *)&g_dd[0]);
  return 0;
}
