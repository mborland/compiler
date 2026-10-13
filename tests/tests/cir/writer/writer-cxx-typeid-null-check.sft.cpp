//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// NULL_CHECK of typeid (clang's isGLValueFromPointerDeref): set when the
// polymorphic operand, through parentheses, the right operand of a comma,
// either arm of a conditional and the casts of a glvalue, is an indirection
// through a pointer; not for a reference or another glvalue.
namespace std { class type_info { public: virtual ~type_info(); }; }
struct A { virtual ~A(); };
struct B : A { };
bool r;
const std::type_info &t1(A *p) { return typeid(*p); }
const std::type_info &t2(A *p) { return typeid((*p)); }
const std::type_info &t3(A *p) { return typeid((r = true), *p); }
const std::type_info &t4(bool c, A *p, A *q) { return typeid(c ? *p : *q); }
const std::type_info &t5(bool c, A *p, A &q) { return typeid(c ? q : *p); }
const std::type_info &t6(B *p) { return typeid(static_cast<A &>(*p)); }
const std::type_info &t7(A &a) { return typeid(a); }
const std::type_info &t8(A &a) { return typeid((r = true), a); }
const std::type_info &t9(bool c, A &a, A &b) { return typeid(c ? a : b); }
const std::type_info &t10(A **pp) { return typeid(**pp); }
