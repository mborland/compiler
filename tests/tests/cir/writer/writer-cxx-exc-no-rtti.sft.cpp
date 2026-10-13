//type:fp
//options:--c++14 --no_rtti
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// With RTTI off, typeinfo objects still exist for exceptions: THROW_END and
// CATCH_BEGIN name them; typeid and dynamic_cast are errors in the front end.
struct E { virtual ~E(); int e; };
struct F : E { };
void g();
void f1() { throw F(); }
void f2() { try { g(); } catch (E &) { } catch (int) { } catch (...) { } }
void f3() throw(F) { g(); }
