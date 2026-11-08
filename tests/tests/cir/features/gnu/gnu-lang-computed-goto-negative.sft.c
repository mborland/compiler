//type:fn
//options:--gcc --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* Labels as values and computed goto. */
int dispatch(int n) {
  static void *targets[] = { &&first, &&second };
  goto *targets[n];
first: return 1;
second: return 2;
}
