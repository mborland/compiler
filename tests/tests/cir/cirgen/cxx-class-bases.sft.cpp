//type:rp
//options:--c++20
//require:BACK_END_TYPE 3
// Classes with bases: a non-POD base whose tail padding the derived class
// reuses (copying the base subobject must not touch it), empty bases and an
// empty [[no_unique_address]] member that have no storage, base casts at
// non-zero offsets (null stays null), members reached through bases.
extern "C" int printf(const char *, ...);
struct E0 {};
struct E1 : E0 {};
class B1 {
  int a;
  char c;
public:
  void set(int x, char y) { a = x; c = y; }
  int ga() const { return a; }
  char gc() const { return c; }
};
class D1 : public B1 {
  char d;
public:
  void setd(char v) { d = v; }
  char gd() const { return d; }
};
static_assert(sizeof(B1) == 8 && sizeof(D1) == 8, "the derived class reuses the tail padding");
struct A1 { int x; int get() const { return x; } };
struct A2 { int y; int get() const { return y; } };
struct M : A1, A2 { int z; int sum() const { return x + y + z; } };
struct W : E1, A1 { int w; };
struct NU { [[no_unique_address]] E0 e; int q; };
static_assert(sizeof(W) == 8 && sizeof(NU) == 4, "empty bases and members take no room");
struct Q { A1 a; E0 e; int tail; };
static_assert(sizeof(Q) == 12, "an empty member has size 1");

int bad = 0;
void check(int ok, int code) { if (!ok) bad = bad * 10 + code; }

int main() {
  M m;
  m.x = 1; m.y = 20; m.z = 300;
  A2 *p2 = &m;
  A1 *p1 = &m;
  M *back = static_cast<M *>(p2);
  check((char *)p2 - (char *)&m == 4, 1);
  check((char *)p1 == (char *)&m, 2);
  check(back == &m, 3);
  A2 *np = nullptr;
  M *nb = static_cast<M *>(np);
  check(nb == nullptr, 4);
  M *nm = nullptr;
  A2 *np2 = nm;
  check(np2 == nullptr, 5);
  printf("%d %d %d\n", m.sum(), p2->get(), p1->get());

  D1 d;
  d.setd('z');
  d.set(5, 'q');
  B1 b;
  b.set(9, 'w');
  static_cast<B1 &>(d) = b;
  check(d.gd() == 'z', 6);
  check(d.ga() == 9 && d.gc() == 'w', 7);
  printf("%d %d %d\n", d.ga(), (int)d.gc(), (int)d.gd());

  W w;
  w.x = 3; w.w = 4;
  E0 *pe = &w;
  check((char *)pe == (char *)&w, 8);
  check(w.get() == 3 && w.w == 4, 9);
  NU nu;
  nu.q = 6;
  Q q;
  q.a.x = 7; q.tail = 8;
  printf("%d %d %d %d\n", w.get(), nu.q, q.a.get(), q.tail);
  printf("bad=%d\n", bad);
  return bad;
}
