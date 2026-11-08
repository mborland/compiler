//type:fp
//options:--c11 --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* __builtin_alloca_with_align is translated as alloca and the library aligns
   the block to 16 bytes, so an alignment above 128 bits, or one that is not
   a constant, is not expressed: the function is dropped with a reason.  An
   alignment of 128 bits or less is translated. */
extern void use(void *);
void wide(unsigned long n) { use(__builtin_alloca_with_align(n, 256)); }
void unknown(unsigned long n, unsigned long a) {
  use(__builtin_alloca_with_align(n, a));
}
void narrow(unsigned long n) { use(__builtin_alloca_with_align(n, 128)); }
void narrower(unsigned long n) { use(__builtin_alloca_with_align(n, 8)); }
