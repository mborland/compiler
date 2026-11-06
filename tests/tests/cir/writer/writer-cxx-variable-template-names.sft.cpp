//type:fp
//options:--c++17 --g++ --gnu_version=150200:--c++17 --g++ --gnu_version=140000:--c++17 --clang_version 220000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The names and linkage of variable template instances (M5.1 W3, CWG2387):
// const instances k and c have external linkage in clang and g++ 15 and
// later (linkonce_odr _Z1kIiE) and internal linkage before g++ 15; a static
// one (s) and one in an unnamed namespace (u) are internal.  A function
// template that has such an instance in its signature (p3 and p9: the
// address of the instance, a non-dependent literal), a dependent
// specialization (p4) and a static member variable template of a class that
// is not dependent (p5, p6) are named as g++ and clang name them.
template<bool B, class T = void> struct enable_if {};
template<class T> struct enable_if<true, T> { typedef T type; };
template<class T> struct is_d { static constexpr bool value = true; };
template<class T> const T k = T(1);
template<class T> constexpr T c = T(2);
template<class T> static const T s = T(3);
template<class T> T nc = T(4);
namespace { template<class T> const T u = T(5); }
const int *g1() { return &k<int>; }
const int *g2() { return &c<int>; }
const int *g3() { return &s<int>; }
int *g4() { return &nc<int>; }
const int *g5() { return &u<int>; }
template<class T> typename enable_if<(&k<int> != nullptr) && is_d<T>::value, T>::type
p3(T x) { return x; }
template<class T> typename enable_if<(&s<int> != nullptr) && is_d<T>::value, T>::type
p9(T x) { return x; }
template<class T> typename enable_if<k<T> || k<T *>, T>::type
p4(T x) { return x; }
struct S5 { template<class T> static constexpr bool sv = true; };
template<class V> struct CV6 { template<class T> static constexpr bool cv = true; };
template<class T> typename enable_if<S5::sv<T>, T>::type
p5(T x) { return x; }
template<class T> typename enable_if<CV6<int>::cv<T>, T>::type
p6(T x) { return x; }
int use() { return p3(1) + p9(1) + p4(1) + p5(1) + p6(1); }
