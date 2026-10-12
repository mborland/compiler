//type:fp
//options:--c++14
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// An extern "C" function whose name is the mangled name of a constructor:
// two IL routines with one symbol and different types (gnu/cpp/pr77550).  The
// stream has one G-id per symbol, and the type of a function cannot change
// after its body, so the function that needs the second one is dropped with
// a reason, a warning (never a library error): the extern "C" declaration is
// only called, and the function that calls it is dropped.  Plain functions
// with one symbol and one type share one G-id.
struct S { S(char *); int v; };
int use_s() { S s(0); return s.v; }
extern "C" void _ZN1SC1EPc(...);
void call_it() { _ZN1SC1EPc(1, 2); }
extern "C" int shared_fn(int);
namespace N { extern "C" int shared_fn(int); }
extern "C" int shared_fn(int x) { return x + 1; }
int use_shared() { return N::shared_fn(2); }
