//type:fp
//options:--c++17 --g++ --gnu_version=70400
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// With g++ 7 and earlier in C++17 the front end lists the base class entries
// of "{ .p = 1 }" after the designated data members, and the data members
// that have an initializer after them.  Each base entry is sent as the entry
// of its base (C_SEL 0, C_SEL 1) and the cursor stays: the data members keep
// theirs, also those whose type is that of a base (the entry is told from
// that of the member by being implicit and the next base in order, the
// qualifiers and typedefs of the member's type not counting).  See
// also writer-cxx-designated-base-last.
struct B0 { int bb = 4; };
struct B1 { int cc; };
struct D : B0 { int p = 5; int q = 6; };
struct E : B0, B1 { int p; int q; };
struct F : B0 { B0 m = {}; B0 n = {7}; int q = 3; };
struct G : B0 { B0 m; B0 n; };
struct K : B0 { int p = 5; B0 n; };
typedef B0 BT;
struct Q1 : B0 { const B0 m = {}; B0 n = {7}; int q = 3; };
struct Q2 : B0 { B0 m = {}; const volatile B0 n = {7}; };
struct Q3 : B0 { BT m = {}; volatile BT n = {7}; };
D d = {.p = 50};
E e = {.p = 5};
E e2 = {.p = 5, .q = 6};
F f = {.m = {1}};
G g = {.m = {1}};
K k = {.p = 50};
Q1 q1 = {.m = {1}};
Q2 q2 = {.m = {1}};
Q3 q3 = {.m = {1}};
int fe(int m) { E l = {.p = m}; return l.q + l.p; }
int ff(int m) { F l = {.m = {m}}; return l.bb + l.n.bb; }
int fk(int m) { K l = {.p = m}; return l.bb + l.n.bb; }
int fq(int m) { Q1 l = {.m = {m}}; return l.bb + l.n.bb; }
