extern "C" int printf(const char *, ...);
#include <stdarg.h>
// Thunks: this-adjusting (non-virtual and virtual), covariant returns (with
// null check, virtual base), variadic (musttail), and targets whose
// parameters or result are NONTRIVIAL_ABI (indirect) records.
struct Q { int q; Q(int x) : q(x) {} Q(const Q &o) : q(o.q + 100) {} ~Q() { printf("~Q%d\n", q); } };  // non-trivial
struct P { int p; P(int x) : p(x) {} };    // trivial, passed in registers
struct Base1 { int b1; virtual int f(int); virtual ~Base1(); };
struct Base2 { int b2; virtual int g(int); virtual int h(int, ...); virtual Q q(Q a, int k); virtual P pp(P a, double d); virtual ~Base2(); };
struct Der : Base1, Base2 {
  int d;
  int g(int) override;           // thunk: non-virtual this adjustment (Base2 at 16)
  int h(int, ...) override;      // variadic thunk
  Q q(Q a, int k) override;      // by-value non-trivial param and result
  P pp(P a, double d) override;
  ~Der() override;
};
// virtual this adjustment through a virtual base
struct VB { int vb; virtual int vf(int); virtual ~VB(); };
struct VL : virtual VB { int l; int vf(int) override; ~VL() override; };
struct VR : virtual VB { int r; ~VR() override; };
struct VD : VL, VR { int d; int vf(int) override; ~VD() override; };   // thunk with virtual adjustment
// covariant
struct CR { int cr; virtual ~CR(); virtual int id(); };
struct CRD : CR { int crd; int id() override; };
struct CV1 { virtual ~CV1(); virtual CR *get(int); virtual CR &ref(); };
struct CV2 { int pad; virtual ~CV2(); };
struct CVD : CV2, CV1 { CRD obj; CRD *get(int) override; CRD &ref() override; ~CVD() override; };   // covariant with adjustment (CRD -> CR is a base at 0 here)
struct Mid : virtual CR { int m; int id() override; };    // CR virtual base of Mid
struct CVV { virtual ~CVV(); virtual CR *get(); };
struct CVVD : CVV { Mid m; Mid *get() override; ~CVVD() override; };   // covariant return through a virtual base
int run_all();
