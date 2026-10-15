//type:fp
//options_all:--clang_v 210000 --c++20
//remark:[GH #?] GNU and clang vector types in native macOS builds
// 10/15/26  [GH #?]
//
// libc++ (<__algorithm/simd_utils.h>, reached by <algorithm>, <string>,
// <vector>, <iostream>, ...) enables its vectorized algorithms for every clang
// compiler and declares its vector type with an alias template that uses
// __ext_vector_type__.  The front end implements the clang "ext_vector_type"
// and GNU "vector_size" attributes only when GNU_VECTOR_TYPES_ALLOWED is set.
// The Linux macro configurations set it, but the macOS ones did not, so in a
// native macOS build both attributes were ignored with an "unknown attribute"
// warning and the alias remained a scalar type.
//
// This is a regression check for the macOS configurations, run with a native
// build (see "MacOS" in HACKING.md); the Linux configurations that the Docker
// tests use have always set the macro, so the test passes there with or
// without the change.

// The vector extension on an alias declaration, plain and as an alias
// template (as in libc++).
using v4i __attribute__((__ext_vector_type__(4))) = int;
static_assert(sizeof(v4i) == 4 * sizeof(int), "plain alias is a vector");

template <class T, unsigned N>
using vec __attribute__((__ext_vector_type__(N))) = T;
static_assert(sizeof(vec<int, 4>) == 4 * sizeof(int),
              "alias template is a vector");
static_assert(sizeof(vec<char, 16>) == 16, "alias template of char");

// A GNU vector_size typedef.
typedef int v4si __attribute__((vector_size(16)));
static_assert(sizeof(v4si) == 16, "vector_size typedef is a vector");

// Element-wise operations on the vector types give vectors.
static_assert(sizeof(v4i{} + v4i{}) == sizeof(v4i), "vector sum is a vector");
static_assert(sizeof(v4i{} == v4i{}) == sizeof(v4i),
              "comparison gives a vector");

// Operations that libc++'s <__algorithm/simd_utils.h> performs on these types.

// The templates over bool vectors (one bit per element) that libc++ defines
// and uses: a vector converted to a bool vector, __builtin_reduce_or and
// __builtin_reduce_and of it (__any_of, __all_of, __none_of), and its bit cast
// to an unsigned integer mask (__find_first_set).  Bool vectors and the
// __builtin_reduce_* functions exist only when emulating clang 15 or later
// (the test's option), which is what the macOS SDK's clang is.
// (The vectors are spelled with the alias template, as in libc++, because the
// front end does not deduce the template arguments of vec<T, N> from a vector
// type that was written some other way, such as v4i above.)
using vi4 = vec<int, 4>;
using v16c = vec<char, 16>;
static_assert(sizeof(vec<bool, 4>) == 1, "a bool vector of four is one byte");
static_assert(sizeof(vec<bool, 8>) == 1, "a bool vector of eight is one byte");
static_assert(sizeof(vec<bool, 16>) == 2, "a bool vector of sixteen is two bytes");

template <class T, unsigned N>
bool any_of(vec<T, N> v) {
  return __builtin_reduce_or(__builtin_convertvector(v, vec<bool, N>));
}
template <class T, unsigned N>
bool all_of(vec<T, N> v) {
  return __builtin_reduce_and(__builtin_convertvector(v, vec<bool, N>));
}
template <class T, unsigned N>
bool none_of(vec<T, N> v) {
  return !__builtin_reduce_or(__builtin_convertvector(v, vec<bool, N>));
}
template <class T, unsigned N>
unsigned long find_first_set(vec<T, N> v) {
  using mask_vec = vec<bool, N>;
  auto impl = [&]<class MaskT>(MaskT) noexcept {
    return __builtin_ctzg(__builtin_bit_cast(MaskT, __builtin_convertvector(v, mask_vec)), (int)N);
  };
  if constexpr (sizeof(mask_vec) == sizeof(unsigned char)) {
    return impl((unsigned char)0);
  } else if constexpr (sizeof(mask_vec) == sizeof(unsigned short)) {
    return impl((unsigned short)0);
  } else {
    static_assert(sizeof(mask_vec) == 0, "unexpected required size for mask integer type");
    return 0;
  }
}
bool any0 = any_of(vi4{0, 0, 3, 0});
bool all0 = all_of(vi4{1, 2, 3, 4});
bool none0 = none_of(vi4{0, 0, 0, 0});
unsigned long first4 = find_first_set(vi4{0, 0, 3, 0});
unsigned long first16 = find_first_set(v16c{});

// __builtin_convertvector between element types, and __builtin_bit_cast of a
// vector to an unsigned integer of the same size.
using v4f = vec<float, 4>;
static_assert(sizeof(__builtin_convertvector(v4i{}, v4f)) == sizeof(v4f),
              "converting an int vector to a float vector");
unsigned long long bits = __builtin_bit_cast(unsigned long long, vec<char, 8>{});

// Subscripting an element.
int element(v4i v) { return v[1]; }
int first = v4i{4, 5, 6, 7}[0];

// Loading a vector with a braced list built from a pack expansion over an
// index sequence, as in __load_vector, __partial_load and __broadcast
// (libc++ writes them as lambda templates; both forms are here).
template <unsigned...> struct seq {};
template <class V, class P, unsigned... I>
V load_from(P p, seq<I...>) { return V{p[I]...}; }
template <class V, class T, unsigned... I>
V broadcast(T x, seq<I...>) { return V{((void)I, x)...}; }
int arr[4] = {1, 2, 3, 4};
v4i l0 = load_from<v4i>(arr, seq<0, 1, 2, 3>{});
v4i l1 = broadcast<v4i>(7, seq<0, 1, 2, 3>{});
template <class V, class P>
V load_lambda(P p) {
  return [=]<unsigned... I>(seq<I...>) { return V{p[I]...}; }(seq<0, 1, 2, 3>{});
}
v4i l2 = load_lambda<v4i>(arr);

int main() { return 0; }
