//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The standard exception machinery as a program sees it: classes that derive
// from a class whose key function is elsewhere are thrown and caught (the
// typeinfo of a thrown or caught class needs those of its bases declared
// first), a catch by base reference, a rethrow, a function-try constructor.
struct Base { Base(); virtual ~Base(); virtual const char *what() const; };
struct Der : Base { Der(); ~Der(); const char *what() const; };
struct Der2 : Der { };
void thrower();
void f1() { throw Der2(); }
int f2() { try { thrower(); } catch (const Base &b) { b.what(); throw; } return 0; }
int f3() { try { f1(); } catch (Der &) { return 1; } catch (Base *) { return 2; } return 0; }
