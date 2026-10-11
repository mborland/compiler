//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
struct A {
  int x;
  A(const A &o);
  int get() const { return x; }
};
A mk(int v);
void take(A a);
int user(int v) {
  A a = mk(v);
  take(mk(v));
  mk(v);
  return a.get() + mk(v + 1).get();
}
A nrvo(int v) { A a = mk(v); a.x = 3; return a; }
A two(int v) { if (v) return mk(v); return mk(0); }
void take2(A a) { }
int takes_get(A a) { return a.get(); }
struct NC { int x; int y; NC(const NC &) = delete; int sum() const { return x + y; } };
NC pickn(int v);
int member_of_call(int v) { return pickn(v).y; }
int method_of_call(int v) { return pickn(v).sum(); }
