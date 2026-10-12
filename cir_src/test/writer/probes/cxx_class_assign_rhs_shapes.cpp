//options:--c++17
// C++17: the right operand of an assignment is evaluated before the left
// one; for a class the (trivial) assignment reads the right operand's object
// when it is called, after the target is designated.  The shapes of the
// right operand: a reference parameter, a call result, a cv-adjusted lvalue.
// (A conditional, a comma expression and a nested assignment are diagnosed:
// tests/cir/writer/writer-cxx-class-assign-rhs-unsupported.)
extern "C" int printf(const char *, ...);
struct T { int a, b; };
T store[4];
T g_a = { 1, 2 }, g_b = { 3, 4 }, obj = { 5, 6 };
T *slot() { g_a.a = 99; return &store[0]; }
void set(const T &v) { *slot() = v; }
T *getp2() { obj.a = 77; return &store[2]; }
T mk() { return T{ 8, 9 }; }
void callr() { *getp2() = mk(); }
void desig() { *getp2() = obj; }
T arr[2] = { { 11, 12 }, { 13, 14 } };
T *getp5() { arr[1].a = 55; return &store[1]; }
void elem(int i) { *getp5() = arr[i]; }
T &getr() { return obj; }
void refcall() { *getp2() = getr(); }
T lits[3];
void lit() { T *p = lits; *p++ = T{ 1, 2 }; int i = 1; lits[i++] = T{ 5, 6 }; lits[i++] = T{ 7, 8 }; }
const T ct = { 40, 41 };
// An assignment that keeps a glvalue (its right operand is a temporary) inside
// the right operand of one that keeps an object's address: the outer
// suppression must survive.
struct T2 { int a, b; constexpr T2(int x = 0) : a(x), b(x) {} };
T2 store2[2];
T2 arr3[2] = { T2(70), T2(71) };
T2 *g2_a() { store2[0].a = -1; return &store2[0]; }
T2 *g2_b() { arr3[0].a = 33; return &store2[1]; }
void inner_glvalue() { *g2_a() = arr3[(*g2_b() = T2(9), 0)]; }
T *getp4() { store[0].a = -1; return &store[0]; }
void cv() { *getp4() = ct; }
void show(const char *n, const T &t) { printf("%s %d %d\n", n, t.a, t.b); }
int main() {
  set(g_a); show("set", store[0]);
  callr(); show("call", store[2]);
  obj.a = 5; refcall(); show("refcall", store[2]);
  lit(); show("lit0", lits[0]); show("lit1", lits[1]); show("lit2", lits[2]);
  desig(); show("desig", store[2]);
  elem(1); show("elem", store[1]);
  cv(); show("cv", store[0]);
  inner_glvalue(); printf("inner %d %d %d %d\n", store2[0].a, store2[0].b, store2[1].a, store2[1].b);
  return 0;
}
