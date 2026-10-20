//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The consumer treats a loop as trivially infinite only when its condition is
// a constant in the stream, as clang does when it folds the condition
// (checkIfLoopMustProgress). The front end folds constants, constexpr calls
// and variables, sizeof and the like; the writer folds the rest of what clang
// folds: a comma expression with a constant right operand and operands
// without side effects ((g, true)), and __builtin_expect of a constant. A
// condition that clang does not fold stays an expression (g || true, a
// variable).
extern int g;
constexpr bool always() { return true; }
constexpr bool k = true;
void constexpr_call() { while (always()) {} }
void constexpr_var() { while (k) {} }
void comma() { while ((void)g, true) {} }
void comma_value() { while (g, true) {} }
void expect() { while (__builtin_expect(1, 1)) {} }
void do_comma() { do {} while (g, true); }
void for_expect() { for (; __builtin_expect(1, 1);) {} }
void not_folded() { while (g || true) {} }
void impure_comma(int x) { while (x++, true) {} }
void variable() { bool b = true; while (b) {} }
