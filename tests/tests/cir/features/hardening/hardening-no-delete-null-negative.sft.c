//type:fn
//options:--gcc -fno-delete-null-pointer-checks
//require:BACK_END_TYPE 3
/* eccp rejects -fno-delete-null-pointer-checks as an unknown option. */
int main(void) { return 0; }
