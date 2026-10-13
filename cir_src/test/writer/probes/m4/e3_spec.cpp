//options: --c++14
// M4a probe: exception specifications.  noexcept(expr) (true and false),
// implicit destructor and deallocation function noexcept, dynamic
// specifications throw(int), throw(), and the pre-C++17 forms.
#include <cstdio>
#include <exception>
struct S { ~S() { std::puts("~S"); } };
void f0() noexcept { S s; }
void f1() noexcept(true) {}
void f2() noexcept(false) {}
template <class T> void f3() noexcept(sizeof(T) > 2) {}
void f4() throw() {}
void f5() throw(int) { throw 1; }
void f6() throw(int, char) {}
struct D { ~D() noexcept(false) {} };   // dtor noexcept(false)
struct N { int *p; ~N() {} };           // implicit noexcept(true) from the dtor rule
struct W : D { };                       // implicit dtor is noexcept(false)
struct M { M() noexcept {} M(const M &) noexcept(false) {} };
struct Y { M m; Y() noexcept = default; };
void *operator new(unsigned long n, int) noexcept(false);
void operator delete(void *, int) noexcept;
void use() {
  f3<char>(); f3<int>();
  try { f5(); } catch (int) {}
  N n; Y y;
  static_assert(noexcept(f0()), "");
  static_assert(!noexcept(f2()), "");
}
void use2() { W w; }
int main() { use(); use2(); return 0; }
void *operator new(unsigned long n, int) noexcept(false) { return ::operator new(n); }
void operator delete(void *p, int) noexcept { ::operator delete(p); }
