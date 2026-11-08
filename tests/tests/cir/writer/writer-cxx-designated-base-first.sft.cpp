//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// Since main's GH #260 the front end lists the base class entry first, before
// the data members, in every mode but g++ 7 and earlier (see
// writer-cxx-designated-base-last), and a member skipped by a designated
// initializer gets its default member initializer in GNU C++ before C++20.
struct B { int bb = 4; };
struct D : B {
  int p = 5;
  int q = 6;
  int r = 7;
};
D d = {.q = 60};
void f(int m) { D e = {.r = m}; (void)e; }
