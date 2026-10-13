//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The typeinfo of an incomplete class is a plain internal __class_type_info
// without bases; a pointer or member pointer whose pointee chain contains an
// incomplete class is internal, each pointer level of the chain has the
// INCOMPLETE mask (0x08), and a member pointer whose class is incomplete has
// INCOMPLETE_CLASS (0x10). The decision is taken when the typeinfo is first
// needed and is sticky: a class that is completed afterwards keeps its
// internal typeinfo (clang's behavior; Later below).
namespace std {
class type_info {
public:
  virtual ~type_info();
  const char *name() const;
  bool operator==(const type_info &) const;
  bool operator!=(const type_info &) const;
};
}
struct I;
struct C { int c; };
struct Later;
const std::type_info &t1() { return typeid(I *); }
const std::type_info &t2() { return typeid(I **); }
const std::type_info &t3() { return typeid(const I *); }
const std::type_info &t4() { return typeid(int I::*); }
const std::type_info &t5() { return typeid(I *C::*); }
const std::type_info &t6() { return typeid(C); }
const std::type_info &t7() { return typeid(int C::*); }
const std::type_info &t8() { return typeid(Later *); }
struct Later { virtual ~Later(); int l; };
const std::type_info &t9() { return typeid(Later *); }
const std::type_info &t10() { return typeid(Later); }
