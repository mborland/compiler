//type:fn
//options:--c++20:--c++20:--c++20:--c++20:--c++20:--c++20:--c++20:--c++20:--c++20:--c++20:--c++20:--clang_version=210000 --c++20:--clang_version=210000 --c++20:--clang_version=210000 --c++20:--clang_version=210000 --c++20:--clang_version=210000 --c++20:--clang_version=210000 --c++20:--clang_version=210000 --c++20:--clang_version=210000 --c++20:--clang_version=210000 --c++20:--clang_version=210000 --c++20:--clang_version=210000 --c++20:--gnu_version=150000 --c++20:--gnu_version=150000 --c++20:--gnu_version=150000 --c++20:--gnu_version=150000 --c++20:--gnu_version=150000 --c++20:--gnu_version=150000 --c++20:--gnu_version=150000 --c++20:--gnu_version=150000 --c++20:--gnu_version=150000 --c++20:--gnu_version=150000 --c++20:--gnu_version=150000 --c++20
//options_all:--instantiate=used
//remark:[GH #?] Errors in attribute target constraints for statements
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//cases:33
//match_regex:(line 22: error: attribute\s+"edg::r_null"\s+requires\s+a\s+null\s+statement|line 25: error: attribute\s+"edg::r_null"\s+requires\s+a\s+null\s+statement|line 28: error: attribute\s+"edg::r_loop"\s+requires\s+an\s+iteration\s+statement|line 31: error: attribute\s+"edg::r_loop"\s+requires\s+an\s+iteration\s+statement|line 34: error: attribute\s+"edg::r_loop"\s+requires\s+an\s+iteration\s+statement|line 37: error: attribute\s+"edg::r_loop"\s+requires\s+an\s+iteration\s+statement|line 40: error: attribute\s+"edg::r_nonull"\s+does\s+not\s+apply\s+to\s+a\s+null\s+statement|line 43: error: attribute\s+"edg::r_noloop"\s+does\s+not\s+apply\s+to\s+an\s+iteration\s+statement|line 46: error: attribute\s+"edg::r_noloop"\s+does\s+not\s+apply\s+to\s+an\s+iteration\s+statement|line 50: error: identifier\s+"i"\s+is\s+undefined|line 53: error: attribute\s+"edg::r_loop"\s+requires\s+an\s+iteration\s+statement)
// 10/14/26  [GH #?]
//
// A statement attribute whose target has a property switch is an error when
// the statement does not have the property, in every dialect (clang and GNU
// modes only downgrade the diagnostics of the front end's own attributes).
// Every case has its own diagnostic; the expression above has an alternative
// for each case, anchored to the line of its error.  Each case is run in
// three dialects: TEST_NUMBER is the number of the case plus 11 for each
// dialect after the first (the default, clang, then GNU mode).

#define CASE ((TEST_NUMBER - 1) % 11 + 1)

#if CASE == 1
// r_null requires a null statement: not a while statement.
void f1(int n) { [[edg::r_null(n > 0)]] while (n) { --n; } }
#elif CASE == 2
// Not a range-based for statement.
void f2(int (&a)[3]) { [[edg::r_null(true)]] for (int v : a) { (void)v; } }
#elif CASE == 3
// r_loop requires an iteration statement: not a null statement.
void f3() { [[edg::r_loop(true)]]; }
#elif CASE == 4
// Not a compound statement.
void f4() { [[edg::r_loop(true)]] { } }
#elif CASE == 5
// Not an if statement.
void f5(int n) { [[edg::r_loop(true)]] if (n) { } }
#elif CASE == 6
// Not an expression statement.
void f6(int n) { [[edg::r_loop(true)]] n = 1; }
#elif CASE == 7
// r_nonull does not apply to a null statement.
void f7() { [[edg::r_nonull(true)]]; }
#elif CASE == 8
// r_noloop does not apply to a while statement.
void f8(int n) { [[edg::r_noloop(true)]] while (n) { --n; } }
#elif CASE == 9
// ... or to a range-based for statement.
void f9(int (&a)[3]) { [[edg::r_noloop(true)]] for (int v : a) { (void)v; } }
#elif CASE == 10
// A loop attribute is scanned before the init-statement of a for statement,
// so a variable it declares is not in scope.
void f10() { [[edg::r_loop(i + 1 > 0)]] for (int i = 0; i < 3; ++i) { } }
#elif CASE == 11
// In a template (instantiated; the instance is checked in every dialect).
template <class T> void f11(T v) { [[edg::r_loop(v > 0)]] if (v) { } }
void use11() { f11(1); }
#endif
