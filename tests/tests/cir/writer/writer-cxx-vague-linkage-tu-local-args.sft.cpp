//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A function that lives in a discardable function is internal when its own
// template arguments are local to the translation unit, as is its static
// local: the call operator of a generic lambda of an inline function (or of
// a template) instantiated with a type of an unnamed namespace.  The
// instantiations with a public type are linkonce_odr.
namespace { struct A { int v; }; }
struct Pub { int v; };
inline int g1() {
  auto l = [](auto x) { return x.v; };
  return l(A{1}) + l(Pub{2});
}
template<class T> int g3(T t) {
  auto l = [](auto x) { static int c; return x.v + ++c; };
  return l(t) + l(Pub{3});
}
int use() { return g1() + g3(A{4}) + g3(Pub{5}); }
