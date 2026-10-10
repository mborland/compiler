#include <stdio.h>
void kr(); void kr(a) int a; { printf("kr %d\n", a); } void kr(int);
static const char *w = __func__;
static const char *n[] = { "x", __func__ };
int main(void) { kr(4); printf("%s %s %s\n", w, n[0], n[1]); return 0; }
