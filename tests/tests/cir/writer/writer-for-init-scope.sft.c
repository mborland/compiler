//type:fp
//options:--c11
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* ECIL 0.7: FOR_BEGIN opens the scope of the init declarations; they follow
   it with no BLOCK around them and stay in scope to FOR_END. */
int declared_init(int n) {
  int s = 0;
  for (int i = 0, j = 10; i < n; i++, j--) s += i * j;
  return s;
}
void no_init(int n) {
  for (; n; n--)
    ;
}
int nested_loops(int n) {
  int s = 0;
  for (int i = 0; i < n; i++)
    for (int j = i; j < n; j++) s += j;
  return s;
}
int expression_init(int n) {
  int i, s = 0;
  for (i = 0; i < n; i++) s += i;
  return s;
}
int init_scope_shadow(int n) {
  int s = 0;
  for (int i = 0; i < n; i++) {
    int i2 = i + 1;
    for (int i = i2; i < n; i++) s += i;
  }
  return s;
}
