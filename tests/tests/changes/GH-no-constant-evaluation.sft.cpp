//type:fn
//options_all:--c++20
//cases:29
//remark:[GH #?] A call of a function that has a registered attribute with the flag RAF_NO_CONSTANT_EVALUATION is not evaluated at compile time
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//match_regex:(line 27: error: expression\s+must\s+have\s+a\s+constant\s+value|line 32: error: expression\s+must\s+have\s+a\s+constant\s+value|line 37: error: expression\s+must\s+have\s+a\s+constant\s+value|line 42: error: expression\s+must\s+have\s+a\s+constant\s+value|line 46: error: expression\s+must\s+have\s+a\s+constant\s+value|line 50: error: expression\s+must\s+have\s+a\s+constant\s+value|line 54: error: expression\s+must\s+have\s+a\s+constant\s+value|line 58: error: expression\s+must\s+have\s+a\s+constant\s+value|line 62: error: expression\s+must\s+have\s+a\s+constant\s+value|line 66: error: expression\s+must\s+have\s+a\s+constant\s+value|line 71: error: expression\s+must\s+have\s+a\s+constant\s+value|line 75: error: expression\s+must\s+have\s+a\s+constant\s+value|line 79: error: expression\s+must\s+have\s+a\s+constant\s+value|line 83: error: expression\s+must\s+have\s+a\s+constant\s+value|line 88: error: expression\s+must\s+have\s+a\s+constant\s+value|line 92: error: expression\s+must\s+have\s+a\s+constant\s+value|line 97: error: call\s+to\s+consteval\s+function\s+"ce"\s+did\s+not\s+produce\s+a\s+valid\s+constant\s+expression|line 101: error: initialization\s+is\s+not\s+constant|line 105: error: expression\s+must\s+have\s+a\s+constant\s+value|line 109: error: an\s+attribute\s+that\s+forbids\s+constant\s+evaluation\s+cannot\s+be\s+applied\s+to\s+a\s+function\s+that\s+is\s+defaulted|line 114: error: an\s+attribute\s+that\s+forbids\s+constant\s+evaluation\s+cannot\s+be\s+applied\s+to\s+a\s+function\s+that\s+is\s+defaulted|line 119: error: an\s+attribute\s+that\s+forbids\s+constant\s+evaluation\s+cannot\s+be\s+applied\s+to\s+a\s+function\s+that\s+is\s+defaulted|line 123: error: an\s+attribute\s+that\s+forbids\s+constant\s+evaluation\s+cannot\s+be\s+applied\s+to\s+a\s+function\s+that\s+is\s+defaulted|line 127: error: an\s+attribute\s+that\s+forbids\s+constant\s+evaluation\s+cannot\s+be\s+applied\s+to\s+a\s+function\s+that\s+is\s+defaulted|line 133: error: expression\s+must\s+have\s+a\s+constant\s+value|line 140: error: expression\s+must\s+have\s+a\s+constant\s+value|line 147: error: expression\s+must\s+have\s+a\s+constant\s+value|line 151: error: attribute\s+"edg::r_noconst"\s+does\s+not\s+take\s+arguments|line 156: error: attribute\s+"edg::r_noconst"\s+does\s+not\s+take\s+arguments)
//match_regex:(line 27: note: cannot\s+call\s+[^\n]*\s+in\s+a\s+constant\s+expression:\s+an\s+attribute\s+of\s+the\s+function\s+does\s+not\s+allow\s+it|line 32: note: cannot\s+call\s+[^\n]*\s+in\s+a\s+constant\s+expression:\s+an\s+attribute\s+of\s+the\s+function\s+does\s+not\s+allow\s+it|line 37: note: cannot\s+call\s+[^\n]*\s+in\s+a\s+constant\s+expression:\s+an\s+attribute\s+of\s+the\s+function\s+does\s+not\s+allow\s+it|line 42: note: cannot\s+call\s+[^\n]*\s+in\s+a\s+constant\s+expression:\s+an\s+attribute\s+of\s+the\s+function\s+does\s+not\s+allow\s+it|line 46: note: cannot\s+call\s+[^\n]*\s+in\s+a\s+constant\s+expression:\s+an\s+attribute\s+of\s+the\s+function\s+does\s+not\s+allow\s+it|line 50: note: cannot\s+call\s+[^\n]*\s+in\s+a\s+constant\s+expression:\s+an\s+attribute\s+of\s+the\s+function\s+does\s+not\s+allow\s+it|line 54: note: cannot\s+call\s+[^\n]*\s+in\s+a\s+constant\s+expression:\s+an\s+attribute\s+of\s+the\s+function\s+does\s+not\s+allow\s+it|line 58: note: cannot\s+call\s+[^\n]*\s+in\s+a\s+constant\s+expression:\s+an\s+attribute\s+of\s+the\s+function\s+does\s+not\s+allow\s+it|line 62: note: cannot\s+call\s+[^\n]*\s+in\s+a\s+constant\s+expression:\s+an\s+attribute\s+of\s+the\s+function\s+does\s+not\s+allow\s+it|line 66: note: cannot\s+call\s+[^\n]*\s+in\s+a\s+constant\s+expression:\s+an\s+attribute\s+of\s+the\s+function\s+does\s+not\s+allow\s+it|line 70: note: cannot\s+call\s+[^\n]*\s+in\s+a\s+constant\s+expression:\s+an\s+attribute\s+of\s+the\s+function\s+does\s+not\s+allow\s+it|line 75: note: cannot\s+call\s+[^\n]*\s+in\s+a\s+constant\s+expression:\s+an\s+attribute\s+of\s+the\s+function\s+does\s+not\s+allow\s+it|line 79: note: cannot\s+call\s+[^\n]*\s+in\s+a\s+constant\s+expression:\s+an\s+attribute\s+of\s+the\s+function\s+does\s+not\s+allow\s+it|line 83: note: cannot\s+call\s+[^\n]*\s+in\s+a\s+constant\s+expression:\s+an\s+attribute\s+of\s+the\s+function\s+does\s+not\s+allow\s+it|line 88: note: cannot\s+call\s+[^\n]*\s+in\s+a\s+constant\s+expression:\s+an\s+attribute\s+of\s+the\s+function\s+does\s+not\s+allow\s+it|line 92: note: cannot\s+call\s+[^\n]*\s+in\s+a\s+constant\s+expression:\s+an\s+attribute\s+of\s+the\s+function\s+does\s+not\s+allow\s+it|line 96: note: cannot\s+call\s+[^\n]*\s+in\s+a\s+constant\s+expression:\s+an\s+attribute\s+of\s+the\s+function\s+does\s+not\s+allow\s+it|line 101: note: cannot\s+call\s+[^\n]*\s+in\s+a\s+constant\s+expression:\s+an\s+attribute\s+of\s+the\s+function\s+does\s+not\s+allow\s+it|line 105: note: cannot\s+call\s+[^\n]*\s+in\s+a\s+constant\s+expression:\s+an\s+attribute\s+of\s+the\s+function\s+does\s+not\s+allow\s+it|line 133: note: cannot\s+call\s+[^\n]*\s+in\s+a\s+constant\s+expression:\s+an\s+attribute\s+of\s+the\s+function\s+does\s+not\s+allow\s+it|line 139: note: cannot\s+call\s+[^\n]*\s+in\s+a\s+constant\s+expression:\s+an\s+attribute\s+of\s+the\s+function\s+does\s+not\s+allow\s+it|line 146: note: cannot\s+call\s+[^\n]*\s+in\s+a\s+constant\s+expression:\s+an\s+attribute\s+of\s+the\s+function\s+does\s+not\s+allow\s+it|line 109: error: an\s+attribute\s+that\s+forbids\s+constant\s+evaluation\s+cannot\s+be\s+applied\s+to\s+a\s+function\s+that\s+is\s+defaulted|line 114: error: an\s+attribute\s+that\s+forbids\s+constant\s+evaluation\s+cannot\s+be\s+applied\s+to\s+a\s+function\s+that\s+is\s+defaulted|line 119: error: an\s+attribute\s+that\s+forbids\s+constant\s+evaluation\s+cannot\s+be\s+applied\s+to\s+a\s+function\s+that\s+is\s+defaulted|line 123: error: an\s+attribute\s+that\s+forbids\s+constant\s+evaluation\s+cannot\s+be\s+applied\s+to\s+a\s+function\s+that\s+is\s+defaulted|line 127: error: an\s+attribute\s+that\s+forbids\s+constant\s+evaluation\s+cannot\s+be\s+applied\s+to\s+a\s+function\s+that\s+is\s+defaulted|line 151: error: attribute\s+"edg::r_noconst"\s+does\s+not\s+take\s+arguments|line 156: error: attribute\s+"edg::r_noconst"\s+does\s+not\s+take\s+arguments)
// 10/18/26  [GH #?]
//
// A registered attribute that has the flag RAF_NO_CONSTANT_EVALUATION (the test attribute
// edg::r_noconst) on a function, on any of its declarations (or on a template
// whose instances are the function), makes the front end refuse to evaluate a call
// of the function at compile time: where a constant is needed (an array bound, a
// template argument, a static_assert, a constexpr variable, an enumerator, a case
// label, ...) the call is not a constant expression, and the diagnostic says that
// an attribute of the function does not allow it, as for a call of a function that
// is not constexpr.  GH-no-constant-evaluation-ok shows that nothing else changes:
// a function that has the attribute is called at run time and appears in
// unevaluated operands as any function does.  The attribute is for programs that
// describe a function that they do not want the compiler to compute with.  Every
// case has its own diagnostic; the expression above has an alternative for each
// case, anchored to its line.

#if TEST_NUMBER == 1
// An array bound (namespace scope).
[[edg::r_noconst]] constexpr int p(int x) { return x + 1; }
int a[p(3)];
#elif TEST_NUMBER == 2
// A template argument of a class template.
[[edg::r_noconst]] constexpr int p(int x) { return x + 1; }
template <int N> struct S { int v = N; };
S<p(3)> s;
#elif TEST_NUMBER == 3
// A template argument in a function body.
[[edg::r_noconst]] constexpr int p(int x) { return x + 1; }
template <int N> struct S { int v = N; };
int f() { S<p(1)> s; return s.v; }
#elif TEST_NUMBER == 4
// An explicit template argument of a function template.
[[edg::r_noconst]] constexpr int p(int x) { return x + 1; }
template <int N> int ft() { return N; }
int f() { return ft<p(1)>(); }
#elif TEST_NUMBER == 5
// A static_assert at namespace scope.
[[edg::r_noconst]] constexpr bool q(int x) { return x >= 0; }
static_assert(q(3), "q");
#elif TEST_NUMBER == 6
// A static_assert in a class.
[[edg::r_noconst]] constexpr bool q(int x) { return x >= 0; }
struct C { static_assert(q(1), "q"); };
#elif TEST_NUMBER == 7
// A static_assert in a body.
[[edg::r_noconst]] constexpr bool q(int x) { return x >= 0; }
int f() { static_assert(q(1), "q"); return 0; }
#elif TEST_NUMBER == 8
// A constexpr variable.
[[edg::r_noconst]] constexpr int p(int x) { return x + 1; }
constexpr int v = p(2);
#elif TEST_NUMBER == 9
// An enumerator.
[[edg::r_noconst]] constexpr int p(int x) { return x + 1; }
enum E { A = p(1) };
#elif TEST_NUMBER == 10
// A case label.
[[edg::r_noconst]] constexpr int p(int x) { return x + 1; }
int f(int x) { switch (x) { case p(1): return 1; default: return 0; } }
#elif TEST_NUMBER == 11
// A constexpr function that calls it, called in a constant expression.
[[edg::r_noconst]] constexpr int p(int x) { return x + 1; }
constexpr int g(int y) { return p(y) + 1; }
int a[g(1)];
#elif TEST_NUMBER == 12
// A member function.
struct M { [[edg::r_noconst]] constexpr int m(int x) const { return x + 1; } };
int a[M().m(1)];
#elif TEST_NUMBER == 13
// A constructor.
struct K { [[edg::r_noconst]] constexpr K(int x) : v(x) {} int v; };
constexpr K k(1);
#elif TEST_NUMBER == 14
// A function template: the instances have the attribute.
template <class T> [[edg::r_noconst]] constexpr T tp(T x) { return x + 1; }
int a[tp(2)];
#elif TEST_NUMBER == 15
// The attribute is on a redeclaration that follows the definition.
constexpr int p(int x) { return x + 1; }
constexpr int p(int x) [[edg::r_noconst]];
int a[p(2)];
#elif TEST_NUMBER == 16
// The attribute is after the parameter list.
constexpr int p(int x) [[edg::r_noconst]] { return x + 1; }
int a[p(2)];
#elif TEST_NUMBER == 17
// A consteval function that calls it.
[[edg::r_noconst]] constexpr int p(int x) { return x + 1; }
consteval int ce(int y) { return p(y); }
int f() { return ce(2); }
#elif TEST_NUMBER == 18
// A destructor (the constant destruction of a constexpr variable).
struct D { constexpr D() {} [[edg::r_noconst]] constexpr ~D() {} };
constexpr D d;
#elif TEST_NUMBER == 19
// The attribute is not the first one of the function.
[[edg::r_fn]] [[edg::r_noconst]] constexpr int p(int x) { return x + 1; }
int a[p(2)];
#elif TEST_NUMBER == 20
// A defaulted copy constructor cannot have the attribute (the call of a trivial
// one copies the bits).
struct S { int a; constexpr S(int x) : a(x) {} [[edg::r_noconst]] S(const S&) = default; };
constexpr S s1{1};
constexpr S s2 = s1;
#elif TEST_NUMBER == 21
// A defaulted copy assignment operator.
struct S { int a; [[edg::r_noconst]] S& operator=(const S&) = default; };
constexpr int g() { S s{1}, t{2}; s = t; return s.a; }
static_assert(g() == 2);
#elif TEST_NUMBER == 22
// A defaulted move constructor.
struct S { int a; constexpr S(int x) : a(x) {} [[edg::r_noconst]] S(S&&) = default; };
constexpr S s2 = static_cast<S&&>(S(1));
#elif TEST_NUMBER == 23
// A defaulted default constructor.
struct S { int a; [[edg::r_noconst]] S() = default; };
constexpr S s{};
#elif TEST_NUMBER == 24
// A defaulted destructor.
struct D { int v = 1; [[edg::r_noconst]] ~D() = default; };
constexpr D d;
#elif TEST_NUMBER == 25
// A library function that is implicitly aliased to a built-in function, declared
// again with the attribute (the built-in evaluation does not bypass it).
extern "C" [[edg::r_noconst]] unsigned long strlen(const char*);
constexpr unsigned long n = strlen("abc");
#elif TEST_NUMBER == 26
// A virtual function that has the attribute, called through the base class: the
// call that names it is refused, whatever the override.
struct B { [[edg::r_noconst]] constexpr virtual int f() const { return 1; } };
struct D : B { constexpr int f() const override { return 2; } };
constexpr int g() { D d; const B& b = d; return b.f(); }
static_assert(g() == 2);
#elif TEST_NUMBER == 27
// An override that has the attribute, called through a base class function that
// does not: the call that is dispatched to it is refused.
struct B { constexpr virtual int f() const { return 1; } };
struct D : B { [[edg::r_noconst]] constexpr int f() const override { return 2; } };
constexpr int g() { D d; const B& b = d; return b.f(); }
static_assert(g() == 2);
#elif TEST_NUMBER == 28
// The attribute does not take arguments: an error (and the attribute does not
// count as one that forbids the evaluation, without an internal error).
[[edg::r_noconst(1)]] constexpr int p(int x) { return x + 1; }
int a[p(1)];
#elif TEST_NUMBER == 29
// The same, the argument names a parameter of a later declaration.
constexpr int p(int x);
[[edg::r_noconst(x)]] constexpr int p(int x) { return x + 1; }
int a[p(1)];
#endif
