// Class shapes for the layout comparison (K7).  Every class is instantiated by
// a global so that clang's CIRGen emits its record types.
struct E0 {};
struct P0 { virtual void f(); };                     // vptr only: nearly empty
struct P1 { int a; virtual void f(); };              // vptr + data (tail padding)
struct P2 : P1 { int b; };                           // primary base P1, tail reuse
struct P3 : P1 { char c; short d; virtual void g(); };
struct P4 : P1 { virtual void h(); double d; };
struct M1 { long m; virtual void m1(); };
struct MI : P1, M1 { int z; };                       // secondary base with its own vptr
struct MI2 : M1, P1 { char z; };
struct E1 : E0 { virtual void f(); int x; };         // empty non-virtual base
struct E2 : P0, E0 { int q; };
struct V0 { virtual void vf(); };                    // nearly empty
struct V1 { int v; virtual void vf(); };
struct VL : virtual V1 { int l; };
struct VR : virtual V1 { int r; };
struct VD : VL, VR { int d; };
struct VE : VD { int e; };
struct NL : virtual V0 { int x; };                   // nearly empty virtual base is primary
struct NR : virtual V0 { int y; };
struct ND : NL, NR { int w; };                       // V0 primary of NL, lives in NL
struct NM : virtual V0, virtual V1 { };
struct VNoPoly { int w; };
struct VP : virtual VNoPoly { int p; };              // virtual base without vptr of its own
struct VQ : VP, virtual V1 { char q; };
struct W1 : virtual E0 { int x; };                   // empty virtual base
struct T1 { int a; char b; T1(); virtual ~T1(); };   // tail padding in a dynamic class
struct T2 : T1 { char c; };
struct T3 : virtual T1 { char c; virtual void t3(); };
struct T4 : T3, T2 { char d; };
struct A5 { char c[5]; virtual void a5(); };
struct A6 : A5 { char d; };
struct A7 : virtual A5 { char e; };
struct S1 { P1 m; int k; };                          // dynamic class as a member
struct S2 { VL m; char k; };
struct S3 : P1 { P2 m; };
struct S4 { T2 a; T2 b; };
#define USE(T) T g_##T;
USE(P0) USE(P1) USE(P2) USE(P3) USE(P4) USE(M1) USE(MI) USE(MI2) USE(E1) USE(E2) USE(V0) USE(V1)
USE(VL) USE(VR) USE(VD) USE(VE) USE(NL) USE(NR) USE(ND) USE(NM) USE(VP) USE(VQ) USE(W1) USE(T2) USE(T3) USE(T4)
USE(A6) USE(A7) USE(S1) USE(S2) USE(S3) USE(S4)
