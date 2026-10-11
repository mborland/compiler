struct B { int a; char c; };
struct D : B { char d; };
struct E1 {};
struct E2 : E1 { int x; };
struct F : E1 { E1 e; int x; };
struct B2 { B b; char z; };
struct D2 : B { B inner; };
D gd; E2 ge2; F gf; B2 gb2; D2 gd2;
unsigned long sz_d = sizeof(D);
