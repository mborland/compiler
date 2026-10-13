//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The typeinfo objects that a function introduces stay declared when the
// function is dropped as unsupported (inline assembly): the typeinfo of an
// incomplete class (typeid(Inc *)) is a plain internal __class_type_info with
// no facts, the one of a complete class has its facts, a catch names the
// typeinfo of a class, and the surviving functions still name the same objects.
namespace std { class type_info { public: virtual ~type_info(); }; }
struct Inc;
struct Cmp { int x; virtual ~Cmp(); };
void thrower(int k);
const std::type_info &dropped(int k) {
  try { thrower(k); } catch (Cmp &) { } catch (int) { }
  asm("nop");
  return typeid(Inc *);
}
const std::type_info &kept() { return typeid(Inc *); }
void kept2() { try { thrower(1); } catch (const Cmp &) { } }
