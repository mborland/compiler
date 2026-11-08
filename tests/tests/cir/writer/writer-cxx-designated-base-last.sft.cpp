//type:fp
//options:--c++17 --g++ --gnu_version=70400
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// With g++ 7 and earlier in C++17 the front end lists the base class entry of
// "{ .x = 1, .y = 2 }" after the data members: it is sent as the entry of the
// base (C_SEL 0), not as a member beyond the last.  (Since main's GH #260 the
// front end lists the base entry first in every other mode, g++ 8 and later
// included: see writer-cxx-designated-base-first.)
struct B { };
struct D : B {
  int x;
  int y;
};
D d = {.x = 1, .y = 2};
void f() { D e = {.x = 1, .y = 2}; (void)e; }
