//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// NOTHROW (spec 12.6): DECL_FUNC NOTHROW only from the front end's
// is_non_throwing_routine, never taken back by a re-issue; CALL_INDIRECT and
// PM_CALL have it from the call's function type; a call of a function declared
// NOTHROW needs none.
struct S { void m() noexcept; void n(); virtual void v() noexcept; };
void nt() noexcept;
void mt();
void (*fp)() noexcept;
void (*fq)();
void (S::*pm)() noexcept;
void (S::*pn)();
void c1() { nt(); mt(); fp(); fq(); }
void c2(S &s) { (s.*pm)(); (s.*pn)(); s.m(); s.n(); s.v(); }
