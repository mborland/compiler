//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A new whose allocation function can return null (std::nothrow): the result
// is kept in a local and the construction is in the arm of a test of it
// (TERN with a null arm); a free cleanup in that arm (with the nothrow
// operator delete) needs its own FULLEXPR around the conditional.
struct nothrow_t { explicit nothrow_t() = default; };
extern const nothrow_t nothrow;
void *operator new(unsigned long, const nothrow_t &) noexcept;
void *operator new[](unsigned long, const nothrow_t &) noexcept;
void operator delete(void *, const nothrow_t &) noexcept;
struct A { A(); ~A(); int v; };
struct B { B(int); int v; };
A *f1() { return new (nothrow) A; }
A *f2() { return new (nothrow) A[3]; }
B *f3() { return new (nothrow) B(2); }
int *f4() { return new (nothrow) int(4); }
A *f5(int n) { return new (nothrow) A[n]; }
