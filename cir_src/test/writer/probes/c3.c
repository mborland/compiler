int f(int n) {
  int s = 0;
  for (;;) {
    if (n > 10) break;
    if (n & 1) { n++; continue; }
    s += n;
    n++;
  }
  do { s--; } while (s > 0);
  switch (n) {
    case 1: s = 1; break;
    case 2:
    case 3: s = 3;
    default: s = 0;
  }
  goto done;
 done:
  return s;
}
