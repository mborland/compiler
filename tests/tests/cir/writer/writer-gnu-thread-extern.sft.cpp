//type:fp
//options:--g++
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The declarations of variables with the GNU __thread specifier carry the TLS
// flag (DECL_VAR flag 4), the extern ones too: a reference to a thread-local
// variable of another translation unit is a TLS reference.
extern __thread void *tv;
extern thread_local void *tv2;
__thread int tdef = 3;
static __thread int tstatic;
int plain_global;
void *get() { return tv; }
void *get2() { return tv2; }
int get3() { return tdef + tstatic + plain_global; }
