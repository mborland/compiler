#include "common.h"
#include <cstdio>

struct I { int x; };

int main() {
  I i = {5};
  int caught = 0;
  try {
    throw_i(&i);
  } catch (I *q) {
    caught = q->x;
  }
  std::printf("caught I* %d\n", caught);
  std::printf("same typeid %d\n", typeid_of_i_pointer() == typeid(I *));
  return 0;
}
