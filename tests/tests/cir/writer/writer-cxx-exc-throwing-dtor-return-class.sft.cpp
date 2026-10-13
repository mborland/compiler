//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// CWG 2176: the returned object is destroyed when a destructor that runs
// after its construction throws. Classic clang does not destroy it (g++ does),
// and neither does the library.
struct X {
  X();
  X(const X &);
  ~X();
};
struct Y {
  ~Y() noexcept(false);
};
X f() {
  Y y;
  return {};
}
