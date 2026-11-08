//type:fn
//options:--gcc -fsanitize=address:--gcc -fsanitize=undefined:--gcc -fsanitize=thread:--gcc -fsanitize=memory
//require:BACK_END_TYPE 3
/* eccp rejects the sanitizer options as unknown options. */
int main(void) { return 0; }
