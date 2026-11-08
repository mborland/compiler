//type:rp
//options:--c++17:--c++17 -O2
//require:BACK_END_TYPE 3
/* cxx17.namespace_attributes: attributes on namespace definitions; the namespace members work. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
namespace [[deprecated]] Old {
int value() { return 12; }
}
namespace [[deprecated("msg")]] Old2 {
int value() { return 13; }
}
namespace Used {
int value() { return 14; }
inline namespace [[deprecated]] Inl {
int deep() { return 15; }
}
}

static void run()
{
  CHECK(Old::value() == 12);
  CHECK(Old2::value() == 13);
  CHECK(Used::value() == 14);
  CHECK(Used::deep() == 15 && Used::Inl::deep() == 15);
}

int main()
{
  run();
  if (failures == 0) puts("cxx17.namespace_attributes ok");
  return failures != 0;
}
