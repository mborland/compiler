// Covariant returns: new slot or not.
struct B0 { virtual B0 *clone(); virtual ~B0(); };
struct D0 : B0 { D0 *clone(); };                                 // no adjustment: same slot
struct M1 { int m; };
struct B1 { virtual B1 *get(); };
struct D1 : M1, B1 { D1 *get(); };                               // this and return adjustment
struct B2 { virtual B2 &get(); };
struct D2 : virtual B2 { D2 &get(); };                           // virtual primary base: new slot
struct E2 : D2 { E2 &get(); };
B0 *B0::clone() { return 0; }
B0::~B0() {}
D0 *D0::clone() { return 0; }
B1 *B1::get() { return 0; }
D1 *D1::get() { return 0; }
B2 &B2::get() { static B2 b; return b; }
D2 &D2::get() { static D2 d; return d; }
E2 &E2::get() { static E2 e; return e; }
