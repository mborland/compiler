//type:fp
//options:--c23 --gnu_version=999999
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// __builtin_c23_va_start () without an argument (gnu/c/c23-stdarg-11) is not
// a va_list to start: the writer used to dereference the missing operand.
void f(...) { __builtin_c23_va_start(); }
