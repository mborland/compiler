#include "p.h"
constexpr D gd_;
const D gd = D();
const M gm = M();
D g2;
int use() {
  const B *p = &gd;
  const B &q = gm.b;
  return p->f() * 10 + q.f();
}
