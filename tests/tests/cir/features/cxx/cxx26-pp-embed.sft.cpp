//type:rp
//options:--c++26:--c++26 -O2
//require:BACK_END_TYPE 3
/* cxx26.pp_embed: #embed: the bytes of a file become an initializer list (the test embeds its own source). */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
static const unsigned char whole[] = {
#embed __FILE__
};

static const unsigned char head[] = {
#embed __FILE__ limit(9)
};

static const unsigned char framed[] = {
#embed __FILE__ limit(4) prefix(0x40,) suffix(, 0x41)
};

static int contains(const char *text)
{
  unsigned long i, j;
  int found = 0;
  for (i = 0; i < sizeof(whole) && !found; i++) {
    for (j = 0; text[j] != '\0' && i + j < sizeof(whole); j++)
      if (whole[i + j] != (unsigned char)text[j]) break;
    if (text[j] == '\0') found = 1;
  }
  return found;
}

static void run()
{
  CHECK(sizeof whole > 200);
  CHECK(sizeof head == 9);
  CHECK(sizeof framed == 6);
  CHECK(framed[0] == 0x40 && framed[5] == 0x41);
  for (unsigned i = 0; i < 9; ++i) CHECK(head[i] == whole[i]);
  for (unsigned i = 0; i < 4; ++i) CHECK(framed[i + 1] == whole[i]);
  CHECK(contains("CHECK(sizeof head == 9);"));
  char needle[] = {'z', 'q', 'j', 'x', 'k', 'w', 0};
  CHECK(!contains(needle));
  int empty_len[] = {
#embed __FILE__ limit(0) if_empty(7)
  };
  CHECK(sizeof empty_len / sizeof(int) == 1 && empty_len[0] == 7);
#if __has_embed(__FILE__)
  CHECK(true);
#else
  CHECK(false);
#endif
}

int main()
{
  run();
  if (failures == 0) puts("cxx26.pp_embed ok");
  return failures != 0;
}
