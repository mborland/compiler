// Special names of the vtable machinery: namespaces, templates, anonymous
// namespace, covariant and virtual-base thunks, deleting destructors.  Checked
// against clang by cir_src/lib/abi/tools/compare_names_with_clang.py.
namespace N {
struct V { virtual void f() {} virtual ~V() {} int v; };
struct A : virtual V { int a; };
struct B : virtual V { int b; };
struct C : A, B { };
template <class T> struct TA : virtual V { T t; };
template <class T> struct TB : virtual V { T t; };
template <class T> struct TC : TA<T>, TB<T> { };
struct D : TA<int>, TB<char> { };
}
struct G : virtual N::V { };
struct H : G, N::A { };
namespace {
struct An : virtual N::V { };
struct Bn : An, N::B { };
}
struct X1 { virtual void f() {} int x; };
struct X2 { virtual void g() {} virtual void h() {} };
struct Y : X1, X2 { void g() override {} void h() override {} };
struct Cov1 { virtual Cov1 *c() { return 0; } virtual ~Cov1() {} };
struct Cov2 { virtual Cov2 *d() { return 0; } virtual ~Cov2() {} };
struct Cov3 : Cov1, Cov2 {
  Cov3 *c() override { return 0; }
  Cov3 *d() override { return 0; }
  ~Cov3() override {}
};
struct VB { virtual ~VB() {} };
struct VD : virtual VB { };
struct P { virtual VB *m() { return 0; } virtual ~P() {} };
struct Q : P { VD *m() override { return 0; } };

void use() {
  new N::C; new N::TC<int>; new N::D; new H; new Bn;
  new Y; new Cov3; new Q;
}
