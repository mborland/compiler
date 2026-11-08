//type:rp
//options:--c99:--c99 -O2
//require:BACK_END_TYPE 3
/* Chunked lowering of a large unit: the CIR back end lowers a module in
   chunks when its emitted function bodies reach 1,250,000 bytes of the
   stream (project.md decision 14).  The preprocessor expands this small file
   to 2000 external functions of 40 statements each, which is well above the
   threshold; the functions are then called across chunk boundaries (the first,
   one in the middle and the last) together with a static variable, a string
   and a function pointer table that the chunks share. */
#define STEP s = s * 3u + 1u;
#define STEPS4 STEP STEP STEP STEP
#define STEPS16 STEPS4 STEPS4 STEPS4 STEPS4
#define STEPS40 STEPS16 STEPS16 STEPS4 STEPS4
#define FUNC(p, q, r) \
  unsigned big_ ## p ## q ## r(unsigned x) \
  { unsigned s = x; STEPS40 return s; }
#define UNITS(p, q) FUNC(p, q, 0) FUNC(p, q, 1) FUNC(p, q, 2) FUNC(p, q, 3) \
  FUNC(p, q, 4) FUNC(p, q, 5) FUNC(p, q, 6) FUNC(p, q, 7) FUNC(p, q, 8) \
  FUNC(p, q, 9)
#define TENS(p) UNITS(p, 0) UNITS(p, 1) UNITS(p, 2) UNITS(p, 3) UNITS(p, 4) \
  UNITS(p, 5) UNITS(p, 6) UNITS(p, 7) UNITS(p, 8) UNITS(p, 9)

/* big_A00 to big_T99: 20 * 100 = 2000 functions, about 1.67 million bytes of
   stream (the threshold is 1,250,000). */
TENS(A) TENS(B) TENS(C) TENS(D) TENS(E) TENS(F) TENS(G) TENS(H) TENS(I)
TENS(J) TENS(K) TENS(L) TENS(M) TENS(N) TENS(O) TENS(P) TENS(Q) TENS(R)
TENS(S) TENS(T)

extern unsigned big_A00(unsigned);
extern unsigned big_J49(unsigned);
extern unsigned big_T99(unsigned);

static unsigned shared_counter = 5;
static const char shared_text[] = "chunks";
static unsigned (*const table[])(unsigned) = { big_A00, big_J49, big_T99 };

static unsigned expect(unsigned x)
{
  unsigned s = x;
  int i;

  for (i = 0; i < 40; i++)
    s = s * 3u + 1u;
  return s;
}

int main(void)
{
  int failures = 0;
  int i;

  for (i = 0; i < 3; i++) {
    shared_counter += table[i](i + 1) - expect((unsigned)i + 1);
  }
  if (shared_counter != 5)
    failures |= 1;
  if (big_A00(7) != expect(7) || big_J49(8) != expect(8)
      || big_T99(9) != expect(9))
    failures |= 2;
  if (shared_text[0] != 'c' || shared_text[5] != 's')
    failures |= 4;
  return failures;
}
