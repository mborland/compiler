//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A class assignment whose right operand is a constant (an aggregate built in
// a temporary: its value is fixed when it is evaluated) under a cv-adjustment
// of a glvalue constant spills the value instead of keeping an address.
struct T { int a, b; };
constexpr T k = {1, 2};
void f(T &t, T *p) {
  t = k;
  *p++ = k;
  *p++ = T{3, 4};
  t = (const T &)k;
}
const T &g() { static const T s = {5, 6}; return s; }
void h(T &t) { t = g(); }
int main() { T t; T arr[2]; f(t, arr); h(t); return t.a; }
