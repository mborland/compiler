//type:fp
//options_all:--c++20
//remark:[GH #?] A function that has a registered attribute with the flag RAF_NO_CONSTANT_EVALUATION is an ordinary function otherwise
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
// 10/18/26  [GH #?]
//
// See GH-no-constant-evaluation.  The functions that have the attribute are called
// at run time, appear in unevaluated operands, have their address taken, and
// constexpr functions that call them are fine when they are not evaluated at
// compile time; the same functions without the attribute (and with an attribute
// that does not have the flag) are evaluated at compile time as before.

// The functions that have the attribute are ordinary functions otherwise.
[[edg::r_noconst]] constexpr int p(int x) { return x + 1; }
[[edg::r_noconst]] constexpr bool q(int x) { return x >= 0; }
struct M { [[edg::r_noconst]] constexpr int m(int x) const { return x + 1; } };
struct K { [[edg::r_noconst]] constexpr K(int x) : v(x) {} int v; };
template <class T> [[edg::r_noconst]] constexpr T tp(T x) { return x + 1; }

// Run-time calls.
int rt(int x) { return p(x) + q(x) + M().m(x) + K(x).v + tp(x); }

// Unevaluated operands (nothing is evaluated).
decltype(p(1)) d1 = 0;
int sz = (int)sizeof(p(1));
bool nx = noexcept(q(1));
int sz2 = (int)sizeof(tp(1));

// The address of the function; a constexpr function that calls it and is not
// evaluated at compile time; a call through a function that is not evaluated.
int (*fp)(int) = &p;
constexpr int g(int y) { return p(y) + 1; }
int use_g(int y) { return g(y); }
template <class T> constexpr int h(T y) { return p(y); }
int use_h(int y) { return h(y); }

// The same functions without the attribute are constant-evaluated.
constexpr int p2(int x) { return x + 1; }
int a1[p2(3)];
template <int N> struct S { int v = N; };
S<p2(3)> s1;
static_assert(p2(1) == 2, "p2");
constexpr int g2(int y) { return p2(y) + 1; }
int a2[g2(1)];

// An attribute that has no such flag does not stop the evaluation.
[[edg::r_fn]] constexpr int p3(int x) { return x + 1; }
int a3[p3(3)];
static_assert(p3(1) == 2, "p3");
