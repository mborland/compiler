//type:fn
//options:--c23 --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
_BitInt(24) add24(_BitInt(24) a, _BitInt(24) b) { return a + b; }
int cmp24(_BitInt(24) a, _BitInt(24) b) { return a < b; }
struct BB { _BitInt(7) f : 3; int z; };
int member(struct BB *p) { return p->z; }
int fine(int n) { return n + 1; }
