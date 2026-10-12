//type:fp
//options:--c++20
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The built-in <=> is CMP3WAY into a temporary of the comparison category
// (a record with one integer member) whose values come from the constants
// that initialize the category's static members; a defaulted operator<=>
// declares the result variable that the front end synthesizes.  (A minimal
// <compare> follows.)
namespace std {
namespace __cmp_cat {
  using type = signed char;
  enum class _Ord : type { equivalent = 0, less = -1, greater = 1 };
  enum class _Ncmp : type { _Unordered = 2 };
  struct __unspec { constexpr __unspec(__unspec *) noexcept { } };
}
struct partial_ordering {
  __cmp_cat::type _M_value;
  constexpr explicit partial_ordering(__cmp_cat::_Ord __v) noexcept
    : _M_value(__cmp_cat::type(__v)) { }
  constexpr explicit partial_ordering(__cmp_cat::_Ncmp __v) noexcept
    : _M_value(__cmp_cat::type(__v)) { }
  static const partial_ordering less;
  static const partial_ordering equivalent;
  static const partial_ordering greater;
  static const partial_ordering unordered;
  friend constexpr bool operator==(partial_ordering, partial_ordering) noexcept = default;
  friend constexpr bool operator==(partial_ordering __v, __cmp_cat::__unspec) noexcept { return __v._M_value == 0; }
  friend constexpr bool operator!=(partial_ordering __v, __cmp_cat::__unspec) noexcept { return __v._M_value != 0; }
  friend constexpr bool operator<(partial_ordering __v, __cmp_cat::__unspec) noexcept { return __v._M_value == -1; }
  friend constexpr bool operator>(partial_ordering __v, __cmp_cat::__unspec) noexcept { return __v._M_value == 1; }
  friend constexpr bool operator<=(partial_ordering __v, __cmp_cat::__unspec) noexcept { return __v._M_value == -1 || __v._M_value == 0; }
  friend constexpr bool operator>=(partial_ordering __v, __cmp_cat::__unspec) noexcept { return __v._M_value == 1 || __v._M_value == 0; }
};
inline constexpr partial_ordering partial_ordering::less(__cmp_cat::_Ord::less);
inline constexpr partial_ordering partial_ordering::equivalent(__cmp_cat::_Ord::equivalent);
inline constexpr partial_ordering partial_ordering::greater(__cmp_cat::_Ord::greater);
inline constexpr partial_ordering partial_ordering::unordered(__cmp_cat::_Ncmp::_Unordered);
struct weak_ordering {
  __cmp_cat::type _M_value;
  constexpr explicit weak_ordering(__cmp_cat::_Ord __v) noexcept
    : _M_value(__cmp_cat::type(__v)) { }
  static const weak_ordering less;
  static const weak_ordering equivalent;
  static const weak_ordering greater;
  constexpr operator partial_ordering() const noexcept { return partial_ordering(__cmp_cat::_Ord(_M_value)); }
  friend constexpr bool operator==(weak_ordering, weak_ordering) noexcept = default;
  friend constexpr bool operator==(weak_ordering __v, __cmp_cat::__unspec) noexcept { return __v._M_value == 0; }
  friend constexpr bool operator!=(weak_ordering __v, __cmp_cat::__unspec) noexcept { return __v._M_value != 0; }
  friend constexpr bool operator<(weak_ordering __v, __cmp_cat::__unspec) noexcept { return __v._M_value < 0; }
  friend constexpr bool operator>(weak_ordering __v, __cmp_cat::__unspec) noexcept { return __v._M_value > 0; }
  friend constexpr bool operator<=(weak_ordering __v, __cmp_cat::__unspec) noexcept { return __v._M_value <= 0; }
  friend constexpr bool operator>=(weak_ordering __v, __cmp_cat::__unspec) noexcept { return __v._M_value >= 0; }
};
inline constexpr weak_ordering weak_ordering::less(__cmp_cat::_Ord::less);
inline constexpr weak_ordering weak_ordering::equivalent(__cmp_cat::_Ord::equivalent);
inline constexpr weak_ordering weak_ordering::greater(__cmp_cat::_Ord::greater);
struct strong_ordering {
  __cmp_cat::type _M_value;
  constexpr explicit strong_ordering(__cmp_cat::_Ord __v) noexcept
    : _M_value(__cmp_cat::type(__v)) { }
  static const strong_ordering less;
  static const strong_ordering equal;
  static const strong_ordering equivalent;
  static const strong_ordering greater;
  constexpr operator partial_ordering() const noexcept { return partial_ordering(__cmp_cat::_Ord(_M_value)); }
  constexpr operator weak_ordering() const noexcept { return weak_ordering(__cmp_cat::_Ord(_M_value)); }
  friend constexpr bool operator==(strong_ordering, strong_ordering) noexcept = default;
  friend constexpr bool operator==(strong_ordering __v, __cmp_cat::__unspec) noexcept { return __v._M_value == 0; }
  friend constexpr bool operator!=(strong_ordering __v, __cmp_cat::__unspec) noexcept { return __v._M_value != 0; }
  friend constexpr bool operator<(strong_ordering __v, __cmp_cat::__unspec) noexcept { return __v._M_value < 0; }
  friend constexpr bool operator>(strong_ordering __v, __cmp_cat::__unspec) noexcept { return __v._M_value > 0; }
  friend constexpr bool operator<=(strong_ordering __v, __cmp_cat::__unspec) noexcept { return __v._M_value <= 0; }
  friend constexpr bool operator>=(strong_ordering __v, __cmp_cat::__unspec) noexcept { return __v._M_value >= 0; }
};
inline constexpr strong_ordering strong_ordering::less(__cmp_cat::_Ord::less);
inline constexpr strong_ordering strong_ordering::equal(__cmp_cat::_Ord::equivalent);
inline constexpr strong_ordering strong_ordering::equivalent(__cmp_cat::_Ord::equivalent);
inline constexpr strong_ordering strong_ordering::greater(__cmp_cat::_Ord::greater);
}
int f1(int a, int b) { return (a <=> b) < 0; }
int f2(double a, double b) { return (a <=> b) < 0; }
std::strong_ordering f3(unsigned a, unsigned b) { return a <=> b; }
std::strong_ordering f4(int *a, int *b) { return a <=> b; }
void f5(int a, int b) { (void)(a <=> b); }
struct S { int a; double b; auto operator<=>(const S &) const = default; };
bool f6(const S &x, const S &y) { return x < y; }
