//type:fn
//options:--gcc -fstack-clash-protection
//require:BACK_END_TYPE 3
/* eccp rejects -fstack-clash-protection as an unknown option. */
int main(void) { return 0; }
