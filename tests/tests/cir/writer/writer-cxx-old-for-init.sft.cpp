//type:fp
//options:--c++17 --old_for_init
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The old scope rule: a for-init variable is used after the loop.  The init
// (the declaration, its initialization and its cleanup, or the temporary that
// a reference to a class with a destructor extends) is sent before FOR_BEGIN,
// in the block that contains the for statement, so the cleanup ends with that
// block.  The unbraced body of a case label or of an if is a scope region of
// its own.
struct A { A(int); ~A(); int v; };
void use(A &);
void use(const A &);
int f() { for (A a(3); a.v < 5;) { use(a); } use(a); return 0; }
int g() { int s = 0; for (int i = 0; i < 3; i++) s += i; return i + s; }
int k() { for (const A &r = A(3); r.v < 5;) { use(r); } return 0; }
int m() { for (const int &r = 3; r < 5;) { } return 0; }
int sw(int x) {
  switch (x)
    case 1:
      for (A a2(7); 1;)
        break;
  return 0;
}
int cond(int x) {
  if (x)
    for (A a(11); a.v < 12; a.v++) use(a);
  return 0;
}
int ret(int n) {
  for (A a(20 + n); a.v < 30; a.v++) {
    if (a.v == 22) return a.v;
  }
  return -1;
}
