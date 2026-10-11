# C++ CIR shape differential: EDG front end + CIRGen against clang's CIRGen

M3c deliverable.  Real front-end output (cpfe-cir, ECIL v1 for C++) compiled
by this library to CIR is compared, function by function, with what the pin's
`clang -fclangir -emit-cir` produces for the same source.

- Run: Sat 2026-10-10, 23:07 EDT (first build) to 23:40 EDT (final numbers).
  Base commit 908f7b680a plus the working tree.  The numbers below are from
  the final runs: `src/` as rsynced to the box tree at 23:32 EDT (it includes
  the writer's 23:13 change that sets `ECIL_LANG_THREADSAFE_STATICS`), and
  `cir_src/` as of the end of the session (with the two library changes in
  section 6).
- Pin: llvm-project 894bfff5 (clang 24.0.0git, `/edg/llvm-cir/bin/clang` in
  the cir-env image), target x86_64-unknown-linux-gnu.
- Inputs: the 12 `tests/tests/cir/cirgen/cxx-*.sft.cpp` sources and the 42
  `.cpp` sources of `cir_src/spike/cxx-shapes/**` (54 sources, 357 functions
  compared; `unions.c` is C and is covered by the C tests).

## 1. Result in one paragraph

No semantic difference remains between the CIR this library produces from the
front end's stream and clang's CIRGen output, for any function of any of the
54 sources, after the normalizations of section 3.  Three real differences
were found along the way: two are not library defects (a data size the front
end reports short of its own class layout, and a GNU-mode lifetime
difference of the front end), one was library-side and is fixed (section 5).
Everything else is one of the 24 benign categories of section 4, all of which
are a different way to write the same operations.  The call sequence of every
function (callees, copies, destructor registrations, deletes) is identical
apart from the benign cases listed there.  After the CIR passes
(`--emit=cir-lowered` against clang's `-emit-cir` with passes) the same
holds, and the static-local guard protocol (`__cxa_guard_acquire` /
`__cxa_guard_release`, `__cxa_atexit`, `_ZGV` guards) is identical once the
writer's thread-safe-statics flag is in the stream.

## 2. Method

```
cir_src/scripts/box.sh sync                       # cir_src to the box tree
rsync -a --delete src/ box:edg-cir-lib/src/       # the writer's tree (23:32)
cir_src/scripts/box.sh run diff bash cir_src/scripts/box_diff.sh FILES...
cir_src/scripts/box.sh run diffp bash cir_src/scripts/box_diff.sh --nobuild --passes FILES...
python3 cir_src/scripts/cir-diff-classify.py build/scratch/diff/*.cmp
```

`box_diff.sh` (inside cir-env): builds, then for each source runs
`cpfe-cir --c++NN` (NN from the file's `//options:` line,
17 by default; sources that include system headers use
`--g++ --gnu_version=150200` and the container's libstdc++ include path, the
others the front end's default mode), `edg-cir --emit=cir --disable-cir-passes`
and clang `-cc1 -fclangir -emit-cir -clangir-disable-passes`, and diffs them
with `cir-compare.py --cxx --fe --keep-going --global-order`.  With
`--passes` both sides run their CIR passes (`edg-cir --emit=cir-lowered`).

`cir-compare.py --cxx --fe` (what it does to both sides before diffing):

- structors: `C1`/`C2`/`D0`/`D1`/`D2` variants collapse to one key (the real
  body wins over an alias or a forwarder); comdat spellings are folded;
- per function: allocas sorted by name with scoped allocas renamed in a scope
  stack; SSA numbers dropped; temporary names folded (`agg.tmp`, `ref.tmp`,
  `tmp`, ...); `__retval` round trips and the writer's body-wide `cir.scope`
  with a trailing `cir.unreachable` folded; `init`, `align`, `deref`,
  `inline_hint`, `no_inline`, `nonnull` attribute noise dropped; a chain of
  base casts is one cast with the summed offset; `this` reloads canonical;
- anonymous and shortened record names; private string literals named by
  their contents (each side numbers `.str.N` by its own emission order);
- reported per DIFF function: the operation multiset delta (`ops:`), the
  ordered call/copy/exit-registration/delete sequence (`calls: same sequence`
  or `calls: DIFFER`), the count of null-checked base/derived casts, and the
  unified diff.  Per file: global initializers, record types (`DIFF type`),
  global initialization order, and functions or globals present on one side
  only.

Not used: `--loose`.  Not changed: the writer's output expectations.

`cir-diff-classify.py` tags every `ops:` delta line with the first matching
benign category of section 4; a delta line no rule matches is tagged `other`
and printed.  The final run has no `other` line.  (The tagging is a reading
aid: a function whose tags are all benign could still hide a count
difference, which is why the call sequences, the record types, the globals
and the run-time comparison below are checked separately.)

Run-time cross-check on the same tree: the 12 tests of
`tests/tests/cir/cirgen` run through `box_realfe.sh` against g++: 11 print
the same output as g++; `cxx-s3-statics` differs from g++ in the order of
unordered dynamic initialization of template statics exactly as recorded
(`7 8 1 2 3 ...`, which is clang's order; the recorded expectation).  lit:
73 of 73 pass (69 at the time of the differential; four tests were added afterwards for the LIB_CRASH and __int128 work).

## 3. What a clean comparison is

For one function the sections of the output mean:

| line | meaning |
|---|---|
| `same` | text identical after normalization |
| `DIFF` with `ops: same multiset` | the same operations, in another order or with another header (attributes) |
| `DIFF` with `ops: ... only` | operations that exist on one side only (tagged in section 4) |
| `calls: same sequence` | the same ordered side effects (calls, copies, registrations, deletes) |
| `DIFF type`, `DIFF global` | a record type or global initializer differs |

## 4. Benign differences

Each category is a difference in how the same thing is written.  `Where` names
the sources where it occurs; the counts are functions (final plain run, 217
DIFF functions in all, many with several tags).

1. **order** (86).  Same operations, another order (allocas, loads of
   `this`, argument evaluation temporaries) or only the function header
   differs.  Calls are in the same order unless noted below.
2. **retval** (45).  Clang's CIRGen stores `0` to `__retval` at the entry of
   `main`, returns through `__retval` where cleanups are open and ends a
   body with an unreachable `^bb`.  The library returns the value directly
   with `cir.return` inside the cleanup scopes (clang does the same for an
   explicit `return` expression inside a cleanup scope) and does not emit the
   dead block.
3. **scope** (33).  Clang wraps every full-expression statement and every
   conditional initialization in `cir.scope` / `cir.cleanup.scope` +
   `cir.if` (an empty cleanup scope for a trivial temporary); the library
   emits the same operations in the enclosing region, and a conditional
   initialization as a void `cir.ternary`.
4. **exprcleanup** (13).  Clang keeps the address of a lifetime-extended or
   cleanup-carried temporary in a `tmp.exprcleanup` alloca; the library uses
   the temporary's own alloca.
5. **temp** (6).  Temporary allocas and stores that exist on one side only
   (clang copies a trivially-copyable class argument into a `TMP` and loads
   it; the library loads the variable).
6. **array-index** (75).  `array_to_ptrdecay` + `get_element` (clang) against
   `bitcast` + `ptr_stride` (the front end's explicit pointer arithmetic);
   `cast integral` for the index.  Lowers to the same GEP.
7. **fe-folded** (38).  Constants and arithmetic the front end folds before
   the stream: constexpr lambda calls with constant arguments (`plain(5)`,
   `outer(3)`, the generic-lambda conditions of `cxx-s5-lambdas`), an
   in-class `static const int` used as a value (`S::sc`: clang loads it
   through a `.const` global, the front end sends the constant 3), constant
   array sizes of `new[]`.
8. **cond-shape** (6).  `cir.ternary` against `cir.if` with a scope, `select`,
   `bool_to_int` / `not` forms of the same condition.
9. **address** (19).  The same address materialized once or twice (a
   `get_global` of a temporary after the destructor registration in a global
   constructor region; a second `get_global` of a static local for the
   store).
10. **nrvo-flag** (3).  Clang's NRVO variable gets an `nrvo` flag alloca and a
    cleanup that runs the destructor of `__retval` when the flag is false.
    The flag is set on the only exit path (the return), so the destructor
    never runs on a normal path; the library emits no flag and no cleanup (it
    has no exceptions in v1).  Sequence difference `[ctor, dtor]` against
    `[ctor]`.
11. **struct-copy** (4).  A trivial copy assignment of a base subobject:
    clang calls the implicit `B::operator=` (and defines it); the front end
    treats it as an aggregate copy and the library emits
    `cir.libc.memcpy` of the data size (skipping tail padding that a derived
    class reuses).  Same bytes.  Also `copy` events for by-value trivially
    copyable arguments that clang copies once more.
12. **base-cast** (9).  A chain of
    base casts against one summed cast; an empty-class member at offset 1
    addressed by `get_member` (library) against a byte offset
    (`bitcast`/`ptr_stride`, clang); the null-preserving form of a base cast
    of an address-of operand (clang) against the `nonnull` form (the front
    end knows `&m` is not null; the run-time checks of `cxx-class-bases`
    cases 3 to 5 cover the null-preserving casts).
13. **alloc-attrs** (6).  Clang marks `operator new` / `operator delete`
    calls `builtin` and the `malloc` call in a replacement `operator new`
    `allocsize`; the library has no stream fact for either and emits plain
    calls.  Optimization hints only.
14. **array-new** (3).  `new[]` with a constant count: clang folds the size
    and `cir.array.ctor`/`dtor` take the array type; the library emits
    `mul.overflow` / `add.overflow` / `select` and the dynamic-count form of
    `cir.array.ctor`.  The size, the cookie and the loop are equal after the
    passes.
15. **lambda-names** (1).  Closure type mangling: `UliE_` / `UlvE_` (front end)
    against clang's `$_N` for internal-linkage lambdas; `_FUN` against
    `__invoke`; the conversion to function pointer is a call of
    `operator PFiiE()` in clang and the address of `_FUN` from the front end.
    Anonymous record numbering (`anon.1`, `anon.8`) differs for the same
    reason and is the only two `DIFF type` lines of the run.
16. **structor** (4).  Clang's `D1`/`C1` forwarders call `D2`/`C2`; the
    library emits one function (or an alias).  After the passes these are the
    same.
17. **signature-attrs** (68).  Clang's parameter attributes
    (`llvm.noundef`; on references and `this` also `llvm.nonnull`,
    `llvm.dereferenceable`, `llvm.align`), `no_inline` at -O0, and the
    `func_info<#cir.cxx_ctor/dtor/assign>` tag.  None is in the ECIL stream.
    The only consumer of `func_info` is the lowering of a call of a trivial
    copy constructor into `cir.copy`, and the front end never calls one.
    Optimization and undef-propagation hints only.
18. **label-names** (2).  `goto` labels: clang `out`, library `L3` (the stream
    carries label ids).
19. **string literals and globals** (3 `DIFF global @sr`, 0 other).  Private
    string literals are numbered by each side's own emission order (the
    comparison names them by content); the lifetime-extended `_ZGR` temporary
    of a global constructor region is read twice.
20. **unreferenced definitions** (112 functions and 5 globals only in the
    library output, 12 functions only in clang's).  The writer emits every
    inline function and constexpr variable the front end marks as needed,
    including unreferenced library inlines (`std::terminate`,
    `__is_constant_evaluated`, the `<cstdlib>` `abs`/`div` overloads, ...) and
    the implicit copy constructors that elision made unnecessary; clang emits
    on demand.  Only in clang: the implicit trivial `operator=` of 2 classes
    (category 11), `~A` for the NRVO flag cleanup (category 10) and 9 closure
    functions under their `$_N` names (category 15).  Dead code only.
21. **record types** (110 common, 2 differ, 15 only in the library output, 8
    only in clang's).  The two that differ are the anonymous closure records
    of category 15.  The one-sided ones are names: the front end sends a
    template specialization or a class in an anonymous namespace under its
    plain name plus the library's counter (`TS`, `TS.1`, `TS.2`, `Priv`) where
    clang spells `TS<char>`, `TS<int>`, `(anonymous namespace)::Priv`, `W<P>`;
    the unreferenced header records of category 20 (5 in `s6/d`); and `E1` in
    `cxx-class-bases`, an empty class that is only the type of an omitted
    member.
22. **global initialization order**.  Identical for all 54 sources (46 with no
    dynamic initializers; the others list `a, b, s, iv, sr, TV<int>::v` and
    the like in the same order on both sides).
23. **after the passes** (`--passes`).  The above plus two shape differences
    of the lowered form: the `__const.main.*` constant aggregates carry the
    record name of the template specialization (category 21), and an empty
    closure object (`generic`, `outer`, `plain` of `cxx-s5-lambdas`) that the
    library initializes with a zero constant gets a `__const.main.*` global
    where clang emits no store at all.  185 functions are the same, 234
    differ in the categories above; the call sequences differ in the same
    places as before the passes (categories 10, 11, 15, 16).
24. **no `nonnull` where clang has it**: none.  The one function with fewer
    null-checked casts than clang's (cxx-class-bases `main`) has more
    `nonnull` casts, not fewer (category 12).

## 5. Real differences

R1. **A class's data size the front end reports short of its layout.**
`struct B { int a; char c; }; struct D2 : B { B inner; };` is sent as
`TY_RECORD_BODY(D2, size 16, data_size 13)`.  Itanium (clang, g++) gives 16:
the data size of a class ends after every non-overlapping member in full, and
the front end's own layout of a class derived from `D2` agrees
(`struct E3 : D2 { char x; }` has `x` at 16 and size 20, and the stream says
so: E3 has data_size 17).  The 13 is what `data_size_of_type`
(`src/types.c`, which calls `compute_dsize` for a class that is not a C++03
POD) returns, most likely 8 + 5, the end of the last byte of `inner`'s last
member (for `struct M { B inner; }`, a C++03 POD, the stream says data_size
8 = size, as Itanium says).  Effect on this library: the nv-size packed rule saw
13 not a multiple of 4 and made `D2` `packed` (clang: not packed), so the
record alignment in the lowered code was 1.  Found by the record-type check
on `spike/cxx-shapes/s7/l.cpp`.  Not a library defect; the library now
defends against it: `a_emitter::consistent_data_size` raises the data size
to the end of the last non-overlapping member or base (`.base` records and
the nv-size packed rule use the raised value), covered by the new lit test
`cxx-dsize-consistent.test` (the stream above; `D2` unpacked, no `D2.base`,
`E3` packed as clang's).  Reported to the orchestrator as a front-end value
to fix at the source.

R2. **GNU mode extends no lifetime for a scalar member of a temporary
bound to a reference** (not in the library).
```
struct T { int id; T(int i); ~T(); };  struct P { T a; int k; };
const int &k = P{T(5), 6}.k;   printf("k=%d\n", k);
```
g++ 15 and cpfe-cir in its default mode print `T(5)`, `k=6`, `~T(5)`; with
`--g++ --gnu_version=150200` the stream binds `k` to a copy of the int and
destroys the temporary at the end of the declaration: `T(5)`, `~T(5)`,
`k=6`.  (Found because `cxx-s2-temporaries` `member_ext` differed in call
order between the plain-mode and the GNU-mode runs of the differential; with
`CMP_OPTIONS='--g++ --gnu_version=150200'` it is the only one of the 12 tests
that fails against g++ besides the deliberate `cxx-s3-statics` order.)  The
class-typed analogue (`const T &r = P{T(5), 6}.a;`) matches g++ in both modes.
The stream is faithful to what the front end's IL says; I did not locate the
cause (front end or writer).

R3. **Thread-safe statics were off in the 23:07 snapshot.**  The writer set
`ECIL_LANG_THREADSAFE_STATICS` at 23:13; at 23:07 the stream had flags 4, so
the library lowered function-local statics with a plain `i8` guard and no
`__cxa_guard_acquire`/`__cxa_guard_release`.  Already fixed in the writer:
after the 23:32 rebuild (flags 12) the lowered statics match clang's.  Noted
because it is exactly what the post-pass comparison is for.

Library-side fix of a shape (not semantic): **a constant-initialized static
local with an exit cleanup** (`static Z lz = {3};`, `Z` with a destructor) was
emitted as `cir.local_init static_local @x ctor { yield } dtor { ... }`; clang
emits the dtor-only form `cir.local_init static_local @x dtor { ... }`.  Now
the library emits no ctor region when the variable already has its constant
initializer (`function.cpp`, `static_init_record`); `cxx-static-init.test`
checks `... @_ZZ2h4vE2lz dtor {`.

## 6. Library changes made during the differential

- `lib/cirgen/function.cpp`: dtor-only `cir.local_init` (above).
- `lib/cirgen/types.cpp`, `emitter.h`: `consistent_data_size` (R1), applied
  in `complete_record_layout` for non-union C++ records.
- `test/cirgen/cxx-dsize-consistent.test` (new), `cxx-static-init.test`
  (tightened check).
- Tools: `scripts/cir-compare.py` (call-sequence check, record-type check,
  null-checked-cast check, string-literal naming, `--passes`,
  `--emit=cir-lowered`), `scripts/box_diff.sh` (mode selection by header use,
  `--passes`), `scripts/cir-diff-classify.py` (new).
- `spike/cxx-shapes/s1lib/nua.cpp`: `//options:--c++20` (the front end
  ignores `[[no_unique_address]]` before C++20, clang does not; without the
  option the two sides lay `X` out differently).
- Verification: lit 73/73; the 12 real-front-end tests against g++ (plain
  mode: 11 same + the deliberate statics order; also in GNU mode: R2 only).

## 6a. Observations for the writer (not library defects)

- Every unreferenced inline function and constexpr variable of the headers is
  emitted (112 functions, 5 globals across the 54 sources; category 20).
  Harmless, but it makes C++ streams several times larger than clang's output
  for `<cstdio>`-style sources, so it costs front-end and library time.
- R1 (data size) and R2 (GNU-mode lifetime) above.
- The front end constant-folds constexpr lambda calls and in-class constants
  (category 7); fine for correctness and visible only as fewer calls than
  clang.
- Possible stream facts that would close the benign attribute gaps, none
  needed for correctness: reference-ness / non-null / dereferenceable size of
  pointer parameters (`signature-attrs`), "replaceable global allocation
  function" on a call (`alloc-attrs`), ctor/dtor/assign kind and triviality of
  a function (`func_info`).

## 7. Per-source tables

Columns: S = functions identical, D = functions that differ, E/C = functions
only in the library output / only in clang's output, tags = the benign
categories of section 4 of the DIFF functions, call = functions whose call
sequence differs (all explained: 10 nrvo, 11 struct-copy, 15 lambda, 16
structor, 11 `copy` argument).  `real` is empty everywhere: no row has a
semantic difference.  Final plain run (`--emit=cir` against
`-clangir-disable-passes`).

### tests/tests/cir/cirgen

| source | S | D | E | C | call | tags | notes |
|---|---|---|---|---|---|---|---|
| cxx-class-bases | 7 | 4 | 0 | 1 | 1 | order, array-index, base-cast, fe-folded, retval, signature-attrs, struct-copy | `B1::operator=` against memcpy; base-cast chains; fewer null checks (nonnull) |
| cxx-class-return | 7 | 3 | 0 | 0 | 0 | order, array-index, fe-folded, retval, signature-attrs | |
| cxx-linkage | 21 | 1 | 0 | 0 | 0 | array-index, fe-folded, retval, signature-attrs | only `main`; linkage, comdat, visibility identical |
| cxx-member-calls | 9 | 1 | 0 | 0 | 0 | array-index, exprcleanup, fe-folded, retval, scope, signature-attrs, temp | |
| cxx-s2-comma | 2 | 7 | 0 | 0 | 0 | order, array-index, exprcleanup, fe-folded, retval, scope, signature-attrs | |
| cxx-s2-ctor-dtor-order | 2 | 23 | 1 | 0 | 0 | order, array-index, exprcleanup, fe-folded, label-names, retval, scope, signature-attrs | unused copy constructor only in the library output |
| cxx-s2-nrvo | 3 | 8 | 0 | 0 | 1 | order, address, array-index, cond-shape, fe-folded, nrvo-flag, retval, scope, signature-attrs | `named()`: nrvo flag cleanup |
| cxx-s2-temporaries | 6 | 10 | 0 | 0 | 0 | order, array-index, exprcleanup, fe-folded, retval, scope, signature-attrs | `member_ext` identical in plain mode (R2 for GNU mode) |
| cxx-s3-statics | 3 | 9 | 0 | 0 | 0 | order, address, array-index, fe-folded, retval, scope, signature-attrs | dtor-only `local_init` now identical; global `@sr` address twice |
| cxx-s4-new-delete | 5 | 12 | 0 | 0 | 1 | order, alloc-attrs, array-index, array-new, base-cast, fe-folded, retval, scope, signature-attrs, structor | `_ZN1CDxEv` forwarder; static array sizes |
| cxx-s5-lambdas | 2 | 3 | 22 | 9 | 1 | address, array-index, cond-shape, fe-folded, lambda-names, retval, signature-attrs, temp | `main` calls: folded constexpr lambda calls, `PFiiE` conversion; 2 `DIFF type` anon numbering |
| cxx-static-members | 4 | 2 | 0 | 0 | 0 | address, array-index, fe-folded, retval, signature-attrs | `S::sc` `.const` global loads vs constants |

### spike/cxx-shapes (S1 elision/sret, S2 return sinking, S3 conditional cleanups, S4 C1/C2 x CallConvLowering, S5 statics and init order, S6 new/delete, S7 layouts)

| source | S | D | E | C | call | tags |
|---|---|---|---|---|---|---|
| s1 (`std::printf` elision shapes) | 3 | 5 | 4 | 0 | 1 | array-index, cond-shape, fe-folded, nrvo-flag, retval, scope, signature-attrs, address |
| s1b | 2 | 9 | 4 | 0 | 0 | array-index, fe-folded, retval, scope, signature-attrs, structor, temp |
| s1c/f | 3 | 1 | 0 | 0 | 0 | order |
| s1c/f3 | 2 | 3 | 0 | 0 | 0 | order |
| s1c/lib, lib2, lib3 | 2, 2, 1 | 5, 4, 4 | 3 each | 0 | 0 | array-index, signature-attrs |
| s1c/main | 1 | 1 | 0 | 0 | 0 | fe-folded, retval |
| s1c/mk | 0 | 2 | 0 | 1 | 1 | nrvo-flag, cond-shape, retval, scope, address (`~A` only in clang: NRVO cleanup) |
| s1c/tk | 1 | 1 | 3 | 0 | 0 | array-index, signature-attrs |
| s1lib/cls, cls2 | 0, 0 | 1, 2 | 1, 0 | 0 | 0 | order |
| s1lib/elision | 3 | 3 | 0 | 0 | 0 | order |
| s1lib/nua (`--c++20`) | 3 | 1 | 0 | 1 | 1 | struct-copy (`B::operator=` against memcpy of the 5 data bytes); `X` / `Y` / `D` layouts and member shapes identical |
| s1lib/static_init | 3 | 9 | 0 | 0 | 0 | order, address, array-index, fe-folded, retval, scope |
| s2/lib | 1 | 2 | 3 | 0 | 0 | array-index, signature-attrs |
| s2/main, main4 | 4, 4 | 1, 1 | 3 each | 0 | 0 | array-index, fe-folded, retval, signature-attrs |
| s2/r, r4 | 0, 0 | 5, 5 | 0 | 0 | 0 | order, retval, scope, label-names (r4) |
| s3/c, c5, c6 | 1 each | 6, 3, 3 | 0 | 0 | 0 | order, exprcleanup, retval, scope |
| s3/lib | 2 | 2 | 3 | 0 | 0 | array-index, signature-attrs |
| s3/main, main5, main6 | 5, 2, 2 | 1 each | 3 each | 0 | 0 | array-index, fe-folded, retval, scope (main6), signature-attrs |
| s3/sum | 1 | 0 | 0 | 0 | 0 | identical |
| s4/a | 1 | 2 | 3 | 0 | 0 | array-index, signature-attrs |
| s4/m | 1 | 3 | 3 | 0 | 1 | order, address, array-index, fe-folded, retval, temp (`copy` of the by-value argument) |
| s4/w | 0 | 5 | 0 | 0 | 3 | order, address, structor, temp (C1/D1 forwarder calls) |
| s5/g, g2, g3 | 2, 0, 1 | 3, 3, 1 | 0 | 0 | 0 | order, address (g2: `@sr` address read twice) |
| s5/lib | 1 | 3 | 3 | 0 | 0 | array-index, signature-attrs |
| s5/main, main2, main3 | 3, 2, 2 | 1 each | 3 each | 0 | 0 | array-index, fe-folded, retval, signature-attrs |
| s6/d (new/delete/new[]) | 6 | 10 | 29 | 0 | 0 | order, alloc-attrs, array-index, array-new, base-cast, fe-folded, retval, scope, signature-attrs, temp; 29 unreferenced `<cstdlib>`/`<new>` inlines and 2 `hardware_*_interference_size` globals only in the library output |
| s7/l | 0 | 0 | 0 | 0 | 0 | identical (all 6 globals, all 7 record types; `D2` after R1) |
| s7/l2, l3 | 0, 0 | 10, 9 | 0 | 0 | 0 | order (headers), base-cast (empty member `e` of `F`/`N` by `get_member` against a byte offset), array-index, fe-folded |

The one-sided counts E for every source that includes `<cstdio>` are the
three `std::terminate` / `__terminate` / `__is_constant_evaluated` inlines
(category 20).

## 8. After the CIR passes

`box_diff.sh --passes` (`edg-cir --emit=cir-lowered`; clang with its passes):
185 functions identical, 234 differ.  The differences are the categories
above plus two lowered-form items: `__const.main.*` constant aggregates carry
the record name of the template specialization (names), and the forwarders
C1 -> C2 are inlined on one side.  Guards, `__cxa_atexit` registration,
`__cxx_global_var_init.N` and `_GLOBAL__sub_I_*` match.  The same call-sequence
differences exist as before the passes and no new one.
