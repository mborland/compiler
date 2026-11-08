//type:fn
//options:--gcc -march=native:--gcc -mtune=generic:--gcc -msse4.2:--gcc -mavx2
//require:BACK_END_TYPE 3
/* eccp rejects the machine options as unknown: the target is the generic
   x86-64 CPU. */
int main(void) { return 0; }
