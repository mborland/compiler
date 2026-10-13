//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// try and catch: TRY_BEGIN, CATCH_BEGIN(typeinfo | nil), LOCAL and
// CATCH_PARAM(l, kind, g_copy) for each parameter (an unnamed one too) with
// the kind chosen from its type (0 reference, 1 pointer, 2 scalar, 4 trivial
// copy, 5 non-trivial copy with the C1 copy constructor, 6 reference to a
// pointer, 7 reference to a record pointer), the cleanup of a destructible
// parameter inside the handler, TRY_END; a catch-all that rethrows.
struct S { S(const S &); ~S(); int v; };
struct E { int e; };
struct Tr { int a; };
enum En { e0 };
struct B { int b; };
struct D : B { };
void g();
void h(const E &);
void use(S);
int f1() {
  try { g(); } catch (const E &e) { h(e); } catch (S s) { use(s); } catch (...) { throw; }
  return 1;
}
int f2() {
  try { g(); }
  catch (int) { return 1; }
  catch (const char *const &p) { return 2; }
  catch (E *&q) { return 3; }
  catch (Tr t) { return t.a; }
  catch (En) { return 4; }
  catch (bool) { return 5; }
  catch (int B::*) { return 6; }
  catch (void (E::*)()) { return 7; }
  catch (D *) { return 8; }
  catch (B &b) { return b.b; }
  catch (long double) { return 9; }
  return 0;
}
void f3() { try { try { g(); } catch (int) { g(); } } catch (...) { } }
void f4() { for (;;) { try { g(); break; } catch (E) { continue; } } }
