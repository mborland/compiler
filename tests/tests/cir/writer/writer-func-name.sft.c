//type:fp
//options:--c99
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
extern int strcmp(const char *, const char *);
int main(void) { return strcmp(__func__, "main") || sizeof(__func__) != 5; }
