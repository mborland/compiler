//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A loop condition is sent as a constant only when clang folds it. A volatile
// read is a side effect for clang (EvaluateAsInt does not fold it away), so
// (v, true) stays an expression, and so does __builtin_expect(1, f()), which
// must still call f; the same with a thread_local variable that is
// initialized dynamically or declared extern, whose access runs the
// initialization (a local thread_local is initialized at its declaration, not
// at the use). Without side effects the comma and the expect are folded
// (g, __builtin_expect(1, g)).
extern volatile int v;
extern int g;
struct S { volatile int vol; int plain; };
extern S s;
extern volatile int *vreg;
int f();
int t();
thread_local int tg = t();
extern thread_local int te;
void volatile_var() { while ((v, true)) {} }
void volatile_member() { while ((s.vol, 1)) {} }
void volatile_pointer() { while ((*vreg, true)) {} }
void plain_var() { while ((g, true)) {} }
void expect_call() { while (__builtin_expect(1, f())) {} }
void expect_pure() { while (__builtin_expect(1, g)) {} }
void tls_dynamic() { while ((tg, true)) {} }
void tls_extern() { while ((te, true)) {} }
void tls_local() { thread_local int x = t(); while ((x, true)) {} }
