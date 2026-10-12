//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The reason of a dropped function is complete in the warning (it is not cut
// after 100 characters) and in the unsupported_reason of the declaration (up
// to 255 bytes).  Here it is 137 characters long.
struct N { N(const N &); ~N(); int v; };
struct W { int x; W &operator=(N n); };
W &getw();
N make();
void f() { getw() = make(); }
