//type:fp
//options:--c11
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// An if with an integral constant condition sends only the arm that is taken
// (the other one may call a function that is not defined); an arm that
// contains a label is kept with the if.
void undefined_fn(void);
int a1(int x) { if (0) undefined_fn(); return x; }
int a2(int x) { if (sizeof(int) == 4) x += 1; else undefined_fn(); return x; }
int a3(int x) { if (3 > 5) { undefined_fn(); return 0; } else if (2 == 2) { x += 2; } else { undefined_fn(); } return x; }
int a4(int x) {
  if (0) { dead: x += 100; }
  if (x > 1000) goto dead;
  return x;
}
int a5(int x) { if (1) { int y = x * 2; x = y; } return x; }
int a6(int x) { if (!1) undefined_fn(); else x++; return x; }
