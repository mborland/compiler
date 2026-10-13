//options:--c++17
// (the S1 writer test of the same name in tests/tests/cir/writer is this program)
// M4 slice S1: constants of dynamic classes.  The front end lists no vptr, so
// the writer's C_SEL indexes skip the TY_VPTR field; the facts of every
// dynamic class whose C_AGG_BEGIN is streamed come first, and its vtable is
// used (the library fills the vptrs); a base subobject does not use its own.
extern "C" int printf(const char *, ...);
struct B { virtual int f() { return 1; } int x = 5; };
struct D : B { int f() override { return 2; } int y = 6; };
struct Holder { D d; B b; int z; };
D gd;
Holder gh = {};
Holder gh2;
B arr[3];
int main() {
  D loc;
  B &r = gd;
  B *p = new D[2];
  printf("%d %d %d %d\n", r.f(), loc.f(), gd.x, gh.d.f());
  delete[] p;
  return 0;
}
