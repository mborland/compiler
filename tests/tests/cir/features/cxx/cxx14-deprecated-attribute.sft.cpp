//type:rp
//options:--c++14:--c++14 -O2
//require:BACK_END_TYPE 3
/* cxx14.deprecated_attribute: [[deprecated]] on functions, classes, variables, typedefs, enums and templates; the entities still work. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
[[deprecated]] static int old_fn(int x) { return x + 1; }
[[deprecated("use new_fn")]] static int old_msg(int x) { return x * 2; }

struct [[deprecated]] OldClass {
  int v;
  int get() const { return v; }
};
class [[deprecated("class")]] OldClass2 {
public:
  int v;
};
[[deprecated]] static int old_var = 40;
typedef int OldInt [[deprecated]];
using OldAlias [[deprecated("alias")]] = long;
enum [[deprecated]] OldEnum { OE_A = 3 };
template <typename T> struct [[deprecated]] OldTmpl { T v; };
template <typename T> [[deprecated]] T old_tmpl(T v) { return v + 1; }
struct Mem {
  [[deprecated]] int field;
  [[deprecated]] int method() const { return field; }
};

static void run()
{
  CHECK(old_fn(1) == 2);
  CHECK(old_msg(4) == 8);
  OldClass oc = {5};
  CHECK(oc.get() == 5);
  OldClass2 o2;
  o2.v = 6;
  CHECK(o2.v == 6);
  CHECK(old_var == 40);
  old_var = 41;
  CHECK(old_var == 41);
  OldInt oi = 7;
  OldAlias oa = 1L << 40;
  CHECK(oi == 7 && oa == 1099511627776L);
  OldEnum oe = OE_A;
  CHECK(oe == 3);
  OldTmpl<int> ot = {9};
  CHECK(ot.v == 9);
  CHECK(old_tmpl(2.5) == 3.5);
  Mem m = {11};
  CHECK(m.method() == 11 && m.field == 11);
}

int main()
{
  run();
  if (failures == 0) puts("cxx14.deprecated_attribute ok");
  return failures != 0;
}
