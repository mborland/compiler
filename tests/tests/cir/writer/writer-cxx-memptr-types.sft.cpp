//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// Pointers to members are TY_DATA_MEMBER(member type, class) and
// TY_METHOD(function type with the object pointer first, class): globals,
// members, arrays, parameters, results, and a pointer to a member of a class
// that is only declared.
struct B { int b; int m(int); virtual int v(int); };
struct D : B { char d; double dd; const char *s; int cm(int) const; };
struct Inc;
typedef int B::*DB;
typedef int (B::*MB)(int);
typedef int (D::*MD)(int) const;
DB g1;
MB g2;
MD g3;
DB garr[3];
MB marr[2];
int Inc::*gi;
void (Inc::*gm)();
struct H { DB a; MB b; DB c[2]; int n; };
H gh;
int take(DB, MB, MD, int Inc::*);
DB give(MB);
MB (*fp)(DB);
int sz() { return (int)(sizeof(DB) + sizeof(MB) + sizeof(H)); }
