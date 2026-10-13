// K6d: constants of pointers to data members that designate a member of a
// non-virtual base (the member's offset plus the base's offset), with an empty
// base in front and bases at non-zero offsets: globals, locals, conversions,
// use.  The values must equal g++'s.
struct E {};
struct B1 { int p; long q; };
struct B2 { char c; int r; double d; };
struct D : E, B1, B2 { int own; };
typedef int D::*DI;
typedef long D::*DL;
typedef double D::*DD;
extern DI g_di[5];
extern DL g_dl[2];
extern DD g_dd[1];
long run(D *);
