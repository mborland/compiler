//type:fn
//options:--gcc -ffast-math:--gcc -fno-math-errno:--gcc -ffp-contract=off
//require:BACK_END_TYPE 3
/* eccp rejects the floating-point flags as unknown options; -Ofast is
   accepted, as -O3 without fast-math (codegen-opt-size-fast). */
int main(void) { return 0; }
