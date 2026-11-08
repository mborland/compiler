//type:fn
//options:--gcc -fno-strict-aliasing
//require:BACK_END_TYPE 3
/* eccp rejects -fno-strict-aliasing as an unknown option: no type-based alias
   metadata is emitted, so the behavior it asks for is always on. */
int main(void) { return 0; }
