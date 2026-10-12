//type:rp
//options:--c++20
//require:BACK_END_TYPE 3
// [[no_unique_address]] members (empty and with tail padding) in by-value records,
// passed and returned. The two-translation-unit version (cir_src/test/abi-probes,
// box_abi_probe.sh) is the ABI proof.
extern "C" int printf(const char *, ...);
// [[no_unique_address]] members passed and returned by value (C++20 attribute
// accepted in C++17 mode as an extension by g++ and clang).
struct E {};
struct B { int a; char c; B(int x, char y) : a(x), c(y) {} };           // 8, dsize 5
struct L { long l; char c; L(long x, char y) : l(x), c(y) {} };       // 16, dsize 9
struct N1 { [[no_unique_address]] E e; int x; N1(int v) : x(v) {} };    // 4
struct N2 { int x; [[no_unique_address]] E e; N2(int v) : x(v) {} };    // 4
struct N3 { [[no_unique_address]] E e1; [[no_unique_address]] E e2; int x;
            N3(int v) : x(v) {} };                                       // 8
struct N4 { [[no_unique_address]] B b; char d; N4(int x, char y, char z)
                                                  : b(x, y), d(z) {} };  // 8, d at 5
struct N5 { [[no_unique_address]] L l; char d; short e; N5(long x, char y, char z, short w)
                                                  : l(x, y), d(z), e(w) {} };  // 16
struct N6 { [[no_unique_address]] B b; [[no_unique_address]] E e; char d; N6(int x, char y, char z)
                                                  : b(x, y), d(z) {} };  // 8
struct N7 { [[no_unique_address]] E e; double f; N7(double v) : f(v) {} };  // 8
struct N8 { [[no_unique_address]] E e; long l; N8(long v) : l(v) {} };   // 8

int f_n1(N1 s);  N1 make_n1(int);  N1 pass_n1(N1);
int f_n2(N2 s);  N2 make_n2(int);
int f_n3(N3 s);  N3 make_n3(int);
int f_n4(N4 s);  N4 make_n4(int, char, char);  N4 pass_n4(N4);
int f_n5(N5 s);  N5 make_n5(long, char, char, short);  N5 pass_n5(N5);
int f_n6(N6 s);  N6 make_n6(int, char, char);
int f_n7(N7 s);  N7 make_n7(double);
int f_n8(N8 s);  N8 make_n8(long);
int f_mix(int a, N1 p, N4 q, N5 r, N7 s, N8 t, int z);

int f_n1(N1 s) { return s.x; }
N1 make_n1(int v) { return N1(v); }
N1 pass_n1(N1 s) { return s; }
int f_n2(N2 s) { return s.x; }
N2 make_n2(int v) { return N2(v); }
int f_n3(N3 s) { return s.x; }
N3 make_n3(int v) { return N3(v); }
int f_n4(N4 s) { return s.b.a * 10000 + s.b.c * 100 + s.d; }
N4 make_n4(int a, char c, char d) { return N4(a, c, d); }
N4 pass_n4(N4 s) { return s; }
int f_n5(N5 s) { return (int)s.l.l * 10000 + s.l.c * 1000 + s.d * 100 + s.e; }
N5 make_n5(long l, char c, char d, short e) { return N5(l, c, d, e); }
N5 pass_n5(N5 s) { return s; }
int f_n6(N6 s) { return s.b.a * 10000 + s.b.c * 100 + s.d; }
N6 make_n6(int a, char c, char d) { return N6(a, c, d); }
int f_n7(N7 s) { return (int)s.f; }
N7 make_n7(double v) { return N7(v); }
int f_n8(N8 s) { return (int)s.l; }
N8 make_n8(long v) { return N8(v); }
int f_mix(int a, N1 p, N4 q, N5 r, N7 s, N8 t, int z) {
  return a + p.x + q.b.a + q.b.c + q.d + (int)r.l.l + r.l.c + r.d + r.e +
         (int)s.f + (int)t.l + z;
}

static int bad = 0;
static void check(const char *what, long got, long want) {
  if (got != want) { ++bad; printf("MISMATCH %s: got %ld want %ld\n", what, got, want); }
}
#define CHECK(e, w) check(#e, (long)(e), (long)(w))
int main() {
  CHECK(f_n1(N1(3)), 3); CHECK(f_n1(make_n1(4)), 4);
  { N1 r = pass_n1(N1(5)); CHECK(r.x, 5); }
  CHECK(f_n2(N2(6)), 6); CHECK(f_n2(make_n2(7)), 7);
  CHECK(f_n3(N3(8)), 8); CHECK(f_n3(make_n3(9)), 9);
  CHECK(f_n4(N4(1, 2, 3)), 10000 + 200 + 3); CHECK(f_n4(make_n4(4, 5, 6)), 40000 + 500 + 6);
  { N4 r = pass_n4(N4(7, 8, 9)); CHECK(r.b.a, 7); CHECK(r.b.c, 8); CHECK(r.d, 9); }
  CHECK(f_n5(N5(1, 2, 3, 4)), 10000 + 2000 + 300 + 4);
  CHECK(f_n5(make_n5(5, 6, 7, 8)), 50000 + 6000 + 700 + 8);
  { N5 r = pass_n5(N5(9, 1, 2, 3)); CHECK(r.l.l, 9); CHECK(r.l.c, 1); CHECK(r.d, 2); CHECK(r.e, 3); }
  CHECK(f_n6(N6(1, 2, 3)), 10000 + 200 + 3); CHECK(f_n6(make_n6(4, 5, 6)), 40000 + 500 + 6);
  CHECK(f_n7(N7(7.0)), 7); CHECK(f_n7(make_n7(8.0)), 8);
  CHECK(f_n8(N8(9)), 9); CHECK(f_n8(make_n8(10)), 10);
  CHECK(f_mix(1, N1(2), N4(3, 4, 5), N5(6, 7, 8, 9), N7(10.0), N8(11), 12),
        1 + 2 + (3 + 4 + 5) + (6 + 7 + 8 + 9) + 10 + 11 + 12);
  { N4 r = make_n4(1, 2, 3); CHECK(r.b.a, 1); CHECK(r.b.c, 2); CHECK(r.d, 3); }
  { N5 r = make_n5(1, 2, 3, 4); CHECK(r.l.l, 1); CHECK(r.l.c, 2); CHECK(r.d, 3); CHECK(r.e, 4); }
  { N7 r = make_n7(3.0); CHECK(r.f, 3); }
  CHECK(sizeof(N1), 4); CHECK(sizeof(N3), 4); CHECK(sizeof(N4), 8); CHECK(sizeof(N5), 16);
  printf("nua: %s (%d mismatches)\n", bad == 0 ? "OK" : "BAD", bad);
  return bad != 0;
}
