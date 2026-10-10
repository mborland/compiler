//type:fp
//options:--c11
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
int ex(int a, long b, unsigned char c, double d, int *p, int **pp) {
  int r = a + (int)b - c * 2 / 3 % 4;
  r += a << 2; r -= b >> 1; r &= 7; r |= 8; r ^= 1; r <<= 1;
  r = a < b && c >= a || !a;
  r = a ? (int)d : (int)b;
  r = (a++, (int)b);
  r++; --r; (*p)++;
  p += 2; p = p - 1; r = (int)(p - *pp);
  r = -a + ~a + (a != 0);
  d = d * 2.5f + a;
  r = *p + p[3] + **pp;
  { char ch = (char)a; short sh = (short)ch; unsigned u = (unsigned)sh; r = (int)u; }
  return r + (int)sizeof(r);
}
