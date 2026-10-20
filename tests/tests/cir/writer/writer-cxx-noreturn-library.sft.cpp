//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The same in C++: only the functions of C language linkage (extern "C", also
// in a namespace) with the library name and signature are noreturn; a C++
// function of that name is not.
extern "C" void abort();
extern "C" void exit(int);
namespace ns { void abort(); void exit(int); }
void f(int x) { if (x) abort(); else ns::exit(1); ns::abort(); exit(1); }
