//options: --c++17
// M4a probe: exception specifications under C++17: `throw()` is kept by the
// front end as a dynamic specification with an empty list (never_throws set),
// noexcept(noexcept(expr)), and the implicit destructor of a class with a
// throw() destructor.
#include <cstdio>
void f4() throw() { std::puts("f4"); }
void f7() noexcept(noexcept(f4())) { std::puts("f7"); }
struct S { ~S() throw(); };
S::~S() throw() { std::puts("~S"); }
int main() { f4(); f7(); S s; return 0; }
