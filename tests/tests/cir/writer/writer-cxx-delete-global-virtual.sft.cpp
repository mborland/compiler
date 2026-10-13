//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// ::delete of an object with a virtual destructor: DELETE VIRTUAL|GLOBAL
// (0x08|0x10): the library calls D1 through the vtable and then the global
// operator delete that the front end chose; a plain delete keeps VIRTUAL
// alone.
struct B { virtual ~B(); };
struct C { virtual ~C(); void operator delete(void *); };
void f(B *p) { ::delete p; }
void g(B *p) { delete p; }
void h(C *p) { delete p; }
void i(C *p) { ::delete p; }
