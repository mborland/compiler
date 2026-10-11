//options:--c++17
// C++17: a built-in assignment of a class evaluates the right operand first.
// (`*p = next(&p)` is in the golden only: g++ stores through the old p.)
extern "C" int printf(const char *, ...);
struct P { int a, b; };
P arr[4];
P next(P **pp) { *pp = &arr[1]; return P{ 7, 8 }; }
struct S { int v; int w; };
S mk(int i) { return S{ i, i * 2 }; }
struct Q { P pt; int k; };
P make_pt() { return P{ 3, 4 }; }
int main() {
  int i = 0;
  S ss[4] = {};
  ss[i] = S{ i++, 5 };
  printf("%d %d %d %d\n", i, ss[0].v, ss[1].v, ss[1].w);
  i = 1;
  ss[i] = mk(i++);
  printf("%d %d %d %d\n", i, ss[1].v, ss[2].v, ss[2].w);
  Q q = {};
  Q *pq = &q;
  pq->pt = make_pt();
  printf("%d %d %d\n", q.pt.a, q.pt.b, q.k);
  return 0;
}
