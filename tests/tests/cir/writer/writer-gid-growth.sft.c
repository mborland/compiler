//type:fp
//options:--c11
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* What this test proves: the stream has the recorded shape when G-ids are
   assigned while the writer is building the record of another G-id (an
   address constant that introduces a variable or a function, a string
   literal inside a variable definition, the completion of records declared
   incomplete) and the G-id table moves (the checking writer moves it at
   every new G-id while there are few).  It does NOT by itself detect a
   record kept across such an assignment: that stale pointer is invisible in
   an ordinary run.  The detection needs the AddressSanitizer FE or
   ECIL_GID_STRESS=1, which the gate's writer-stress step runs over this
   directory.  (The negative test of the same name has the failing
   variants.) */
int gx;
int *gp = &gx;
extern int later_var;
int *gp_later = &later_var;
int later_func(void);
int (*gfp_later)(void) = later_func;
int later_var = 3;
int later_func(void) { return 4; }
const char *gs = "hello";
struct A { const char *s; int *p; double (*cb)(char); } ga = { "x", &gx };
struct B;
struct C;
struct B *pb;
struct C *pc;
int uses_incomplete(struct B *b, struct C *c) { return b != 0 && c != 0; }
struct B { short *x; long double (*cb)(float); struct C *c; };
struct C { int *q; };
int after_completion(void) { return 0; }
