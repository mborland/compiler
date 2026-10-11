//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
int ext(int x) { return x; }
static int stat(int x) { return x; }
inline int inl(int x) { return x; }
static inline int sinl(int x) { return x; }
constexpr int cx(int x) { return x; }
namespace { int anon(int x) { return x; } inline int anoninl(int x) { return x; } }
struct S { int m() { return 1; } int n(); static int sm() { return 2; } S(); ~S(); };
int S::n() { return 3; }
inline int S2n(S &s) { return s.m(); }
template <class T> T tf(T x) { return x; }
template <> int tf<int>(int x) { return x + 1; }
template <class T> struct TS { T v; T get() { return v; } };
template <> struct TS<char> { char v; char get() { return v; } };
int use(S &s) { TS<int> a{1}; TS<char> c{2}; return tf(1) + tf(2.0) + a.get() + c.get() + inl(1) + sinl(2) + cx(3) + anon(1) + anoninl(1) + stat(1) + ext(1) + S2n(s); }
