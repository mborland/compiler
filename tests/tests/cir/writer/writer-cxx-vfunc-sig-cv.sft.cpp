//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// CLASS_VFUNCS: functions that differ in the cv-qualification of this have
// different sigs (clang's HasSameVirtualSignature), and a function that
// returns a reference has the REF_RESULT flag (0x08).
struct R { };
struct S : R { };
struct A {
  virtual void *foo();
  virtual const void *foo() const;
  virtual R &f();
  virtual R *g();
};
struct B : virtual A {
  void *foo();
  const void *foo() const;
  S &f();
  S *g();
};
void *B::foo() { return 0; }
const void *B::foo() const { return 0; }
S &B::f() { static S s; return s; }
S *B::g() { return 0; }
