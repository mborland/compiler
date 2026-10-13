//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A class value returned through a comma or a conditional: the arms are built
// in the return slot (the master entry of the arms has no variable); thrown
// through a conditional is diagnosed (the destination is on the stack).
struct X { X(bool); X(const X &); ~X(); int i; };
X g() { return X(true), X(false); }
X h(bool c) { return c ? X(true) : X(false); }
