//type:fn
//options:--gcc --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* A builtin_ia32 builtin other than add-with-carry. */
unsigned long long rdtsc(void) { return __builtin_ia32_rdtsc(); }
void pause_(void) { __builtin_ia32_pause(); }
