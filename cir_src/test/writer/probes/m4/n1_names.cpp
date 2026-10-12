// M4a probe: type manglings that CLASS_MANGLE and typeinfo names need:
// globals of every kind of typeinfo type, and the classes of l1_local_names.
struct A { virtual ~A() {} };
struct B : A { };
struct I;
enum E { e0 };
enum class EC : short { c0 };
namespace N { struct Pub { virtual void f() {} }; namespace { struct Hid { virtual void g() {} }; } }
namespace { struct Anon { virtual void f() {} }; }
typedef struct { virtual void f() {} int x; } TN;           // typedef name for linkage
typedef struct { virtual void f() {} } *TNP;
struct { virtual void f() {} } anon_obj;
template <class T> struct Tm { virtual void f() {} };
Tm<int> tmi; Tm<N::Pub> tmp; Tm<Anon> tma;
int local() { struct Loc { virtual int v() { return 1; } }; Loc l; return l.v(); }
template <class T> int local_t() { struct Loc { virtual int v() { return 2; } }; Loc l; return l.v(); }
int use_local_t() { return local_t<int>() + local_t<char>(); }
inline int in_inline() { struct Loc { virtual int v() { return 3; } }; Loc l; return l.v(); }
int use_in_inline() { return in_inline(); }
auto lam = [](int) { struct InL { virtual int v() { return 9; } }; InL i; return i.v(); };
Anon gan; N::Hid ghid; TN gtn; TNP gtnp;
// non-class typeinfo types
const int *g1; int *const g2 = 0; int A::*g3; int (A::*g4)(); void (*g5)(int, ...); int g6[3]; const char (*g7)[4]; E g8; EC g9;
I *g10; const I *g11; void *g12; volatile int *g13; A **g14; const A *const *g15; int B::*const *g16; void (A::*g17)() const; int (&g18)[2] = *(int (*)[2])0;
unsigned long long g19; wchar_t g20; char16_t g21; decltype(nullptr) g22; __int128 g23; long double g24; bool g25; float _Complex g26;
int main() { return local() + use_local_t() + use_in_inline() + lam(1); }
