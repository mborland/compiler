//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A static local of a function whose mangled name is not final yet (a member
// of an unnamed class that a typedef names for linkage later) cannot be
// named: the function is dropped with that reason.
typedef struct {
  int f() { static int n = 0; return ++n; }
} TN;
int main() { TN t; return t.f() - 1; }
