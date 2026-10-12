#include "p.h"
struct HD2 : Base1, Base2 {
  int hd2;
  int f(int x) override { return x + 7; }
  int g(int x) override { return x + 70; }
  int h(int n, ...) override { va_list ap; va_start(ap, n); int s = 5000; for (int i = 0; i < n; ++i) s += va_arg(ap, int); va_end(ap); return s; }
  Q q(Q a, int k) override { return Q(a.q + k * 3); }
  P pp(P a, double d) override { return P(a.p + (int)d * 2); }
};
int main() {
  int s = run_all();
  printf("run_all %d\n", s);
  { HD2 h; Base2 *b2 = &h; Base1 *b1 = &h;
    printf("%d %d %d\n", b2->g(1), b1->f(1), b2->h(2, 10, 20));
    { Q r = b2->q(Q(1), 2); printf("%d\n", r.q); }
    printf("%d\n", b2->pp(P(2), 3.5).p); }
  { Der d; d.d = 9; Base2 *b2 = &d;
    printf("%d %d\n", b2->g(1), b2->h(1, 4)); Q r = b2->q(Q(4), 1); printf("q %d pp %d\n", r.q, b2->pp(P(1), 2.0).p); }
  { VD v; v.d = 1; v.l = 2; VB *p = &v; printf("%d\n", p->vf(5)); }
  { CVD c; CV1 *p = &c; printf("%d %d\n", p->get(1) != nullptr, p->get(0) == nullptr); }
  { CVVD c; CVV *p = &c; printf("%d\n", p->get()->id()); }
  return 0;
}
