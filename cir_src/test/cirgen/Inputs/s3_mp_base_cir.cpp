#include "s3_mp_base_p.h"
DI g_di[5] = { &B1::p, &B2::r, &D::own, nullptr, &B2::c == nullptr ? nullptr : &B1::p };
DL g_dl[2] = { &B1::q, nullptr };
DD g_dd[1] = { &B2::d };
long run(D *d) {
  DI a = &B1::p;       // local constants
  DI b = &B2::r;
  DL q = &B1::q;
  DD f = &B2::d;
  long s = 0;
  s += d->*a + d->*b + d->*(&D::own) + (long)(d->*q) + (long)(d->*f);
  s += d->*g_di[0] + d->*g_di[1] + d->*g_di[2] + d->*g_dl[0] + (long)(d->*g_dd[0]);
  s += (g_di[3] == nullptr) + (g_dl[1] == nullptr);
  s += (a == &B1::p) + (b != a);
  return s;
}
