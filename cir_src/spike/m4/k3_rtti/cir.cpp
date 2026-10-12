#include "p.h"
S0::~S0() {}
S2::~S2() {}
VB::~VB() {}
int W::fn() { return 1; }
const std::type_info &ti_int() { return typeid(int); }
const std::type_info &ti_ptr_int() { return typeid(int *); }
const std::type_info &ti_ptr_cint() { return typeid(const int *); }
const std::type_info &ti_ptr_ptr_vol() { return typeid(volatile char *const *); }
const std::type_info &ti_ptr_inc() { return typeid(Inc *); }
const std::type_info &ti_ptr_s1() { return typeid(S1 *); }
const std::type_info &ti_memptr() { return typeid(int W::*); }
const std::type_info &ti_fnptr() { return typeid(int (*)(int, double)); }
const std::type_info &ti_arr() { return typeid(int[3]); }
const std::type_info &ti_enum() { return typeid(En); }
const std::type_info &ti_np() { return typeid(NP); }
const std::type_info &ti_m() { return typeid(M); }
const std::type_info &ti_vd() { return typeid(VD); }
const std::type_info &ti_pri() { return typeid(Pri); }
const std::type_info &ti_dyn(S0 &s) { return typeid(s); }
S0 *mk(int which) {
  switch (which) {
  case 0: return new S0;
  case 1: return new S1;
  case 2: return new M;
  default: return nullptr;
  }
}
int run_all() {
  int s = 0;
  S0 *p0 = new S1; M *pm = new M; S0 *pm0 = pm; S2 *pm2 = pm;
  VD *vd = new VD; VB *vb = vd; VL *vl = vd; VR *vr = vd;
  s += dynamic_cast<S1 *>(p0) != nullptr;            // 1
  s += dynamic_cast<M *>(p0) == nullptr;             // 1: S1 object is not an M
  s += dynamic_cast<M *>(pm2) == pm;                 // cross cast from second base: 1
  s += dynamic_cast<S2 *>(pm0) == pm2;               // sibling cross cast: 1
  s += dynamic_cast<void *>(pm2) == (void *)pm;      // most derived: 1
  s += dynamic_cast<VD *>(vb) == vd;                 // downcast from virtual base: 1
  s += dynamic_cast<VR *>(vl) == vr;                 // cross cast through virtual bases: 1
  s += dynamic_cast<VL &>(*vb).l == vd->l;           // reference form
  s += dynamic_cast<void *>(vb) == (void *)vd;
  int caught = 0;
  try { (void)dynamic_cast<M &>(*p0); } catch (const std::bad_cast &) { caught = 1; }
  s += caught;                                       // bad_cast
  S0 *np = nullptr; caught = 0;
  try { (void)typeid(*np); } catch (const std::bad_typeid &) { caught = 1; }
  s += caught;                                       // bad_typeid
  s += typeid(*p0) == typeid(S1);
  s += typeid(*pm2) == typeid(M);
  s += typeid(*vb) == typeid(VD);
  s += typeid(S0) != typeid(S1);
  return s;
}
