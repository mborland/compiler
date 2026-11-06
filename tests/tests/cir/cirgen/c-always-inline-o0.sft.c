//type:rp
//options:--c11 --gcc
//require:BACK_END_TYPE 3
/* A gnu_inline always_inline function with no definition anywhere else, as
   glibc's fortify wrappers and the x86 intrinsic headers have: at -O0 the
   body is kept and the AlwaysInliner inlines it, so the program links.  (Before
   that, -O0 made it an external declaration and the link failed.)  A static
   always_inline function is inlined too. */
extern __inline __attribute__((__gnu_inline__, __always_inline__))
int wrap(int x)
{
  return x + 1;
}

static __inline __attribute__((__always_inline__)) int twice(int x)
{
  return x * 2;
}

int main(void)
{
  return wrap(twice(20)) == 41 ? 0 : 1;
}
