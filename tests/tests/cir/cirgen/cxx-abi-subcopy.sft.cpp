//type:rp
//options:--c++20
//require:BACK_END_TYPE 3
// Assigning a base subobject or a [[no_unique_address]] member whose class has reusable tail
// padding must not write the padding (the derived class keeps members there). The
// two-translation-unit version (cir_src/test/abi-probes, box_abi_probe.sh) is the ABI proof.
extern "C" int printf(const char *, ...);
// Copying and assigning a base subobject or [[no_unique_address]] member whose
// class has reusable tail padding must not write the padding: the derived
// class keeps a member there.
struct B { int a; char c; B(int x, char y) : a(x), c(y) {}
           B(const B &) = default; B &operator=(const B &) = default; };    // 8, dsize 5
struct D : B { char d; short e; D(int x, char y, char z, short w)
                                   : B(x, y), d(z), e(w) {} };              // 8: d at 5, e at 6
struct N { [[no_unique_address]] B b; char d; short e;
           N(int x, char y, char z, short w) : b(x, y), d(z), e(w) {} };    // 8
struct L { long l; char c; L(long x, char y) : l(x), c(y) {}
           L(const L &) = default; L &operator=(const L &) = default; };    // 16, dsize 9
struct DL : L { char d; long m; DL(long x, char y, char z, long w) : L(x, y), d(z), m(w) {} };  // 24: d at 9, m at 16
struct NL { [[no_unique_address]] L l; char d; char e[6]; NL(long x, char y, char z) : l(x, y), d(z) { for (int i = 0; i < 6; ++i) e[i] = i + 1; } };  // 16

void set_base(D &d, const B &b);
void set_base_val(D &d, B b);
void set_nua(N &n, const B &b);
B get_base(const D &d);
B get_nua(const N &n);
void set_base_l(DL &d, const L &b);
void set_nua_l(NL &n, const L &b);
B &base_ref(D &d);
void assign_through_ref(B &dst, const B &src);

void set_base(D &d, const B &b) { static_cast<B &>(d) = b; }
void set_base_val(D &d, B b) { static_cast<B &>(d) = b; }
void set_nua(N &n, const B &b) { n.b = b; }
B get_base(const D &d) { return static_cast<const B &>(d); }
B get_nua(const N &n) { return n.b; }
void set_base_l(DL &d, const L &b) { static_cast<L &>(d) = b; }
void set_nua_l(NL &n, const L &b) { n.l = b; }
B &base_ref(D &d) { return d; }
void assign_through_ref(B &dst, const B &src) { dst = src; }

static int bad = 0;
static void check(const char *what, long got, long want) {
  if (got != want) { ++bad; printf("MISMATCH %s: got %ld want %ld\n", what, got, want); }
}
#define CHECK(e, w) check(#e, (long)(e), (long)(w))
int main() {
  D d(1, 2, 3, 4); B b(9, 8);
  set_base(d, b);
  CHECK(d.a, 9); CHECK(d.c, 8); CHECK(d.d, 3); CHECK(d.e, 4);
  D d2(1, 2, 3, 4);
  set_base_val(d2, b);
  CHECK(d2.a, 9); CHECK(d2.c, 8); CHECK(d2.d, 3); CHECK(d2.e, 4);
  N n(1, 2, 3, 4);
  set_nua(n, b);
  CHECK(n.b.a, 9); CHECK(n.b.c, 8); CHECK(n.d, 3); CHECK(n.e, 4);
  { B g = get_base(d); CHECK(g.a, 9); CHECK(g.c, 8); }
  { B g = get_nua(n); CHECK(g.a, 9); CHECK(g.c, 8); }
  D d3(5, 6, 7, 8);
  assign_through_ref(base_ref(d3), b);
  CHECK(d3.a, 9); CHECK(d3.c, 8); CHECK(d3.d, 7); CHECK(d3.e, 8);
  DL dl(1, 2, 3, 4); L l(7, 6);
  set_base_l(dl, l);
  CHECK(dl.l, 7); CHECK(dl.c, 6); CHECK(dl.d, 3); CHECK(dl.m, 4);
  NL nl(1, 2, 3);
  set_nua_l(nl, l);
  CHECK(nl.l.l, 7); CHECK(nl.l.c, 6); CHECK(nl.d, 3);
  for (int i = 0; i < 6; ++i) CHECK(nl.e[i], i + 1);
  CHECK(sizeof(D), 8); CHECK(sizeof(N), 8); CHECK(sizeof(DL), 24); CHECK(sizeof(NL), 16);
  printf("subcopy: %s (%d mismatches)\n", bad == 0 ? "OK" : "BAD", bad);
  return bad != 0;
}
