//type:rp
//options:--c++20
//require:BACK_END_TYPE 3
// Built-in <=> and defaulted operator<=> through CMP3WAY (cir.cmp3way), with a
// minimal <compare> whose category values the writer reads; the output was
// compared with g++ (probe cir_src/test/writer/probes/cxx_spaceship.cpp).
// Built-in <=> and defaulted operator<=>, with a minimal <compare> (the front
// end reads the category values from it).  Compared with g++.
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
extern "C" int printf(const char *, ...);
int cat(std::strong_ordering o) { return o < 0 ? -1 : o > 0 ? 1 : 0; }
int cat(std::partial_ordering o) { return o < 0 ? -1 : o > 0 ? 1 : o == 0 ? 0 : 2; }
int cat(std::weak_ordering o) { return o < 0 ? -1 : o > 0 ? 1 : 0; }
enum E { ea, eb, ec };
enum class SE { x, y };
struct S { int a; double b; auto operator<=>(const S &) const = default; };
struct W { int a; std::strong_ordering operator<=>(const W &o) const { return a <=> o.a; } bool operator==(const W &o) const { return a == o.a; } };
struct M { int a; unsigned b; char c; bool operator==(const M &) const = default; std::strong_ordering operator<=>(const M &) const = default; };
struct N { double d; auto operator<=>(const N &) const = default; };
struct Wk { int v; std::weak_ordering operator<=>(const Wk &o) const { return v < o.v ? std::weak_ordering::less : v > o.v ? std::weak_ordering::greater : std::weak_ordering::equivalent; } };
int main() {
  int a = 3, b = 5;
  printf("%d %d %d\n", cat(a <=> b), cat(b <=> a), cat(a <=> a));
  unsigned u = 4000000000u, v = 5;
  printf("%d %d\n", cat(u <=> v), cat(v <=> u));
  long long l1 = -1, l2 = 1;
  printf("%d\n", cat(l1 <=> l2));
  volatile double zero = 0; double x = 1.5, y = 2.5, nan = zero / zero;
  printf("%d %d %d %d\n", cat(x <=> y), cat(y <=> x), cat(x <=> x), cat(x <=> nan));
  float f1 = 1, f2 = 2;
  printf("%d\n", cat(f1 <=> f2));
  printf("%d %d\n", cat(ea <=> ec), cat(ec <=> eb));
  printf("%d\n", cat(SE::y <=> SE::x));
  int arr[4]; int *p = &arr[1], *q = &arr[3];
  printf("%d %d %d\n", cat(p <=> q), cat(q <=> p), cat(p <=> p));
  char c1 = 'a'; short s1 = 70;
  printf("%d\n", cat(c1 <=> s1));
  S s1x{1, 2.0}, s2x{1, 3.0}, s3x{0, 9.0};
  printf("%d %d %d %d\n", cat(s1x <=> s2x), cat(s2x <=> s1x), cat(s1x <=> s3x), (int)(s1x < s2x));
  W w1{1}, w2{2};
  printf("%d %d\n", cat(w1 <=> w2), (int)(w2 > w1));
  M m1{1, 2u, 'x'}, m2{1, 2u, 'y'};
  printf("%d %d %d\n", cat(m1 <=> m2), (int)(m1 == m2), (int)(m1 <= m2));
  N n1{1.0}, n2{nan};
  printf("%d %d\n", cat(n1 <=> n2), (int)(n1 < n2));
  Wk k1{1}, k2{2};
  printf("%d\n", cat(k1 <=> k2));
  bool bb = (a <=> b) < 0;
  printf("%d\n", (int)bb);
  (void)(a <=> b);
  return 0;
}
