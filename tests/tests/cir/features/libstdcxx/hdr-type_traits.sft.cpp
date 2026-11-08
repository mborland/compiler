//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <type_traits>: type predicates and transformations, evaluated at compile time and at run time.
#include <type_traits>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
struct A3 { virtual ~A3() {} };
struct B3 : A3 {};
struct Abs3 { virtual void f() = 0; };
struct F3 final {};
struct S3 { int a; char b; };
struct Empty3 {};
enum class E3 : unsigned char { X };
namespace std { struct string_like { string_like(const string_like &) {} }; }
int main() {
  static_assert(std::is_same_v<std::remove_cv_t<const volatile int>, int>);
  static_assert(std::is_same_v<std::conditional_t<true, int, long>, int>);
  static_assert(std::is_trivially_copyable_v<int> && !std::is_trivially_copyable_v<std::string_like>);
  static_assert(std::is_same_v<std::common_type_t<int, double>, double>);
  static_assert(std::is_same_v<std::decay_t<int(&)[3]>, int *>);
  static_assert(std::is_same_v<std::invoke_result_t<int (*)(char), char>, int>);
  static_assert(std::is_base_of_v<A3, B3> && std::is_convertible_v<B3 *, A3 *>);
  static_assert(std::is_same_v<std::underlying_type_t<E3>, unsigned char>);
  static_assert(std::is_enum_v<E3> && std::is_class_v<A3> && std::is_pointer_v<int *>);
  static_assert(std::is_polymorphic_v<A3> && std::is_abstract_v<Abs3> && std::is_final_v<F3>);
  static_assert(std::extent_v<int[5]> == 5 && std::rank_v<int[2][3]> == 2);
  static_assert(std::is_nothrow_move_constructible_v<int> && std::has_virtual_destructor_v<A3>);
  static_assert(std::is_same_v<std::make_unsigned_t<int>, unsigned> && std::is_signed_v<int>);
  static_assert(std::is_same_v<std::void_t<int>, void>);
  static_assert(std::is_aggregate_v<S3> && std::is_standard_layout_v<S3>);
  static_assert(std::is_same_v<std::remove_reference_t<int &&>, int>);
  static_assert(std::is_same_v<std::add_pointer_t<int>, int *>);
#if __cplusplus > 202002L
  static_assert(std::is_scoped_enum_v<E3>);
#endif
  static_assert(std::is_bounded_array_v<int[3]>);
  CHECK(std::is_constant_evaluated() == false);
  CHECK(std::integral_constant<int, 5>::value == 5 && std::true_type::value);
  CHECK(std::alignment_of_v<double> == 8 && std::is_empty_v<Empty3>);
  printf("type_traits: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
