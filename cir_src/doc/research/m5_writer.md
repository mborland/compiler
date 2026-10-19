# M5a writer research: W3 (writer cost) and W4 (region keep #2)

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

## 2. W4 region keep #2 (prototype instantiations)

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

| TU | regions | kept #5 (prototype instantiation) | #5 with no other reason | #2 local class types | #1 inline body |
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

So **keep #2 holds 0.4 MB on the C++ TU and at most 5.8 MB (4.9 MB exclusive)
on the template TU: 1.4% of the front end's peak RSS, and under 1% of the
in-process peak (719 MB)**. Removing it would not move the memory numbers, and
it is unsafe for C++20 requires-expressions.

The **local-class keep (#2, "contains_local_class_type")** is the larger one: 71
MB of payload on the lambda-heavy TU, about 20% of the front end's peak RSS
there (lambda closure classes count as local classes, `mem_manage.c:1432-1437`;
about 30 KB per kept region). It is needed because "local class types may have
generated special members that need to be defined because of later
instantiations". A narrower condition (a region is kept only while a member of
a local class is still undefined or a template instantiation can still name
it) might release most of it; that needs an analysis of when the last use can
happen and is a memory item for S3/S4, not W4.

### 2.3 Proposal for S4

- **Do not remove keep #2.** Correct the comment at `mem_manage.c:1461-1473`:
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

## 3. Reproduction

- Box scratch: `~/edg-cir-writer/.tmp/w4/` (scripts `w3_*.sh`, `w4_*.sh`,
  `w4_patch.py`, which instruments `mem_manage.c` on the box copy only and
  restores it; `tu/` the three generated TUs).
- The instrumentation patch (`w4_patch.py`) and the `FALSE &&` variant are box
  only and are not in the repository; the keep-reason counters would become the
  CHECKING-only `ECIL_STATS` line proposed in §2.3.
- `cg_writer.py` (`cir_src/test/writer/tools`) is the one new tool.
