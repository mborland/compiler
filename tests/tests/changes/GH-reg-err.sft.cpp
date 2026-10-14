//type:fn
//options_all:--c++20 --instantiate=used
//remark:[GH #?] Errors in registered attributes
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//cases:19
//match_regex:(line 18: error: unknown\s+attribute\s+"edg::r_stmtx"|line 21: error: identifier\s+"undeclared_c2"\s+is\s+undefined|line 25: error: expression\s+must\s+have\s+bool\s+type\s+\(or\s+be\s+convertible\s+to\s+bool\)|line 28: error: attribute\s+"edg::r_fn"\s+does\s+not\s+apply\s+here|line 31: error: attribute\s+"edg::requires"\s+does\s+not\s+apply\s+here|line 34: error: attribute\s+"edg::r_stmt"\s+does\s+not\s+apply\s+here|line 37: error: expected\s+a\s+"\("|line 40: error: expected\s+a\s+"\)"|line 43: error: attribute\s+"edg::r_fn"\s+does\s+not\s+take\s+arguments|line 46: error: attribute\s+"edg::requires"\s+does\s+not\s+apply\s+here|line 50: error: attribute\s+"edg::r_expr"\s+cannot\s+refer\s+to\s+a\s+function\s+parameter\s+pack|line 53: error: attribute\s+"edg::requires"\s+cannot\s+be\s+a\s+pack\s+expansion|line 56: error: attribute\s+"edg::r_expr"\s+cannot\s+be\s+a\s+pack\s+expansion|line 59: error: attribute\s+"edg::requires"\s+cannot\s+refer\s+to\s+a\s+function\s+parameter\s+pack|line 62: error: unknown\s+attribute\s+"edg::r_stmtx"|line 67: error: expression\s+must\s+have\s+class\s+type\s+but\s+it\s+has\s+type\s+"int"|line 72: error: attribute\s+does\s+not\s+apply\s+to\s+any\s+entity|line 75: error: attribute\s+"edg::r_fn"\s+does\s+not\s+apply\s+here|line 78: error: attributes\s+are\s+not\s+allowed\s+here)
// 10/14/26  [GH #?]
//
// Misuse of a registered attribute is an error, with a diagnostic that is
// specific to the mistake.  Every case has its own diagnostic.  The one
// regular expression above has an alternative for each case, anchored to the
// line of the case, so each case must produce its own diagnostic at its own
// line (the same expression applies to every case, and a case cannot match
// another case's alternative because that is at another line).

#if TEST_NUMBER == 1
// A name that is not registered in a closed namespace is an error.
void f1() { [[edg::r_stmtx]]; }
#elif TEST_NUMBER == 2
// An undeclared name in an argument.
void f2() { [[edg::r_stmt(undeclared_c2)]]; }
#elif TEST_NUMBER == 3
// A class type that cannot be converted to bool.
struct S3 {};
void f3(S3 s) { [[edg::r_stmt(s)]]; }
#elif TEST_NUMBER == 4
// A routine attribute on a variable.
[[edg::r_fn]] int v4;
#elif TEST_NUMBER == 5
// An attribute on a class.
struct [[edg::requires(true)]] T5 {};
#elif TEST_NUMBER == 6
// A statement attribute on a declaration.
[[edg::r_stmt(true)]] int v6;
#elif TEST_NUMBER == 7
// The arguments are required.
void f7() { [[edg::r_stmt]]; }
#elif TEST_NUMBER == 8
// Too many arguments.
void f8() { [[edg::r_stmt(true, true)]]; }
#elif TEST_NUMBER == 9
// Arguments on an attribute that takes none.
[[edg::r_fn(1)]] int f9();
#elif TEST_NUMBER == 10
// An attribute on an empty declaration.
[[edg::requires(true)]];
#elif TEST_NUMBER == 11
// A function parameter pack in an argument (after the parameter list, where
// the parameters are in scope).
template <class... Ts> int f11(Ts... args) [[edg::r_expr(sizeof...(args))]];
#elif TEST_NUMBER == 12
// A pack expansion of an argument.
template <class... Ts> int f12(Ts... args) [[edg::requires(sizeof(Ts) > 0 ...)]];
#elif TEST_NUMBER == 13
// A pack expansion of the attribute.
template <class... Ts> int f13(Ts... args) [[edg::r_expr(sizeof(Ts) > 0)...]];
#elif TEST_NUMBER == 14
// A fold expression over a function parameter pack.
template <class... Ts> int f14(Ts... args) [[edg::requires(((args > 0) && ...))]];
#elif TEST_NUMBER == 15
// The "using" form of the attribute namespace.
void f15() { [[using edg: r_stmtx]]; }
#elif TEST_NUMBER == 16
// A function parameter pack in a statement attribute is substituted for each
// instance (an empty substitution would have been accepted silently): the
// instance gets the error.
template <class... Ts> void f16(Ts... a) { [[edg::r_stmt((a.x && ...))]]; }
void u16() { f16(1); }
#elif TEST_NUMBER == 17
// An enumeration of a template parameter type, in a template (the front end
// drops the attributes here; the end of the translation unit diagnoses it).
template <class T> void f17() { enum [[edg::r_fn]] T x; }
#elif TEST_NUMBER == 18
// An alias template (never used) cannot have a registered attribute.
template <class T> using AL18 [[edg::r_fn]] = T;
#elif TEST_NUMBER == 19
// A static_assert declaration.
[[edg::r_fn]] static_assert(true, "");
#endif
