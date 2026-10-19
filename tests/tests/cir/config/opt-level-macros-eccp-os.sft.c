//type:rp
//options:--gcc -Os
//require:BACK_END_TYPE 3
/* eccp passes -Os on as --cir_opt_level=2 (an approximation).
   gcc defines __OPTIMIZE__ when optimizing and __NO_INLINE__ when not; the
   exit status is 0 if the program sees what level it was compiled at. */
int main(void) {
  int opt = 0;
  int noinline = 0;
#ifdef __OPTIMIZE__
  opt = 1;
#endif
#ifdef __NO_INLINE__
  noinline = 1;
#endif
  return opt == 1 && noinline == !1 ? 0 : 1;
}
