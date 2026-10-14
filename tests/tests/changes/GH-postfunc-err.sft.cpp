//type:fn
//options_all:--c++20
//remark:[GH #?] Errors in registered attributes after the parameter list
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//cases:18
//match_regex:(line 23: error: attribute\s+"edg::r_pre"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 27: error: attribute\s+"edg::r_pre2"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 31: error: attribute\s+"edg::r_pre"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 35: error: attribute\s+"edg::r_pre"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 39: error: attribute\s+"edg::r_pre"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 43: error: attribute\s+"edg::r_pre2"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 46: error: standard\s+attributes\s+cannot\s+appear\s+on\s+friend\s+declarations\s+that\s+are\s+not\s+definitions|line 49: error: attribute\s+"edg::r_pre"\s+does\s+not\s+apply\s+here|line 52: error: attribute\s+"edg::r_pre"\s+does\s+not\s+apply\s+here|line 55: error: attribute\s+"edg::r_pre"\s+does\s+not\s+apply\s+here|line 58: error: identifier\s+"x11"\s+is\s+undefined|line 62: error: attribute\s+"edg::r_pre"\s+does\s+not\s+apply\s+here|line 65: error: attribute\s+"edg::r_pre"\s+does\s+not\s+apply\s+here|line 68: error: attribute\s+"edg::r_pre"\s+does\s+not\s+apply\s+here|line 72: error: attribute\s+"edg::r_pre"\s+does\s+not\s+apply\s+here|line 75: error: attribute\s+"edg::r_pre"\s+does\s+not\s+apply\s+here|line 79: error: attribute\s+"edg::r_pre"\s+does\s+not\s+apply\s+here|line 82: error: attribute\s+"edg::r_pre"\s+cannot\s+refer\s+to\s+a\s+function\s+parameter\s+pack)
// 10/14/26  [GH #?]
//
// The attributes of a declaration of a function that must match (r_pre and
// r_pre2 in the edg namespace) must match those of the earlier declaration
// that has any (the first cases), and a registered attribute after the
// parameter list is attached to the function only when that is the
// parameter list of the top-level declarator of a function declaration (and
// not of a function type: a pointer to function, a typedef or alias, a
// returned function type, a lambda, a requires-expression, a deduction
// guide, a template argument).  Every case has its own diagnostic; the
// expression above has an alternative for each case, anchored to the line of
// its error.

#if TEST_NUMBER == 1
// A different predicate on a later declaration.
int e1(int x) [[edg::r_pre(x > 0)]];
int e1(int x) [[edg::r_pre(x > 1)]];
#elif TEST_NUMBER == 2
// An extra attribute on the definition.
int e2(int x) [[edg::r_pre(x > 0)]];
int e2(int x) [[edg::r_pre(x > 0)]] [[edg::r_pre2(x < 9)]] { return x; }
#elif TEST_NUMBER == 3
// A later declaration that has only some of the attributes (the list is shorter).
int e3(int x) [[edg::r_pre(x > 0)]] [[edg::r_pre2(x < 9)]];
int e3(int x) [[edg::r_pre(x > 0)]];
#elif TEST_NUMBER == 4
// Function templates that differ.
template <class T> T e4(T x) [[edg::r_pre(x > 0)]];
template <class T> T e4(T x) [[edg::r_pre(x > 1)]] { return x; }
#elif TEST_NUMBER == 5
// A member defined outside its class with a different attribute.
struct M5 { int mm(int a) [[edg::r_pre(a > 0)]]; };
int M5::mm(int a) [[edg::r_pre(a > 1)]] { return a; }
#elif TEST_NUMBER == 6
// The same attributes in the other order.
int e6(int x) [[edg::r_pre(x > 0)]] [[edg::r_pre2(x < 9)]];
int e6(int x) [[edg::r_pre2(x < 9)]] [[edg::r_pre(x > 0)]];
#elif TEST_NUMBER == 7
// A friend declaration that is not a definition.
struct A7 { friend int ff(int x) [[edg::r_pre(x > 0)]]; };
#elif TEST_NUMBER == 8
// A pointer to function (the attribute is on the type, not a function).
int (*pf8)(int x) [[edg::r_pre(x > 0)]];
#elif TEST_NUMBER == 9
// A function typedef.
typedef int F9(int x) [[edg::r_pre(x > 0)]];
#elif TEST_NUMBER == 10
// A lambda.
auto l10 = [](int x) [[edg::r_pre(x > 0)]] { return x; };
#elif TEST_NUMBER == 11
// The attribute in front of the declaration cannot name the parameters.
[[edg::r_pre(x11 > 0)]] int e11(int x11);
#elif TEST_NUMBER == 12
// The attribute after the parameter list of the returned function type of a
// function that returns a pointer to function: not the declared function's.
int (*e12(int x))(double d) [[edg::r_pre(d > 0)]];
#elif TEST_NUMBER == 13
// Same, returning a reference to function.
int (&e13(int x))(double d) [[edg::r_pre(d > 0)]];
#elif TEST_NUMBER == 14
// The parameters of a requires-expression.
template <class T> concept C14 = requires (T x) [[edg::r_pre(x > 0)]] { x + 1; };
#elif TEST_NUMBER == 15
// A deduction guide.
template <class T> struct S15 { S15(T); };
template <class T> S15(T x) [[edg::r_pre(x > 0)]] -> S15<T>;
#elif TEST_NUMBER == 16
// An alias declaration of a function type.
using F16 = int(int x) [[edg::r_pre(x > 0)]];
#elif TEST_NUMBER == 17
// A function type as a template argument.
template <class T> struct Box17 {};
Box17<int(int y) [[edg::r_pre(y > 0)]]> b17;
#elif TEST_NUMBER == 18
// A function parameter pack.
template <class... Ts> int e18(Ts... a) [[edg::r_pre(((a > 0) && ...))]];
#endif
