//type:fn
//options:--gcc -fwrapv:--gcc -fno-strict-overflow
//require:BACK_END_TYPE 3
/* eccp rejects -fwrapv and -fno-strict-overflow as unknown options: signed
   overflow is undefined (nsw), as in gcc without the flag. */
int main(void) { return 0; }
