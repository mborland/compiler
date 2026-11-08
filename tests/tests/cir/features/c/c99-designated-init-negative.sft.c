//type:fn
//options:--c99 --gcc --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* A designator after a wide string initializer of the same array. */
typedef __WCHAR_TYPE__ wchar_t;
struct K { wchar_t l[6]; int m; } k[] = { { { L"foo" }, 1 }, [0].l[2] = L'x' };
