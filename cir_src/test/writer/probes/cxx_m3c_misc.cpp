//options:--c++17
// A static array of aggregates with a run-time element, a delegation to a
// trivial constructor (it value-initializes: zeroes), a static member
// function selected from an expression with side effects, a class member with
// a destructor.
extern "C" int printf(const char *, ...);
extern "C" void *memset(void *, int, unsigned long);
inline void *operator new(unsigned long, void *p) noexcept { return p; }
struct C {
  int i;
  ~C() { printf("~C %d\n", i); }
};
void f(int k) { static C d[] = { {4}, {7}, {10}, {k} }; printf("f %d\n", d[3].i); }
struct test {
  int i, j;
  test() = default;
  test(int) : test() {}
};
struct A {
  A() { printf("A()\n"); }
  ~A() { printf("~A\n"); }
};
struct B {
  A a1;
  B() { printf("B body\n"); }
};
struct S {
  static int sf() { printf("sf\n"); return 7; }
};
struct P {
  S *operator->() { printf("operator->\n"); return &s; }
  S s;
};
int main() {
  f(22);
  f(23);
  alignas(test) unsigned char space[sizeof(test)];
  memset(space, 0xff, sizeof(space));
  test *t = new (&space) test(42);
  printf("test %d %d\n", t->i, t->j);
  {
    B b;
    printf("in scope\n");
  }
  P p;
  int v = p->sf();
  printf("v %d\n", v);
  return 0;
}
