//type:fp
//options:--c++20 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// __builtin_is_corresponding_member and
// __builtin_is_pointer_interconvertible_with_class on pointers to data
// members that are not constants: the offsets are read as integers (null is
// all ones) and compared as the front end's lowering does (equal, below the
// limit of the common initial sequence; zero); a pointer to a member function
// or a class that is not standard-layout (a union is exempt for the second)
// gives false after the operands are evaluated.  Reading the storage of a
// data member pointer as an integer is right because the library lowers
// !cir.data_member to i64 (see the -O2 run probe s5_pmb_* in
// cir_src/test/writer/probes/m4, which an optimizer must not break).
struct S1 { int a; int b; char c; };
struct S2 { int x; int y; long z; };
struct N { virtual void f(); int a; };
union U { int a; double b; };
bool cm(int S1::*p, int S2::*q) { return __builtin_is_corresponding_member(p, q); }
bool ci(int S1::*p) { return __builtin_is_pointer_interconvertible_with_class(p); }
bool cu(int U::*p) { return __builtin_is_pointer_interconvertible_with_class(p); }
bool cn(int N::*p) { return __builtin_is_pointer_interconvertible_with_class(p); }
bool cf(void (N::*p)(), int S1::*q) { return __builtin_is_corresponding_member(p, q); }
