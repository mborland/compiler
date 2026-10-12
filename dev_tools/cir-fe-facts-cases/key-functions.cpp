// The key function: first virtual function that is not pure, not inline.
struct K1 { virtual void a(); virtual void b(); };               // a, defined here
struct K2 { virtual void a() {} virtual void b(); };             // inline a: key is b (not defined)
struct K3 { virtual void a() = 0; virtual void b(); };           // pure a: key is b
struct K4 { virtual void a(); };                                 // inline later: no key function
inline void K4::a() {}
struct K5 { inline virtual void a(); virtual void b(); };        // declared inline: key is b
void K5::a() {}
struct K6 { virtual ~K6() {} };                                  // no key function
struct K7 { virtual ~K7(); };                                    // key ~K7, not defined here
struct K8 : K7 { };                                              // implicit dtor only: no key
void K1::a() {}
void K1::b() {}
void K3::b() {}
K1 k1; K2 k2; K3 *k3; K4 k4; K5 k5; K6 k6; K8 *k8;
