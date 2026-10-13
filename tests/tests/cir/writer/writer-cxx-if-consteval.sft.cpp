//type:fp
//options:--c++23
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// if consteval and if !consteval: code that runs is not being evaluated as a
// constant expression, so only the else arm (then arm for the negated form)
// is written, in place of the if (libstdc++'s <typeinfo> and std::
// __is_constant_evaluated use it in C++23).
constexpr bool is_ce() {
  if consteval { return true; } else { return false; }
}
constexpr int pick(int x) {
  if consteval { return 1; }
  return x;
}
constexpr int neg(int x) {
  if !consteval { return x + 1; } else { return 0; }
}
constexpr int only_then(int x) {
  if consteval { x = 5; }
  return x;
}
bool f1() { return is_ce(); }
int f2(int x) { return pick(x) + neg(x) + only_then(x); }
