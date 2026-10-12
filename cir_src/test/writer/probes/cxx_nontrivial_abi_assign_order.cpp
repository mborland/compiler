//options:--c++17
// C++17: a built-in assignment of a class that is not trivially copyable (a
// trivial copy assignment with a destructor or a copy constructor) evaluates
// the right operand first; its temporary is destroyed at the end of the full
// expression, after the store.  (`*p = next(&p)` is in the golden only: g++
// stores through the old p.)
extern "C" int printf(const char *, ...);
struct T { int v; ~T() { printf("~T(%d)\n", v); } };
T tarr[4];
int idx;
T *nxt() { printf("nxt %d\n", idx); return &tarr[idx++]; }
T make(int v) { printf("make %d\n", v); return T{ v }; }
struct R {
  long long r1, r2;
  R();
  R(const R &r) { printf("R copy\n"); r1 = r.r1; r2 = r.r2; }
};
R::R() { r1 = 5; r2 = 6; }
struct M {
  M(R m);
  R val;
};
M::M(R m) { val = m; }
R *rp;
R &getr() { printf("getr\n"); return *rp; }
int main() {
  *nxt() = make(7);
  printf("after1 %d %d\n", tarr[0].v, idx);
  tarr[nxt() - tarr] = make(8);
  printf("after2 %d %d %d\n", tarr[0].v, tarr[1].v, idx);
  T t0{ 3 };
  T *p = &t0;
  *p = make(9);
  printf("after3 %d\n", t0.v);
  M m(R{});
  printf("m %lld %lld\n", m.val.r1, m.val.r2);
  R rr;
  rr.r1 = 11;
  rp = &rr;
  getr() = getr();
  printf("end\n");
  return 0;
}
