//type:rp
//options:--gcc
//require:BACK_END_TYPE 3
/* The canary of the opt-corpus gate step: it passes at every level and says
   which one it was built at.  Its output is recorded at -O0, so that in a run
   at -O2 it shows as PASS--OUTPUT MISMATCH, which tells the step that the
   level reached that run. */
extern int puts(const char *);

int main(void) {
#ifdef __OPTIMIZE__
  puts("canary: optimizing");
#else
  puts("canary: not optimizing");
#endif
  return 0;
}
