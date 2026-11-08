//type:rp
//options:--c++17:--c++17 -O2
//require:BACK_END_TYPE 3
/* cxx17.nested_namespace_definitions: namespace A::B::C { } definitions, reopened pieces, and lookup. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
namespace A::B::C {
int f() { return 1; }
struct S { int v; S() : v(7) {} };
}
namespace A::B {
int g() { return C::f() + 10; }
}
namespace A {
namespace B::C {
int h() { return f() + 100; }
}
}
namespace A::B::C {
static int counter;
int bump() { return ++counter; }
}
namespace X::Y::Z::W { enum E { V = 42 }; }

static void run()
{
  CHECK(A::B::C::f() == 1);
  CHECK(A::B::g() == 11);
  CHECK(A::B::C::h() == 101);
  A::B::C::S s;
  CHECK(s.v == 7);
  A::B::C::bump();
  CHECK(A::B::C::bump() == 2);
  CHECK(X::Y::Z::W::V == 42);
  namespace Alias = A::B::C;
  CHECK(Alias::f() == 1);
}

int main()
{
  run();
  if (failures == 0) puts("cxx17.nested_namespace_definitions ok");
  return failures != 0;
}
