#include "common.h"

int A1::f() { return 1; }
R2 *A2::get() {
  static R2 r2_object;
  return &r2_object;
}
int C::f() { return 100; }
A2 *make_c() { return new C; }
A1 *make_c_as_a1() { return new C; }
int through_a2(A2 *p) { return p->get()->b; }
bool subobject_ok(A2 *p) {
  C *c = static_cast<C *>(p);
  return static_cast<R2 *>(&c->object) == p->get();
}
int call_f(A1 *p) { return p->f(); }
