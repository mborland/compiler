//type:fn
//options:--g++ --c++20 --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// __builtin_constant_p in a constexpr function: the front end keeps the call
// there, and the CIR back end has no mapping for it yet (llvm.is.constant in
// M6.2).
constexpr int pick(int n) { return __builtin_constant_p(n) ? 1 : 2; }
int use(int v) { return pick(v); }
