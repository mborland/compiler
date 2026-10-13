//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A function-try-block: a constructor's or destructor's is TRY_BEGIN(
// FUNCTION_TRY) right after the header (the PARAMs and FUNC_EH_SPEC), the
// MEMINIT groups, VPTR_INIT and the destructor's cleanup prologue inside it,
// and the handler ends with the front end's implicit RETHROW; a plain function
// has an ordinary TRY around its body.
void g();
struct Q { Q(int); ~Q(); };
struct M { M(); ~M(); };
struct S2 : Q {
  M m;
  S2() try : Q(1) { } catch (...) { g(); }
  ~S2() try { g(); } catch (...) { }
};
struct V { virtual ~V(); V() try { } catch (...) { } };
int f() try { g(); return 1; } catch (...) { return 2; }
struct Del { Del(); Del(int) try : Del() { } catch (...) { g(); } };
