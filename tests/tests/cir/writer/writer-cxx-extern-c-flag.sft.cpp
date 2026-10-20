//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// EXTERN_C (0x100): a C++ function with C language linkage (clang gives its
// result no noundef).  The functions of an extern "C" block, definitions and
// declarations, have it; a C++ function does not, nor does main, a function
// with an asm label, an extern "C++" function inside an extern "C" block, a
// member function or a lambda.
extern "C" int c_def(int x) { return x; }
extern "C" int c_decl(int);
extern "C" {
  int c_block(int);
  extern "C++" int cxx_in_c(int);
  static int c_static(int x) { return x; }
}
int cxx_plain(int x) { return x; }
int cxx_in_c(int x) { return x; }
int with_label(int) __asm("label_name");
int with_label(int x) { return x; }
struct S { int m(int x) { return x; } static int s(int x) { return x; } };
int main() {
  auto l = [](int x) { return x; };
  S o;
  return c_def(1) + c_decl(2) + c_block(3) + cxx_in_c(4) + cxx_plain(5) +
         with_label(6) + o.m(7) + S::s(8) + l(9) + c_static(10);
}
