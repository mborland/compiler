//type:fn
//options:--c11
//require:BACK_END_TYPE 3
/* The failing variants of writer-gid-growth: a definition whose initializer
   introduces G-ids (a string literal) and then cannot be expressed, and the
   completion of a record with a member the writer cannot describe, which
   runs the type work stack (assigning G-ids) before the record is marked
   failed.  What this test proves: the stream shape of the dropped and
   failed entities when the G-id table moves.  A stale an_ecil_gid_info
   pointer in these paths is detected only with the AddressSanitizer FE or
   ECIL_GID_STRESS=1 (the gate's writer-stress step); an ordinary run
   passes either way. */
struct F { const char *s; double _Complex z; } gfail = { "fail", 1.0 };
struct B;
struct B *pb;
int uses_incomplete(struct B *b) { return b != 0; }
struct B { short *x; _Atomic(int) *q; long double (*cb)(float); };
int after_completion(void) { return 0; }
