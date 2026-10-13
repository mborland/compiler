// K6c: zero- and value-initialization of types that contain pointers to data
// members (null is -1, not 0).  Every case copies the bytes of the object into
// a caller buffer; the host (g++) prints them, so mixed == ref shows that the
// ClangIR-compiled code produced the same bytes as g++.
// Layout is padding-free (all members 8 bytes).
struct S { long a; long S::*pm; long b; };
struct B { long S::*bpm; long x; };
struct D : B { long y; long D::*dpm; };
struct N { S s; long c; long N::*npm; S arr[2]; };
struct M { long a; long (S::*mp)(); };
struct F { long S::*pm = nullptr; long q = 5; };
struct V { virtual void f(); long S::*vpm; long z; };
typedef void (*snap_fn)(unsigned char *);
struct Case { const char *name; snap_fn fn; unsigned size; };
extern const Case cases[];
extern const unsigned ncases;
