//type:fp
//options:--c11 --gnu_version=999999
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The target triple is captured when the predefined macros are entered: a
// source that undefines the operating system macros, or defines those of
// another system, still gets the triple of the target.  (GNU mode lets a
// source undefine a predefined macro.)
#undef __linux__
#undef __unix__
#define __APPLE__ 1
#define _WIN32 1
int answer(void) { return 42; }
