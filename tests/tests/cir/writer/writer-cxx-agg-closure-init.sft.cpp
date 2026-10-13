//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// An aggregate whose first element is constant and whose second is a closure
// call: the constant level is skipped at the level of the closure's own
// aggregate, never in the closure's.
struct S { int a; int b; };
int f() {
  S s = {1, [n = 5] { return n; }()};
  return s.a + s.b;
}
int main() { return f(); }
