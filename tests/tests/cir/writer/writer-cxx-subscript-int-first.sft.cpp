//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// E1[E2] is sequenced E1 before E2 (C++17) also when E1 is the integer, as
// in 2[p]: the integer is kept in a temporary and the pointer is evaluated
// after it.
int f(int);
int *g(int);
int i;
void h() {
  i = (f(1) - 1)[g(2)];
  3[g(4)] = 5;
}
