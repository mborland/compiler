//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A typeinfo of a TU-local type (an unnamed-namespace class or enum, a pointer
// to one, a member pointer of one) is internal with TU_LOCAL, so that its name
// string starts with "*"; the typeinfo of a class that is local to a
// function (the linkage of the function's statics) is internal in an ordinary
// function and linkonce_odr in an inline one.
namespace std {
class type_info {
public:
  virtual ~type_info();
  const char *name() const;
  bool operator==(const type_info &) const;
  bool operator!=(const type_info &) const;
};
}
namespace { struct Hid { virtual ~Hid(); int x; }; enum HE { h0 }; }
struct A { virtual ~A(); };
inline const std::type_info &in_inline() {
  struct L { virtual ~L() {} };
  return typeid(L);
}
const std::type_info &in_plain() {
  struct P { virtual ~P() {} };
  return typeid(P *);
}
const std::type_info *all[] = {
  &typeid(Hid), &typeid(Hid *), &typeid(int Hid::*), &typeid(HE),
  &typeid(HE *), &typeid(Hid **), &typeid(A), &typeid(A *), &typeid(int)
};
const std::type_info *use() { return all[0] ? &in_inline() : &in_plain(); }
