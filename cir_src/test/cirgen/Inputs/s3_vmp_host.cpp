// The host side of cxx-s3-virtual-member-pointers.test.
struct V1 { virtual int f(int x) { return x + 1; } virtual int g(int x) { return x + 2; } int v1; };
struct V2 { virtual int h(int x) { return x * 3; } int v2; };
struct W : V1, V2 { int g(int x) override { return x + 20; } int h(int x) override { return x * 30; } int w; };
W *make_w();

extern int (V1::*vpf)(int);
extern int (V1::*vpg)(int);
extern int (W::*vph)(int);
extern int (W::*varr[2])(int);
extern "C" {
int call_w(W *p, int (W::*m)(int), int x);
int (V1::*mk_vf())(int);
int (W::*mk_vh())(int);
}

int main()
{
  W *w = make_w();

  if ((w->*vpf)(1) != 2) return 1;
  if ((w->*vpg)(1) != 21) return 2;            /* W::g overrides */
  if ((w->*vph)(2) != 60) return 3;            /* V2::h through W */
  if ((w->*varr[0])(2) != 60 || (w->*varr[1])(1) != 21) return 4;
  if (vph != static_cast<int (W::*)(int)>(&V2::h)) return 5;
  if (varr[1] != &W::g) return 6;
  if (call_w(w, &W::h, 3) != 90) return 7;
  if (call_w(w, static_cast<int (W::*)(int)>(&V1::f), 1) != 2) return 8;
  if (call_w(w, &W::g, 1) != 21) return 9;
  if (call_w(w, static_cast<int (W::*)(int)>(&V2::h), 4) != 120) return 10;
  if (mk_vf() != &V1::f) return 11;
  if (mk_vh() != static_cast<int (W::*)(int)>(&V2::h)) return 12;
  if ((w->*mk_vh())(5) != 150) return 13;
  delete w;
  return 0;
}
