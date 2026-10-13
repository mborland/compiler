//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// M4 slice S1: which translation unit defines a vtable and a class typeinfo
// (clang's key function rule, decided at the end of the translation unit):
// the first virtual function that is not pure, not inline and not defined in
// the class.  No key function: linkonce_odr with COMDAT (a class template
// instance too; an explicit instantiation is weak_odr); the key function
// elsewhere: an external declaration, and only if the vtable is used.
struct K1 { virtual void a(); virtual void b() {} };
struct K2 { virtual void a() {} virtual void b(); };
struct K3 { virtual void a(); };
inline void K3::a() {}
struct K4 { inline virtual void a(); virtual void b(); };
struct K5 { virtual void a() = 0; virtual void b(); };
struct K6 { virtual ~K6(); };
struct K7 { virtual ~K7() = default; virtual void a(); };
struct K8 { virtual ~K8(); };
template <class T> struct TK { virtual void a(); };
template <class T> void TK<T>::a() {}
template struct TK<int>;
template <> struct TK<long> { virtual void a(); };
void TK<long>::a() {}
extern template struct TK<short>;
struct K9 : K1 {};
struct K10 { virtual void a(); };
void K10::a() {}
void K1::a() {}
void K2::b() {}
void K4::a() {}
void K4::b() {}
void K5::b() {}
K6::~K6() {}
void K7::a() {}
void use() {
  TK<long> tl;
  TK<short> ts;
  K9 k9;
  K10 k10;
  TK<char> tk;
  K8 *p = 0;
  K3 k3;
  K4 k4;
  K5 *k5 = 0;
  K7 k7;
  K6 k6;
  K1 k1;
  K2 k2;
  (void)tl; (void)ts; (void)p; (void)k5;
}
int main() { return 0; }
