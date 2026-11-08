//type:fn
//options:--gcc --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* __builtin_ia32_addcarryx_u64 and _sbb_u64. */
unsigned char add(unsigned char c, unsigned long a, unsigned long b, unsigned long long *out) {
  return __builtin_ia32_addcarryx_u64(c, a, b, out);
}
