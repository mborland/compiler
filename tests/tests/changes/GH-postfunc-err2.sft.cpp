//type:fn
//options_all:--c++20 --instantiate=used
//remark:[GH #?] More errors in registered attributes after the parameter list
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//cases:9
//match_regex:(line 18: error: attribute\s+"edg::r_pre"\s+does\s+not\s+apply\s+here|line 22: error: attribute\s+"edg::r_pre"\s+does\s+not\s+apply\s+here|line 30: error: attribute\s+"edg::r_pre"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 36: error: attribute\s+"edg::r_pre"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 44: error: attribute\s+"edg::r_pre"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 48: error: (expression\s+must\s+have\s+class\s+type\s+but\s+it\s+has\s+type\s+"int"|bad\s+attribute\s+argument\s+substitution)|line 55: error: bad\s+attribute\s+argument\s+substitution|line 60: error: attribute\s+"edg::r_pre"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 64: error: attribute\s+"edg::r_pre"\s+does\s+not\s+apply\s+here)
//match_regex:\A(?![\s\S]*line 64: error: attribute\s+does\s+not\s+apply\s+to\s+any\s+entity)
// 10/14/26  [GH #?]
//
// More errors in registered attributes after the parameter list, in
// particular for function templates and their instances and specializations.
// Every case has its own diagnostic; the expression above has an alternative
// for each case, anchored to the line of its error.

#if TEST_NUMBER == 1
// A declaration at block scope has no entity to attach the contract to; it is
// an error (and a declaration at namespace scope can follow).
void u1() { int f1(int x) [[edg::r_pre(x > 1)]]; }
int f1(int x) [[edg::r_pre(x > 1)]];
#elif TEST_NUMBER == 2
// The same, with a file-scope redeclaration that has a different contract.
void u2() { int f2(int x) [[edg::r_pre(x > 1)]]; }
int f2(int x) [[edg::r_pre(x > 0)]];
#elif TEST_NUMBER == 3
// An explicit specialization declared (and defined) with a contract that is
// not the primary template's.
template <class T> int f3(T x);
template <> int f3<int>(int x) [[edg::r_pre(x > 5)]] { return x; }
template <> int f3<long>(long x);
template <class T> int f3(T x) [[edg::r_pre(x > 0)]];
int u3() { return f3(1) + f3(2L) + f3(3.0); }
#elif TEST_NUMBER == 4
// An explicit specialization declared after the primary template's contract
// has a different one.
template <class T> int f4(T x) [[edg::r_pre(x > 0)]];
template <> int f4<int>(int x) [[edg::r_pre(x > -5)]] { return x; }
int u4() { return f4(1); }
#elif TEST_NUMBER == 5
// An explicit specialization declared before the primary template's contract,
// then defined with a different one.
template <class T> int f5(T x);
template <> int f5<long>(long x);
template <class T> int f5(T x) [[edg::r_pre(x > 0)]];
template <> int f5<long>(long x) [[edg::r_pre(x > -5)]] { return 0; }
#elif TEST_NUMBER == 6
// A contract that cannot be substituted for an instance (the expression is an
// error for the type of the instance).
template <class T> int f6(T x) [[edg::r_pre(x.size() > 0)]];
int u6() { return f6(1); }
#elif TEST_NUMBER == 7
// The same, with the instance created before the contract is attached to the
// template.
template <class T> int f7(T x);
int u7() { return f7(1); }
template <class T> int f7(T x) [[edg::r_pre(x.size() > 0)]];
#elif TEST_NUMBER == 8
// The declarations of a function with C language linkage in two namespaces
// are one function: they must have the same contract.
namespace N8 { extern "C" int f8(int x) [[edg::r_pre(x > 0)]]; }
extern "C" int f8(int x) [[edg::r_pre(x > 1)]];
#elif TEST_NUMBER == 9
// A function with a trailing return type: the attribute after it is on the
// return type.
auto f9(int x) -> int [[edg::r_pre(x > 0)]];
#endif
