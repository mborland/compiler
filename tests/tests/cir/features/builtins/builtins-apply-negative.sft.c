//type:fn
//options:--gcc --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* __builtin_apply_args, __builtin_apply and __builtin_return. */
int target(int x) { return x; }
void forward(void) { void *args = __builtin_apply_args(); __builtin_return(__builtin_apply((void (*)())target, args, 64)); }
