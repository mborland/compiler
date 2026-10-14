//type:fn
//options_all:--c++20
//remark:[GH #?] A registered attribute cannot refer to a parameter that follows a function parameter pack
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//cases:3
//match_regex:(line 21: error: attribute\s+"edg::r_expr"\s+cannot\s+refer\s+to\s+a\s+function\s+parameter\s+that\s+follows\s+a\s+function\s+parameter\s+pack|line 24: error: attribute\s+"edg::r_expr"\s+cannot\s+refer\s+to\s+a\s+function\s+parameter\s+that\s+follows\s+a\s+function\s+parameter\s+pack|line 27: error: attribute\s+"edg::r_expr"\s+cannot\s+refer\s+to\s+a\s+function\s+parameter\s+that\s+follows\s+a\s+function\s+parameter\s+pack)
// 10/15/26  [GH #?]
//
// A registered attribute on a function declaration (here after the parameter
// list, where the parameters are in scope) cannot refer to a parameter that
// follows a function parameter pack: in an instance the pack is expanded, so
// the number of the parameter in the attribute (which is copied from the
// prototype) would be that of another parameter.  The error is in the
// prototype.  (A reference to the pack has its own diagnostic, and a reference
// to a parameter before the pack is fine.)  Every case has its own diagnostic; the expression above has an alternative for each case,
// anchored to the line of its error.

#if TEST_NUMBER == 1
// A function template: the parameter after the pack has another number in an
// instance.
template <class... Ts> int f1(Ts... a, int y) [[edg::r_expr(y)]];
#elif TEST_NUMBER == 2
// A member of a class template with a pack.
template <class... Ts> struct S2 { int m(Ts... xs, int y) [[edg::r_expr(y >= 0)]]; };
#elif TEST_NUMBER == 3
// A parameter that follows the pack, in a larger expression.
template <class... Ts> int f3(Ts... a, int y, int z) [[edg::r_expr(z + 1)]];
#endif
