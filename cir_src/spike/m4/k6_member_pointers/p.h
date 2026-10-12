extern "C" int printf(const char *, ...);
// Pointers to data members and member functions: by-value {i64,i64}
// parameters and results (CallConvLowering), constants in globals, virtual
// methods, base/derived conversions with non-zero adjustment, comparison and
// bool conversion.
struct B1 { int b1; virtual int vf(int); int nf(int); static int sf(int); virtual ~B1(); };
struct B2 { int b2; double bd; virtual int g(int); int ng(int); virtual ~B2(); };
struct D : B1, B2 { int d; int vf(int) override; int g(int) override; int dn(int); ~D() override; };
typedef int (B2::*B2Method)(int);
typedef int (D::*DMethod)(int);
typedef int B2::*B2Data;
typedef int D::*DData;
int apply(D *d, DMethod m, int x);                 // by-value member function pointer
DMethod pick(int which);
DMethod from_base2(B2Method m);                           // by-value result
int read(D *d, DData m);
DData data_pick(int which);
DData from_base_data(B2Data m);
extern DMethod g_methods[6];
extern DData g_datas[4];
extern B2Method g_bmethods[3];
bool is_null(DMethod m);
bool same(DMethod a, DMethod b);
bool same_data(DData a, DData b);
int run_all();
