//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
template <class T> auto g1(T x) { return x + 1; }
int u() { return (int)sizeof(g1(2)); }
template <class T> T g2(T x) { return x + 2; }
extern template int g2<int>(int);
int v() { return g2(3); }
template <class T> struct B { T t; T get() const { return t; } };
extern template struct B<int>;
int w(B<int> *b) { return b->get(); }
template <class T> struct B2 { T t; T get() const { return t; } T other() const { return t + 1; } static T sv; static int cnt; };
template <class T> T B2<T>::sv = 7;
template <class T> int B2<T>::cnt = 3;
template struct B2<int>;
template <class T> T f2(T x) { return x * 2; }
template int f2<int>(int);
template <class T> T g3(T x) { return x * 3; }
template int g3<int>(int);
int use(B2<int> *b) { return b->get(); }
