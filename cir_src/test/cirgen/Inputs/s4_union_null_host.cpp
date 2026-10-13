// Host of cxx-s4-union-null-member-pointer.test: zero-initialized unions that
// contain a pointer to data member.  Only the first member of a union gets its
// null value (clang's EmitNullConstant, g++ the same): U's pointer is null
// (-1), V's first member is a long that is 0 (its pointer is then offset 0).
struct D { int a, b; };
union U { int D::*p; long x; };
union V { long x; int D::*p; };
struct S { int k; U u; };
extern U gu1;
extern V gv;
extern U gu_arr[2];
extern S gs;
int main() {
  if (gu1.p != nullptr) return 1;
  if (gv.x != 0) return 2;
  if (gu_arr[0].p != nullptr || gu_arr[1].p != nullptr) return 3;
  if (gs.k != 0 || gs.u.p != nullptr) return 4;
  return 0;
}
