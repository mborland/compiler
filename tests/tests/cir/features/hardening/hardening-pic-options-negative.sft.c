//type:fn
//options:--gcc -fPIC:--gcc -fPIE:--gcc -fno-pic:--gcc -fno-pie
//require:BACK_END_TYPE 3
/* eccp rejects the position-independence options as unknown: the code is
   always position independent (the library's relocation model is PIC). */
int main(void) { return 0; }
