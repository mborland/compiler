//type:fn
//options:--c99 --gcc --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* The initializer of a block-scope integer variable that is the address of an
   object cast to the integer type. */
typedef unsigned long address_t;
int g;
address_t take(void) { address_t x = (address_t)&g; return x; }
