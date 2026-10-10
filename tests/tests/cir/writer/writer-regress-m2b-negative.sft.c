//type:fn
//options:--c23 --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
struct A; void ta(struct A *);
void f1(void) { ta(0); }
struct A { _Atomic int n; };
struct B { struct A a; int x; };
int use(struct B *p) { struct B l; return p->x; }
