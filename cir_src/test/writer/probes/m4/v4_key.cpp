// M4a probe: key functions (clang's rule vs the FE's decider).
struct K1 { virtual void a(); virtual void b() {} };          // key a
struct K2 { virtual void a() {} virtual void b(); };          // key b
struct K3 { virtual void a(); };                              // key a
inline void K3::a() {}                                        // inline later: none
struct K4 { inline virtual void a(); virtual void b(); };     // a inline in class? key b
struct K5 { virtual void a() = 0; virtual void b(); };        // key b (pure skipped)
struct K6 { virtual ~K6(); };                                 // key ~K6
struct K7 { virtual ~K7() = default; virtual void a(); };     // ~K7 inline in class: key a
struct K8 { virtual ~K8(); };
template <class T> struct TK { virtual void a(); };           // no key for instances
template <class T> void TK<T>::a() {}
template struct TK<int>;                                       // explicit instantiation
template <> struct TK<long> { virtual void a(); };                // explicit specialization: an ordinary class
void TK<long>::a() {}
extern template struct TK<short>;                              // extern template: vtable not defined here
struct K9 : K1 { };                                           // no own virtual: none
struct K10 { virtual void a(); };
void K10::a() {}                                              // key defined here
void K1::a() {}
void K2::b() {}
void K4::a() {}
void K4::b() {}
void K5::b() {}
K6::~K6() {}
void K7::a() {}
void use() { TK<long> tl; TK<short> ts; (void)tl; (void)ts; K9 k9; K10 k10; TK<char> tk; K8 *p = 0; (void)p; K3 k3; K4 k4; K5 *k5 = 0; (void)k5; K7 k7; K6 k6; K1 k1; K2 k2; }
int main() { return 0; }
