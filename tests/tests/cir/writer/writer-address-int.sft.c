//type:fp
//options:--c11 --gcc
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
typedef long intptr_t;
int g;
int arr[4];
static unsigned char us[5] = "abcd";
struct P { unsigned char n[5]; int v; } sp = { "xyz", 3 };
intptr_t fa(void) { return (intptr_t)&g; }
intptr_t fb(void) { return (intptr_t)&arr[2] - (intptr_t)arr; }
float kr(a) float a; { return a * 2; }
int fc(void) { static unsigned char loc[5] = "wxyz"; unsigned char l2[5] = "pqrs"; return loc[0] + l2[0]; }
void *fn(void) { return (void *)0; }
