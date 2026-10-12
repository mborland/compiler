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
