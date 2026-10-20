//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A function that reaches a closure in a default argument or an initializer
// is internal or external depending on where the closure's parent is
// recorded, which happens after the closure is made: an answer of "not local
// to the translation unit" for such a function must not be remembered before
// the names are final (the memo of cir_entity_is_tu_local). S::g has a
// parameter of a type in an unnamed namespace and a default argument that is a
// lambda call: it is internal, as are fal (the closure is of an unnamed
// namespace variable) and the instance ft<closure of AL>; T::h and fl are
// not.
namespace { struct X {}; }
struct S { static void g(X, int = []{ return 1; }()); };
void use() { S::g(X{}); }
struct T { int m = []{ return 2; }(); static void h(int = []{ return 3; }()); };
void use2() { T t; T::h(); }
inline auto L = []{ return 4; };
inline int fl(decltype(L) l = L) { return l(); }
void use3() { fl(); }
namespace { auto AL = []{ return 5; }; }
int fal(decltype(AL) a = AL) { return a(); }
void use4() { fal(); }
template<class F> int ft(F f, int x = [] { return 6; }()) { return f() + x; }
void use5() { ft([]{ return 1; }); ft(AL); }
