//type:fn
//options:--gcc --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* __builtin_setjmp and __builtin_longjmp. */
void *buffer[5];
int try_it(void) { if (__builtin_setjmp(buffer) == 0) { __builtin_longjmp(buffer, 1); } return 1; }
