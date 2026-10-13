//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// typeid: the static form names the typeinfo of the type operand and pops
// nothing (TYPEID g_ti t_result 0); the dynamic form of a polymorphic glvalue
// has a nil typeinfo and pops the glvalue, with NULL_CHECK only for an
// indirection through a pointer (typeid(*p)), not for a reference.
namespace std {
class type_info {
public:
  virtual ~type_info();
  const char *name() const;
  bool operator==(const type_info &) const;
  bool operator!=(const type_info &) const;
};
}
struct A { virtual ~A(); int a; };
struct B : A { int b; };
struct N { int n; };
const std::type_info &s1() { return typeid(int); }
const std::type_info &s2() { return typeid(B); }
const std::type_info &s3() { return typeid(N); }
const std::type_info &s4(N &n) { return typeid(n); }
const std::type_info &s5(N *n) { return typeid(*n); }
const std::type_info &d1(A &a) { return typeid(a); }
const std::type_info &d2(A *a) { return typeid(*a); }
const std::type_info &d3(B &b) { return typeid(b); }
const std::type_info &d4(A **a) { return typeid(**a); }
const std::type_info &d5(const A &a) { return typeid(a); }
const char *n1(A &a) { return typeid(a).name(); }
bool e1(A &a, A &b) { return typeid(a) == typeid(b); }
bool e2(A &a) { return typeid(a) != typeid(B); }
bool e3(A &a) { return typeid(a) == typeid(const B &); }
