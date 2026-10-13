#include "s3_mp_spike_p.h"
int B1::vf(int x) { return x + 1; }
int B1::nf(int x) { return x + 2; }
int B1::sf(int x) { return x + 3; }
B1::~B1() {}
int B2::g(int x) { return x + 4; }
int B2::ng(int x) { return x + 5; }
B2::~B2() {}
int D::vf(int x) { return x * 10 + d; }
int D::g(int x) { return x * 100 + d; }
int D::dn(int x) { return x + 6 + d; }
D::~D() {}
int apply(D *d, DMethod m, int x) { return (d->*m)(x); }
DMethod pick(int w) {
  switch (w) {
  case 0: return &D::vf;       // virtual, in primary base slot
  case 1: return &D::g;        // virtual, in secondary base (needs adjustment)
  case 2: return &D::dn;       // non-virtual of D
  case 3: return &B1::nf;      // non-virtual of base 1 (adjustment 0)
  case 4: return &B2::ng;      // non-virtual of base 2 (adjustment 16)
  case 5: return &B2::g;       // virtual of base 2
  default: return nullptr;
  }
}
DMethod from_base2(B2Method m) { return static_cast<DMethod>(m); }
int read(D *d, DData m) { return d->*m; }
DData data_pick(int w) {
  switch (w) {
  case 0: return &D::b1;       // via B1
  case 1: return &D::b2;       // via B2 (offset adjusted)
  case 2: return &D::d;
  default: return nullptr;
  }
}
DData from_base_data(B2Data m) { return static_cast<DData>(m); }
DMethod g_methods[6] = { &D::vf, &D::g, &D::dn, &B1::nf, &B2::ng, nullptr };
DData g_datas[4] = { &D::b1, &D::b2, &D::d, nullptr };
B2Method g_bmethods[3] = { &B2::g, &B2::ng, nullptr };
bool is_null(DMethod m) { return !m; }
bool same(DMethod a, DMethod b) { return a == b; }
bool same_data(DData a, DData b) { return a == b; }
int run_all() {
  D d; d.b1 = 1; d.b2 = 2; d.d = 3;
  int s = 0;
  for (int w = 0; w < 7; ++w) { DMethod m = pick(w); if (m) s += apply(&d, m, 1); else s += 1000; }
  for (int w = 0; w < 4; ++w) { DData m = data_pick(w); if (m) s += read(&d, m); else s += 2000; }
  for (int i = 0; i < 5; ++i) s += apply(&d, g_methods[i], 2);
  for (int i = 0; i < 3; ++i) s += read(&d, g_datas[i]);
  B2 *pb2 = &d;
  for (int i = 0; i < 2; ++i) s += (pb2->*g_bmethods[i])(3);
  s += apply(&d, from_base2(&B2::g), 4);
  s += apply(&d, from_base2(&B2::ng), 4);
  s += read(&d, from_base_data(&B2::b2));
  s += is_null(nullptr) + !is_null(&D::vf) + is_null(pick(9));
  s += same(pick(0), &D::vf) + !same(pick(0), pick(1)) + same(pick(9), nullptr);
  s += same_data(data_pick(1), &D::b2) + !same_data(data_pick(1), data_pick(2));
  return s;
}
