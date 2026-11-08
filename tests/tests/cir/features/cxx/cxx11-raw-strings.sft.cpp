//type:rp
//options:--c++11:--c++11 -O2
//require:BACK_END_TYPE 3
/* cxx11.raw_strings: raw string literals: backslashes, newlines, delimiters, and the encoding prefixes. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
static int eq(const char *a, const char *b)
{
  while (*a && *a == *b) { ++a; ++b; }
  return *a == *b;
}

static void run()
{
  const char *a = R"(a\nb)";
  CHECK(a[1] == '\\' && a[2] == 'n' && a[3] == 'b' && a[4] == 0);
  CHECK(sizeof(R"(abc)") == 4);
  const char *d = R"xy(a)"b)x)xy";
  CHECK(eq(d, "a)\"b)x"));
  const char *nl = R"(line1
line2)";
  CHECK(nl[5] == '\n' && nl[6] == 'l');
  CHECK(sizeof(R"(
)") == 2);
  CHECK(eq(R"()", ""));
  CHECK(sizeof(R"(\)") == 2);
  const char *cc = R"(??/x)";
  CHECK(cc[0] == '?' && cc[1] == '?' && cc[2] == '/');
  const char *u8 = u8R"(\n\t)";
  CHECK(u8[0] == '\\' && u8[1] == 'n' && u8[2] == '\\' && u8[3] == 't' && u8[4] == 0);
  const wchar_t *w = LR"(w\n)";
  CHECK(w[0] == L'w' && w[1] == L'\\' && w[2] == L'n' && w[3] == 0);
  const char16_t *w16 = uR"(a\b)";
  CHECK(w16[1] == u'\\' && w16[2] == u'b');
  const char32_t *w32 = UR"(z"q)";
  CHECK(w32[1] == U'"' && w32[2] == U'q' && w32[3] == 0);
  const char *cat = R"(ab)" "cd";
  CHECK(eq(cat, "abcd"));
  static const char tbl[] = R"(0123456789)";
  int sum = 0;
  for (unsigned i = 0; i + 1 < sizeof tbl; ++i) sum += tbl[i] - '0';
  CHECK(sum == 45);
}

int main()
{
  run();
  if (failures == 0) puts("cxx11.raw_strings ok");
  return failures != 0;
}
