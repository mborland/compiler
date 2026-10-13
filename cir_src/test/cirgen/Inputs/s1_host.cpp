// The g++ side of cxx-s1-twotu.test: classes whose bases and virtual
// functions are compiled by the CIR back end (A, B, use, g_d), a class derived
// from A in this translation unit (H) and virtual dispatch both ways.
#include <typeinfo>

struct A { virtual int f(int); virtual ~A(); int x; A(); };
struct B : A { int f(int) override; ~B(); int y; B(); };
extern int g_d;
int use(A *p);

struct H : A {
  int h = 5;
  int f(int v) override { return v + 100; }
  ~H() override { g_d += 100; }
};

int main()
{
  g_d = 0;
  {
    B b;
    if (use(&b) != 12 || b.x != 1 || b.y != 2)
      return 1;
  }
  if (g_d != 2)
    return 2;
  g_d = 0;
  {
    H h;
    if (use(&h) != 110 || h.x != 1)
      return 3;
    if (dynamic_cast<A *>(&h) != &h)
      return 4;
  }
  if (g_d != 101)
    return 5;
  A *q = new H;
  if (q->f(1) != 101)
    return 6;
  g_d = 0;
  delete q;
  if (g_d != 101)
    return 7;
  q = new B;
  g_d = 0;
  delete q;
  if (g_d != 2)
    return 8;
  B b2;
  A &r = b2;
  if (typeid(r) != typeid(B) || typeid(r).name()[0] != '1')
    return 9;
  if (dynamic_cast<B *>(&r) != &b2)
    return 10;
  H h2;
  A &s = h2;
  if (dynamic_cast<B *>(&s) != nullptr)
    return 11;
  return 0;
}
