extern "C" int printf(const char *, ...);
template <class F> int apply(F f, int x) { static int calls = 0; calls++; return f(x) + calls * 1000; }
static int local1() { return apply([](int a) { return a + 1; }, 10); }
static int local2() { auto l = [](int a) { return a + 2; }; int (*fp)(int) = l; return fp(1) + apply(l, 1); }
namespace { auto ns_lambda = [](int a) { return a * 5; }; }
int tu2();
int tu1() { return local1() + local2() + apply(ns_lambda, 2); }
int main() { printf("%d %d\n", tu1(), tu2()); return 0; }
