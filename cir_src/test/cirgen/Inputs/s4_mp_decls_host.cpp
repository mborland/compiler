// Host of cxx-s4-member-pointer-decls.test: defines the member pointers the CIR
// unit only declares (the class Z is incomplete there) and checks the pointer
// to an empty [[no_unique_address]] member that the CIR unit defines.
struct Z { int pad; int f(); };
int Z::f() { return 7 + pad; }
Z zz = {35, };
Z *p = &zz;
int (Z::*m)() = &Z::f;
struct D { int a; int b; };
int D::*dm = &D::b;
struct E {};
struct M { [[no_unique_address]] E e; int f; };
extern E M::*g_pe;
extern "C" int c_callm();
extern "C" int c_getb(D *);
int main() {
  D d = {1, 2};
  if (c_callm() != 42) return 1;
  if (c_getb(&d) != 2) return 2;
  return g_pe == &M::e ? 0 : 3;
}
