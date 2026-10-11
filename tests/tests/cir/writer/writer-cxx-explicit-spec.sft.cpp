//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
template <class T> struct X { static T s; static int u; };
template <class T> T X<T>::s = 1;
template <> int X<int>::s = 5;
template <class T> int X<T>::u = 2;
template int X<long>::u;
template <class T> T vt = T(3);
template <> int vt<int> = 7;
template long vt<long>;
inline int iv0 = 4;
struct NonConst { NonConst(); ~NonConst(); };
inline NonConst unused_dyn;
inline int unused_const = 3;
int use() { return X<int>::s; }
struct D { D(); ~D(); int v; };
int use_ext() { const D (&r)[2] = {D(), D()}; return r[0].v; }
