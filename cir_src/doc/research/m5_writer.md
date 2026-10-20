# M5a writer research: W3 (writer cost), W4 (the prototype-instantiation region keep) and W5 (FE facts for S2)

Author: cir-writer, session 4 (2026-10-19). Research only: nothing here ships.
Fixes are held for slice S4. All numbers are instruction counts (Ir) from
valgrind, which are deterministic; wall times and RSS were taken solo on the
box (the box was otherwise loaded by other jobs, so times are indicative).

Tree: `~/edg-cir-writer` on the box at HEAD `0ce8b90333` plus the M4.1 writer
items (W1, W2). Front end: `build/gcc-release-cir/bin/cpfe-cir`
(RelWithDebInfo, library linked, stream to `/dev/null`) unless stated.

## 1. W3 writer cost

### 1.1 Method

- `valgrind --tool=callgrind` on the release front end. The new
  `cir_src/test/writer/tools/cg_writer.py` parses the callgrind files and
  reports, per run:
  - **writer self**: Ir on lines of `ecil_*.c`, `ecil_*.h`, `cir_be.c`;
  - **writer inclusive**: the cost of every call that enters a writer
    function from a non-writer line (the front end's hooks:
    `ecil_hand_off_region`, `ecil_stream_*`, ...). It includes everything the
    writer calls: libc (`malloc`, `memcpy`), the front end's mangler and
    other helpers;
  - the writer's own functions by self and inclusive Ir, the functions outside
    the writer that it calls (with the writer caller), and call counts.
- Inputs: the three generated TUs of the M3/M4 perf work
  (`edg-cir-rss --generate-functions 6000`, `--generate-cxx-functions 6000`,
  `--generate-cxx-templates 300`) and a corpus sample (every 4th C test, every
  3rd C++ test of `cir-c-corpus.txt` / `cir-cpp-corpus.txt`, `rp,lp`; each
  case through `eccp`, so the options are the ones of the tests).
- Commands (box, in `cir-env`, repository at `/edg/workspace`):
  `valgrind --tool=callgrind -q --callgrind-out-file=cg.out
  build/gcc-release-cir/bin/cpfe-cir --c11|--c++17 --ecil_output=/dev/null
  FILE` then `python3 -I cir_src/test/writer/tools/cg_writer.py cg.out`; for
  the corpus `stream_sweep.py ... --sample N --fe <wrapper that execs
  valgrind>` and `cg_writer.py --delete cg.*`.

### 1.2 Share of the front end

| Input | Total Ir | Writer self | Writer inclusive |
| --- | ---: | ---: | ---: |
| C, 6000 functions | 1,690.9 M | 285.2 M (16.9%) | 315.2 M (**18.6%**) |
| C++, 6000 functions | 564.6 M | 76.8 M (13.6%) | 134.4 M (**23.8%**) |
| C++ templates and lambdas, 300 units (12,001 regions) | 3,644.5 M | 291.2 M (8.0%) | 563.7 M (**15.5%**) |
| C corpus sample, 641 runs | 10,111 M | 128.9 M (1.3%) | 286.2 M (2.8%) |
| C++ corpus sample, 665 runs | 16,210 M | 242.8 M (1.5%) | 755.8 M (4.7%) |

The corpus rows are dominated by start-up and built-in table set-up (about
15.8 M Ir per C run and 24.4 M per C++ run), so they say that the writer is
not a corpus-level cost; the big TUs say what it costs when the input is big.
On the big C++ TUs the writer is **a quarter to a sixth of the front end**, and
about half of that is not the writer's own lines but the front-end helpers it
calls (mangling, TU-local analysis).

### 1.3 Where it goes

**C, 6000 functions** (writer inclusive 315 M):
- Expression and statement translation: `emit_function` 260 M inclusive
  (15.4% of the FE), of which `run_code_stack`/`do_expr` 227 M (13.4%).
- Record encoding primitives, self Ir: `ecil_rec_begin` 33.5 M (858 k calls),
  `ecil_put_uint` 24.3 M (1.03 M calls), `ecil_arg_uint` 13.1 M,
  `ecil_buf_reserve` 13.7 M (1.96 M calls), `ecil_rec1`/`rec0`/`rec_end`
  10.4 M: **95 M, 5.6% of the FE**, about 110 Ir per record.
- Globals: `ecil_emit_globals` 48.5 M (2.9%); variable definitions
  `ecil_define_variable` 44.6 M (2.6%); constants `ecil_emit_constant_sources`
  38.4 M (2.3%).
- Types: `ecil_type_id` 27.2 M (816 k calls, 33 Ir each).
- Interning: `ecil_intern_string` 10.7 M (60 k calls, 178 Ir each, 0.6%).

**C++, 6000 functions** (writer inclusive 134 M; 5,401 function regions):

| Item | Ir inclusive | % of FE | Calls |
| --- | ---: | ---: | ---: |
| `ecil_function_id` (G-id and DECL_FUNC facts) | 86.8 M | 15.4 | 18,908 |
| of it `compute_func_state` | 52.4 M | 9.3 | 23,413 |
| of it `routine_symbol_text` (mangling) | 23.3 M | 4.1 | 13,512 |
| `get_mangled_function_name_full` | 23.0 M | 4.1 | 13,512 |
| `cir_entity_is_tu_local` | 17.3 M | 3.1 | 23,717 (731 Ir each) |
| `cir_structor_name` | 5.6 M | 1.0 | 3,000 |
| `emit_function` (bodies) | 104.4 M | 18.5 | 5,401 |
| `ecil_reissue_function` (second pass over the file scope) | 22.6 M | 4.0 | 12,007 |
| `ecil_resolve_late_names` | 12.7 M | 2.3 | 1 |
| `ecil_submit_frame` | 1.5 M | 0.27 | 5,105 |

**C++ templates and lambdas** (writer inclusive 564 M, 15,901 `emit_function`
calls): `ecil_function_id` 399 M (11.0%), `compute_func_state` 242 M (6.7%,
50,105 calls), mangling `routine_symbol_text` 124 M (3.4%, 28,804 calls, 4.3 k
Ir per call), `cir_entity_is_tu_local` 82.5 M (2.3%, 47,107 calls, 1.75 k Ir per
call), `ecil_reissue_function` 74.9 M (2.1%), `ecil_resolve_late_names`
47.9 M (1.3%).

### 1.4 Findings, ranked by size

1. **Per-function C++ declaration facts are recomputed many times.**
   `compute_func_state` runs 4.3 times per function (23.4 k calls for 5.4 k
   functions; 50.1 k for 15.9 k on the template TU), and the mangled name is
   built 2.5 times per function (13.5 k calls for 5.4 k functions). The
   repeats are structural: the first `ecil_function_id` (G-id), the re-check at
   the definition (`emit_func_decl_if_changed`), and the second pass of
   `ecil_reissue_function`/`ecil_resolve_late_names`, which compute the facts
   "as they are now" with `gid == 0` while names are not final. Together with
   the TU-local analysis this is **9–11% of the front end's instructions on C++
   TUs**. Estimated reachable saving by caching what cannot change after the
   first call (type, linkage inputs, the sent name): about half, **4–5% of the
   FE**. The proposal for S4 is in §1.6.
2. **`cir_entity_is_tu_local` has no memo before the names are final**
   (`lower_name.c:16635`, `tu_memo_enabled` is set by
   `cir_tu_local_names_are_final`). Until then every call re-walks the entity
   (731 Ir on average, 1.75 k on the template TU): 3.1% and 2.3% of the FE. The
   answer can change only for a lambda whose parent is not recorded yet; an
   entity that is neither a lambda nor reaches one can be memoized at once. The
   estimated saving is about 2% of the FE on C++ TUs.
3. **Record encoding primitives are not inlined and have no fast path**: 5.6%
   of the FE on the C TU (110 Ir per record; `ecil_buf_reserve` is called
   twice per record). A static-inline fast path (a 1-byte value, a capacity
   check that is almost always true) would save perhaps a third to a half of
   it: 2–3% of the FE on C, about 1.5% on C++.
4. **The second pass** (`ecil_reissue_function` 4.0% and
   `ecil_resolve_late_names` 2.3% of the C++ 6000 TU) exists because C++ names
   are not final until the file scope is complete (spec 7). It overlaps with
   finding 1: the repeated name computation is mostly here.
5. **The per-submit buffer is not a hot spot.** `ecil_submit_frame` costs 1.5 M
   Ir on the C++ TU (0.27% of the FE) and 2.2 M on the C TU (0.13%); malloc,
   memcpy and free of the frame buffers together are below 0.1% of the FE
   (5,105 and 6,004 frames). `ecil_sink_acquire`/`ecil_sink_submit` do not
   appear among the top 40 inclusive functions. Pooling the buffers
   (`ecil_write.c:2101-2121`, `ecil_sink.c:163-171`) would save less than
   0.1%, in line with the user's "pool only if a profile shows allocation cost".
   The one in-process cost that would matter is on the library side (the
   decode copy), not here.
6. **Allocation in general is small**: `malloc` 4.8 M (C) / 2.1 M (C++), `free`
   3.3 M / 1.7 M, `memcpy` 2.3 M / 1.7 M, `realloc` and `calloc` below 1 M each
   (all under 0.5% of the FE). `ecil_new_gid` moves the G-id table by
   `memcpy`/`realloc`: 1.05 M per TU; in the C++ corpus sample 51 M in total
   (0.3%), the table growth of many small runs.
7. **Interning** (`ecil_intern_string`, `ecil_word_*`): 0.6–0.9% of the FE;
   178–260 Ir per call. Not worth a change.
8. **Constants**: `value_of_integer_constant` called from
   `ecil_arg_integer_constant` costs 10.8 M (0.64%) on the C TU: it is the FE's
   evaluation of constants the writer asks for again; one candidate for a
   cache, small.

Pending records (`ecil_complete_pending_records` and the incomplete-record
looks) do not show among the top functions on these inputs.

### 1.5 `ECIL_STATS`: design of the frame and byte counters (not added)

`ECIL_STATS=1` today prints, in CHECKING builds only, the region residency at
`ecil_stream_finish` (`ecil_write.c:2418`) and the work-stack high-water mark.
The design below makes it a release feature at no cost when off:

- **Where.** A single `an_ecil_stats` struct inside `an_ecil_stream`
  (`ecil_write.h`), not a global, so it moves with the stream if a second
  stream is ever open (concurrency "room"). Whether stats are wanted is read
  once at `ecil_stream_open` into a flag (one `getenv`); the counters are
  incremented unconditionally (a handful of adds per frame and per record, below
  noise) and printed only when the flag is set. They never contain addresses.
- **Per frame kind** (HEADER, REGION, GLOBALS, NAMES, END, ABORT): number of
  frames, total bytes including the 8-byte header, decl-section bytes,
  body-section bytes, and the largest frame. Incremented in
  `ecil_submit_frame`.
- **Per record**: count and encoded bytes by opcode (two arrays of 256; the
  bytes are the buffer position delta in `ecil_rec_end`), reported as the top 12
  by bytes. This is the data the lazy-emission design needs (how much of a
  frame is declaration, how much is body).
- **Entities**: G-ids by kind, strings interned (count, bytes), types
  introduced, late-name entries resolved, `ecil_reissue_function` calls,
  `compute_func_state` and `routine_symbol_text` calls, `cir_entity_is_tu_local`
  calls and memo hits.
- **Memory**: the sum of the frame-buffer sizes handed to the sink and the peak
  of `decl.capacity + body.capacity`; `ecil_sink_acquire` is the only malloc of
  frame size, so the sum is one add. The CHECKING-only region residency line
  stays.
- **Output**: one line per item on stderr, `ecil-stats: <key> <value>`, sorted,
  so that `edg-cir-perf`/`edg-cir-rss` rows can parse it and the lines are
  diffable in a golden if wanted. In-process and file sinks print the same.
- **Test**: a writer golden cannot hold stderr; a ctest that runs the FE with
  `ECIL_STATS=1` on a fixed TU and checks the keys and that the frame bytes sum
  to the stream size.

### 1.6 Proposals for S4 (writer performance), in order

1. Memoize the TU-local answer for entities that cannot change before the names
   are final (finding 2). Needs the lambda-parent rule stated in
   `lower_name.c` to be turned into a predicate; the ASan poison runs and the
   stream-identity sweep (`stream_sweep.py`, as used for W1) prove it.
   Expected: about 2% of the C++ FE.
2. Cache the per-routine declaration facts that are final after the first
   computation (the name, linkage inputs, the function type id) in
   `an_ecil_gid_info`, so the definition re-check and the reissue pass reuse
   them (finding 1). Expected: 2–3% of the C++ FE. The stream bytes must not
   change (identity sweep).
3. Inline the encoding fast paths (finding 3). Expected: 2–3% on C, about 1.5%
   on C++.
4. Pooling of frame buffers: **not recommended** (finding 5).
5. Add the `ECIL_STATS` counters of §1.5 first, so the above is measured with
   the writer's own numbers.

All of them keep the stream byte-identical, so they can be proved with the
stream sweep (`stream_sweep.py`, hash per case) over both corpora, as W1 was.
Together they would remove roughly 6–9% of the front end's instructions on
C++ TUs and 3–4% on C ones. None of them changes the library's cost, which is
the bigger share (on the 6000-function C++ TU the library takes 11.8 G Ir against 0.56 G for the front end, M3 §11.1).

## 2. W4 the prototype-instantiation region keep

Numbering: handoff_3 section 6 calls this "region keep #2" (the second of the
two keeps that M3.1 W14 re-assessed, the other being the lambda keep). Below,
a keep is named by its **reason**, the numbers of the table in 2.2, which are
the order of the tests in `memory_region_should_be_kept_for_routine`: reason 1
an inline function kept for inlining, reason 2 a local class type, reason 5 a
prototype instantiation (the keep of this section).

### 2.1 Root cause

The keep (`mem_manage.c:1461-1473`, `rout->is_prototype_instantiation`) is
**not needed by `gnats-26823`** at this tree, and the comment's attribution is
out of date. Evidence, box tree, debug FE (ASan, CHECKING) with the keep
disabled (`FALSE && rout->is_prototype_instantiation`, box only, reverted):

- `edg/cmeerw/gnats-26823.sft.cpp` with `--c++26`, `--c++26 --gn 160100`,
  `--c++26 --clang_version 220100` (`-w -tused`): rc 0, no assertion and no
  ASan report. The `-compat`, `-diag`, `-disp` variants and
  `changes/EDGcpfe26823` have the same status as with the keep (the `-disp` one
  is a CATASTROPHE with and without the keep: `--il_display`).
- The whole C++ corpus (`cir-cpp-corpus`, 1958 cases): 0 differences in stream
  hash and exit status against the build with the keep.
- `edg/cmeerw` and `changes` (1888 runs, `edg_x86_64_cir`, all test types)
  against the same run with the keep: **11 more failures, all ABORT, in
  C++20 tests that use requires-expressions**: `changes/EDGcpfe26103`,
  `edg/cmeerw/gnats-24907-noexcept`, `gnats-26298` (2 cases, PASS before),
  `gnats-27505` (3), `gnats-27845` (3), `gnats-28909`.

The failures are:
- `heap-use-after-free` in `scan_requires_expr` at `expr.c:43675`, called while
  a function body is instantiated (`scan_function_body` <-
  `instantiate_template_function_full`, `scan_expr_full`), and one SEGV in
  `copy_param_type_list_with_substitution` (`templates.c:17428`) reached from
  `requires_expr_satisfied_full`.

The owner of the data is `requires_ranges`, the translation-unit-wide map
(`exprutil.c:27955`, `new_fe<a_requires_range_map>`): when a requires-expression
is first scanned in a template context, `expr.c:43634-43641` stores
`rrd.requires_expr = node` (an `enk_requires` tree allocated in the *current
function's IL region*, together with its parameter list) under the token
sequence number of the `requires` token. A later instantiation does not rescan
the tokens (`expr.c:43650-43700`): it reads `rrd.requires_expr` and
`copy_expr_tree`s it. When the first scan happened while a prototype
instantiation was being parsed, that node lives in the prototype's region, so
the region must outlive the compilation. This is the "later instantiations read
what the file scope holds about it" of the comment; the "address constant"
symptom of M3.1 W14 does not reproduce any more (not investigated further which
later change removed it).

### 2.2 What the kept regions cost

Instrumented front end (box only; per region counted once: payload bytes from
`next_avail_in_block - start_of_block` of the region's blocks; reasons are those
of `memory_region_should_be_kept_for_routine`, first match wins):

| TU | regions | kept for reason 5 (prototype instantiation) | reason 5 with no other reason | reason 2 (local class types) | reason 1 (inline body) |
| --- | ---: | --- | --- | --- | --- |
| C, 6000 functions | 6,001 | 0 | 0 | 0 | 0 |
| C++, 6000 functions | 5,401 | 300 regions, 0.43 MB | 300 regions, 0.43 MB | 300 regions, 2.1 MB | 0 |
| templates and lambdas, 300 units | 12,001 | 2,100 regions, 5.8 MB | 1,800 regions, **4.9 MB** | 2,400 regions, **71.1 MB** | 300 regions, 2.4 MB |

Release front end, peak RSS (`/usr/bin/time`, stream to `/dev/null`):

| TU | default | `--ecil_keep_regions` (everything kept) |
| --- | ---: | ---: |
| C, 6000 functions | 70 MB | 136 MB |
| C++, 6000 functions | 63 MB | 75 MB |
| templates and lambdas, 300 units | 362 MB | 387 MB |

So **the prototype-instantiation keep (reason 5) holds 0.4 MB on the C++ TU and at most 5.8 MB (4.9 MB exclusive)
on the template TU: 1.4% of the front end's peak RSS, and under 1% of the
in-process peak (719 MB)**. Removing it would not move the memory numbers, and
it is unsafe for C++20 requires-expressions.

The **local-class keep (reason 2, "contains_local_class_type")** is the larger one: 71
MB of payload on the lambda-heavy TU, about 20% of the front end's peak RSS
there (lambda closure classes count as local classes, `mem_manage.c:1432-1437`;
about 30 KB per kept region). It is needed because "local class types may have
generated special members that need to be defined because of later
instantiations". A narrower condition (a region is kept only while a member of
a local class is still undefined or a template instantiation can still name
it) might release most of it; that needs an analysis of when the last use can
happen and is a memory item for S3/S4, not W4.

### 2.3 Proposal for S4

- **Do not remove the prototype-instantiation keep (reason 5).** Correct the comment at `mem_manage.c:1461-1473`:
  the reason is requires-expressions held by `requires_ranges`, not
  `gnats-26823`. Gain: none in memory, but the next reader is not misled.
- If it must be narrowed anyway: keep the region of a prototype instantiation
  only when a requires-expression was recorded in `requires_ranges` while it was
  being scanned (set `scope->do_not_free_memory_region`, which
  `check_for_done_with_memory_region` already honours, at
  `expr.c:43641`, CIR-gated). It would release 4.9 MB of 5.8 MB on the template
  TU. The proof is the 11 ABORT cases above turning into passes, plus the
  stream sweep (0 differences) and the ASan poison runs. Expected value: very
  small; do it only with the local-class work (one change to the keep policy,
  one measurement).
- Add the `ECIL_STATS` line "kept regions by reason, count and bytes" (the
  instrumentation of this study, as a CHECKING-only counter) so that the
  regions held are measured where they are decided.

## 3. W5 FE facts for slice S2

Design research for the facts that the optimization work (S2) needs from the
front end: the IL source, the stream form, the writer change, the re-issue
comparison and the risk of each. Nothing here is implemented in the writer
yet. Probes ran in the box scratch tree (`~/edg-cir-writer/.tmp/w4/w5*`;
box-only builds of the writer, reverted; the probe files are not in the
repository). The 1.3 contract additions that follow from it are `esk_used`
(11), `esk_nonnull_ptrs` (15) on `TY_FUNC` and the `CALL` flag
`ECIL_CALL_ALLOC_EXPR` (0x40), in `ecil_format.h`.

### 3.0 Method and the reference behaviour

The reference for "parity" is `clang -fclangir` at the pin (24.0.0git
`2d6f180e`, the box image's clang), with classic clang next to it for what
CIRGen does not do yet. One probe file (`w5src/f.cpp`: references, `this`,
`[[likely]]`, loops, `used`, `new`/`delete`, a captureless lambda, `abort`,
a gnu_inline function) was compiled with both at `-O2 -disable-llvm-passes`
and the result compared with our stream and our `edg-cir -O2` IR. The CIRGen
source of the pin (`~/edg-cir-config/llvm-project`, read only) settles what
upstream does and does not do.

What `clang -fclangir` emits and ours does not, in the probe:

| Fact | clangir | classic clang | ours today |
| --- | --- | --- | --- |
| `noundef nonnull align A dereferenceable(N)` on a reference or `this` parameter, a reference result, the call-site arguments and results | yes | yes | none |
| function `mustprogress` (C++11 and later, every function, unless it has a trivial infinite loop) | yes | yes | only where LLVM infers it |
| `builtin allocsize(0)` on the allocation call of a new-expression, `builtin nounwind` on the delete call | yes | yes | `nobuiltin` on the declaration only |
| `llvm.compiler.used` for `used` | yes | yes | none |
| `available_externally` body of a gnu_inline function | yes | yes | library item (L4) |
| call-site `noreturn nounwind` on a call of a noreturn function | yes | yes | not on the call site |
| the invoker's dummy object (`unused.capture`, align 1) | yes | yes | `null` |
| `[[likely]]`/`[[unlikely]]`: `llvm.expect`/`!prof` | **no** (`MissingFeatures::emitCondLikelihoodViaExpectIntrinsic`, `createProfileWeightsForLoop`) | yes (`llvm.expect.i1`; `!prof` on switch) | none |
| `!llvm.loop` metadata (`mustprogress`, and so on) | **no** (`MissingFeatures::loopInfoStack`) | yes | none |

So: S2 needs 3.1 and 3.4 to 3.6 for parity. 3.2 and the loop metadata are
beyond what the pin does; 3.3 is a function-level fact only; 3.7 and 3.8 are
not writer work.

Order proposed for S2 (writer side), cheapest first: 3.6 (no format change),
3.5 (one flag), 3.4 (one key, same pattern as `noinline`), 3.1 (one key on
`TY_FUNC`), then 3.2 as an option. 3.3, 3.7 and 3.8 need no writer code.

### 3.1 Reference-ness (`nonnull`, `dereferenceable`, `align`)

**What the front end has.** `p->type` of a parameter, and
`return_type` of a routine type, are `tk_pointer` types, and
`is_reference_type()` tells a reference (lvalue or rvalue) from a pointer.
The implicit object parameter is not in `param_type_list`; the writer adds it
(`finish_function_type`: `has_this` from
`routine_type_is_nonstatic_member_function`, its type from
`f_implicit_this_param_type_of`). The ECIL type loses the difference
(`ecil_type_id`, `case tk_pointer`: "a reference is a pointer to its
referent"), and `TY_PTR` is interned by pointee.

**What clang puts on it.** `noundef nonnull align(alignof pointee)
dereferenceable(sizeof pointee)` on a reference parameter, on `this` of every
non-static member function (constructors and destructors included), and on a
reference result; the same attributes on the call-site arguments and results,
also at indirect and virtual calls (clangir `call noundef i32 %8(i32 noundef 1)`
for a function pointer). A reference to an incomplete type or to a function
has no `dereferenceable` (and a function has no `align`).

**Options.**

| | `TY_PTR` flag (1.3, `REFERENCE`) | supp key on `DECL_FUNC` | **supp key on `TY_FUNC`** |
| --- | --- | --- | --- |
| covers params, results, `this` of declarations and definitions | yes | yes | yes |
| covers indirect and virtual calls (the type is what the call has) | yes | no (derive from the callee `g` for a virtual call, none for a function pointer) | yes: every `CALL g t ...` and `CALL_INDIRECT t ...` names the function type |
| covers references that are not at a function boundary (fields, locals) | yes | no | no (clangir has nothing there either; classic puts `!nonnull` on loads of references, a `MissingFeatures` item) |
| type identity | splits `T*` from `T&`: every writer site that makes a pointer (`ecil_ptr_type`, `ADDR`, `STORE` into a reference local, `CAST`) must keep the two in step, and the library's "value type equals slot type" checks must learn that they are compatible | none | splits only function types that differ in the mask: harmless, since C++ does not let `void(*)(T&)` and `void(*)(T*)` meet without a `reinterpret_cast` |
| old readers | reject the flag (spec section 9) | skip the key | skip the key |
| bytes | none | about 4 per function | about 4 per distinct function type (stl.cpp, 262 KB: 761 function types, at most 3 KB, 1.2%) |
| re-issue | n/a | new fact joins the DECL_FUNC compare | part of `decl_type`: a different type is already a re-issue |

**Recommendation: a supp key on `TY_FUNC`**, `esk_nonnull_ptrs = 15`
, a uint mask: bit 0 = the result is a reference; bit
`i + 1` = parameter `i` of the `TY_FUNC` list is a reference or is the
implicit object parameter (the list already holds `this` first for a
non-static member function). Meaning: "this pointer is never null and
points to an object of its pointee type". The library derives `align` and
`dereferenceable` from the pointee type (record layout, type size) and
decides where clang omits them (incomplete or function pointee, the
`nvsize` of a base-variant constructor's `this`). The mask is 64 bits;
parameters beyond the 63rd are conservatively not marked. A
`TY_PTR` flag is the wrong tool: the cost of a type that splits is large and
reaches every pointer-making site, to buy facts nobody uses (fields, locals).

**Writer.** `finish_function_type` (`ecil_type.c:1054`) builds the mask from
`is_reference_type(p->type)` (after `skip_typerefs`), the return type, and
`has_this`; the mask word joins the interning key (`key[]`), and the key is
written through the multi-key builder after `TY_FUNC` (`ecil_arg_array` then
`ecil_rec_supp_end`). `promote_params` (K&R) and `no_proto` have no
references. `ecil_func_type_id` is the only entry.

**Risks.**
- `this` becomes non-null: the static invoker's null `this` is UB then (3.6,
  must land first or together).
- A function type that is the same except for the mask: `ecil_gid_info(...)
  ->decl_type` of a function that is re-issued with a changed type after its
  body ("the type of a function cannot change after its body", spec 8.2). The
  mask is a function of the declared type only, so it cannot change.
- Template instances and `decltype(auto)` results: use the instantiated
  routine's type.
- `T&&` and `T&` are marked alike (clang does the same).
- Member pointers (`TY_METHOD`) use the function type of the member: the
  mask on it is accurate (the first parameter is `this`).
- A virtual call with a covariant reference result: `REF_RESULT` already
  exists in `CLASS_VFUNCS`.
- `noundef` (not a reference fact, but L4 will ask): the GNU `return;` in a
  non-void C++ function returns an uninitialized value (spec 8.3). A
  per-function fact "has a `return;` without a value in a non-void function"
  is easy to compute (the writer emits that return path with `__retval`);
  without it `noundef` on results is a miscompile in GNU C++ mode. Suggested
  carrier: a `DECL_FUNC` flag bit in 1.3 or a key; to decide with L4.

**Tests.** A golden with a reference parameter, an rvalue reference, a
reference result, a member function, a constructor, a function pointer and
a virtual call (the mask shows in the dump); a golden where two overloads
`f(int*)` and `f(int&)` get two function types.

### 3.2 Branch hints (`[[likely]]`, `[[unlikely]]`)

**IL.** `a_statement.is_likely`/`is_unlikely` (`il_def.h:16577`) on the
statement that carries the attribute: for `if (c) [[likely]] A else
[[unlikely]] B` it is on `A` and `B` (the arm statements, not on the `if`),
for a loop on the body statement, for `[[likely]] case 1:` on the
`stmk_switch_case` statement; `a_label.is_likely` for a goto label. "The front
end does not take any action based on this value". Checked with `--il_display`
(`w5/lk.il`).

**`__builtin_expect`.** Already covered end to end: `BUILTIN expect` in the
stream, `llvm.expect` at -O0, `!prof !{"branch_weights","expected",2000,1}`
at -O1 and above (LowerExpectIntrinsic). Classic clang lowers `[[likely]]` on
an `if` through the same intrinsic (`emitCondLikelihoodViaExpectIntrinsic`).

**Design.** No format change is needed for `if` and loops: when an arm carries
a likelihood, the writer wraps the condition as `expect(long(cond), 1)` or
`0` and ends with `TO_BOOL`, as for `__builtin_expect`
(`CAST`, `CONST_INT`, `BUILTIN expect 2`, `TO_BOOL`). The combined hint is
clang's `Stmt::getLikelihood(Then, Else)`: the same attribute on both arms
cancels, one arm's hint is the other's inverse, a loop body's hint applies to
the condition. A `&&` or `||` condition is left alone (clang splits the
weights; we do not). `esk_branch_hint` (key 2, 0 unlikely / 1 likely) is
needed only for switch cases (weights on the `switch`) and is what a library
could use for exact weights; classic clang emits `!prof` for a switch,
clangir nothing.

**Not parity.** `clang -fclangir` ignores the attribute; doing it makes
`attr-diff` report `only-ours:prof` or an `llvm.expect` call, which the
known-differences file lists as an improvement. So this is optional in S2: do
it last, with the codequality kernels as the evidence.

**Risk.** A hint changes no semantics. The risk is cost: an extra cast per
hinted branch and one more path in the code stack walker (`cik_*` item for the
`IF` condition).

### 3.3 The loop-progress fact (`mustprogress`)

**The rule at the pin** (`clang/lib/CodeGenUtils/StmtUtils.cpp`, used by
CIRGen, `CIRGenFunction.h:625`):
- the function attribute `mustprogress` is on every function definition in
  C++11 and later (not C);
- it is removed again when the function has a *trivial infinite loop*: a loop
  whose controlling expression is a constant that evaluates true (or absent:
  `for (;;)`) and whose body is empty (a null statement or an empty compound;
  a `for` with an increment is not empty);
- C loops with a non-constant condition get loop metadata in classic clang;
  CIRGen has no loop attribute at the pin.

**What the IL tells us after folding.** The stream already carries both
halves: the controlling expression of a folded constant is `CONST_INT n`
followed by `TO_BOOL` (`while (1)`, `while (t)` with `constexpr bool t`,
`while (sizeof(int) == 4)`, `while (1 || n)` all fold); `for (;;)` is
`FOR_COND false`; an empty body is `BLOCK_BEGIN BLOCK_END` straight after
`WHILE_BODY`/`FOR_BODY`/`DO_BEGIN` (probe `w5/lp.cpp`, `lp2.cpp`). So **no
new fact and no writer change**: the library can compute "this function has a
trivial infinite loop" while it translates the loops and set `mustprogress`
at `FUNC_END`. The plan's "needs an FE fact for loops with a constant
condition" does not hold: the fold is visible.

**Differences from clang's test, and their direction.**
- Safe (we remove `mustprogress` where clang keeps it): `while (1) {;}` and
  `for (;;) {;}` have an empty stream body, a non-empty compound for clang
  (`body_empty()` is false). A conservative extra removal; `attr-diff` shows
  `only-clangir:mustprogress`, to list as known.
- Unsafe (we keep it where clang removes it): a condition clang evaluates to
  true and the FE does not fold. The six probes (`1 || n`, `n ? 1 : 1`,
  `n || !n`, `(void)n, 1`, `1 || n` in a `for`, `sizeof`) behave like clang
  (only the first, the `for` and the `sizeof` form are constants for both).
  A corpus differential (`attr-diff` on the C++ smoke list) is the check; if
  a case turns up, the writer normalizes the condition (an evaluate-as-int
  helper before translating a loop condition), which is local to `do_while`
  and friends.

**Risk.** `mustprogress` on a function with a trivial infinite loop is a
miscompile (LLVM may delete the loop): the library must implement the removal
exactly; test: `void f() { while (1); }` at -O2 must stay an infinite loop.

### 3.4 `used` (key 11) on `DECL_FUNC` and `DECL_VAR`

**IL.** `a_routine.has_gnu_used_attribute` and `a_variable.has_gnu_used_attribute`
(`il_def.h:12662`, `11342`), set by `apply_used_attr` for `__attribute__((used))`
and `[[gnu::used]]` (GNU modes), which also calls `mark_as_needed`. The writer
already reads them for roots (`routine_is_root`, `ecil_code.c:10176`) and
nothing else. The attribute accumulates over declarations, so a later
declaration can add it.

**Library meaning.** clangir puts a `used` entity in `llvm.compiler.used`
(not `llvm.used`; `llvm.used` is for `retain`), `CIRGenModule::setCommonAttributes`
-> `addUsedOrCompilerUsedGlobal`; only definitions. The probe's IR:
`@llvm.compiler.used = appending global [2 x ptr] [@_ZL11used_statici,
@used_var]`. The plan text says `llvm.used`: it should say
`llvm.compiler.used`. It is also a lazy-emission root (an internal or linkonce
function that nothing refers to stays).

**Writer.**
- `DECL_FUNC`: `a_func_state.used` from `rout->has_gnu_used_attribute`
  (`compute_func_state`, under `GNU_EXTENSIONS_ALLOWED`), key 11 in the
  builder, sticky exactly like `noinline`. In the re-issue compare, replace
  `decl_noinline` by a small bit set `decl_facts` (`NOINLINE`, `USED`) in
  `an_ecil_gid_info`, so that the next fact is one more bit.
- `DECL_VAR`: `ecil_emit_decl_var` (`ecil_type.c:426`) gets the fact as an
  argument; callers with a variable: `begin_variable_definition`
  (`ecil_const.c:1686`) and `ecil_global_variable_id` (`ecil_type.c:5714`);
  the string-literal, compound-literal and dropped-declaration calls pass
  false. A variable has no re-issue pass: its definition is sent in `F_GLOBALS`
  (after the whole translation unit) and carries the attribute known by then.
  A reference earlier in a function region sends a declaration without it,
  and the definition adds it, which is enough (sticky).
- A `used` definition that is dropped stays an error (`routine_is_root`).

**Risks.**
- A `used` static local or a `used` function in a local class: the entity is
  still defined only where its body is streamed; the library puts the
  definition in the list.
- Template instances: the attribute is on the template; check that the FE
  copies the bit to instances (golden with `template<class T>
  __attribute__((used)) void f(T)` instantiated).
- A `used` declaration without a definition: ignored by the library (clang
  requires a definition).
- Sticky vs per-issue: spec text, "if any issue of the G-id carries it".
- Another silent gap closes: handoff_3 section 4, "attributes `used`/`section`/
  `transparent_union`/`ms_abi` are ignored".

**Tests.** Goldens: a `used` static function, a `used` variable, a `used`
inline function, the attribute on a later declaration, a template instance.

### 3.5 A fact for the allocation calls (`builtin`)

**What clang does.** The declaration of the replaceable allocation and
deallocation functions has `nobuiltin` (ours has it); a call that comes from a
new-expression or a delete-expression, or from `__builtin_operator_new` and
`__builtin_operator_delete`, carries the call-site attribute `builtin`, which
lets LLVM elide or merge the allocations. A direct call `::operator new(n)`
does not. Probe (clangir): `call noundef nonnull ptr @_Znwm(i64 noundef 4)
#10` with `#10 = { builtin allocsize(0) }`; `call void @_ZdlPvm(...) #11`,
`#11 = { builtin nounwind }`.

**What the stream has.**
- delete-expression: `DELETE g dtor flags ...` is its own record. The library
  knows the call comes from a delete-expression.
- new-expression: a plain `CALL g t n flags` to the allocation function (and
  `NEW_ARRAY_SIZE_THROW` before an array count); a direct
  `::operator new(n)` is the same record. **This is the gap.**
- `__builtin_operator_new/delete` (Clang mode only; `sfk_builtin_operator_new`,
  `sfk_builtin_operator_delete`): a plain `CALL` to `_Znwm`/`_ZdlPv` (probe
  `w5/bon.cpp`).

**Design: a `CALL` flag**, `ECIL_CALL_ALLOC_EXPR 0x40` (1.3): "the allocation or
deallocation call of a new-expression, or of `__builtin_operator_new/delete`".
It costs no bytes, the call record has the flags operand, and a 1.3 library is
the only reader. The writer sets it in `do_new_delete` for the allocation call
and in the call translation when the callee's special kind is
`sfk_builtin_operator_new/delete`. The library adds `builtin` only when the
callee symbol is one of the standard replaceable names (`_Znwm`, `_Znam`, the
align and nothrow forms, `_ZdlPv...`, `_ZdaPv...`): that is clang's "replaceable
global allocation function" and it excludes class-specific and placement forms
by name (`_ZN1AnwEm`, `_ZnwmPv`...). The writer need not decide it. A supp key
on `CALL` would also work and would be skippable, but it costs 4 bytes on every
new-expression, and a library that cannot see the flag only loses an
optimization; so the flag.

**Risks.**
- Checked against clangir (`w5/g.cpp`): `delete p` of a class with a
  virtual destructor is a virtual call (no `builtin`), and the
  `operator delete` call inside the deleting destructor D0, which the library
  writes, has `builtin nounwind`; a class-specific `operator new`/`delete`
  call has no attribute; placement new has no call; a direct
  `::operator new(n)` call has the call-site attribute `allocsize(0)` only
  (no `builtin`) and `noundef nonnull` on the result. So the library decides
  everything but the new-expression origin from the callee name.
- An overloaded `operator new` declared by the user in the TU: still
  "replaceable" in clang's sense if global, so the call keeps `builtin` while
  the function has a body in the TU. That is the case that the earlier
  miscompile (`nobuiltin` missing) came from; the library puts `nobuiltin` on
  every declaration and definition of those functions.
- Under `-fno-builtin` and `-ffreestanding`-like options: none exist here.

### 3.6 The static invoker's null `this`

**Today.** `emit_invoker_body` (`ecil_code.c`, `ecil_rec1(frame,
eop_const_null, closure_ptr)`) passes `C_NULL` as the object pointer of the
call operator (spec 11.9). It is the only such call: the other
`eop_const_null` use is a null constant of a pointer type. With `nonnull` on
`this` (3.1) it is undefined behaviour.

**What clang does.** An alloca of the closure type, named `unused.capture`,
align 1 (`CIRGenClass.cpp:941`), passed as `this`. A captureless closure is an
empty class of size 1.

**Design (no format change; spec 11.9 text changes).** After the `PARAM`s:
`LOCAL l closure_type nil 0 0`, `LREF l`, `ADDR`, in place of `CONST_NULL`.
The local is never constructed (clang does not either; a captureless closure
is trivial). Prototype: box build, two invokers (a captureless lambda and a
generic one) converted to function pointers; the stream differs by three
records per invoker; `edg-cir` -O0 and -O2 run gives the same output; at -O2
the invoker inlines the call operator (`define internal i32
@_ZZ4mainENUliiE_4_FUNEii` is `add nsw i32 %1, %0`). No effect on the C++23
`static` call operator: it has no invoker (probe `w5/sl.cpp`).

**Risks.** The L-id numbering after the parameters (`new_lid()`); the scope
rule that a `LOCAL` belongs to its `BLOCK` (the invoker body has none; the
prototype passed validation without one: a `LOCAL` directly in the function
scope is legal, spec 8.3). A variadic lambda has no invoker (already
unsupported). The type must be complete: the closure is.

### 3.7 Inline-only bodies as `available_externally`

**Does the writer stream every such body?** Yes. Every defined routine's
region is streamed (`ecil_stream_function_region`: `defined`, not filtered,
not already handled), whether or not anything refers to it. Probe (`w5/io.c`,
C99 and GNU C): `c99a` (referenced), `c99b` (unreferenced), `gia`
(referenced), `gib` (unreferenced), `c99c` (inline plus a later non-inline
declaration) all have a `FUNC_BEGIN`, with `DECL_FUNC` flags `0x13`
(defined here, inline, `INLINE_ONLY`). `INLINE_ONLY` is set from
`definition_for_inlining_only || suppress_inline_body`
(`compute_func_state`), which the front end sets for C99 inline definitions,
GNU C `extern inline`, GNU C++ `gnu_inline` and Microsoft `dllimport`.
`INLINE_ONLY` is re-issued in `F_GLOBALS` when an external definition appears
(`c99c`).

**What is not streamed.**
1. A second routine with the same symbol (my shared-symbol fix): the first
   body only; for glibc's `memchr` overloads the bodies are equal.
2. A body dropped as unsupported (an inline-only function using inline asm,
   a VLA...): the declaration stays, so no `available_externally` body for it.
3. `is_prototype_instantiation`, deduction guides, `consteval`.
4. GNU C++ `inline __attribute__((gnu_inline))` without `extern`: the front end
   cannot tell, so it is inline-only and never emitted out of line, where gcc
   would emit it (handoff_2). With `available_externally` that is the same
   behaviour as today (the call links to nothing), not a new gap.

**Conclusion.** Nothing to change in the writer for 3.7. The library
keeps the `INLINE_ONLY` bodies as `available_externally` at -O1 and above.
A test: the glibc `memchr` shape at -O2 must end with one
`available_externally` body (both overloads call the one symbol).

### 3.8 `noreturn` on declarations

**It is sent.** `DECL_FUNC NORETURN` (flag 4) comes from
`rout_type_supp(t)->does_not_return` in `compute_func_state`, which runs for
every issue, declarations included, and the re-issue pass recomputes it. Probe
with the real headers (`w5/n8.cpp`, `<stdlib.h>`): `exit` and `abort` are
`DECL_FUNC ... flags 132` (`NORETURN | NOTHROW`) at the declaration, and
the calls are `CALL g t n 6` (`NORETURN | RESULT_UNUSED`). With a
`__attribute__((noreturn))` declaration of one's own: flag 4.

**The gap is at the call site, in the library.** The IR of `edg-cir` at -O0
has `declare void @abort() #3` with `#3 = { noreturn }` but `call void
@abort()` without call-site attributes; clang has `call void @abort() #13`,
`#13 = { noreturn nounwind }`. The stream has what is needed
(`ECIL_CALL_NORETURN`, `ECIL_CALL_NOTHROW`). Nothing for the writer.

### 3.9 Format and contract items this section asks for

| Item | Where | Kind |
| --- | --- | --- |
| key 11 `used` | `DECL_FUNC`, `DECL_VAR`; sticky | supp key (in `ecil_format.h`) |
| key 15 `nonnull_ptrs` | `TY_FUNC`; uint mask, bit 0 result, bit i+1 parameter i; part of the type's identity | supp key (in `ecil_format.h`) |
| `ECIL_CALL_ALLOC_EXPR 0x40` | `CALL` flags | 1.3 flag (in `ecil_format.h`; kept only if clangir's IR carries `builtin`) |
| spec 11.9 | the invoker passes the address of a local of the closure type | text, no format change |
| `used` maps to `llvm.compiler.used` | design text | correction |
| optional: key 2 `branch_hint` on `CASE` | only if switch weights are wanted | existing key |
| optional: a per-function "has a value-less return in a non-void function" fact | for `noundef` on results | to decide with L4 |

Writer-side structure for the multi-key supp builder (done in M4.1): the
re-issue compare grows from `decl_noinline` to a `decl_facts` bit set
(`NOINLINE`, `USED`), compared and OR-ed (sticky) in
`emit_func_decl_if_changed`; `TY_FUNC` gets a supp through the same builder.

### 3.10 Probe files

`~/edg-cir-writer/.tmp/w4/w5src/`: `f.cpp` (reference, hints, loops, used,
new/delete, lambda, abort, gnu_inline; the three IR dumps are in
`../w5/f.ir.*`, `f.classic.ir`, `f.ours.*.ll`), `lp.cpp`, `lp2.cpp` (loops),
`lk.cpp` (the IL of the hints, `lk.il`), `io.c` (inline-only), `n8.cpp`
(noreturn with system headers), `inv.cpp`, `sl.cpp` (invokers), `bon.cpp`
(`__builtin_operator_new`), `stl.cpp` (stream sizes), `g.cpp` (allocation
calls in classes), `lang.c`/`lang.cpp` and `nl.cpp` (language linkage),
`inl.cpp` (the INLINE flag), `dr.cpp`, `ea.cpp`, `ea2.cpp` and `eas/` (the
empty-class assignment of section 4). Box-only patches: the invoker
prototype (`proto6.py`), reverted.

### 3.11 Two questions from the design document

**(B) Is `extern "C"` visible in the stream for a function?** Not today.
`DECL_FUNC` has no language-linkage flag, `HDR_LANG` gives only the language
of the translation unit, and the symbol name is not a proxy (`main`, a C++
function with an asm label such as glibc's `memchr`, and anything in an
`extern "C++"` block are unmangled or differently mangled). It matters:
clang puts `noundef` on the **result** only of a function with C++ language
linkage; probe `w5/lang.cpp` with `clang -fclangir`: `define i32 @cf(i32
noundef)` for `extern "C" int cf(int)` (no `noundef` on the result, also on the
declaration `declare i32 @ext(i32 noundef)`), against `define noundef i32
@_Z5callci(...)` for a C++ function; in a C file no result carries it.
Parameters are `noundef` in every case. The front end has the fact exactly: the
routine's `name_linkage` is `nlk_external` ("C") for an `extern "C"` function
and `nlk_cplusplus_external` ("C++") for the rest, `main` and asm-label
functions included (checked with `--il_display`, `w5/nl.cpp`). Proposal, if
`noundef` on results is taken: a `DECL_FUNC` flag `EXTERN_C` (0x100, a 1.3
flag), set from `!C_mode() && name_linkage == nlk_external`, which joins the
re-issue comparison through `decl_flags` like every flag; in a C stream the
library applies the C rule to everything.

**(F) What does the `DECL_FUNC` `INLINE` flag mean?** It is `rout->is_inline`,
"specified in the source as a candidate for inlining": the `inline` keyword
**or** a definition inside a class, and the front end also sets it for
`constexpr` functions, implicitly defined special members and the members of
a lambda's closure. It is the language-level "inline function" (vague
linkage, C99 inline rules), not "declared with the keyword". Probe
(`w5/inl.cpp`): INLINE is set on `inline int expl`, `constexpr int cx`, an
in-class member `in_class` and `static int st()`, an implicit `D::D()`, and
on every lambda member (the invoker `_FUN` is the exception), and not on a
plain function, an out-of-class member, or a template instance without the
keyword. clang's `inlinehint` (`isInlineSpecified()`, the same probe with
`clang -fclangir`) is on `expl`, `in_class_inline`, `tmpl_inl`, `gi` and the
lambda call operator, and **not** on `cx`, `in_class` and `st`. So INLINE does
not map to `inlinehint` one to one: the library currently applies it to all of
them (`globals.cpp:179`), which attr-diff will show for constexpr and in-class
members and implicit special members. The front end keeps no
"keyword written" bit on a routine (`has_inline_keyword` belongs to asm
statements); the nearest facts are `inline_in_class_definition`,
`is_declared_constexpr` and `compiler_generated`. Approximation without an
FE change: `INLINE` and not constexpr and not defined in its class and not
compiler generated, plus lambda members; it misses `constexpr inline` and an
in-class `inline`. Exact: a CIR-gated bit set where the declaration
specifier `inline` is processed (a new FE bit, outside the writer's files).

## 4. L5 triage: boost/multiprecision is rejected with "DROP of a NONTRIVIAL_ABI record prvalue"

**Cause: a writer bug; the library rule is right.** Reproduction (box,
`dev_tools/cir-realworld/boost/multiprecision.cpp`, system headers, level 0 or
2, `edg-cir --trace` for the position): the failing function is
`boost::multiprecision::backends::cpp_int_base<0, ..., std::allocator<unsigned
long long>, false>::assign(const cpp_int_base &)`, which has

    static_cast<allocator_type &>(*this) = static_cast<const allocator_type &>(o);

`std::allocator` is empty and has a user-provided destructor, so it is a
NONTRIVIAL_ABI class without data. The writer's rule for an assignment of an
empty class (`ecil_code.c`, `eok_assign`, phase `EMPTY_ASSIGN_RHS`: "the
assignment copies nothing; the operands are evaluated, the right one first")
translates the right operand as a *value* and drops it. For that operand the
front end has a class `eok_base_class_cast` node that is a class prvalue by its
flag (the trivial copy assignment takes its argument as a value), so
`emit_class_cast` ends with `DEREF` and `emit_load_if_value`: the stream has
`BASE_CAST; DEREF; LOAD 0; DROP`, a record prvalue that is read from the object
and thrown away. A prvalue of a class with a destructor must be materialized
and destroyed (spec 12.2: a copy is a constructor call), and the library rejects
the `DROP`. In C++ no value exists: nothing is read.

**Minimal reproduction** (`w5/eas/*.cpp`, six shapes, each fails in
`edg-cir`; `refs` (`a = b` with references), `call` (`a = make()`) and `used`
(the result of the assignment bound to a reference) pass):

    struct A { ~A() {} };
    struct B : A { int x; };
    void bases(B &t, const B &o) { static_cast<A &>(t) = static_cast<const A &>(o); }
    void vars() { A a1, a2; a1 = a2; }
    void ptrs(B *t, const B *o) { *static_cast<A *>(t) = *static_cast<const A *>(o); }
    void glob(A &a) { a = g_a; }                 // a variable
    void members() { struct H { A m; } h1, h2; h1.m = h2.m; }
    void arrays(A (&x)[2], A (&y)[2]) { x[0] = y[1]; }

The left operand is fine (`DEREF; DROP`, a glvalue is dropped without a read);
the right operand is the problem, whenever it is a class value that designates
an object and is not a reference parameter read: a variable, a member, an
element, an indirection, a base-class cast. Other discards of a class glvalue
(`o;`, `(void)o;`, `static_cast<const P &>(q);`, a comma) are correct:
`DEREF; DROP` (probe `w5/dr.cpp`).

**Fix (S2/S4 material; not made).** In the `EMPTY_ASSIGN_RHS` setup, when the
right operand is a class value that designates an object and its class is
NONTRIVIAL_ABI (`ecil_class_is_nontrivial_abi`), suppress its load as the
non-empty class assignment already does (`suppress_load_node`, with
`rhs_designates_object`, extended by the base and derived class casts), so
that the stream has `DEREF; DROP`. Streams of classes that are not
NONTRIVIAL_ABI do not change. A golden with the six shapes above and a run
test (the assignments must still evaluate their operands, the right one
first). Impact: every translation unit that assigns a `std::allocator` (or
another empty class with a destructor) through a base-class cast or a
variable; boost multiprecision is the first one found.

## 5. Reproduction

- Box scratch: `~/edg-cir-writer/.tmp/w4/` (scripts `w3_*.sh`, `w4_*.sh`,
  `w4_patch.py`, which instruments `mem_manage.c` on the box copy only and
  restores it; `tu/` the three generated TUs).
- The instrumentation patch (`w4_patch.py`) and the `FALSE &&` variant are box
  only and are not in the repository; the keep-reason counters would become the
  CHECKING-only `ECIL_STATS` line proposed in §2.3.
- `cg_writer.py` (`cir_src/test/writer/tools`) is the one new tool.
