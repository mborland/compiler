//type:fn
//options_all:--c++20 --instantiate=used
//remark:[GH #?] Errors when a registered attribute names the result of a function
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//cases:25
//match_regex:(line 19: error: attribute\s+"edg::r_post"\s+cannot\s+name\s+the\s+result\s+of\s+a\s+function\s+that\s+returns\s+void|line 22: error: attribute\s+"edg::r_post"\s+cannot\s+name\s+the\s+result\s+of\s+a\s+function\s+whose\s+return\s+type\s+is\s+deduced|line 25: error: attribute\s+"edg::r_post"\s+cannot\s+name\s+the\s+result\s+of\s+a\s+function\s+whose\s+return\s+type\s+is\s+deduced|line 28: error: attribute\s+"edg::r_post"\s+cannot\s+name\s+the\s+result\s+of\s+a\s+function\s+whose\s+return\s+type\s+is\s+deduced|line 31: error: attribute\s+"edg::r_post"\s+cannot\s+name\s+the\s+result\s+of\s+a\s+constructor|line 34: error: attribute\s+"edg::r_post"\s+cannot\s+name\s+the\s+result\s+of\s+a\s+constructor|line 37: error: attribute\s+"edg::r_post"\s+cannot\s+name\s+the\s+result\s+of\s+a\s+constructor|line 40: error: declaration\s+of\s+"r"\s+hides\s+function\s+parameter|line 44: error: attribute\s+"edg::r_post"\s+can\s+name\s+the\s+result\s+only\s+after\s+the\s+parameter\s+list|line 47: error: attribute\s+"edg::r_post"\s+can\s+name\s+the\s+result\s+only\s+after\s+the\s+parameter\s+list|line 50: error: attribute\s+"edg::r_post"\s+can\s+name\s+the\s+result\s+only\s+after\s+the\s+parameter\s+list|line 54: error: attribute\s+"edg::r_post"\s+can\s+name\s+the\s+result\s+only\s+after\s+the\s+parameter\s+list|line 58: error: expected\s+a\s+"\)"|line 61: error: identifier\s+"r"\s+is\s+undefined|line 66: error: attribute\s+"edg::r_post"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 69: error: identifier\s+"r"\s+is\s+undefined|line 72: error: attribute\s+"edg::r_post"\s+can\s+name\s+the\s+result\s+only\s+after\s+the\s+parameter\s+list|line 75: error: expression\s+must\s+have\s+class\s+type|line 79: error: attribute\s+"edg::r_post"\s+cannot\s+refer\s+to\s+a\s+function\s+parameter\s+that\s+follows\s+a\s+function\s+parameter\s+pack|line 82: error: declaration\s+of\s+"T"\s+hides\s+template\s+parameter|line 86: error: identifier\s+"x"\s+is\s+undefined|line 90: error: attribute\s+"edg::r_post"\s+does\s+not\s+apply\s+here|line 95: error: attribute\s+"edg::r_post"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 101: error: attribute\s+"edg::r_post"\s+cannot\s+name\s+the\s+result\s+of\s+a\s+function\s+that\s+returns\s+void|line 107: error: attribute\s+"edg::r_post"\s+cannot\s+name\s+the\s+result\s+of\s+a\s+function\s+that\s+returns\s+void)
//match_regex:\A(?![\s\S]*unknown\s+attribute\s+"edg::r_post")
//match_regex:\A(?![\s\S]*cannot\s+name\s+the\s+result\s+of\s+a\s+function\s+that\s+returns\s+void[\s\S]*cannot\s+name\s+the\s+result\s+of\s+a\s+function\s+that\s+returns\s+void)
// 10/14/26  [GH #?]
//
// The result of a function can be named only after the parameter list of the
// declaration of a function that has a return type that is not deduced and
// not void, and the name must not be that of a parameter.  Every case has its
// own diagnostic; the expression above has an alternative for each case,
// anchored to the line of its error.

#if TEST_NUMBER == 1
// The result of a function that returns void cannot be named.
void c1(int x) [[edg::r_post(r, true)]];
#elif TEST_NUMBER == 2
// The same for a function that returns a deduced type.
auto c2(int x) [[edg::r_post(r, true)]] { return x; }
#elif TEST_NUMBER == 3
// And decltype(auto).
decltype(auto) c3(int x) [[edg::r_post(r, true)]] { return x; }
#elif TEST_NUMBER == 4
// A trailing return type: the attribute is scanned before the type is known.
auto c4(int x) [[edg::r_post(r, true)]] -> int;
#elif TEST_NUMBER == 5
// A constructor has no result.
struct C5 { C5() [[edg::r_post(r, true)]]; };
#elif TEST_NUMBER == 6
// A destructor has no result.
struct C6 { ~C6() [[edg::r_post(r, true)]]; };
#elif TEST_NUMBER == 7
// A conversion function has no declared result type.
struct C7 { operator int() [[edg::r_post(r, true)]]; };
#elif TEST_NUMBER == 8
// The name of the result cannot be the name of a parameter.
int c8(int r) [[edg::r_post(r, true)]];
#elif TEST_NUMBER == 9
// Not after the parameter list of a function declaration: a pointer to
// function.
int (*c9)(int x) [[edg::r_post(r, true)]];
#elif TEST_NUMBER == 10
// A function type (a typedef).
typedef int F10(int x) [[edg::r_post(r, true)]];
#elif TEST_NUMBER == 11
// A lambda.
auto c11 = [](int x) [[edg::r_post(r, true)]] { return x; };
#elif TEST_NUMBER == 12
// A variable (the attribute does not apply, so the name of the result is not
// the problem).
[[edg::r_post(r, true)]] int c12;
#elif TEST_NUMBER == 13
// Without an identifier and a comma first there is no name: the first argument
// is the condition, and a second argument is one too many.
int c13(int x) [[edg::r_post(1, true)]];
#elif TEST_NUMBER == 14
// A single identifier is the condition, not a name for the result (no comma).
int c14(int x) [[edg::r_post(r)]];
#elif TEST_NUMBER == 15
// The names of the result and the parameters do not matter, the predicates
// must match.
int c15(int x) [[edg::r_post(r, r > x)]];
int c15(int y) [[edg::r_post(q, q > y + 1)]];
#elif TEST_NUMBER == 16
// The name of the result is not in scope after the attribute.
int c16(int x) [[edg::r_post(r, true)]] [[edg::r_pre(r > 0)]];
#elif TEST_NUMBER == 17
// A block-scope declaration is not an entity that can have the attribute.
void c17() { int f(int x) [[edg::r_post(r, true)]]; }
#elif TEST_NUMBER == 18
// The result is of the return type: no member of an int.
int c18(int x) [[edg::r_post(r, r.size() > 0)]];
#elif TEST_NUMBER == 19
// The parameter after a function parameter pack has another number in an
// instance.
template <class... Ts> int c19(Ts... a, int y) [[edg::r_post(r, r == y)]];
#elif TEST_NUMBER == 20
// The name of the result is the name of a template parameter.
template <class T> T c20(int x) [[edg::r_post(T, true)]];
#elif TEST_NUMBER == 21
// Without a name the first argument is the condition: it cannot use the
// parameters of the function in front of the declaration.
[[edg::r_post(x > 0)]] int c21(int x);
#elif TEST_NUMBER == 22
// Without a name the attribute still applies only after the parameter list of
// a function.
[[edg::r_post(true)]] int c22;
#elif TEST_NUMBER == 23
// A name for the result is a different attribute from one without a name (the
// result is not a parameter).
int c23(int x) [[edg::r_post(r, r > x)]];
int c23(int x) [[edg::r_post(x > 0)]];
#elif TEST_NUMBER == 24
// A name for the result of a member of a class template that returns void in
// an instance: the definition is outside the class, so the type of the result
// was dependent when it was scanned.
template <class T> struct S24 { T get(); };
template <class T> T S24<T>::get() [[edg::r_post(r, true)]] { return T(); }
void u24() { S24<void> s; s.get(); }
#elif TEST_NUMBER == 25
// A named result of a void instance of a function template is one error (the
// declaration is scanned again for the instance, and the attribute is copied
// to it).
template <class T> T c25() [[edg::r_post(r, true)]];
void u25() { c25<void>(); }
#endif
