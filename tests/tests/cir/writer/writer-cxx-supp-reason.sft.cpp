//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// ECIL 1.1: every unsupported DECL_FUNC and DECL_VAR carries the reason that
// the writer gives in its diagnostic, as the unsupported_reason supp key
// (the frame has the supp flag, the stream the SUPP feature).  Both variants
// of a constructor have it, and so does a variable.
struct VB { int b; };
struct V : virtual VB { int a; };      // virtual bases: M4 slice S2
V gv = V();
thread_local int tl = 5 + gv.a;
int *use() { return &tl; }
void thrower() { asm("nop"); }
int ok = 1;
