//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The masks of a pointer typeinfo take the cv of the pointee's array element
// (the cv of an array type is that of its element, also through arrays of
// arrays, as in clang's extractPBaseFlags): const int (*)[3] has CONST, so has
// const int (*)[2][3], volatile int (*)[3] VOLATILE, a pointer to member of an
// array type, and a plain int (*)[3] has none.
namespace std { class type_info { public: virtual ~type_info(); }; }
struct A { int arr[3]; const int carr[3]; };
const std::type_info *all[] = {
  &typeid(const int (*)[3]), &typeid(volatile int (*)[3]), &typeid(int (*)[3]),
  &typeid(const int (*)[2][3]), &typeid(const volatile int (*)[3]),
  &typeid(const int (A::*)[3]), &typeid(int (A::*)[3]),
  &typeid(const int (*const)[3]), &typeid(const int (**)[3])
};
int use() { return all[0] == 0; }
