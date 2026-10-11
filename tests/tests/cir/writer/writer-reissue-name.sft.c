//type:fp
//options:--c11 --gcc
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* A function first referenced under one name and then given an asm label: the
   later DECL_FUNC re-issues the entity with a different non-nil name, which
   renames it (spec 7, effective name). */
int use(void) { extern int f(void); return f(); }
int f(void) __asm__("g_name");
int f(void) { return 1; }
/* After the rename the old name "f" is free: another function whose asm name
   is "f" has a G-id of its own. */
int other(void) __asm__("f");
int other(void) { return 2; }
