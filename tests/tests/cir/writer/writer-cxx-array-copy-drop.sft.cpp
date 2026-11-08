//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The copy or move constructor of a class with an array member of class
// objects (non-trivial elements) copies the elements one by one with the
// source element implied.  The writer does not express that loop (it would
// call the element constructor without the source), so each such function is
// dropped with a reason: the implicit constructors, the defaulted ones, and a
// nested array.  Arrays of trivially copyable elements, and the assignment
// operators of the same classes, are translated (no warning).
struct E {
  int v;
  E() : v(0) {}
  E(const E &o) : v(o.v + 1) {}
  E(E &&o) : v(o.v + 10) {}
  E &operator=(const E &o) { v = o.v + 100; return *this; }
};
struct Implicit { E items[2]; };
struct Defaulted {
  E items[2];
  Defaulted() {}
  Defaulted(const Defaulted &) = default;
};
struct Nested { int n; E items[2][2]; };
struct P { int x, y; };
struct Plain { P ps[3]; int n[4]; };
Implicit copy_implicit(const Implicit &b) { return b; }
Implicit move_implicit(Implicit &b) { return static_cast<Implicit &&>(b); }
Defaulted copy_defaulted(const Defaulted &b) { return b; }
Nested copy_nested(const Nested &b) { return b; }
Plain copy_plain(const Plain &b) { return b; }
void assign_implicit(Implicit &a, const Implicit &b) { a = b; }
