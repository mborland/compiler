//source_files: side.c
//type: rp
//options: -w
//require:BACK_END_TYPE 3
// A .c side file of a C++ test is compiled as C (eccp passes --c in the CIR
// configurations): its function keeps its C name, and it may use a C
// identifier that is a C++ keyword.
extern "C" int side(int);

int main() {
  return side(41) == 42 ? 0 : 1;
}
