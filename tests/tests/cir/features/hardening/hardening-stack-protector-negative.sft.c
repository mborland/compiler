//type:fn
//options:--gcc -fstack-protector:--gcc -fstack-protector-strong:--gcc -fstack-protector-all
//require:BACK_END_TYPE 3
/* eccp rejects the stack protector options as unknown options. */
int main(void) { return 0; }
