//type:rp
//options:--c23:--c23 -O2
//require:BACK_END_TYPE 3
/* #embed (C23): the bytes of a file become an initializer list.  The file is
   the test's own source (the harness processes the directive lines first, so
   the test searches for its own text and compares the three embeds with each
   other), which needs no side file. */
static const unsigned char whole[] = {
#embed __FILE__
};

static const unsigned char head[] = {
#embed __FILE__ limit(9)
};

static const unsigned char framed[] = {
#embed __FILE__ limit(4) prefix(0x40,) suffix(, 0x41)
};

/* Whether the bytes of whole contain the text. */
static int contains(const char *text)
{
  unsigned long i, j;

  for (i = 0; i < sizeof(whole); i++) {
    for (j = 0; text[j] != '\0' && i + j < sizeof(whole); j++)
      if (whole[i + j] != (unsigned char)text[j])
        break;
    if (text[j] == '\0')
      return 1;
  }
  return 0;
}

int main(void)
{
  int failures = 0;
  int i;

  if (sizeof(whole) < 400)
    failures |= 1;
  if (!contains("#embed (C23)") || contains("absent " "marker"))
    failures |= 2;
  if (sizeof(head) != 9)
    failures |= 4;
  for (i = 0; i < 9; i++)
    if (head[i] != whole[i])
      failures |= 8;
  if (sizeof(framed) != 6 || framed[0] != 0x40 || framed[5] != 0x41)
    failures |= 16;
  for (i = 0; i < 4; i++)
    if (framed[i + 1] != whole[i])
      failures |= 32;
  return failures;
}
