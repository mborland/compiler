#include "common.h"

clone_base const *make_b(int c) {
  my_error e(c + 100);
  return new clone_impl<my_error>(e);
}
int twice_clone(clone_base const *p) {
  clone_base const *q = p->clone();
  int r = q->get();
  delete q;
  return r;
}
