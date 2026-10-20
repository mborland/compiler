//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The allocation call of a new-expression carries ALLOC_EXPR (0x40), whatever
// function it calls: the consumer decides from the callee's name whether it
// is a replaceable global operator (then it may be builtin: allocation
// elision).  So do the free call of the cleanup that runs when the
// initialization throws, and the placement and class-specific forms.  A
// direct call of ::operator new does not carry it.  A delete-expression is
// its own record (DELETE).
namespace std { struct nothrow_t {}; extern const nothrow_t nothrow; }
void *operator new(unsigned long);
void *operator new[](unsigned long);
void *operator new(unsigned long, const std::nothrow_t &) noexcept;
void *operator new(unsigned long, void *) noexcept;
void operator delete(void *) noexcept;
struct T { T(); ~T(); int v; };
struct C { static void *operator new(unsigned long); static void operator delete(void *); int v; };
int *scalar() { return new int(5); }
int *array(int n) { return new int[n]; }
int *nothrow_new() { return new (std::nothrow) int; }
T *with_ctor() { return new T; }
T *with_ctors(int n) { return new T[n]; }
int *placement(void *p) { return new (p) int(3); }
C *class_specific() { return new C; }
void *direct(unsigned long n) { return ::operator new(n); }
void del(int *p, T *t, C *c) { delete p; delete t; delete c; }
