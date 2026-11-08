//type:rp
//options:--c++20:--c++20 -O2
//require:BACK_END_TYPE 3
/* cxx20.using_enum: using enum declarations at block, namespace and class scope; in switch. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
enum class Color { Red, Green = 5, Blue };
namespace N {
enum class Mode : unsigned char { Off, On, Auto = 9 };
}
struct Holder {
  enum class Kind { A = 1, B, C };
  using enum Kind;
  Kind k;
  Holder() : k(B) {}
};

namespace M {
using enum N::Mode;
}

static int name_of(Color c)
{
  using enum Color;
  switch (c) {
  case Red: return 1;
  case Green: return 2;
  case Blue: return 3;
  }
  return 0;
}

static void run()
{
  CHECK(name_of(Color::Red) == 1 && name_of(Color::Green) == 2 && name_of(Color::Blue) == 3);
  {
    using enum Color;
    Color c = Blue;
    CHECK((int)c == 6 && (int)Green == 5);
  }
  Holder h;
  CHECK(h.k == Holder::B && Holder::C == Holder::Kind::C && (int)Holder::A == 1);
  CHECK((int)M::Auto == 9 && M::On == N::Mode::On);
  using enum N::Mode;
  unsigned char m = (unsigned char)Auto;
  CHECK(m == 9 && Off == N::Mode::Off);
}

int main()
{
  run();
  if (failures == 0) puts("cxx20.using_enum ok");
  return failures != 0;
}
