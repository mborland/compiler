//type:fp
//options:--c++17 --gnu_version=999999
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// #pragma redefine_extname is applied at the end of the translation unit: a
// function whose name was sent before gets the new name (a re-issue of its
// DECL_FUNC); gnu/cpp/other/pragma-re-2 failed an assertion.
extern "C" {
struct old_name { int i; };
#pragma redefine_extname old_name new_name
int old_name(void);
}
int foo(void) { return old_name(); }
