//type:fn
//options:--c11 --gnu_version=140000
//require:BACK_END_TYPE 3
/* The library error for a definition that was dropped because it asks
   __builtin_alloca_with_align for an alignment above 128 bits (16 bytes). */
extern void use(void *);
void wide(unsigned long n) { use(__builtin_alloca_with_align(n, 256)); }
void ok(unsigned long n) { use(__builtin_alloca_with_align(n, 128)); }
