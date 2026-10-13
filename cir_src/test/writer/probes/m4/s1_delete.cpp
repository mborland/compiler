//options:--c++17
// (the S1 writer test of the same name in tests/tests/cir/writer is this program)
// M4 slice S1: the deleting destructor.  A virtual destructor has a D0 with
// its own DECL_FUNC and a STRUCTOR_DELETING naming the class's operator delete
// (unsized, sized or aligned: the flags match the parameter count of it); D0 is
// defined iff D1 is; delete through a base pointer is DELETE VIRTUAL.
extern "C" void *malloc(unsigned long);
extern "C" void free(void *);
struct G { virtual ~G() {} int a; };
struct S : G {
  ~S() override {}
  static void operator delete(void *p, unsigned long) { free(p); }
};
struct U : G {
  static void operator delete(void *p) { free(p); }
};
struct alignas(32) AL : G { int b; };
struct Decl { virtual ~Decl(); };
void del_g(G *p) { delete p; }
void del_s(S *p) { delete p; }
void del_u(U *p) { delete p; }
void del_al(AL *p) { delete p; }
void del_decl(Decl *p) { delete p; }
void del_array(G *p) { delete[] p; }
void make() {
  G *g = new G;
  S *s = new S;
  AL *al = new AL;
  del_g(g);
  del_s(s);
  del_al(al);
}
int main() { make(); return 0; }
