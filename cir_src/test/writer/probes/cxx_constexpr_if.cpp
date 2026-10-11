//options:--c++17
// if constexpr: only the selected branch exists (the other may not even
// compile for the type).
extern "C" int printf(const char *, ...);
template<class T> int f(T t) {
  if constexpr (sizeof(T) > 2) { return 1; } else { return 2; }
}
template<class T> int g(T t) {
  if constexpr (sizeof(T) > 2) return 7;
  return 8;
}
struct W { int v; };
template<class T> int h(T t) {
  if constexpr (sizeof(T) == sizeof(W)) return t.v;
  else return (int)t;
}
template<class T> int k(T t) {
  if constexpr (sizeof(T) > 100) return 1;
  return t;
}
int main() {
  printf("%d %d %d %d %d %d %d\n", f(1), f(1L), g(1), g('a'), h(W{ 5 }), h(3.5),
         k(9));
  return 0;
}
