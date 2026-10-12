// M4a probe: CLASS_MANGLE of TU-local classes: anonymous namespace, local
// class in a function (and in a template), unnamed class with a typedef name
// for linkage, lambda closure, nested in a class template.
#include <cstdio>
namespace { struct Anon { virtual void f() { std::puts("Anon"); } }; struct AnonD : Anon { void f() { std::puts("AnonD"); } }; }
namespace N { namespace { struct Deep { virtual ~Deep() {} }; } struct Pub { virtual void f() {} struct In { virtual void g() {} }; }; }
typedef struct { virtual void f() { std::puts("tn"); } } TN;           // typedef name for linkage
typedef struct { virtual void f() { std::puts("tn2"); } } *TNP;
struct { virtual void f() {} } anon_obj;                              // unnamed, no linkage
template <class T> struct Tm { virtual void f() { std::puts("Tm"); } struct In : Anon { }; };
template <class T> int local_in_template() { struct Loc { virtual int v() { return sizeof(T); } }; Loc l; return l.v(); }
int local() { struct Loc { virtual int v() { return 1; } }; Loc l; Loc &r = l; return r.v(); }
int local2() { struct Loc { virtual int v() { return 2; } }; Loc l; Loc &r = l; return r.v(); }
int main() {
  Anon a; AnonD ad; Anon &ar = ad; ar.f(); N::Deep dp; N::Pub p; N::Pub::In in; TN tn; tn.f(); Tm<int> t; t.f(); Tm<char>::In ti; anon_obj.f();
  std::printf("%d %d %d\n", local(), local2(), local_in_template<int>());
  auto lam = [](int) { struct InL { virtual int v() { return 9; } }; InL i; InL &r = i; return r.v(); };
  std::printf("%d\n", lam(0));
  return 0;
}
