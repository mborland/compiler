#include "common.h"
#include <cstdio>

int a_next();

int main() {
  int r = util::bump(3) + util::bump(2L);
  Registry reg;
  reg.id = Registry::add(4) + Registry::add(6);
  // next_id() has one static in both TUs (a COMDAT/guard-free inline).
  int n1 = a_next();
  int n2 = next_id();
  int n3 = a_next();
  std::printf("%s %d %d %d %d %d %d\n", util::name(), r, util::counter,
              reg.id, n1, n2, n3);
  return r == 20 ? 0 : 1;
}
