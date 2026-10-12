#include "p.h"
#include <cstring>
struct HS : S1 { int h; };     // g++ class deriving from a CIR-compiled class: its typeinfo refers to CIR's
struct HVB : virtual VB { };
static void show(const char *n, const std::type_info &t) { printf("%s: %s %d\n", n, t.name(), (int)t.hash_code() != 0); }
int main() {
  int s = run_all();
  printf("run_all %d\n", s);
  show("int", ti_int()); show("int*", ti_ptr_int()); show("cint*", ti_ptr_cint()); show("pp", ti_ptr_ptr_vol());
  show("inc*", ti_ptr_inc()); show("s1*", ti_ptr_s1()); show("memptr", ti_memptr()); show("fnptr", ti_fnptr());
  show("arr", ti_arr()); show("enum", ti_enum()); show("np", ti_np()); show("m", ti_m()); show("vd", ti_vd()); show("pri", ti_pri());
  // identity of typeinfo objects across the two compilers
  printf("same: %d %d %d %d %d %d %d\n", &ti_int() == &typeid(int), ti_ptr_int() == typeid(int *), ti_ptr_cint() == typeid(const int *),
         ti_ptr_ptr_vol() == typeid(volatile char *const *), ti_ptr_inc() == typeid(Inc *), ti_ptr_s1() == typeid(S1 *), ti_memptr() == typeid(int W::*));
  printf("same2: %d %d %d %d %d %d %d\n", ti_fnptr() == typeid(int (*)(int, double)), ti_arr() == typeid(int[3]), ti_enum() == typeid(En),
         ti_np() == typeid(NP), ti_m() == typeid(M), ti_vd() == typeid(VD), ti_pri() == typeid(Pri));
  S0 *a = mk(1), *b = mk(2), *c = mk(0);
  printf("dyn: %d %d %d\n", ti_dyn(*a) == typeid(S1), ti_dyn(*b) == typeid(M), ti_dyn(*c) == typeid(S0));
  // g++ casts on CIR-built objects
  printf("cast: %d %d %d\n", dynamic_cast<S1 *>(a) != nullptr, dynamic_cast<M *>(b) != nullptr, dynamic_cast<S2 *>(b) != nullptr);
  S0 *hs = new HS; printf("hs: %d %d\n", dynamic_cast<S1 *>(hs) != nullptr, typeid(*hs) == typeid(HS));
  return 0;
}
