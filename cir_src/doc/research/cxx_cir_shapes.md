# C++ CIR shapes at the pin (M3a spikes 1-7)

Author: cir-lib. Pin: llvm-project `894bfff5` (clang 24.0.0git), the toolchain in
the `cir-env` image (`/edg/llvm-cir/bin`: `clang`, `cir-opt`, `cir-translate`,
assertions enabled). Inputs: `m3_ecil_v1_design.md` 1c, 4, 6, 7 and the plan's
M3a.

**Method.** Sources, scripts and the address-logging ctor/dtor runtime are in
`cir_src/spike/cxx-shapes/` (see its README).
1. The reference shape is clang's own plain CIRGen output:
   `clang -fclangir -emit-cir -Xclang -clangir-disable-passes f.cpp`. It has
   records by value, `cir.cleanup.scope`, `cir.ternary` and so on, and no ABI
   lowering. It is the shape the library emits.
2. Every snippet is run two ways and the logs compared: the full
   `clang -fclangir` driver, and that raw CIR pushed through the pin's passes
   by `cir-to-obj.sh` (the order of `runCIRToCIRPasses`, then flatten, goto
   solver, `cir-translate --cir-to-llvmir`, `clang -c`, link with `g++`).
3. Deviations the library might make are written by hand (one function body
   replaced; the replaced text is quoted) and run through the same pipeline,
   to find out which rules are real.

`cir-compare.py` gained `--cxx --structors --global-order` (C++ noise,
constructor/destructor variants, init order; C mode output is unchanged, the new
mode is smoke-tested on a C stream only and needs its first real use in M3c),
and `cir-layout-compare-cxx.py` is new (spike 7).

## Summary

| # | Spike | Verdict |
| --- | --- | --- |
| 1 | sret/byval identity (`A a = make();`, `return make();`, NRVO, `take(A(...))`, callee param) | **Works as designed**, with four sharpened rules (below). |
| 2 | Return sinking past cleanups, `__retval` routing | **Works with the design's fallback as the rule:** route every value return through `__retval` while a cleanup is open. |
| 3 | Conditional cleanups | **Works with change X.** Eager wrapping works, but the scope must be opened at the outermost conditional op, not at FULLEXPR_BEGIN (design 1c would reverse destructor order). |
| 4 | C1 as `aliasee` of C2 x CallConvLowering | **Works as designed** (also with a coerced record parameter, comdat C5, linkonce_odr/internal). |
| 5 | Ctor regions, init order, guards | **Works as designed**; exact attribute pairing rules recorded. |
| 6 | `cir.delete_array`, cookie, `new[]` | **Works as designed**; clang's CIRGen shape is the rule. |
| 7 | `.base` subobject records | **Works as designed** with the existing M2 layout machinery; only cosmetic differences, and `EMPTY_FOR_ABI` must not be used for empty class members. |

No fallback was needed, so there is nothing to ask about a pin bump.

Facts that shaped several verdicts:
- The pass order in `clang/lib/CIR/Lowering/CIRPasses.cpp` is canonicalize,
  [simplify], TargetLowering, CXXABILowering, **LoweringPrepare,
  CallConvLowering** (LoweringPrepare first so outlined initializers are
  classified). Our library's `runCIRToCIRPasses` call is the same one.
- The toolchain is built with assertions. A shape the passes do not accept
  is sometimes a diagnosed error and sometimes an assertion (abort), as
  below; the library's crash recovery turns an abort into
  `EDG_CIR_ERROR_INTERNAL`, but the rules exist so it is never reached.
- `ast(#cir.var.decl.ast)` on globals and `func_info<#cir.cxx_ctor/dtor ...>`
  on functions are **not needed** (variants 5g and `a.nofi` build and run
  identically without them). The library need not produce them.

## Spike 1: sret / byval identity through CallConvLowering

Class `A { int x; A(); A(int); A(const A&); ~A(); }` with logging;
`cir.record_layouts` entry `A = <arg_passing_kind = cannot_pass_in_regs,
has_trivial_dtor = false, record_align = 4>`.

**Reference (raw CIRGen).**
```
cir.func @_Z4makev() -> !rec_A {                    // A make() { return A(5); }
  %0 = cir.alloca "__retval" ... ; cir.call @_ZN1AC2Ei(%0, %c5)
  %1 = cir.load %0 ; cir.return %1 : !rec_A }
cir.func @_Z5chainv() -> !rec_A {                   // return make();
  %0 = cir.alloca "__retval" ; %1 = cir.call @_Z4makev() : () -> !rec_A
  cir.store %1, %0 ; %2 = cir.load %0 ; cir.return %2 }
cir.func @_Z4nrvov() -> !rec_A {                    // A a; ...; return a;
  %0 = cir.alloca "__retval" init ; %1 = cir.alloca "nrvo" : !cir.bool
  cir.store false, %1 ; cir.call @_ZN1AC2Ev(%0)
  cir.cleanup.scope { ...; cir.store true, %1; %6 = cir.load %0; cir.return %6
  } cleanup normal { %3 = cir.load %1; %4 = cir.not %3
      cir.if %4 { cir.call @_ZN1AD2Ev(%0) } }
  cir.trap }
// caller:  A a = make();          %27 = cir.alloca "a"
//          %28 = cir.call @_Z4makev() ; cir.store %28, %27      (direct store)
// take(A()):  %1 = alloca "agg.tmp0"; call C1(%1)
//   cir.cleanup.scope { %2 = cir.load %1 ; cir.call @_Z4take1A(%2)
//   } cleanup normal { cir.call @_ZN1AD1Ev(%1) }
// callee:  cir.func @_Z4take1A(%arg0: !rec_A) { %0 = alloca "p"
//          cir.store %arg0, %0 ; ... }
```
After `--cir-call-conv-lowering=target=x86_64` the signatures become sret and
pointer parameters (`@_Z4take1A(%arg0: !cir.ptr<!rec_A> {llvm.nofreeobj ...})`:
the spill alloca is removed and `p` is the caller's temporary; the caller
destroys it, Itanium style).

**Runtime** (`s1.cpp`, `s1b.cpp`; the full clang driver and the raw CIR through
the pin's passes give the same log): in every case the constructor's `this`
equals the address of the final object and no copy constructor runs:
`A a = make()`, `chain()`, `nrvo()`, `take(A())` (param address = temporary
address), `take(make())`, `B b{make(), 3}` (ctor writes straight into
`b.a`), `makeb()` (member of the sret slot), `const A &r = make()`,
`takeref(make())`, `pick(true)` (copy ctor into `__retval`, as C++ requires).

**Hand variants** (the function body of `f()` replaced; `lib.cpp` supplies `make`,
`take`):

| Variant | Result |
| --- | --- |
| baseline (clang shape): call, `store` into `agg.tmp` alloca, scope { `load` alloca, call take } | works, identity kept |
| `n1`: `%1 = cir.call @make(); cir.call @take(%1)` (call result used directly as the argument) | **error:** `'cir.call' op non-byval indirect argument that does not name the caller's storage is not yet implemented in CallConvLowering` |
| `n5`: argument is `cir.load` of a pointer that was itself loaded from a slot | same error. The load's address must be the alloca itself |
| `n2`: call result stored into `agg.tmp0` **and** into a second alloca (two uses) | compiles; **silent miscompile**: take's parameter address differs from the ctor's `this` (the result is moved bitwise through a different slot) |
| `n4`: store the result through a pointer loaded from a slot (`%6 = load %5; store %1, %6`) | works, identity kept ("dominating address" need not be an alloca) |
| `n3`: result discarded (`cir.call @make()` unused) | compiles; ctor runs in a hidden sret temporary; no dtor can be run on it |
| callee `take`: `%arg0` stored into two allocas | **error:** `'cir.store' op non-byval parameter 0 spilled more than once is not yet implemented in CallConvLowering` |
| `make3` without `__retval`: local `a`, `cir.cleanup.scope { %5 = load a; cir.return %5 } cleanup { dtor(a) }` | compiles; **silent miscompile**: the local is destroyed, then its bits are copied to the sret slot; the copy constructor is never called |
| `make3` NRVO with `__retval` + `nrvo` flag (clang shape) | works: ctor `this` = caller's object |

**VERDICT: works as designed.** Emission rules for the library (the design's
copy-elision rules, sharpened):
1. Records are by value in signatures and values. `cir.record_layouts` carries
   `arg_passing_kind = cannot_pass_in_regs` and `has_trivial_dtor` from
   `NONTRIVIAL_ABI`/`NONTRIVIAL_DTOR` (`can_pass_in_regs` otherwise); a wrong
   value changes the ABI silently.
2. `STORE(INIT)` of a call result: the call's result has **exactly one use,
   that `cir.store`**, into an address that dominates the call (alloca, a
   `get_member` of one, or a pointer loaded earlier). `agg.tmp`/`ref.tmp`
   style temporaries are ordinary allocas.
3. A by-value `NONTRIVIAL_ABI` argument is always the `cir.load` of an
   alloca *directly*: copy/construct into an `agg.tmp` alloca first (also for
   `take(b.a)`, `take(*p)`), call with `load agg.tmp`, destroy `agg.tmp` in a
   cleanup scope around the call. Never pass a call result or a
   member/pointer load.
4. A callee spills each by-value record parameter exactly once
   (`store %argN, %slot`) and never uses `%argN` otherwise.
5. A function returning a record or array returns `cir.load __retval`; the
   object is constructed in or copied into `__retval` (NRVO: `LOCAL(RETURN_SLOT)` is
   `__retval` plus the `nrvo` bool, the cleanup tests `!nrvo`).
6. **Design change:** `DROP` of a `NONTRIVIAL_ABI` prvalue is allowed only
   when the record has a trivial destructor. Otherwise the result must be
   stored into a temporary (`agg.tmp.ensured`, MATERIALIZE_TEMP) with a dtor
   cleanup, as clang does (a discarded call leaves an unnameable sret slot).

## Spike 2: return sinking past cleanups

`int f1() { int i = 0; G g(&i); return i; }` with `~G() { ++*p; }`; plus
`f2` (return inside an `if`, then fall off), `f3` (return inside `for(;;)`).

**Reference.**
```
cir.cleanup.scope { %4 = cir.load %1 ; cir.store %4, %0       // %0 = __retval
                    %5 = cir.load %0 ; cir.return %5
} cleanup normal { cir.call @_ZN1GD1Ev(%2) } ; cir.trap
```
Runtime (classic clang, `clang -fclangir`, and the raw CIR through the pin's
passes agree): `f1=0`, `f2(true)=5`, `f2(false)=9`, `f3=0`; `~G` runs after the
value was read.

**Hand variants** (body of `f1`):

| Variant | Result |
| --- | --- |
| `h1`: `%4 = cir.load %1; cir.return %4` (bare load of an entry-block alloca, no `__retval`) | compiles; **`f1=1`, wrong.** FlattenCFG sinks the load past the cleanup: `^bb6: %8 = cir.load %2 ; cir.return %8` after `call ~G` |
| `h2`: `%5 = cir.add %4, 0; cir.return %5` (a computed value) | `f1=0`, right (only a *bare load of an alloca* sinks) |

**VERDICT: works, with the fallback as the rule.** The hazard is real and
silent. Rule: while any cleanup is open (inside a `cir.cleanup.scope` body),
a value `RETURN_STMT` is `store %v, __retval; %r = load __retval; cir.return %r`
(`return_slot()` already creates `__retval` for aggregates; scalars use the
same slot in C++ functions that have cleanups). With no cleanup open a plain
`cir.return %v` is fine. `GOTO_STMT.n_cleanups` is not needed: `cir.goto "out"`
out of a cleanup scope, `break`, `continue` and `return` from nested scopes all
run the cleanups (`r4.cpp`: `f4=1 f5=13 f6(1)=50 f6(2)=0`, identical to
classic clang). The cleanup scope is the `return`'s enclosing scopes, and `cir.return` may sit
inside the scope body (followed by `cir.trap` after the scope, as clang does).

## Spike 3: conditional cleanups

Snippets (`T(int)`, `~T()`, `bool ok()` with logging): `b ? T(1).v : 0`,
`a && T(2).ok()`, `(a ? T(3).ok() : T(4).ok()) && b`, `a ? T(5).v + T(6).v : 7`,
`sum(T(1).v, a ? T(2).v : 0)` (an unconditional temporary *before* a
conditional one), `a && (b || T(7).ok())`.

**Reference shape (clang).**
```
cir.cleanup.scope {                       // the whole full-expression
  %5 = cir.load a
  cir.cleanup.scope {                     // opened at the outermost conditional op
    cir.store false, %flag                // %flag = cir.alloca "cleanup.cond"
    %7 = cir.ternary(%5, true {
        cir.call @_ZN1TC1Ei(%2, %8) ; cir.store true, %flag
        ... cir.yield %11 }, false { ... })
    cir.store %7, %1 ; cir.yield          // scope body continues to the end of the full-expr
  } cleanup normal { %6 = cir.load %flag
        cir.if %6 { cir.call @_ZN1TD1Ev(%2) } ; cir.yield }
  cir.yield } cleanup normal { cir.yield }
```
Two conditional temporaries get two flags and two `cir.if`s in one cleanup
region, innermost (last constructed) first. A value that must survive the
full-expression is spilled to `tmp.exprcleanup` (`cir.cleanup.scope` has no
results) and reloaded after the scope. An unconditional cleanup in the same
full-expression gets its own nested scope (`sum(T(1).v, a ? T(2).v : 0)`:
`scope{ ctor T1; scope{ ...; scope{flag; ternary; call sum} cleanup{if flag ~T2} } cleanup{~T1} }`).

Runtime (full clang = raw CIR through the pin): `T(1) ~T(1)`; `T(2) ~T(2)`
(taken), nothing (not taken); `T(5) T(6) ~T(6) ~T(5)`; `c5(1)`: `T(1) T(2)
~T(2) ~T(1)`, `c5(0)`: `T(1) ~T(1)`; `c6`: `T(7) ~T(7)` only when evaluated.
An empty cleanup scope (no conditional cleanup registered in the arms) is
accepted (clang emits empty ones around every `return`).

**Hand variant `c5.wrong_order`** (the design's rule as written: all conditional
cleanups attach to the scope opened at FULLEXPR_BEGIN, the unconditional
`T(1)` scope nested inside it): runs `T(1) T(2) ~T(1) ~T(2)`, **wrong order**.

**VERDICT: works with change X.** Eager wrapping is fine; the design text
(1c, "defers it to that FULLEXPR's end") needs this refinement. Rule:
- FULLEXPR_BEGIN opens a `cir.cleanup.scope` (empty cleanup region), closed at
  FULLEXPR_END (carried value through a `tmp.exprcleanup` alloca).
- The **first conditional region** (TERN/LAND/LOR begin that is not inside
  another conditional) opens its own cleanup scope immediately before the
  `cir.ternary`; it stays open to FULLEXPR_END and is closed (innermost
  first) there. Any CLEANUP_BEGIN inside any arm of that conditional
  registers on this scope: a `cir.alloca "cleanup.cond"` flag (entry block),
  `store false` at the top of the scope body (inserted at the block start),
  `store true` at CLEANUP_BODY, and `cir.if (load flag) { cleanup code }`
  inserted at the **front** of the scope's cleanup region (reverse order).
- Unconditional cleanups after the conditional (`T(1)` before, anything
  after) open nested scopes inside it, so destruction order is
  reverse of construction.
- A scope with an empty cleanup region may be left as is (or its body
  spliced into the parent; both build).

## Spike 4: C1 as `aliasee` of C2 x CallConvLowering

`struct P { long a, b; }; struct A { long s; A(P p); ~A(); }` (a coerced,
two-register record parameter); `template <class T> struct W` explicitly
instantiated (weak_odr); inline class `I` (linkonce_odr).

**Reference (raw CIRGen, no pass).**
```
cir.func @_ZN1AC2E1P(%arg0: !cir.ptr<!rec_A>, %arg1: !rec_P) func_info<#cir.cxx_ctor<!rec_A, custom>> {...}
cir.func private dso_local @_ZN1AC1E1P(!cir.ptr<!rec_A>, !rec_P) alias(@_ZN1AC2E1P) func_info<...>
// weak_odr class:
cir.func comdat("_ZN1WI1PEC5ES0_") weak_odr @_ZN1WI1PEC2ES0_ ... { body }
cir.func weak_odr private @_ZN1WI1PEC1ES0_(!cir.ptr<!rec_W>, !rec_P) alias(@_ZN1WI1PEC2ES0_)
// linkonce_odr / internal class: only the C2 body, comdat linkonce_odr; calls use C2
cir.func comdat linkonce_odr @_ZN1IC2E1P(...) { body }       // no C1 symbol at all
```
After CallConvLowering both functions get the coerced signature
(`%arg1: !s64i, %arg2: !s64i` and `@_ZN1AC1E1P(!cir.ptr<!rec_A>, !s64i, !s64i)
alias(@_ZN1AC2E1P)`), and the LLVM output has `@_ZN1AC1E1P = alias void
(ptr, i64, i64), ptr @_ZN1AC2E1P`. Runtime: a driver calling `A a(p)` through
`_ZN1AC1E1P` prints `A(P) 0x.. s=7`, `s=7`, `~A`, the same as the clang
driver. For W the LLVM is `@_ZN1WI1PEC1ES0_ = weak_odr alias ..., ptr
@..C2ES0_` and the C2 definition carries `comdat($_ZN1WI1PEC5ES0_)`.

**VERDICT: works as designed**; no forwarding body needed. Rules (match
design 4, STRUCTOR):
- external linkage: C2 body; C1 declaration `cir.func private` with
  `aliasee = @C2` and the same (pre-lowering) type.
- weak_odr: C2 body with `comdat("<name with C5/D5>")`, `weak_odr`; C1
  `weak_odr private` with `aliasee`.
- linkonce_odr / internal (discardable): C2 body with its own comdat (linkonce);
  no C1 symbol: every reference to C1 is substituted by C2 at finish. The M2.1
  finish-time substitution map (`symbol_changes_`, one walk) does this;
  **the design's "through the per-entity `calls`/`addresses` lists" is
  wrong** (they do not cover constant attributes).
- Names derived from final names (comdat, `aliasee`) are computed at finish
  after the substitution (see `assign_final_names`).

## Spike 5: ctor regions, init order, guards, exit registration

`int a = f(1);`, `int b = (f(2)>0 && f(3)>0) ? f(4) : f(5);`, `S s(6);`,
`inline int iv = f(7);`, `template <class T> T TV<T>::v = f(8);`, locals
`static int x = f(9); static S ls(10);`, `const S &sr = S(11);`,
`static const S &lr = S(12);`, and `static int y = (b && f(1)>0) ? f(2) : f(3);`
inside a function.

**Reference (raw CIRGen).**
```
cir.global external @a = #cir.int<0> : !s32i ctor { %0 = cir.get_global @a ; ... cir.store %2, %0 }
cir.global external @s = #cir.zero : !rec_S ctor { ...C1(%0,%1) } dtor { %0 = cir.get_global @s ; cir.call @_ZN1SD1Ev(%0) }
cir.global linkonce_odr comdat dynamic_init_guard<"_ZGV2iv"> @iv = #cir.int<0> : !s32i ctor { ... }
      dynamic_init_info<local = false, tls = none, is_inline = true, tsk = undeclared>   // TV<int>::v: tsk = implicit_instantiation
cir.global "private" internal dynamic_init_guard<"_ZGVZ1hvE1x"> @_ZZ1hvE1x = #cir.int<0> : !s32i
      dynamic_init_info<local = true, tls = none, is_inline = false, tsk = undeclared>
// in h():  %1 = cir.get_global static_local @_ZZ1hvE1x
//          cir.local_init static_local @_ZZ1hvE1x ctor { ... cir.yield }
//          cir.local_init static_local @_ZZ1hvE2ls ctor { C1 } dtor { %8 = cir.get_global static_local @..; D1 }
// const S &sr = S(11):
cir.global external @sr = #cir.ptr<null> : !cir.ptr<!rec_S> ctor { %1 = cir.get_global @_ZGR2sr_ ; call C1(%1, 11)
      cir.register_exit_dtor @_ZGR2sr_ { %3 = cir.get_global @_ZGR2sr_ ; cir.call @_ZN1SD1Ev(%3) }
      cir.store %1, %0 }
cir.global "private" internal @_ZGR2sr_ = #cir.zero : !rec_S      // the lifetime-extended temporary
```
The `b` ctor region has two `cir.ternary` ops, one block (the `&&` is a ternary).
The `local_init` ctor region may use values of the enclosing function (it
reads the function's `b` alloca) and holds `?:`/`&&` as well; it builds and
runs (`g3`).

**Runtime** (full clang = raw CIR through the pin; also equal to
classic clang's order): `f(7) f(8) f(1) f(2) f(3) f(4) S(6) main f(9) S(10)
h=19 h=19 tv=8 ~S(10) ~S(6)`; `g2`: `S(11) main S(12) h2=12 h2=12 ~S(12)
~S(11)` (`__cxa_atexit` calls generated by LoweringPrepare).

**Init order is the module order of the `cir.global` ops.** Hand variant
`5a`: `@b` moved before `@a` -> `f(7) f(8) f(2) f(3) f(4) f(1) S(6) ...`.
LoweringPrepare puts every guarded vague-linkage global (`iv`, `TV<int>::v`)
into its own `llvm.global_ctors` entry (priority 65535, associated with the
variable, before `_GLOBAL__sub_I_*`), and the ordered ones in module order
inside `_GLOBAL__sub_I_*`; this reproduces classic clang's order.

**Hand variants** (all on the module above):

| Variant | Result |
| --- | --- |
| `5b`: remove `dynamic_init_guard` from `@_ZZ1hvE1x` | **verifier error**: `'dynamic_init_guard' and 'dynamic_init_info' must be present together` |
| `5c`: `cir.get_global` of `@_ZZ1hvE1x` without `static_local` | **verifier error**: `static_local attribute mismatch` |
| `5d`: remove `dynamic_init_info` from `@iv` (guard kept) | **verifier error**: `Cannot have a static-local global-op with a constructor or destructor ... require in-function initialization via LocalInitOp` |
| `5e`: no `cir.lowering_lang_options` module attribute | builds, guards still emitted (default thread-safe); keep the attribute (`exceptions=false`, `threadsafe_statics`) anyway |
| `5f`: dtor region whose first op is not the `get_global` | **assertion** `cast<GetGlobalOp>` in LoweringPrepare (`getOrCreateDtorFunc`); with a dead `cir.const` first it only worked because canonicalize erased the const |
| `5g`: all `ast(#cir.var.decl.ast)` removed | builds and runs identically |

**VERDICT: works as designed.** Rules:
- Create the `cir.global` of every dynamically initialized variable **in
  dynamic-initialization record order** (move it to the end of the module when
  its STATIC_INIT is processed, as the design says); the initializer is the
  `ctor` region (one block; `cir.ternary` regions are fine, no other block-making
  statement) and optionally a `dtor` region that **starts with a
  `get_global` of the variable**.
- A guarded variable (static local, inline variable, template static member:
  `DECL_VAR GUARDED`) carries `dynamic_init_guard<"_ZGV...">` **and**
  `dynamic_init_info<local, tls, is_inline, tsk>` together; the name is the
  Itanium static-guard name (`mangleStaticGuardVariable`): `_ZGV` + the
  variable's mangled name without `_Z` (`_ZGVZ1hvE1x` for `h()::x`,
  `_ZGVN2TVIiE1vE` for `TV<int>::v`), or `_ZGV<len><id>` for an unmangled
  global (`_ZGV2iv` for `iv`). Static locals do not get ctor/dtor regions on
  the global: every `get_global` of the variable inside the function carries
  `static_local`, and the initialization is `cir.local_init static_local @sym
  ctor {...} [dtor {...}]` at the declaration point (the ctor region may use
  the function's values).
- An exit cleanup on another entity (lifetime-extended temporary in a static
  init) is a separate internal `cir.global` (`_ZGR...`) plus
  `cir.register_exit_dtor @thatglobal { get_global; dtor call }` inside the
  ctor/local_init region.
- Keep `cir.lowering_lang_options`. Do not emit `ast(...)` or `func_info`.

## Spike 6: `cir.delete_array`, cookie, `new[]`

`struct D { D(); ~D(); }` (cookie), `struct E { ~E(); static void
operator delete[](void*, size_t); }` (sized class operator), `int` (no
cookie), plus `delete p` for `D`.

**Reference (raw CIRGen), `new D[n]` (cookie 8):**
```
%3 = cir.cast integral %n : u32 -> u64
%result, %overflow = cir.mul.overflow %3, 4 ; %result_0, %overflow_1 = cir.add.overflow %result, 8
%6 = cir.or %overflow, %overflow_1 ; %8 = cir.select if %6 then 0xFFFF..FF else %result_0
%9 = cir.call @_Znam(%8) {allocsize = ..., builtin}
%10 = bitcast %9 to !u8i* ; cir.store %3, (bitcast %10 to !u64i*)       // the count, at the start
%13 = cir.ptr_stride %10, 8 ; ... %17 = bitcast to !rec_D*
cir.array.ctor %17, %3 : !cir.ptr<!rec_D>, !u64i { ^bb0(%arg1: !cir.ptr<!rec_D>): cir.call @_ZN1DC2Ev(%arg1) }
```
**`delete[]`:**
```
cir.if (p != null) { cir.delete_array %1 : !cir.ptr<!rec_D> delete_fn(@_ZdaPvm) delete_params<size = true>
                       element_dtor(@_ZN1DD2Ev) element_align(4) }
cir.if (p != null) { cir.delete_array %1 : !cir.ptr<!rec_E> delete_fn(@_ZN1EdaEPvm) delete_params<size = true> ... }   // class operator
cir.if (p != null) { cir.delete_array %1 : !cir.ptr<!s32i> delete_fn(@_ZdaPv) element_align(4) }                   // trivial: no cookie, no params
```
(`delete p`: `cir.if (p != null) { cir.cleanup.scope { call D2(p) } cleanup normal { call _ZdlPvm(p, 4) } }`.)
`cir.delete_array` is lowered by CXXABILowering (`hasCXXABILowering`), part of
the library's pass list. `UsualDeleteParamsAttr` = `{size, alignment (optional),
type_aware_delete, destroying_delete}`; `element_align` must be the value
`new[]` used to size the cookie (cookie = max(8, align)).

**Runtime** (full clang = raw CIR through the pin): `D() D() D() a=.. cookie=3
~D() ~D() ~D()` (reverse), `E`: `cookie=2`, `~E() ~E()`, `E::delete[] sz=16` (2*4
+ cookie 8), int: no cookie, `delete p`: `D() ~D()`.

**VERDICT: works as designed.** Rules: NEW_ARRAY_SIZE is the overflow
builtins + `or` + `select` above; NEW_ARRAY_COOKIE stores the count at the
returned pointer (as `u64`) and `ptr_stride`s by the cookie bytes;
ARRAY_CTOR_BEGIN is `cir.array.ctor %ptr, %count` with a one-block region
(element pointer is the block argument; element ctor is the **C2 symbol**);
DELETE with `array` is `cir.if (!= null) { cir.delete_array ... }` with
`delete_params` only when the selected operator takes the size/alignment
(cookie present exactly then or when there is a non-trivial dtor) and
`element_dtor` the D2 symbol; non-array DELETE is the cleanup-scope shape above.

## Spike 7: `.base` subobject record types

Classes in `cir_src/spike/cxx-shapes/s7/l2.cpp`, `l3.cpp` (non-POD base `B`
with tail padding, derived `D`, empty bases `E1`/`E2`/`E5`/`F`/`M`/`G`,
two-level `D3`, member of the base type `D2`, 8-byte alignment `K`/`L`/`N`).
`cir-layout-compare-cxx.py` takes the facts from `-fdump-record-layouts`,
encodes each class as ECIL (the class used as a base with `dsize < size` gets
a second `TY_RECORD` `"<name>.base"` of size `dsize`, which is what `BASE` +
`POTENTIALLY_OVERLAPPING` ask the library to synthesize; empty bases are left
out; bases at their offsets), runs `edg-cir --emit=cir` **with the M2.1
library unchanged** and compares the record types with clang's.

Result (22 record types compared over the two files; `E1` is in both):
**15 match exactly**, including `B.base packed
{s32, s8}`, `D {B.base, s8, pad[2]}`, `D.base {B.base, s8}`, `D3 {D.base, s8,
pad}`, `G {B.base, s16}` (empty base omitted), `M`, `E2 {s32}`, `E5 {pad}`,
`K.base`, `L {K.base, s32}`, `P`, `Q`, `R`. The 7 that differ (`B`, `B2`,
`D2`, `F`, `K`, `L2`, `N`) show two kinds of difference, one cosmetic and one a
flag mistake of my encoder:

| Difference | Cause | Effect |
| --- | --- | --- |
| clang `B packed {s32, s8, pad[3]}`, edg `{s32, s8}` (also `B2`, `D2`, `K`, `L2`, `N`: explicit tail/gap padding arrays, `packed`) | clang writes the tail padding of a class with `dsize < size` and between a packed `.base` and the next aligned member explicitly | none: both give the same size/offsets; `cir-compare.py --cxx` strips `pad` arrays and `packed` |
| `F`: edg `{pad, empty E1, s32}`, clang `{pad, data E1, s32}` | the encoder set `EMPTY_FOR_ABI` on the empty **member** `E1 e` | a C++ member of empty class type is a real 1-byte `data` member; **do not set `EMPTY_FOR_ABI` for members in C++**, only the empty *bases* are omitted |
| `N`: edg `{K.base, K, empty E1}` | same flag on member `E1 e` | same fix |

**VERDICT: works as designed** with the M2 layout lowering. The library rule
for ECIL v1: a REC_FIELD with `BASE` of an empty class contributes nothing;
`POTENTIALLY_OVERLAPPING` selects the `<name>.base` record (synthesized from
the class's own fields, size = `dsize`, `packed` when the size is not a
multiple of its alignment), created on first use; `record_layouts` has an entry
for the complete record only (also what `cir-layout-compare.py` checks).
Optional parity with clang (not needed for correctness): add the explicit tail
pad when `dsize < size`.

## Remaining checks recorded for M3b

- Re-run `cir-compare.py --cxx --structors --global-order` on the first C++
  slice against clang for the same sources; `cir-layout-compare-cxx.py` for the
  record types.
- `func_info`/`ast` omission was tested on non-virtual classes only; M4
  (virtual bases/vtables) may need `func_info`.
- The spikes ran on hand-edited **plain** CIRGen output; the first library
  emission should be diffed against `clang-cir-shapes.sh` output for the same
  snippets (`s1b.cpp`, `s3/c*.cpp`, `s5/g*.cpp`, `s6/d.cpp`) before the
  corpus.
