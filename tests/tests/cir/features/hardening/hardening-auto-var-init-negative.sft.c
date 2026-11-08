//type:fn
//options:--gcc -ftrivial-auto-var-init=zero:--gcc -ftrivial-auto-var-init=pattern
//require:BACK_END_TYPE 3
/* eccp rejects -ftrivial-auto-var-init as an unknown option. */
int main(void) { return 0; }
