//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// C++17: a built-in assignment of a class evaluates the right operand first.
extern "C" int printf(const char *, ...);
struct P { int a, b; };
P arr[4];
P next(P **pp) { *pp = &arr[1]; return P{ 7, 8 }; }
struct S { int v; int w; };
S mk(int i) { return S{ i, i * 2 }; }
struct Q { P pt; int k; };
P make_pt() { return P{ 3, 4 }; }
int main() {
  P *p = &arr[0];
  *p = next(&p);
  printf("%d %d %d %d\n", arr[0].a, arr[1].a, arr[1].b, (int)(p - arr));
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
// A class that is not trivially copyable (a trivial copy assignment with a
// destructor) cannot have its value kept in a temporary: diagnosed.
struct T { int v; ~T() { } };
T tarr[2];
T next_t(T **pp) { *pp = &tarr[1]; return T{ 5 }; }
void ntv() { T *tp = &tarr[0]; *tp = next_t(&tp); }
