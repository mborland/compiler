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
