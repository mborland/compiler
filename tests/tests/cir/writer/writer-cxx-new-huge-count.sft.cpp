//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A new[] whose constant count does not fit 32 bits is diagnosed (it was
// allocated with the count truncated).
int main() {
  char *p = new char[0x100000010];
  p[0x20] = 1;
  return 0;
}
