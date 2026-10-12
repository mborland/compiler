#include "p.h"
extern "C" int printf(const char *, ...);
int main() {
  for (unsigned i = 0; i < ncases; ++i) {
    unsigned char buf[512] = {0};
    cases[i].fn(buf);
    printf("%-6s", cases[i].name);
    for (unsigned k = 0; k < cases[i].size; k += 8) {
      long v = 0;
      for (int b = 7; b >= 0; --b) v = (v << 8) | buf[k + b];
      printf(" %ld", v);
    }
    printf("\n");
  }
  return 0;
}
