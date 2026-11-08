//type:fn
//options:--gcc --gnu_version=150000 --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* __builtin_clear_padding. */
struct padded { char c; int i; };
void clear(struct padded *p) { __builtin_clear_padding(p); }
