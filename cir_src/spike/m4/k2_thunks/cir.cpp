#include "p.h"
int Base1::f(int x) { return x + 1; }
Base1::~Base1() {}
int Base2::g(int x) { return x + 2; }
int Base2::h(int n, ...) { va_list ap; va_start(ap, n); int s = n; for (int i = 0; i < n; ++i) s += va_arg(ap, int); va_end(ap); return s; }
Q Base2::q(Q a, int k) { return Q(a.q + k); }
P Base2::pp(P a, double d) { return P(a.p + (int)d); }
Base2::~Base2() {}
int Der::g(int x) { return x + d; }
int Der::h(int n, ...) { va_list ap; va_start(ap, n); int s = 1000; for (int i = 0; i < n; ++i) s += va_arg(ap, int); va_end(ap); return s; }
Q Der::q(Q a, int k) { return Q(a.q * 2 + k + d); }
P Der::pp(P a, double dd) { return P(a.p * 3 + (int)dd + d); }
Der::~Der() {}
int VB::vf(int x) { return x; }
VB::~VB() {}
int VL::vf(int x) { return x + l; }
VL::~VL() {}
VR::~VR() {}
int VD::vf(int x) { return x + 1000 + d; }
VD::~VD() {}
CR::~CR() {}
int CR::id() { return 7; }
int CRD::id() { return 8; }
CV1::~CV1() {}
CR *CV1::get(int) { return nullptr; }
CR &CV1::ref() { static CR c; return c; }
CV2::~CV2() {}
CRD *CVD::get(int k) { return k ? &obj : nullptr; }
CRD &CVD::ref() { return obj; }
CVD::~CVD() {}
int Mid::id() { return 9; }
CVV::~CVV() {}
CR *CVV::get() { return nullptr; }
Mid *CVVD::get() { return &m; }
CVVD::~CVVD() {}
int run_all() {
  int s = 0;
  { Der d; d.d = 5; Base2 *b2 = &d; Base1 *b1 = &d;
    s += b2->g(1) + b1->f(1) + b2->h(3, 1, 2, 3);
    { Q r = b2->q(Q(1), 2); s += r.q; }
    s += b2->pp(P(2), 3.5).p; }
  { VD v; v.d = 3; v.l = 4; VB *p = &v; VR *r = &v; s += p->vf(1); }
  { CVD c; CV1 *p = &c; s += p->get(1)->id() + (p->get(0) == nullptr) + p->ref().id(); }
  { CVVD c; CVV *p = &c; s += p->get()->id(); }
  return s;
}
