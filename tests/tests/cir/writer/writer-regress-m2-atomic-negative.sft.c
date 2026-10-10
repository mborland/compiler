//type:fn
//options:--c11 --gcc --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
struct A { _Atomic int x; int y; };
int use(struct A *p) { struct A l; return p->y; }
