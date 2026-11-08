//type:rp
//options:--c++17:--c++17 -O2
//require:BACK_END_TYPE 3
/* cxx17.enumerator_attributes: attributes on enumerators. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
enum Color {
  Red [[deprecated]] = 1,
  Green [[deprecated("green")]] = 2,
  Blue [[maybe_unused]] = 4,
  Alpha
};
enum class Mode : unsigned char {
  Off [[maybe_unused]],
  On [[deprecated]] = 5,
  Auto [[maybe_unused]] = 9
};

static void run()
{
  int s = 0;
  Color c = Blue;
  s += c;
  CHECK(s == 4);
  CHECK(Alpha == 5);
  CHECK(Mode::Off == Mode(0) && (int)Mode::On == 5 && (int)Mode::Auto == 9);
  Mode m = Mode::Auto;
  CHECK(sizeof m == 1);
  unsigned sum = 0;
  Color all[] = {Red, Green, Blue, Alpha};
  for (unsigned i = 0; i < 4; ++i) sum += all[i];
  CHECK(sum == 12);
}

int main()
{
  run();
  if (failures == 0) puts("cxx17.enumerator_attributes ok");
  return failures != 0;
}
