//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
namespace ns { int nf(int x) { return x + 1; } namespace { int anon(int y) { return y; } } }
struct A { int m(int a) { static int cnt = 0; return a + cnt++; } static int sm(); A(); ~A(); int v; };
int A::sm() { return 3; }
int blocks(int c) { if (c) { static int s = 1; return s; } else { static int s = 2; return s; } }
template <class T> T twice(T x) { return x + x; }
template <class T> struct Box { T t; T get() const { return t; } static int count; };
template <class T> int Box<T>::count = 5;
int inst(Box<int> &b) { return twice(3) + twice(2.0) + b.get() + Box<long>::count; }
inline int inl(int a) { static const int tbl[2] = { 1, 2 }; return tbl[a & 1]; }
int call_inl() { return inl(1); }
int main() { return ns::nf(1) + ns::anon(2) + A::sm() + call_inl() + blocks(1); }
