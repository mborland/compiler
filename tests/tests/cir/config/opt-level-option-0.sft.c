//type:fp
//options:--gcc
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* The option --cir_opt_level is 0 (the default) (given to the front end directly, so
   eccp is not involved): __OPTIMIZE__ is defined at 1 and above and
   __NO_INLINE__ below that, as gcc does.  The initializers show which. */
#ifdef __OPTIMIZE__
int opt_on = 11;
#else
int opt_on = 22;
#endif
#ifdef __NO_INLINE__
int no_inline_on = 33;
#else
int no_inline_on = 44;
#endif
