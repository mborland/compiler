//type:rp
//options:--c++23:--c++23 -O2
//require:BACK_END_TYPE 3
/* cxx23.named_character_escapes: named universal character escapes in character and string literals. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
static unsigned len(const char *s)
{
  unsigned n = 0;
  while (s[n]) ++n;
  return n;
}

static void run()
{
  CHECK('\N{LATIN CAPITAL LETTER A}' == 'A');
  CHECK('\N{DIGIT SEVEN}' == '7');
  CHECK('\N{SPACE}' == ' ');
  const char *a = "x\N{LATIN SMALL LETTER B}y";
  CHECK(a[0] == 'x' && a[1] == 'b' && a[2] == 'y' && a[3] == 0);
  const char8_t *e = u8"\N{LATIN SMALL LETTER E WITH ACUTE}";
  CHECK(e[0] == 0xC3 && e[1] == 0xA9 && e[2] == 0);
  const char8_t *g = u8"\N{GREEK SMALL LETTER ALPHA}";
  CHECK(g[0] == 0xCE && g[1] == 0xB1 && g[2] == 0);
  CHECK(U'\N{GRINNING FACE}' == 0x1F600);
  CHECK(u'\N{GREEK SMALL LETTER OMEGA}' == 0x03C9);
  CHECK(L'\N{LATIN SMALL LETTER E WITH ACUTE}' == 0xE9);
  const char32_t *u = U"\N{EURO SIGN}a";
  CHECK(u[0] == 0x20AC && u[1] == 'a' && u[2] == 0);
  const char16_t *v = u"\N{GRINNING FACE}";
  CHECK(v[0] == 0xD83D && v[1] == 0xDE00 && v[2] == 0);
  const char *raw = R"(\N{SPACE})";
  CHECK(len(raw) == 9 && raw[0] == '\\');
}

int main()
{
  run();
  if (failures == 0) puts("cxx23.named_character_escapes ok");
  return failures != 0;
}
