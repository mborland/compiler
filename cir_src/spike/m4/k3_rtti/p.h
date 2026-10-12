#include <typeinfo>
extern "C" int printf(const char *, ...);
struct S0 { virtual ~S0(); int s0; };                    // class_type_info
struct S1 : S0 { int s1; };                              // si_class_type_info
struct S2 { virtual ~S2(); int s2; };
struct M : S1, S2 { int m; };                            // vmi (two bases)
struct VB { virtual ~VB(); int vb; };
struct VL : virtual VB { int l; };                       // vmi with a virtual base
struct VR : virtual VB { int r; };
struct VD : VL, VR { int d; };                           // diamond
struct Pri : private S0 { };                             // vmi, private base
struct NP { int x; };                                    // non-polymorphic
struct Inc;                                              // incomplete
enum En { E1, E2 };
struct W { int mem; int fn(); };                         // for member pointers
int run_all();
// typeinfo of various types, for the host to compare by address and name
const std::type_info &ti_int();
const std::type_info &ti_ptr_int();
const std::type_info &ti_ptr_cint();
const std::type_info &ti_ptr_ptr_vol();
const std::type_info &ti_ptr_inc();
const std::type_info &ti_ptr_s1();
const std::type_info &ti_memptr();
const std::type_info &ti_fnptr();
const std::type_info &ti_arr();
const std::type_info &ti_enum();
const std::type_info &ti_np();
const std::type_info &ti_m();
const std::type_info &ti_vd();
const std::type_info &ti_pri();
const std::type_info &ti_dyn(S0 &s);
S0 *mk(int which);
