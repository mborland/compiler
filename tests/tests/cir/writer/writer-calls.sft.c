//type:fp
//options:--c11 --gcc
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
extern int ext(int, double);
extern int vfn(const char *, ...);
extern int unproto();
int (*fp)(int, double);
int callee(int x, double y) { return ext(x, y) + fp(x, y); }
int varcalls(int n) { return vfn("n=%d", n) + vfn("%f", 1.5) + unproto(n, 2.0f); }
int kr(a, b) char a; float b; { return a + (int)b; }
typedef __builtin_va_list va_list;
int sum(int n, ...) { va_list ap; int s = 0; __builtin_va_start(ap, n); while (n--) s += __builtin_va_arg(ap, int); __builtin_va_end(ap); return s; }
