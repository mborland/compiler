//type:fn
//options:--c11 --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* A function that is needed although nothing refers to it (the constructor
   attribute) must not be dropped silently: it is an error, not a warning. */
void ctor(void) __attribute__((constructor));
int i;
void ctor(void) { int n = i + 2; int a[n]; a[0] = 1; i = a[0]; }
