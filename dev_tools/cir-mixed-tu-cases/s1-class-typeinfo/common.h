/* Class typeinfo across translation units: the three class kinds
   (no base: __class_type_info; one public base at offset 0:
   __si_class_type_info; two bases: __vmi_class_type_info).  The vtable's
   typeinfo slot is how ref_main.cpp (always the reference compiler) reaches
   them: typeid of the dynamic type and dynamic_cast between the bases. */
struct A { virtual int f(); virtual ~A(); int a = 1; };
struct B : A { int f() override; };
struct C { virtual int g(); virtual ~C(); int c = 2; };
struct D : B, C { int g() override; };
A *make_a();   // a.cpp
A *make_b();   // b.cpp
C *make_d_as_c();   // b.cpp
