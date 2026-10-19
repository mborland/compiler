//type:rp
//options:--c++17 --gnu_version=140000
//require:BACK_END_TYPE 3
// The canary of the opt-corpus gate step for the C++ runs (see
// opt-level-canary.sft.c): it passes at every level and says which one it was
// built at.
extern "C" int puts(const char *);

int main() {
#ifdef __OPTIMIZE__
  puts("canary: optimizing");
#else
  puts("canary: not optimizing");
#endif
  return 0;
}
