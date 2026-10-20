//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// Supp keys 4 and 5 in C++: the attributes on functions, members and
// templates, and the library builtins only for a function of C language
// linkage (an extern "C" declaration, also in a namespace): a C++ function or
// a member that has the name of a library function is an ordinary function.
__attribute__((pure)) int pure_fn(int *p);
__attribute__((const)) int const_fn(int x);
struct S {
  __attribute__((pure)) int member_pure(int x) const { return x; }
  __attribute__((const)) static int member_const(int x) { return x; }
  int abs(int x) { return x; }
};
template<class T> __attribute__((const)) T tmpl_const(T x) { return x; }
extern "C" int isalnum(int);
extern "C" int tolower(int);
namespace ns {
int abs(int);
extern "C" int toupper(int);
}
int fabs(int);
int use(int i, int *p, S &s) {
  return pure_fn(p) + const_fn(i) + s.member_pure(i) + S::member_const(i) +
         s.abs(i) + tmpl_const(i) + isalnum(i) + tolower(i) + ns::abs(i) +
         ns::toupper(i) + fabs(i);
}
