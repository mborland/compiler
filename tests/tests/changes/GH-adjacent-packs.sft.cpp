//type:fp
//options:--c++11:--c++14:--c++17:--c++20:--c++23
//remark:[GH #?] Two function parameter packs in a row named in a trailing return type or an exception specification
// 10/15/26  [GH #?]
//
// A function parameter pack named after the parameter list of a function
// template (in a trailing return type, an exception specification or an
// attribute argument) was given the elements of the function parameter packs that follow
// it immediately: when the elements of the parameters were counted for an
// instance, every parameter that was an element of a pack counted for the
// pack, whatever pack it belonged to.  For two function parameter packs in a
// row (Ts... as, Us... bs), a use of both in one expansion
// (decltype(g((as + bs)...)), noexcept(noexcept(g((as + bs)...))), a fold
// expression over both) was therefore an error for an instance of the template,
// "pack "bs" does not have the same number of elements as "as"", where the
// packs have the same length.  Only the parameters that have the number of the
// pack are its elements now.
//
// None of the declarations below is an error, for any number of elements (as
// for Clang and GCC).  Packs of different lengths are still an error
// (GH-adjacent-packs-mismatch).

// Two function parameter packs that follow each other, both named after the
// parameter list of a function template: in a trailing return type and in an
// exception specification.  No enable_if involved.
template <class... Vs> int g(Vs...);

// Cross-pack expansion in a trailing return type.
template <class... Ts, class... Us>
auto tr(Ts... as, Us... bs) -> decltype(g((as + bs)...)) { return 0; }
int a0 = tr<int, int>(1, 2, 3, 4);
int a1 = tr<int, int, int>(1, 2, 3, 4, 5, 6);
int a2 = tr<>();
int a3 = tr<int>(1, 2);

// The same in an exception specification.
template <class... Ts, class... Us>
void ne(Ts... as, Us... bs) noexcept(noexcept(g((as + bs)...)));
void b0() { ne<int, int>(1, 2, 3, 4); }
void b1() { ne<>(); }

// The second pack only, and the first only, with the other one empty.
template <class... Ts, class... Us>
auto second(Ts... as, Us... bs) -> decltype(g(bs...)) { return 0; }
int c0 = second<>(1, 2);                       // first pack empty
int c1 = second<int, int>(1, 2);               // second pack empty
template <class... Ts, class... Us>
auto first(Ts... as, Us... bs) -> decltype(g(as...)) { return 0; }
int c2 = first<int, int>(1, 2, 3);
int c3 = first<>(1, 2);

// A pack between two others (not deduced, so empty) and a parameter between
// two packs.
template <class... Ts, class... Us, class... Vs>
auto three(Ts... as, Us... bs, Vs... cs) -> decltype(g((as + cs)...)) {
  return 0;
}
int d0 = three<int, int>(1, 2, 3, 4);
template <class... Ts, class... Us>
auto between(Ts... as, int m, Us... bs) -> decltype(g((as + bs + m)...)) {
  return 0;
}
int d1 = between<int, int>(1, 2, 5, 3, 4);
struct S {
  template <class... Ts, class... Us>
  auto mem(Ts... as, Us... bs) -> decltype(g((as + bs)...)) { return 0; }
  template <class... Ts, class... Us>
  static void smem(Ts... as, Us... bs) noexcept(noexcept(g((as + bs)...)));
};
int e0 = S().mem<int, int>(1, 2, 3, 4);
void e1() { S::smem<int, int>(1, 2, 3, 4); }

// A class parameter pack in a row with a function parameter pack of a member
// template (the class pack is already expanded in the instance of the class).
template <class... Cs>
struct CP {
  template <class... Ts>
  auto m(Cs... cs, Ts... ts) -> decltype(g((cs + ts)...)) { return 0; }
  template <class... Ts>
  static auto sm(Cs... cs, Ts... ts) noexcept(noexcept(g((cs + ts)...))) -> int {
    return 0;
  }
};
CP<int, int> cp;
int g0 = cp.m(1, 2, 3, 4);
int g1 = CP<int, int>::sm(1, 2, 3, 4);

// Three packs in a row: a class pack and two function packs, used in two
// expansions of one expression and in different combinations, with an empty
// pack between two that are not.  (Clang does not accept the call of a member
// template that uses all three in one expansion with explicit arguments for the
// first of the function packs, which GCC accepts; it is not in this test.)
template <class... Cs>
struct CQ {
  template <class... Ts, class... Us>
  auto n(Cs... cs, Ts... ts, Us... us) -> decltype(g((ts + us)..., cs...)) {
    return 0;
  }
  template <class... Ts, class... Us>
  auto k(Cs... cs, Ts... ts, Us... us) -> decltype(g((cs + us)...)) { return 0; }
};
CQ<int, int> cq;
int h0 = cq.n<int, int>(1, 2, 3, 4, 5, 6);
int h1 = cq.k<int, int>(1, 2, 3, 4, 5, 6);
int h2 = cq.k<>(1, 2, 3, 4);                   // the first function pack is empty

#if __cplusplus >= 201703L
// Fold expressions over both packs.
template <class... Ts, class... Us>
auto tf(Ts... as, Us... bs) -> decltype(((as + bs) + ... + 0)) { return 0; }
int f0 = tf<int, int>(1, 2, 3, 4);
template <class... Ts, class... Us>
void nf(Ts... as, Us... bs) noexcept(noexcept(((void)(as + bs), ...)));
void f1() { nf<int, int>(1, 2, 3, 4); }
template <class... Ts, class... Us>
auto cmp(Ts... as, Us... bs) -> decltype(((as > bs) && ...)) { return true; }
bool f2 = cmp<int, int>(5, 6, 1, 2);
#endif
