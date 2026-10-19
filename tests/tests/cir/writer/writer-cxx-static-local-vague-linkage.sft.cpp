//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The static locals of a function with vague linkage (and the functions that
// live in it) are linkonce_odr in a COMDAT, as in Itanium, g++ and clang:
// the variable of an anonymous union is named after its first member
// (pr39188), the statics of local classes and of lambdas follow the function
// that encloses them, at any depth.  Those of a function that is not
// discardable stay internal.
struct NT { NT(); ~NT(); int v; };
inline int a1() { static union { int i; float f; }; return i; }
template<class T> T a2(T x) { static union { T t; }; T r = t; t = x; return r; }
inline int a3(bool c) {
  if (c) { struct L { int m() { static int s; return ++s; } }; return L().m(); }
  else { struct L { int m() { static NT n; return n.v; } }; return L().m(); }
}
inline int a4() {
  auto outer = []() {
    auto inner = []() { static int q; return ++q; };
    return inner();
  };
  return outer();
}
inline int a5() { static int x; return ++x; }
int b1() { struct L { int m() { static int s; return ++s; } }; return L().m(); }
static inline int b2() { static union { int i; }; return i; }
namespace { inline int b3() { auto l = []() { static int a; return ++a; }; return l(); } }
template char a2<char>(char);
int use() { return a1() + a2<int>(1) + a3(true) + a4() + a5() + b1() + b2() + b3(); }
