//type:fp
//options:--c11
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
int cf(int n) {
  int r = 0, i;
  if (n > 3) r = 1; else if (n < 0) r = 2; else r = 3;
  while (n > 0) { n--; if (n == 5) continue; if (n == 2) break; r += n; }
  do { r++; } while (r < 10);
  for (i = 0; i < n; i++) { r += i; }
  for (;;) { if (r > 100) break; r *= 2; }
  switch (n) { case 0: r = 1; break; case 1: case 2: r = 2; /* fall through */ default: r += 3; }
  if (r == 7) goto done;
  r = 9;
done:
  return r;
}
