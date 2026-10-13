//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// In C++17 the front end lists the base class entry of "{ .x = 1, .y = 2 }"
// after the data members: it is sent as the entry of the base (C_SEL 0), not
// as a member beyond the last.
struct B { };
struct D : B {
  int x;
  int y;
};
D d = {.x = 1, .y = 2};
void f() { D e = {.x = 1, .y = 2}; (void)e; }
