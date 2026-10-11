//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
int g1 = 5;
static int g2 = 6;
const int g3 = 7;
namespace N { int n1 = 8; static int n2 = 9; namespace { int n3 = 10; } }
struct S { static int sm; static const int sc = 3; static constexpr int scx = 4; int x; };
int S::sm = 11;
template <class T> struct TS { static int tm; static constexpr int tc = 5; };
template <class T> int TS<T>::tm = 12;
inline int iv = 13;
extern "C" int cg = 14;
int use() { return g1 + g2 + g3 + N::n1 + N::n2 + N::n3 + S::sm + S::sc + S::scx + TS<int>::tm + TS<int>::tc + iv + cg; }
int f() { static int a = 1; a++; { static int a = 2; return a; } }
inline int inl() { static int is = 9; return is; }
int use2() { return f() + inl(); }
