//type:fn
//options:--gcc --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* __builtin_return_address and __builtin_frame_address. */
void *caller(void) { return __builtin_return_address(0); }
void *frame(void) { return __builtin_frame_address(0); }
