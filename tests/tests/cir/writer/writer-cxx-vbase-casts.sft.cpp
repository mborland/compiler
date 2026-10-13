//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// M4 slice S2: conversions and member access through virtual bases.  An
// upcast to a virtual base is VBASE_CAST (NONNULL for a reference or this, with
// a null check for a pointer that may be null; COMPLETE where the object is the
// complete object being constructed), a path through a virtual base is
// VBASE_CAST and then BASE_CASTs (X is a non-virtual base of V), the members of
// a virtual base are reached through the cast.
extern "C" int printf(const char *, ...);
struct X { int x; };
struct V : X { int v; virtual int g() { return v; } };
struct A : virtual V { int a; A() { v = 1; x = 2; a = 3; } };
struct B : virtual V { int b; B() { v = 4; b = 5; } };
struct C : A, B { int c; C() { v = 6; x = c = 7; } int g() override { return c; } };
struct E : C { int e; };
int by_pointer(C *c, A *a) {
  V *pv = c;
  X *px = c;
  V *pa = a;
  return pv->v + px->x + pa->v + a->v + c->x + c->A::a;
}
int by_reference(C &c, B &b) {
  V &rv = c;
  X &rx = b;
  return rv.v + rx.x + b.v + c.g();
}
int null_pointer() {
  C *none = 0;
  V *pv = none;
  return pv == 0;
}
int main() {
  C c;
  E e;
  return by_pointer(&c, &e) + by_reference(c, e) + null_pointer();
}
