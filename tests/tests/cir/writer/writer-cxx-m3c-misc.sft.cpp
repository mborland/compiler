//type:fp
//options:--c++20 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
const char *fn() { return __func__; }
const char *pf() { return __PRETTY_FUNCTION__; }
constexpr bool ce() { return __builtin_is_constant_evaluated(); }
bool use_ce() { return ce(); }
typedef int I;
void vacuous(int *p, int x) { p->I::~I(); x.I::~I(); }
struct NT { NT(); NT(const NT &); ~NT(); int x; };
int printf(const char *, ...);
void variadic_class(NT &a) { printf("%d", a); }
struct P { int x; virtual int f(); };
P pc{};
