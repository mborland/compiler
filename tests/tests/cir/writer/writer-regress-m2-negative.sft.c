//type:fn
//options:--c11 --gcc --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
struct C { double _Complex z; };
struct C make(void) { struct C c = { 1 }; return c; }
