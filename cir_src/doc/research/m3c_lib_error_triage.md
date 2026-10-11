# M3c: C++ corpus library-error triage and run stage

2026-10-11, base commit 908f7b680a plus the working tree.  The writer (src/)
is the tree as rsynced to the box at about 09:12 EDT and rebuilt; the library
is cir_src as of the end of this session.

## 1. Method

All of it runs in the cir-env container on the box (`~/edg-cir-lib`).

- `cir_src/scripts/box_corpus.py` (+ `box_corpus1.sh`): the 1771 corpus cases
  through cpfe-cir (the early runs passed `--ecil_allow_cxx`, since removed) and `edg-cir --emit=asm` only, library
  default mode (set `CORPUS_KG=` for no `--keep-going`; that is what is quoted
  below).  Front-end options are the corpus `variant` strings.
- `cir_src/scripts/box_runall.py`: every case of the 1866 recorded commands
  (`eccp ...` of the last checkpoint run, so the front-end options are the
  real ones) through front end, library, link and run.  Run-and-print tests
  only; the program's output is compared with the recorded expectation
  (`.NAME.rto/default.K.2.txt`, K = TEST_NUMBER) with hex addresses masked.
  Classes: FE_DIAG/FE_NONE (front end stopped), LIB_CRASH, LIB_ERROR, LINK_FAIL,
  RUN_TIMEOUT, RUN_CRASH (signal), RUN_RC (non-zero exit, output equal),
  RUN_DIFF, RUN_OK, NOT_RP.
- The reference expectations are checked as the orchestrator asked:
  `tests/expectations/edg_x86_64/{imported,edg,cwg}.log` list the cases that
  fail with the reference EDG (paths are relative to imported/, edg/, cwg/).
  A CIR failure whose (path, variant) is in them is "ref FAIL" and is not
  chased.
- `cir_src/scripts/box_reasons.py`: for the "was dropped" errors, why the
  writer dropped the entity (its own warnings).

## 2. Numbers (default mode, the writer's src of this morning)

Run stage (`box_runall.py`, 1866 case rows):

| class | count | of which ref also fails |
|---|---|---|
| RUN_OK | 677 | (6 + 4 + 2 pass where the reference fails) |
| LIB_ERROR | 630 | 49 + 14 + 2 + 2 (a negative or MS-mode case) |
| NOT_RP (not run-and-print, or -DNEG) | 394 | |
| FE_DIAG / FE_NONE | 85 / 43 | my harness (multi-file `.mft`, `-DNEG` negative variants, MS and aarch64 options) |
| RUN_CRASH | 19 | 18 |
| RUN_RC | 12 | 10 |
| RUN_DIFF | 5 | 1 |
| LINK_FAIL | 1 | 0 |
| LIB_CRASH | 0 | |

No LIB_CRASH: the FlattenCFG assertion cases are fixed (section 3).

## 3. Library bugs, fixed (each with a lit test)

1. **A dropped definition of a variable with a body-less record type was
   erased silently** (the global was thrown away with the incomplete-type
   cleanup before the unsupported policy saw it).  The program then ran
   without constructing the object: edg/ccs/inhctor/gnats17687b,
   gnats20175, gnats20175b (`struct X : virtual W ... X x(0);`).  Now an
   error in the default mode.  Test: `cxx-incomplete-global-dropped.test`.
2. **The "unused discardable definition is erased" policy must not erase
   variables.**  `template <class T> struct S { S() { i = 1; } }; static
   S<int> s[1];` has its initializer dropped (writer: "non-constant
   initializer element") and nothing names `s`, but the constructor had an
   effect: imported/gnu/cpp/template/ctor2 ran and returned 1.  Only
   function definitions are erased now; a dropped variable is an error unless
   `--keep-going`.  Test: `unsupported-variable.test`.  (This narrows the
   2026-10-11 spec text "a dropped definition": please read it as "a dropped
   function definition".)
3. **FlattenCFG assertion "cleanup scope body has no exit"** (is_polymorphic,
   has_virtual_destructor, both ext/): a noreturn call as the last thing in a
   cleanup scope body was followed by `cir.unreachable`.  The library now
   marks the call `noreturn` and continues, as CIRGen does.  Test:
   `cxx-noreturn-cleanup.test` (runs).
4. **Reader rule "a BASE field comes before the data members"** rejected every
   polymorphic derived class: the vptr (an unnamed opaque field) is sent
   first, then the bases: abi/layout1, vbase13, expr/cast4, inherit/using2,
   ipa/pr67056, torture/pr111019, tree-ssa/pr95638.  The vptr no longer ends
   the run of bases.  Fixture: `test/format/fixtures/good_v1_vptr_before_base.txt`.
   Please state the order (vptr, bases, members) in 11.3.
5. **`p + (a < b)`: a bool PTR_STRIDE index** (opt/pr59470) is promoted to int.
   Test: `ptr-stride-bool.test` (runs).
6. **`return;` in a non-void C++ function** (a GNU extension, parse/crash78a)
   trapped; it now returns the return slot like C.  (Covered by the corpus
   case; no separate lit test.)
7. Earlier this session and still relevant to this list: incomplete
   `GREF`/`DEREF` (layout2, unroll1, defarg10), function-pointer `DEREF`
   (default4, const1), constant record type check, 128-bit.

Error messages now name what failed: PTR_STRIDE gives the operand types and
"the type of function X changed after its body was emitted" gives the symbol
and the two types.

## 4. Writer stream bugs (one repro each)

Each has the affected tests.  "Stream" lines are `ecil-dump` excerpts.

**W1. No FULLEXPR around each array-element initializer** (init/array12;
wrong destruction order; **the reference also fails it**, so this is the FE's IL
and not CIR-specific: low priority).
```
struct A { A(); A(const A&); ~A(); };
struct B { B(int, const A& = A()); };
int main() { B b[] = { 1, 2 }; }       // expected: A() B() ~A() A() B() ~A()
```
The stream has both elements' `CLEANUP_BEGIN` directly in the block, ending at
`BLOCK_END`, not inside a `FULLEXPR` per element:
```
AGG_INIT_BEGIN 36 0
  AGG_SEL 0 ... CONST_INT 1 1  LOCAL 2 ... CALL ctor  CLEANUP_BEGIN 3 nil 0 .. CLEANUP_BODY  CALL B(int,const A&)
  AGG_SEL 1 ... (the same with LOCAL 3)
AGG_INIT_END
```
We print `A() B() A() B() ~A() ~A()`.  Wrap each element's initializer in
`FULLEXPR_BEGIN/END` (the temporary dies at the end of that element).

**W2. NRVO variable has no CLEANUP** (opt/nrv22; **the reference also fails
it**: the FE's IL has no destruction for the NRVO variable on the goto path: `L: A a; if (cnt--) goto L;
return a;`).  `LOCAL(RETURN_SLOT)` is sent but its destruction is not, so the
goto leaves the object undestroyed.  The library already guards a cleanup
whose first operation is `LREF` of the NRVO local by an `nrvo` flag (set by
`RETURN_FROM_SLOT`), see `cxx-nrvo-goto.test`.  Send the normal cleanup.

**W3. `__attribute__((constructor/destructor [(priority)]))` is silently
dropped** (init/attrib1, special/initpri1).  `void f() __attribute__((constructor));
void f(){ i = 1; }` is an ordinary function in the stream and nothing says it
must run before `main`.  Diagnose "unsupported" (function dropped) until the
contract carries the fact (suggest `DECL_FUNC` flags `constructor`/`destructor`
plus a priority `EXT`).  Today the TU compiles and misbehaves, which is worse
than an error.

**W4. A namespace-scope array of class objects with a constructor** is dropped
as "a non-constant initializer element" (template/ctor2, and 3 more "a
non-constant initializer element" cases: defaulted45 and others):
`static S<int> s[1];`.  Expressible with `STATIC_INIT` and `ARRAY_CTOR`.

**W5. Static versus dynamic initialization of a variable that EDG initializes
statically** (cpp0x/constexpr-static4):
```
int f() { if (ar[0] != 42 || ar[1] != 0) abort(); return 1; }
int i = f();
int ar[2] = { 42, i };        // the reference: initialized statically (i is 0)
```
The stream has two `STATIC_INIT` blocks in source order (`i` then `ar`), so
`f()` runs before `ar` is set and aborts.  The reference binary does not
(C++11 3.6.2/3 allows the early static initialization).  If the FE's IL says
the initialization is static, send `VAR_INIT` constants.

**W6. A function body is emitted with an unprototyped type, and the real type
arrives later** (pr77550; the reference gives BADC(COMPILE) too):
```
TY_FUNC 17 4 1 []                       <- "(...)"
DECL_FUNC 18 17 0 0 "_ZNSt7__cxx1112basic_string...12_Alloc_hiderC1EPcOS3_"
FUNC_BEGIN 18 0 .. FUNC_END             <- the body
... later: the same g with the real type (this, char*, allocator&&)
```
R8 says to re-issue before `FUNC_BEGIN`.  Make the type final first, or retype
(the library refuses it by design after the body).

**W7. Record bodies emitted out of dependency order** (torture/pr89303;
CRTP with a by-value member that points back):
```
template <class T> struct W { T *p; W() : p(0) {} };
template <class T> struct E { W<T> w; };
struct B : E<B> { int x; };
B b;
```
```
TY_RECORD_DECL 5 0 "W"   TY_RECORD_DECL 6 0 "B"   TY_RECORD_DECL 7 0 "E"
TY_RECORD_BODY 7 8 8 8 0 1   REC_FIELD 5 0 ...      <- G-id 5 (W<B>) has no body yet
```
`E<B>`'s body is sent before `W<B>`'s although `W<B>` needs only a pointer to
the incomplete `B`.

**W8. `--old_for_init`: the for-init variable is used after the loop**
(edg/daveed/d00350c, `-DTEST_NUMBER=3 --old_for_init`): the stream has
`LREF 2` after `FOR_END` for a `LOCAL 2` declared inside the for's scope
region.  Hoist the LOCAL to the enclosing scope region in that mode.

**W9. A jump into a switch past a declaration** (edg/rma/lifetime03, `-DTEST_NUMBER=2`):
`LREF 31` of a temporary whose `LOCAL`/`MATERIALIZE_TEMP` is not on that path
("unknown local id").  The source is `if (j == 0) goto lab; switch (j) { case 999:
i = 2; lab: a = i ? A(1) : A(2); break; ... }`; I did not get it smaller.

**W10. A used variable of a body-less class** (abi/layout2, inherit/thunk7:
`E e;` where E has virtual bases; `main` takes `&e` as `C_ADDR` and is not
marked unsupported).  The record has no body, so `e` has no layout; the library
now errors ("... has a record type without a body and is used" or "was dropped
(and it is referenced)").  The writer should drop the users (or send the body).

**W11. Tests that rely on the optimizer** (cpp0x/enum_base_warn: `if (always
false) link_error();` fails to link at -O0).  The reference is fine because
the dead branch vanishes.  Folding a constant `IF_BEGIN` condition in the
writer would fix the ~90 `link_error` cases of both corpora; the library does
not fold.

## 5. Legitimately unsupported (writer diagnosed; the library errors by policy)

The "was dropped" errors are 638 of the LIB_ERRORs (the entity is dropped, and
it is a definition that something references, or external).  Reasons from the
writer's own warnings, in cases (a case can have several):

| cases | reason | example |
|---|---|---|
| 238 | a constructor or destructor of a class with virtual functions or virtual bases | label13a |
| 207 | a virtual function | has_trivial_assign |
| 123 | this kind of statement (= **try/catch**) | P2718R0 |
| 91 | a virtual call | covariant1 |
| 78 | the expression kind 8 (= **throw**) | sync-4 |
| 44 | a class conditional that does not initialize a variable | ref16 |
| 36 | a constant of a class with virtual functions or virtual bases | covariant2 |
| 33 | a variable of a record type that is not supported | label13a |
| 26 | inline assembly | new39 |
| 21 | a vector type | vector30 |
| 20 | the initialization of an undeclared local variable | gnats20911 |
| 19 | the constant kind 8 | pm1 |
| 18 | a parameter of an unsupported type | lifetime4 |
| 16 | a return type that is unsupported | array51 |
| 14 | a reference to an undeclared local variable | array18 |
| 14 | a cast through an indirect base class | covariant17 |
| 13 | the builtin operation 47 | vshuf-v16hi |
| 12 each | GNU function multiversioning; an alias, ifunc or weakref attribute; this kind of aggregate member initializer; this kind of address constant | mv1, dr2137-1, label13 |
| 11 | a virtual base class | null1 |
| 9 | a cleanup attribute | cleanup-10 |
| <= 8 each | a call returning an unsupported type (8), the initializer of a member of an anonymous union or struct (8), a static member selected from an expression with side effects (7), a floating constant of this format (7), delete of an object with a virtual destructor (7), the operator 64 (7), an initializer of a new[] with a run-time count (6), this conversion (6), a variable length array (6), an operation on an unsupported scalar type (5), a reference to a parameter by position (4), the GNU ?: with an omitted operand (4), the constant kind 5 (4), a non-constant initializer element (4), the operator 108 (4), a complex type (3), the constant kind 17 (3), __builtin_clear_padding (3), a GNU statement expression (3), the operator 109 (3), __builtin_launder (3), ... | |

"Diagnose instead" / better-diagnose items for the writer:

- **Name the numbered reasons.**  "the expression kind 8" is `enk_throw` (checked
  with `void f(){ throw 1; }`) and "this kind of statement" is try/catch
  (`void f(){ try {...} catch (int) {} }`); the other ordinals ("the constant
  kind 8/5/17/11", "the operator 64/108/109/18/86", "the builtin operation
  47/65/57", "the expression kind 11") should print the enumerator name.  Both
  throw and try/catch are 200+ cases (EH is off in v1: say "exceptions").
- **Library errors that should have been writer diagnostics** (the library
  rejected a stream it should never have received): the W6-W10 causes above, and
  the `REC_FIELD` overlap cases mentioned in the previous report (an extra
  indirect-base field, see below) for classes with indirect empty bases.
- **Silent loss** (no diagnostic, wrong behavior): W3 (constructor attribute),
  W5 (init order).  These are the ones to fix first.

Other stream observation (not an error today): for `struct C : A, B` where B
derives from A, the writer lists an extra BASE field for B's inner A; and the
`data_size` of classes with empty bases is short of their layout (the library
now raises it to the end of the last member).  Both were reported earlier; the
corpus classes concerned are abi/empty4, empty8, empty11, init/empty1,
ipa/pr46053, pr46287-*, pr46984, opt/pr23299, torture/pr45699.

## 6. Run stage: the failures

The cases whose reference run passes (plus array12 and nrv22 for completeness),
with a first guess of cause.
"Not CIR-specific" means the harness cannot compare them (they print stack or
static addresses that the recorded files mask differently) or the reference
fails too.

| case | class | first guess |
|---|---|---|
| cpp0x/constexpr-static4 | RUN_CRASH (abort) | W5 (init order) |
| init/attrib1, special/initpri1 | RUN_RC | W3 (constructor attribute dropped) |
| cpp0x/enum_base_warn | LINK_FAIL | W11 (`link_error` at -O0) |
| cpp0x/constexpr-tuple, init/elide3, tc1/dr20, opt/alias2 | RUN_DIFF | not CIR-specific: addresses printed (the recorded `STACK` placeholders and static addresses differ; compare by masking) |
| init/array12, opt/nrv22 | RUN_DIFF / RUN_CRASH | W1 and W2; the reference fails both, not chased |

The remaining RUN_CRASH (18), RUN_RC (10) and RUN_DIFF (1) are reference
failures.  Among the 55 cases that became compilable with the
unsupported-discardable policy (default mode; before the variable fix of
section 3.2): 36 RUN_OK, 7 not run-and-print, 4 run failures that the reference
also has (aligned-new9, icf2, icf3 abort; bitfield8 rc=1), and 8 that are
errors again after the policy fixes of section 3 (items 1 and 2, on purpose).

## 7. Open questions for the orchestrator

1. Policy text: variables are never erased in the default mode (3.2).
2. The vptr-first field order (3.4) should be written down in 11.3.
3. W3 and W5 are silent miscompiles; I suggest a writer diagnostic now.
