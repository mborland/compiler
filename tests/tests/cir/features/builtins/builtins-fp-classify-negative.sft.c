//type:fn
//options:--gcc --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* __builtin_fpclassify with a result operand that is not a constant. */
int classify(double x, int normal) {
  return __builtin_fpclassify(0, 1, normal, 3, 4, x);
}
