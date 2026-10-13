//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A call of a destructor that may throw (noexcept(false)) as a normal-path
// cleanup runs the cleanups that remain (the library makes the call an invoke
// into the enclosing scopes), so locals, temporaries and enclosing scopes are
// fine. The destructor of an array element that may throw is diagnosed: the
// library skips the elements before the one that threw, while clang and g++
// destroy them (watch upstream); the function is dropped, not miscompiled.
struct Th { ~Th() noexcept(false); };
struct N { ~N(); };
struct HasTh { Th arr[2]; };
void only() { Th t; }
void two() { N n; Th t; }
void arr() { Th a[2]; }
void member() { HasTh h; }
void order() { Th t; N n; }
