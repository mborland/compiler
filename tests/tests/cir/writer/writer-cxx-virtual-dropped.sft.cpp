//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A virtual function whose definition is dropped (it uses inline assembly, which is not supported)
// cannot be in a vtable with vague linkage defined here: the vtable of Q (no
// key function) becomes an external declaration, its typeinfo (which refers
// to no function) is unchanged, and the warning names the class.  The vtable
// of K, whose key function is defined here, is defined; the library decides
// about its reference to the dropped function.
struct Q { virtual void f() { asm("nop"); } virtual void g() {} };
struct K { virtual void f(); virtual void g() { asm("nop"); } };
void K::f() {}
Q q;
K k;
int main() { return 0; }
