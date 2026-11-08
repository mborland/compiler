//type:fn
//options:--gcc --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* Inline assembly. */
void barrier(void) { __asm__ volatile ("" : : : "memory"); }
