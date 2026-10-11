//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
struct S { static int decl_only; static int defd; static const int sc = 3; static constexpr int scx = 4; static const int sc2 = 5; static int uninit_defd; };
int S::defd = 11;
int S::uninit_defd;
const int S::sc2;
int use() { return S::decl_only + S::defd + S::sc + S::scx + S::sc2 + S::uninit_defd; }
template <class T> struct TS { static int tm; static constexpr int tc = 5; static int unused; };
template <class T> int TS<T>::tm = 12;
template <class T> int TS<T>::unused = 1;
int use2() { return TS<int>::tm + TS<int>::tc; }
extern int ext_decl; int ext_def = 4; int tent;
int uses() { return ext_decl + ext_def + tent; }
namespace N { extern int nd; int ndef = 3; }
int usen() { return N::nd + N::ndef; }
