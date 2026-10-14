//type:fn
//options_all:--c++20 --instantiate=used
//remark:[GH #?] A contract cannot refer to a parameter that follows a function parameter pack
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//cases:5
//match_regex:(line 22: error: attribute\s+"edg::r_pre"\s+cannot\s+refer\s+to\s+a\s+function\s+parameter\s+that\s+follows\s+a\s+function\s+parameter\s+pack|line 25: error: attribute\s+"edg::r_pre"\s+cannot\s+refer\s+to\s+a\s+function\s+parameter\s+that\s+follows\s+a\s+function\s+parameter\s+pack|line 28: error: attribute\s+"edg::r_pre"\s+cannot\s+refer\s+to\s+a\s+function\s+parameter\s+that\s+follows\s+a\s+function\s+parameter\s+pack|line 32: error: attribute\s+"edg::r_pre"\s+cannot\s+refer\s+to\s+a\s+function\s+parameter\s+that\s+follows\s+a\s+function\s+parameter\s+pack|line 35: error: attribute\s+"edg::r_pre"\s+cannot\s+refer\s+to\s+a\s+function\s+parameter\s+that\s+follows\s+a\s+function\s+parameter\s+pack)
// 10/15/26  [GH #?]
//
// A registered attribute after the parameter list (a contract) cannot refer to
// a parameter that follows a function parameter pack: in an instance the pack
// is expanded, so the number of the parameter in the attribute (which is copied
// from the prototype) would be that of another parameter.  The error is in the
// prototype, in function templates, abbreviated function templates and members
// of class templates with a pack, whether or not the template is instantiated.
// (A reference to a parameter before the pack is fine: see
// GH-postfunc-account-ok.)  Every case has its own diagnostic; the expression
// above has an alternative for each case, anchored to the line of its error.

#if TEST_NUMBER == 1
// A function template: the parameter after the pack has another number in an
// instance (for S<int,long> it would be the long).
template <class... Ts> int f1(Ts... a, int y) [[edg::r_pre(y >= 0)]];
#elif TEST_NUMBER == 2
// An abbreviated function template.
int f2(auto... a, int y) [[edg::r_pre(y >= 0)]];
#elif TEST_NUMBER == 3
// A member of a class template with a pack.
template <class... Ts> struct S3 { int m(Ts... xs, int y) [[edg::r_pre(y >= 0)]]; };
#elif TEST_NUMBER == 4
// The same for the attribute of a definition.
template <class... Ts> int f4(Ts... a, int y) { return y; }
template <class... Ts> int f4(Ts... a, int y) [[edg::r_pre(y >= 0)]] { return y; }
#elif TEST_NUMBER == 5
// A parameter that follows the pack, in a larger expression.
template <class... Ts> int f5(Ts... a, int y, int z) [[edg::r_pre(z + 1 > 0)]];
#endif
