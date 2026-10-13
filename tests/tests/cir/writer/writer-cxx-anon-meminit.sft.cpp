//type:fp
//options:--c++11 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// Default member initializers of members of anonymous structs and unions: the
// MEMINIT group is numbered by the unnamed member that holds them and the
// object is reached through it (FIELD of the unnamed member, then of the
// member); the entries of one holder share a group.
struct A { A(); A(const A &); };
struct B {
  struct { int m_1 = 1; int m_2 = 2; };
  union { int u1; char u2 = 'x'; };
  int z = 3;
  A dummy;
};
B b;
