extern "C" int printf(const char *, ...);
// __cxa_free_exception while constructing the thrown object (upstream CIRGen
// omits the cleanup; patch.py inserts the shape our library emits).
struct Ex { int code; Ex(int c) : code(c) { printf("Ex(%d)\n", c); } ~Ex() { printf("~Ex(%d)\n", code); } };
struct Big { int a[8]; Big(int c) { for (int i = 0; i < 8; ++i) a[i] = c; if (c == 4) throw 44; } ~Big() { printf("~Big\n"); } };
int mk(int k);
int f1(int k);
int f2(int k);
int f3(int k);
