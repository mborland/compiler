//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
int f() { static int a = 1; a++; { static int a = 2; return a; } }
struct A { int m(int x) { static int cnt = 0; return x + cnt++; } };
template <class T> T tf(T x) { static int c = 0; c++; return x + c; }
int inst() { return tf(3) + tf(2.0); }
struct B { B(); int k; int get() { static int bs = 5; return bs + k; } };
inline int inl() { static int is = 9; return is; }
int use() { return inl(); }
extern "C" int cf() { static int cfs = 4; return cfs; }
int g1() { struct L { int q; int get() { static int ms = 6; return ms; } }; L *l = 0; return l->get(); }
