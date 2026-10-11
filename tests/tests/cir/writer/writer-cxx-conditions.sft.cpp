//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
struct M { int v; M(); ~M(); operator bool() const; };
M mk();
int f(int c) {
  if (M cv = mk()) { c++; } else { c--; }
  while (M w = mk()) { c++; if (c > 3) break; }
  for (int i = 0; M q = mk(); ) { c += i++; if (c > 9) break; }
  switch (M s = mk(); (int)s.v) { case 1: c++; }
  if (int x = c) c += x;
  return c;
}
int g2(int c) {
  switch (M s = mk(); s.v) { case 1: c++; }
  if (M t = mk(); t.v) c--;
  return c;
}
int h2(int c) { if (int x = c + 1; x > 2) c++; switch (int y = c; y) { case 1: c--; } return c; }
int for_step(int c) { for (int i = 0; M q = mk(); i++) { c += i; } return c; }
