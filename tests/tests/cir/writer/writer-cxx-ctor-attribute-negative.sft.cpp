//type:fn
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A function that is needed although nothing refers to it (the destructor or
// used attribute) must not be dropped silently: it is an error.
struct V { virtual void g(); };
V *pv;
void dtor() __attribute__((destructor));
void dtor() { pv->g(); }
