//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// M4 slice S1: a polymorphic class without virtual bases.  The layout facts
// follow the record body; the vtable facts (CLASS_VFUNCS, CLASS_OVERRIDES, the
// destructors with STRUCTOR_DELETING) come at the first need; VPTR_INIT is in
// every constructor and destructor; calls through a pointer or a reference
// are CALL VIRTUAL, a delete with a virtual destructor is DELETE VIRTUAL; the
// key function (the destructor of B, f of D) decides the linkage of the
// vtable and the typeinfo, which come at the end of the translation unit.
extern "C" int printf(const char *, ...);
struct B {
  int x;
  B() : x(1) {}
  virtual ~B();
  virtual int f(int a);
  virtual int pure() = 0;
  int plain() { return x; }
};
B::~B() { printf("~B\n"); }
int B::f(int a) { return a + x; }
struct D : B {
  int y;
  D() : y(5) { f(1); }
  ~D() { f(2); }
  int f(int a) override { return a + y; }
  int pure() override { return 3; }
};
int through_pointer(B *p) { return p->f(1) + p->pure() + p->plain(); }
int through_reference(B &r) { return r.f(2); }
void destroy(B *p) { delete p; }
int object_calls() {
  D d;
  B &r = d;
  return d.f(1) + r.f(1);
}
struct E : D { int z; E() : z(0) {} int f(int a) override { return D::f(a); } };
int main() {
  E e;
  D *pd = new D;
  int n = through_pointer(pd) + through_reference(e) + object_calls();
  destroy(pd);
  return n == 0;
}
