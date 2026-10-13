//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// FUNC_EH_SPEC: noexcept for every noexcept(true) definition (the implicit
// destructor and the deallocation function with their compiler-generated
// specification included) and for throw() in C++17; none for noexcept(false),
// nothrow attributes and trivial members, which are NOTHROW only.
void g();
struct N { ~N(); };
N::~N() { }
struct W { N n; };
struct Th { ~Th() noexcept(false); };
Th::~Th() noexcept(false) { }
struct Imp { int x; };
void a1() noexcept { g(); }
void a2() noexcept(true) { g(); }
void a3() noexcept(false) { g(); }
void a4() throw() { g(); }
template <class T> void a5(T) noexcept(sizeof(T) > 2) { g(); }
void a6() { a5(1); a5('c'); }
void *operator new(unsigned long n) { return 0; }
void operator delete(void *p) noexcept { }
void a7() __attribute__((nothrow));
void a7() { }
