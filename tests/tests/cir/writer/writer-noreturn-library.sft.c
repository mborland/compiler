//type:fp
//options:--c11 --gcc
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// abort, exit, _Exit and _exit are noreturn in Clang's builtin table
// (Builtins.inc, the letter r): a declaration of one of them that does not say
// so gets NORETURN (4) in its DECL_FUNC, as clang's declaration has the
// attribute. A static function with that name or any other function has none.
extern void abort(void);
extern void exit(int);
extern void _Exit(int);
static void _exit(int x) { (void)x; }
void f(int x) { if (x) abort(); else exit(1); }
void g(int x) { _Exit(x); }
void h(int x) { _exit(x); }
