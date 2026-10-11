//type:fn
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// An error from the writer (a function that must exist but cannot be
// translated) in a translation unit with a TU-local function that takes
// a parameter: the compilation ends with the front end's error status.
static inline unsigned swap1(unsigned x) { return x ^ 1u; }
struct V { virtual void g(); };
V *pv;
void ctor() __attribute__((constructor));
void ctor() { pv->g(); }
int main() { return swap1(3u) != 2u; }
