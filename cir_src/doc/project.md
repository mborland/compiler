# EDG ClangIR back end — project state (the living handoff)

This is the one document to read before working on the ClangIR (CIR) back
end. The binding requirements are in `brief.md`; this file is how the
project meets them. It replaces the session handoffs, which are archived
unchanged in `archive/` (historical, not required reading). Keep this file
current: each gate replaces §0 and §9 and updates §2 to §8. Do not append
history; git keeps it.

Tags: `H1`–`H4` cite the archived handoffs (`archive/handoff.md` …
`handoff_4.md`); "user" marks a user decision with its date.

## 0. State

**Branch** `cir`, local only, never pushed. On 2026-11-12 it is checked
out in the git worktree
`/Users/mattborland/Documents/compiler/.tmp/agent-fix252`, and the main
checkout is on `main`, which is 23 commits past the rebase point
`cc408045d4`. That worktree is also used for other branches (it was
switched away and back during the planning session, which discarded
uncommitted edits): run `git worktree list` first, and do not leave work
uncommitted or unstaged longer than needed.

| Gate | Commit | What it added |
| --- | --- | --- |
| R, M0, M1, M2 | `4afcc43a2c` `e20b6cbafd` `7ec98bca47` `9f45e8fc85` | Design and ECIL format; `BACK_END_TYPE`; cpfe-cir, writer and library skeleton; a C subset runs |
| M2.1, M3a, M3 | `70c0ac1300` `ae35b026f4` `9d442187de` | Refactors; the ECIL 1.0 C++ contract; core C++ |
| M3.1, M4a, M4 | `1075681800` `5d408774ba` `777b8afa0e` | Re-look items (ECIL 1.1); the ECIL 1.2 contract; virtual functions, RTTI, EH, member pointers |
| M4.1, M5p, M5a, M5 | `8c230eb5ee` `119d44a89b` `8f2ff27d96` `47f83f5db1` | -O plumbing; pin 2d6f180e; the ECIL 1.3 contract and C API 2; lazy emission, optimization facts, chunked lowering, writer speed |
| (answers) | `89c82d56e7` | The user's answers to H4 §8 |
| M5.1-docs, M5.1a, M5.1b, M5.1 | `f975af6b9d` `fed47acbbb` `ac210ca02b` `c9a459a718` | This document set; clang4 and the re-derived baselines; attr-diff facts, the -O0 always_inline inliner, the mangler and `__has_builtin` (ungated), CWG2387 under CIR; the size-based chunk decision, the guard-worker exit fix, the milestone gate |
| M6.0 | `e06b3419a0` | The M6 plan (§6); the support table `support/` (305 rows); the security model and audit schedule `security.md`; the §8 answers (decisions 51–56) |
| M6.R | `70b1a2e326` | The rebase onto `main` `cc408045d4`; main's image namespace and expectation inheritance for CIR; the CIR Changes entries as pending files; the re-derived baselines `m6r-base-*` |
| M6.1a | (the commit that adds this row) | The clang4 sandbox for the auditor and its guard; SEC-1 on Mythos and the findings register; the feature probes, negative tests and the libstdc++ header sweep (0 `unverified`); the `support-sweep` gate step; diagnoses for `__builtin_alloca_with_align` above 16 bytes, the array-of-class copy and `--set_flag=coroutines`; fixes for `__thread` TLS, designated bases, the parameter-63 nonnull and `C_STRING` element widths |

M6.R rebased every commit above onto `main` and ended each subject with
` []` (main's commit policy; user, 2026-11-07), so their hashes changed.
The pre-rebase history is the local branch `cir-pre-m6r`; older documents
and the archive cite its hashes, and `git range-diff
43e10b90fc..cir-pre-m6r main..cir` maps them to the new ones. Only the
tip is gated: the intermediate rebased commits were not rebuilt, and their
messages describe evidence taken before the rebase.

**Format and toolchain.** ECIL **1.3** (frozen at M5). C API **2**
(`EDG_CIR_API_VERSION`). llvm-project pin
`2d6f180e7dbad81db52f5163a34502c1b8058012` (`cir_src/LLVM_REVISION`), no
local patches.

**Host.** x86-64 work runs on `matt@clang4.cpp.al` (user, 2026-11-05).
The old box `192.168.1.167` is retired: its home directory was wiped on
2026-10-28, with the M5 trees, gate evidence and every baseline. M5.1
session 1 rebuilt the images and baselines on clang4.

**Numbers at M6.1a** (clang4, lean gate, §9): C corpus 1663/2561 (M6.R:
1664; `pr78468` is now diagnosed, §4.1); C++ corpus 1472/1823; cir-suite
798/798 in both configurations; support table 425 rows (267 yes, 45
partial, 96 no, 17 ignored, 0 unverified); mixed-tu 70/70; identity, day
and day32 identical to the M6.R baselines. Perf, rss, chunk-rss and
attr-diff are not in the lean step set and were not re-measured; the
assembly is identical (2563 C and 1956 C++ files) apart from the three
attributed status changes (§9).

**Now:** M6.1a is the gate on 2026-11-08 (§6, §9). It:

- built the clang4 sandbox `edg-cir-sandbox` and the Mac-side guard of
  the auditor (decision 58; reviews R1, R2), and ran SEC-1 on Mythos:
  two Critical findings were fixed in this session (user, 2026-11-08), the
  rest are in the register (`security.md` §5) for M6.1b;
- proved every support row: 425 rows, 0 `unverified`; a negative test for
  every `no` and `partial` row; the header sweep of 120 libstdc++ headers;
  the gate step `support-sweep`;
- found silent miscompiles with the probes: GNU `__thread` (fixed), the
  array-of-class copy in implicit constructors (now diagnosed; fix M6.1b),
  the volatile initialization store and `FENV_ACCESS` (M6.1b).

Next is **M6.1b**. Its plan is `plans/M6.1b-sec1-triage-housekeeping-fuzzing.md`
(written 2026-11-12 and checked against the code; where it differs from
§6, the plan wins). P-7 is fixed: GH #525 was cherry-picked onto `cir`
as `b1c6c60d7c` on 2026-11-12 (decision 59; plan Phase 0 step 2).

## 1. Map

| Topic | Where |
| --- | --- |
| Requirements | `brief.md` |
| Architecture and decision history per milestone | `design.md` (§2 components; §10–§16 are milestone logs) |
| The ECIL wire format | `ecil_format.md` (normative; `include/edg_cir/ecil_format.h` wins) |
| The FE writer | `writer.md` (§9.12 is the authoritative list of C++ gaps) |
| Research and measurements | `research/README.md` (index, live or historical) |
| Library sources | `cir_src/lib/{format,cirgen,abi,driver}`, C API `cir_src/include/edg_cir/edg_cir.h` |
| Writer sources | `src/ecil_*.{c,h}`, `src/cir_be.{c,h}`, CIR-gated hooks (§7) |
| How to build, run, test and gate | `cookbook.md` |
| Gate tooling | `dev_tools/bin/edg-cir-*`, filters `tests/.edgy/filters/cir-*.txt` |
| Executive summary deck (update only when the user asks; user, 2026-11-07) | `exsum/` (`exsum/README.md`) |
| What the back end supports (cppreference-style; the single source for docs and deck) | `support/` (`support/README.md`; rendered `support/support.md`, `doc/source/cir_support.inc` (included by `doc/source/cir_be.rst`), `exsum/support.svg`) |
| Threat model, security audits, findings register | `security.md` |
| Session plans (one per sub-milestone, from M6.1b) | `plans/` |
| Defect reports written for `main` | `reports/` |

The scratch notes of M5.1 and of M6.0 to M6.1a (`.tmp/m51/`, `.tmp/m6/s0`
to `s2`) and the plans of those sessions were deleted on 2026-11-12
(decision 59). A citation of them anywhere in the tree or in the archive
is historical; what later sessions need is in this file, the cookbook or
the current plan.

## 2. Decisions

Keep these unless there is a reason to change them; changing one is a user
decision.

**Scope and architecture**
1. Only non-lowered IL. The FE sends the facts it computes; the library
   computes the Itanium mechanics and ports clang's vtable and VTT builders
   (`lib/abi`) (H1 §2.1, H3 §2.2).
2. Standard C++ first; GNU and then MSVC extensions later (brief).
3. Build isolation: `cir_src` is outside the FE CMake tree, found through
   `EdgCirConfig.cmake` and linked with `LINK_DEPENDS_NO_SHARED`; `cir_src`
   never includes `src/` headers and the FE never includes LLVM or MLIR
   headers (H1 §2.6).
4. The library never depends on FE macros; facts such as INLINE_HINT,
   EXTERN_C and NOTHROW travel in the stream (H4 §2.19).
5. `BACK_END_TYPE` with an enumeration, no new `BACK_END_IS_*` (brief, M0).
6. Every FE change is CIR-gated (`BACK_END_TYPE == BACK_END_TYPE_CIR`). The
   only approved exceptions are the M5.1 mangler fix and the
   `__has_builtin(__builtin_operator_new/delete)` values (user, 2026-11-05).
7. Concurrency: leave room, implement nothing (H4 §2.2).

**ECIL format**
8. Forward-only and pointer-free, post-order. Minor versions only add. The
   reader fails only on unknown opcodes, enum values, flags or required
   bits. Validation is strict; a writer contract violation is a format
   error (H1 §2.2).
9. Optional facts are supp keys, skippable, several per blob (1.3).
10. Dense G-ids and L-ids; the `ecil_id` prefix slot is reset by
    `clear_il_entry_prefix`; one G-id per symbol; a declaration is
    re-issued to change its type (H1 §2.4).

**Streaming and memory**
11. Each finished function is streamed from
    `check_for_done_with_memory_region` and freed only if the keep tests
    allow; a bitmap on `function_def_number` stops re-streaming;
    `MAINTAIN_NEEDED_FLAGS=0`, with GlobalDCE at every -O level (H1 §2.3).
12. Lazy emission: the library defers discardable bodies and replays the
    reachable ones at finish only. A missed edge is an error, never a
    silent undefined reference. `EAGER_BODIES` is the escape hatch and the
    mode for tools that compare every body (user; H4 §2.3).
13. Bounded memory: chunk design (a), output-identical (except the `.N`
    suffixes of LLVM-named local data at -O2, §4.6), behind a threshold; the CIR passes stay whole-module; a failed chunk is a
    diagnosed error; design (b) is rejected (user; H4 §2.4).
14. The default chunk decision is a size rule (M5.1): chunk when the
    emitted-body bytes (the reader offset minus the bodies lazy emission
    dropped) reach 1,250,000, which bounds the unchunked peak. Explicit
    values keep their body-count meaning: N bodies, 1 forces,
    `UINT32_MAX` never (user; H4 §2.5, M5.1 plan).

**Robustness**
15. Decoding on the caller's thread; a 256 MiB worker with an alternate
    signal stack; diagnostics queued; crash recovery under one mutex; a
    poisoned session leaks on purpose (H1 §2.7, design §13).
16. Unsupported constructs are diagnosed, never guessed. An unreferenced
    discardable dropped function is a warning, other drops are errors,
    dropping a root is a `cir_error`; the reason travels in the stream
    (user; H2 §2.9, H3 §2.8).
17. Verifiers run only under `VERIFY_EACH` (H4 §2.18).

**ABI and semantics**
18. x86-64 SysV only. Plain signatures with records by value;
    CallConvLowering does sret and byval; an unprototyped call is a
    variadic bitcast plus an indirect call; `agg.tmp` plus one `__retval`;
    allocas at function entry (H1 §2.5, H2 §2.7).
19. Itanium names everywhere. Internal names are deterministic (`_ZL`,
    `_GLOBAL__N_1`, no mtime or cwd module id); the TU-local predicate
    forces internal linkage; late names are nil names plus `NAME_BIND`
    with a CHECKING re-mangle (user; H2 §2.3).
20. Linkage: `linkonce_odr` plus COMDAT; `weak_odr` for explicit
    instantiations; explicit specializations external; vague linkage on
    definitions only; dropping an external non-discardable definition is
    an error (H2 §2.4, design §11, §13).
21. Structors: one body under C2/D2; C1 is an alias, a C5 comdat plus
    alias, or a finish-time substitution; arrays and `new` use C1/D1; no
    C1→C2 canonicalization (H2 §2.5, design §12, H4 §2.17).
22. Cleanups are pushed after construction; conditional cleanups are
    flagged; `__retval` while a cleanup is open; a guarded NRVO cleanup
    (H2 §2.6).
23. Static initialization: `cir.local_init`, the global ctor and dtor
    regions, `_ZGV` guards, `UseInitArray`, clang's module order (H2 §2.8).
24. ABI ties follow clang (user), with two exceptions: object layout is
    the FE's (`vbase13`), and typeinfo names of TU-local types get the
    `*` prefix (H3 §2.3).
25. Where clang and g++ behave differently, follow classic clang: throw
    temporaries are destroyed during unwinding, the array element whose
    constructor threw, CWG 2176 (user; H3 §2.4).
26. `uwtable` on every function, C included (user; H3 §2.6).
27. `__builtin_is_pointer_interconvertible_with_class` on a
    non-standard-layout union is true (H3 §2.9).
28. Mangling: M5.1 fixes the dependent variable-template defect for every
    back end and uses g++'s `sr` form in GNU mode; clang's as-written form
    under clang mode is M6 (user, 2026-11-05). The ABI guard is
    `ABI_COMPATIBILITY_VERSION >= 701`, the release under development.
28a. CWG2387 (external linkage for const variable-template instances, as
    g++ 15 and clang give them) applies under CIR only, in GNU mode 15 and
    later; cpfe and cpfe-cp keep internal linkage (user, 2026-11-06; §5).

**Optimization and code quality**
29. -O: `--cir_opt_level=N` (0–3, CIR-gated) predefines `__OPTIMIZE__` and
    `__NO_INLINE__` by gcc's rules; eccp maps the -O forms (user; H4 §2.12,
    design §13).
30. Parity with clangir at the pin on the high-value attributes; where
    clangir lacks a fact we do not add it; the residue is reported, not
    gated (user; H4 §2.7).
31. attr-diff: checked RISK and MISSED rows are recorded as known entries
    so the step can pass (user; H4 §8 Q2).
32. Library function facts come from clang's `Builtins.inc` (unconditional
    letters U, c, r, n; glibc's cancellation points `fprintf`, `vprintf`,
    `vfprintf` are not nothrow), in a separate generated header; the FE's
    builtin tables are untouched (user; H2 §2.10, H4 §2.8).
33. Allocation elision follows clang: `builtin` on new-expression calls
    (user; H4 §2.9).
34. `used` → `llvm.compiler.used`; `used` static data members of class
    templates follow gcc (H4 §2.15).
35. Library builtins stay plain calls (`memcpy`, not `llvm.memcpy`); a
    possible M6 item (H4 §2.17).
36. The native macOS cpfe-cir sends no INLINE_HINT. Never record goldens
    on the Mac (H4 §2.16).
37. The generated C++ unit's S1 target applies at -O0 only (user; H4
    §2.10) and is 92% of M5a from M5.1 on: unchunked it measures 91.7%;
    the points over the old 90% are the cost of S2's facts (user,
    2026-11-06).

**Toolchain and configuration**
38. Pin an upstream llvm-project `main` SHA and bump early when it helps;
    no local llvm-project patches (work around in the library or
    diagnose); the G1–G8 workarounds stay (user; H2 §8 Q4, H3 §2.5, H4
    §2.6).
39. The toolchain is the Docker images `edgcpp/llvm-cir` → `edgcpp/cir-env`
    on the x86-64 host, where the runtime tests run (CallConvLowering is
    x86-64 SysV only) (user, 2026-10-09).
40. cpfe-cir defaults to `DEFAULT_INSTANTIATION_MODE=tim_used`; the test
    configs use the debug FE; `release-smoke` covers the shipping macros
    (user; H2 §2.12). The release FE macro pins come in three groups and
    are kept for the IL-semantic group (an `#error` guard enforces it) and
    decided one by one at M7 for the others (H3 §2.7).
41. Test runtime: libstdc++, libsupc++ and libgcc_s plus `edg_cir_rt`,
    never `edgrt`; gcc assembles and links; `DisableIntegratedAS` (H1
    §2.8).
42. Orphan killing and a 120 s per-program timeout are unconditional;
    cir-env uses EDG headers; Microsoft-mode variants are REQUIREMENTS NOT
    MET (user; H2 §2.11).
43. `--c` for C side files, in the CIR configurations only; edgdocker
    forwards `EDG_CPFE_DEFAULT_OPTIONS` only when set (user; H4 §2.11,
    §2.13).
44. The roughly 16 GNU builtins of v0 are accepted (user; H1 §8 Q1).

**M6 scope, support and security** (user, 2026-11-07)
51. `support/` is the single source of what the back end supports,
    rendered for the docs and the deck. Every gate:
    - updates the rows its change set affects;
    - runs `make_support.py --check --gate <id>`.

    `yes` needs a test that compiles and runs under CIR. Anything
    unproven is `unverified`. The thresholds are:
    - 0 `unverified` from M6.1a;
    - 0 `ignored` from M6.4a.
52. Threat model (`security.md` §1):
    - Source code and ECIL bytes are untrusted. The reader must be
      memory-safe on any bytes.
    - The facts in a well-formed stream are trusted like an object
      file's. A wrong fact may break the generated code, but never the
      library's memory safety.
    - The embedder and the environment are trusted. Release builds still
      carry no hook that writes files or skips the verifier.
    - Nothing is silently ignored.
53. Security audits:
    - **Agent.** `cir-security` runs on the Mythos model. It is read-only,
      except that it may run proof-of-concept inputs in a throwaway
      clang4 container.
    - **Schedule.** SEC-1 to SEC-4, plus a standing trigger on the reader,
      validator, C API, crash recovery, file I/O and stream header. At
      most one Mythos review per session.
    - **Severity** (`security.md` §2–§3):
      - A critical or high finding that matches a scheduled gap (a
        support row or a §4 observation with `planned`) takes that gate
        as its disposition.
      - Any other critical or high finding preempts the next session.
      - Every M6.x gate needs 0 unscheduled open critical or high
        findings; M6.12 needs 0 at all.
      - A medium finding needs a user-accepted disposition.
    - **Fallback.** cir-review, marked "non-Mythos". SEC-4 must run on
      Mythos.
54. A GNU item enters M6 only if standard headers need it (the builtins,
    atomics, visibility) or it is silent today. A silent item must stop
    being silent: implement it where that is cheap, otherwise diagnose
    it. Other GNU items wait for the extensions phase (`Ext`).
55. M6 scope and order:
    - builtins first;
    - full debug info and the hardening flags are in M6;
    - coroutines and C++20 modules move to after M7 (they stay diagnosed
      or rejected);
    - the sub-milestones are in §6.
56. The §8 answers (user, 2026-11-07):
    - **Rebase onto `main` first,** as M6.R before M6.1a.
    - **Changes.** The older CIR entries already in `src/Changes` (gates
      M0 to M5.1b) move into `src/pending_changes/*.txt` (in M6.R). Their
      GitHub tag stays open (`[]`) for now.
    - **RSS ceilings.** Tighten the C rows of the `rss` step to 1.25 times
      the M5.1 values. If the rebase's re-derived baselines move them, use
      those and report it.
    - **clang4 clean-up.** Remote `rm -rf` is denied to the agents and to
      the orchestrator: write a prune script and ask the user to run it.
    - **Mythos.** The model id is `claude-mythos-5-2` since 2026-11-12
      (`~/.claude/agents/cir-security.md`); SEC-1 ran on
      `claude-mythos-5-1`.
    - **Mangler defects** (§4.8: the lost qualifier of
      `C<T>::template tf<T>(x)`; `dn` where g++ writes `co`). Fix them
      for every back end, ungated as the M5.1 fix was, unless `main`
      already fixed them (check in M6.R; fix in M6.5).

**Process** (details in §7)
45. An Opus orchestrator, three Sonnet implementers with exclusive file
    ownership, and a read-only Opus reviewer (user; H1 §2.9). From M6,
    the Mythos auditor `cir-security` joins them (decision 53). From
    M6.1b the orchestrator also has a Fable advisor (the `advisor` tool;
    user, 2026-11-12); each plan names the points where it is called.
46. Only the orchestrator commits, once per reviewer-approved gate, on
    local `cir`; never push (user, confirmed each session; M5.1 plan).
47. Lean gates: per change the targeted checks; per gate the steps the
    change set can affect; the full gate only at milestones (user; H4
    §2.14).
48. Checkpoints run on a synced host tree; reviews read a frozen
    `build/review-snap-*` (H2 §2.13).
49. Prune anything not needed; anything we made may be deleted; never
    touch anyone else's files, trees or containers (user; H4 §8 Q4).
50. Each session fits one orchestrator context window; this file is the
    handoff (user, 2026-11-05).
57. Commit subjects follow main's policy (`edg-check-commit-messages`):
    they end in a tag list, `[]` until the work has a GitHub issue (user,
    2026-11-07; M6.R rewrote the 21 earlier subjects).
58. The cir-security sandbox (user, 2026-11-08): the agent reaches clang4
    only through the wrapper `edg-cir-sandbox`, whose `docker run` flags
    are fixed. A fail-closed PreToolUse guard
    (`~/.claude/hooks/cir-security-guard.py`) enforces that and confines
    its reads to the repository. It runs twice, as a user-level settings
    hook scoped to `agent_type` cir-security (user, 2026-11-08) and as the
    agent's frontmatter hook (which did not fire in the first tests, only
    after the definition reloaded); either one blocks. It logs every call
    with its mode.
    clang4's `authorized_keys` is unchanged: no forced-command key.
59. The answers of 2026-11-12 (user):
    - **Medium findings.** The proposed dispositions are accepted: SEC-1-6
      is triaged in M6.1b; P-2 and P-3 go to M6.4a with their rows.
    - **Extended floating types.** `_Float16`, `_Float128` and `bfloat16`
      move from M6.7 to M6.1c, right after M6.1b (the orchestrator's
      recommendation, as the user asked).
    - **Splits.** A session that grows may be split without asking; the
      user hears about it at the gate.
    - **P-7** is a front-end defect of every configuration. The user fixes
      it on `main` (report: `reports/`); the fix reaches `cir` by a
      cherry-pick or the next rebase. CIR sessions do not fix it. Done
      2026-11-12: GH #525 (PR head `341b1c2757`, not yet merged) is on
      `cir` as `b1c6c60d7c`; the next rebase drops it if `main` gets the
      same patch.
    - **Plans and scratch.** Each session's plan is a tracked file in
      `plans/`, written by a planning session that also tidies the docs
      and deletes the scratch of the finished sessions. Nothing a later
      session needs may live only in scratch.

### 2.1 Retired

| Was | Now |
| --- | --- |
| A no-exceptions v1 stream (H2 §2.2) | ECIL 1.2 exceptions (M4) |
| Prefer `release/24.x` (H1 §3) | Pin `main`, bump early (decision 38) |
| A libc flag in the FE builtin tables (H1 §8 Q3) | A separate generated header (decision 32) |
| Never commit `cir_design_guidance.md` (H1 §7) | Committed verbatim as `brief.md` (user, 2026-11-05) |
| Ask before deleting box trees that are not ours (H2 §8 Q1) | Decision 49; the old box is retired |
| The mangler issue is FE maintenance (H4 §4.2, §6) | CIR fixes it in M5.1 (decision 28) |
| Next: M5.1 or M6 (H4 §6) | M5.1 only (user, H4 §8 Q1) |
| attr-diff stays FAIL until the RISK rows are checked (H4 §4.1) | Decision 31 |
| REFS-driven lazy decode (design §3) | Library-side finish-only replay (decision 12); `REFS` stays specified and unused |
| The consumer drops C99 inline-only bodies (design §3) | available_externally at -O1 and up (H4 §1.2) |
| An unsupported construct is an error only if a definition references it and it is internal (design §3) | Decision 16 |
| A 60 s per-program timeout (H1 §3) | 120 s (decision 42) |
| Box `192.168.1.167`, the `cm-edgbox` socket | clang4 (§0, §3) |

## 3. Cookbook

Commands, gate steps, baselines, the lean-gate map and host hygiene are in
`cookbook.md`; read it on demand.

## 4. Known gaps

Items are one line each with their source; details live in the code and the
cited notes. "M5.1" marks an item the current milestone fixes (§6).

### 4.1 Diagnosed as unsupported (a named reason; the body or variable is dropped)

- The C list: VLAs; `_Complex`, vector types, `_Atomic`, `_BitInt`
  bit-fields; inline asm, statement expressions, computed goto; the alias,
  ifunc, weakref, cleanup, constructor, destructor and init_priority
  attributes; builtins without a mapping (`__sync_*`, `__atomic_*`, the
  setjmp family); `_FloatN` and other float formats; a wide string followed
  by elements, flexible-array-member initializers, a GNU range designator
  followed by more initializers (H1 §4; `ecil_type.c`, `ecil_code.c`,
  `ecil_const.c`).
- C++: `writer.md` §9.12 is the authoritative list (H2 §4); the M4-scope
  list is `research/il_to_ecil_map_m4.md` §13.2; library-side diagnoses
  and their tests are `research/m4_library_status.md` §2.
- How often (H3 §4, 528 C++ drop notes): vector 143, multiversioning 64,
  inline asm 56, float formats 37, vector builtins 22, alias/ifunc/weakref
  18, cleanup 17, address constants 11, init_priority 9, source_location 8,
  VLA 7, flexible-array init 7.
- Microsoft mode is skipped for cpfe-cir; native Windows edgy is
  unsupported (H1 §4, H2 §5; M7).
- A default member initializer that names another member through implicit
  `this`, in a non-constant aggregate initialization (local, `new`,
  return; designated or positional): "a reference to a parameter by
  position" (`ecil_code.c` `enk_param_ref` handles only
  `in_field_initializer`; found in M6.R, older than the rebase; M6.7).
- `__builtin_alloca_with_align` with an alignment above 128 bits or one
  that is not a constant (M6.1a; it used to be aligned to 16 silently;
  M6.2 sends the alignment).
- The copy of an array of class objects in an implicit, defaulted or move
  constructor (M6.1a; P-5: it used to be emitted without its source, so
  `struct S { std::string a[2]; }` copies crashed; the fix is M6.1b).
- C++23 units that include `<numbers>` (about 45 libstdc++ headers,
  `<iostream>` among them): its `_Float16`, `bfloat16` and `_Float128`
  constants are reported as errors although unused (header sweep, M6.1a;
  C++20 works). The writer cannot encode constants of those formats
  (`arg_float_value`, `ecil_const.c`), and the library reports every
  dropped unreferenced variable as an error, because its initializer may
  have had effects (`globals.cpp`; decision 16). M6.1c encodes them.

### 4.2 Silent (not diagnosed)

- `section`, `transparent_union` and `ms_abi` are ignored on supported
  entities (H1–H3 §4; verified 2026-11-05). The fix is a declaration fact
  in a 1.x minor. (`used` was closed in M5: supp key 11.)
- Found in M6.0 (M6.4a unless noted):
  - The visibility attribute is ignored. The supp keys `esk_section` and
    `esk_visibility` exist (`ecil_format.h:732`), but the writer never
    sends them.
  - `regparm`, `stdcall` and `fastcall` are ignored, as gcc and clang
    ignore them on x86-64 (both warn). The silent ABI hazards are
    `ms_abi`, and `transparent_union` with a floating first member
    (review 1).
  - `setjmp`, `_setjmp`, `sigsetjmp` and `vfork` are plain calls without
    `returns_twice`. `__builtin_setjmp` is diagnosed.
  - `-D_FORTIFY_SOURCE` is accepted, but `__builtin_object_size` folds to
    "unknown", so it gives no protection (M6.2).
  - eccp passes `-g` only to the assembling gcc, so there is no source
    debug info and no warning (M6.4a warns; M6.8 implements).
  - eccp rejects other unknown `-f*` options (`-fwrapv`,
    `-fstack-protector*`, `-fcf-protection`, `-fPIE`) with "unknown
    option" (`util/eccp.sh:2341`): diagnosed, not silent.
- Found by the M6.1a probes (`security.md` §5): `#pragma STDC FENV_ACCESS
  ON` is ignored (P-1, High; M6.1b makes it loud); the initialization
  store of a volatile object is not volatile (P-4, High; M6.1b); a C++
  `extern thread_local` is accessed directly, not through its `_ZTW`
  wrapper, so the dynamic initialization of a variable defined in a g++
  object is skipped (row `cxx11.thread_local_dynamic`, M6.7);
  `scalar_storage_order` and `trivial_abi` get only the FE's "unknown
  attribute" warning (P-2, P-3; §8).
- `#pragma GCC unroll`, `ivdep` and `novector`: since main's GH #174 the FE
  binds them to the next loop (`pk_gcc_next_statement`) and no longer warns;
  the writer drops them, so only the hints are lost (the stream is
  byte-identical to the pragma-free source; M6.R;
  `gnu_lang.pragma_loop_hints`, M6.4a).

### 4.3 Approximations

- `__builtin_object_size` and `__builtin_dynamic_object_size` fold to the
  "unknown" value (`ecil_code.c:3220-3240`; needs an object-size builtin
  and inlining) (H1 §4).
- `-fwrapv` is not plumbed: only the `EDG_CIR_WRAPV` environment variable
  (`ecil_write.c:249-258`) (H1 §4).
- No debug info; with chunking it will need one `DICompileUnit` per TU
  (`chunk.cpp` `copy_attributes`) (H1 §4, H4 §4.2; M6).
- `_Float16` and `_Float128` reach the library as half and quad; their
  constants are not encoded (§4.1). M6.1c (decision 59).
- `gnu_inline` in GNU C++ is always inline-only: the IL cannot say whether
  the definition is `extern` (H2 §4).
- The writer streams every header inline function, referenced or not; lazy
  emission removes the library cost (H2 §4, H4 §1.1).

### 4.4 Builtins and library facts

- No mapping for `__builtin_constant_p` (blocks libstdc++ algorithms and
  containers, Boost algorithm and range, the codequality `sort` kernel),
  `__atomic_*` (memory, regex, Boost format), or x86
  `__builtin_ia32_addcarryx_u64`/`_sbb_u64` (boost/multiprecision)
  (H4 §4.2; M6).
- `isascii`/`toascii` get attributes from LLVM inference only (not in
  clang's table) (H4 §4.2).
- Sized delete: the FE picks `_ZdlPvm` or `_ZdlPv` differently from
  clangir in 27 probe cases; not traced beyond D0 (H4 §4.2). The array
  new-expression cleanup calls unsized `_ZdaPv` where clang calls
  `_ZdaPvm` (`ecil_code.c` around 6495-6540, L-c).
- An extern `thread_local` access is treated as possibly dynamically
  initialized (impure); a local one is not (H4 §4.2).
- Without exceptions the writer marks NOTHROW as clang does (M5.1b,
  `ecil_format.md` 12.6) except destructor declarations under C++
  `--no_exceptions`, which clang marks (the safe direction; M6).
- GNU `nonnull` attributes do not reach the nonnull mask, and `memcmp` is
  not in the generated table: 16 + 4 checked attr-diff rows (M6).
- `__has_builtin(__builtin_operator_new/delete)` is `201802L` in every
  clang mode (M5.1b); clang 18 to 22 confirmed, clang 6 and earlier
  returned 1, the first version is unconfirmed.

### 4.5 attr-diff after M5.1b

attr-diff passes with 0 unexplained gated differences (M5: 539). Fixed:
`this`/reference dereferenceable (L1), alwaysinline (W2, L2), nounwind on
C declarations (W1), `builtin` on `_Znwm`/`_ZdlPvm` (W4, with the harness
at `--gnu-version 150000` for the image's g++ 15.3.1), the `use_facet`
result `noundef` (B2). `tests/.edgy/filters/cir-attr-known.txt` holds the
checked rows (decision 31), each with its reason:

| Rows | Disposition |
| --- | --- |
| nounwind and noreturn only ours on `abort`, `exit`, `std::terminate`, `__cxa_bad_cast`, `_exit` (declarations and C++ call sites) | True facts clangir's declarations lack |
| Call-site nounwind and noreturn only ours, in C | C streams are nothrow everywhere (`ecil_format.md` 12.6) |
| `align 8` vs 16 on `llvm.memset`, `llvm.memcpy` alignment, `memcmp` nounwind, GNU `nonnull` on `_Rb_tree_insert_and_rebalance`, `dereferenceable(0)` on references to functions | We claim less (M6 for `nonnull` and `memcmp`, §4.4) |
| `builtin` and `noundef` on new42's `delete[]` | Structural: for a constant `new T[0]` we emit the constructor loop and its cleanup |
| dereferenceable only clangir (`_Base_manager` helpers) | Likely `final` classes; a `FINAL` flag is an M6 option |
| Residue (not gated; 1913 at M5) | callset 1805; sret spelling 58; noalias on `_Znwm` 34; cold 10; uwtable 6. L2 inlines always_inline bodies away at -O0, so the pinned units' only-clangir symbols rose from 114 to 181 |

### 4.6 Library

- `.N` suffixes under chunking: at -O2 about 1.7% of corpus streams (forced
  to one function per chunk) differ only in the numeric suffix of local
  data an LLVM pass names; the pinned units are byte-identical (H4 §4.2).
- clangir's D0 trap: the deleting destructor of a class with virtual bases
  is `llvm.trap` in clangir; ours is a real body (H4 §4.2).
- Library builtins stay plain calls (30 pair rows), decision 35.
- Structor variants: C1/D1 where clangir calls C2/D2 (542 rows); no C2 for
  implicit inline constructors; extra linkonce vtable or D0 for classes
  without a key function (H3 §5, H4 §4.1).
- Entry-block allocas lose slot reuse and scoped lifetime markers; `agg.tmp`
  plus one `__retval` costs a copy per aggregate call or return (waits on
  upstream CallConvLowering NRVO) (H1 §5).
- The quadratic MLIR verifier at nesting depth (20k nested ifs about 49 s;
  not re-measured since the pin moved) (H1 §5).

### 4.7 Documented differences from g++ (clang-consistent)

Falling off a non-void C++ function traps; `return;` in a non-void
function returns an undefined value; init order of inline and template
statics follows clang; GNU mode does not extend the lifetime of a scalar
member of a temporary (`init/ref19`); C++17 assignment order follows the
standard; typeinfo of a class completed after a file-scope initializer's
first need is `linkonce_odr` where clang has internal; `link_error` tests
that need -O0 folding fail on the reference too (H2 §4, H3 §5).

### 4.8 FE issues found

- NRV only for variables in the function's outermost scope (`opt/nrv23`,
  `nrv26`, `eh/return1`) (H3 §4).
- The FE does not fold an address constant through a cast
  (`init/addr-const1`); a lambda captures `this` where gcc does not
  (`cpp1y/pr61636-2`); `friend72`; ilp32 `abi/bitfield9`
  (`research/il_to_ecil_map_m4.md` §13.4).
- `cpp1z/elide2` (DR1697): the FE marks the member temporary as an
  expression temporary; the reference fails too (H2 §5).
- `ipa/pr64049-2`: the test's second source is missing; environment (H3 §4).
- Found by W-b (2026-11-05): EDG rejects a dependent `tref<vt<T>,T>`
  reference template argument that g++ and clang accept; an internal
  error at `folding.c:6477` (`binary_operation`) on `(&vtg<T> != nullptr)`.
  It also folds `vt<int>` to `Lb1E` inside dependent conditions and adds a
  spurious `ad` on `CT<T>::template cv<T>`.
  Also, in every mode: `C<T>::template tf<T>(x)` loses its qualifier in
  the mangled name, and a pseudo-destructor is `dn` where g++ writes `co`
  (M5.1b). `main` has not fixed
  either (checked in M6.R: `lower_name.c` unchanged; `dn` is a defect in
  GNU mode only, clang writes `dn` too); M6.5 fixes them (decision 56).
  A class-type NTTP object mangles as `tlK1S` where g++ writes `tl1S`
  (`A<S{3,4}>`, M6.R; M6.5).
- Found in M6.1a: the FE does not implement P0963 (structured bindings in
  conditions), C23 `constexpr` in C, `_Decimal32`, or attributes on
  structured bindings (their rows were removed); in `--c++26` mode a
  reference bound to a reference parameter (found as by-reference
  structured binding packs, `auto &[...r]`) gave wrong values in every
  configuration (P-7, High; fixed by GH #525, on `cir` as `b1c6c60d7c`
  since 2026-11-12, decision 59;
  `reports/P-7-structured-binding-pack-by-reference.md`);
  `__has_include(<stdarg.h>)` is false with the EDG include directory. A
  referenced defaulted copy constructor of `std::__shared_ptr<_Dir_stack>`
  is not emitted under CIR, an undefined reference at link time (decision
  12 wants an error; M6.1b).

## 5. Needs re-look

**On every LLVM bump**
- Regenerate the library data and diff the NOTHROW set (H4 §5).
- Re-probe the INLINE_ONLY drop rule and the INLINE_HINT rules (CodeGen
  behavior probed at the pin) (H4 §5).
- `lowering-drift.test`: on failure read upstream's diff of
  `lowerDirectlyFromCIRToLLVMIR`, then update `lower_to_llvm_ir` and the
  chunk assumptions (H4 §5).
- Re-test the G1–G8 workarounds (`research/m4_library_status.md` §3) and
  the upstream watch: vtable 10000 virtuals 1.2 s; diamond 160 layers
  31.5 s and 4.2 GB; `new` chains 2500 17 s; unwind 4000 20 s; nested try
  500/1000/2000 = 1.5/13.8/208 s (CIRCanonicalize); override chain 2000
  26 s; FlattenCFG's array-element EH gap (`research/m4_stress.md`) (H3 §5).
- Route new upstream ops through `CIRCompat.h` (H1 §5).

**Facts the library cannot check** (`research/m4_library_status.md` §5)
- NOTHROW truthfulness (two sources: the FE predicates and the 231-name
  table; a wrong NOTHROW silently skips cleanups), NONTRIVIAL_ABI and
  NONTRIVIAL_DTOR, class facts against the source, linkage flags. Review
  whenever the writer changes them (H3 §5, H4 §5).
- User `pure`/`const` functions give `nounwind willreturn`; in C++ they can
  throw (clang does the same). `torture/pr101373` fails at -O0 for every
  compiler (H4 §5).

**Writer**
- The base-entry rule of a designated initializer (`ecil_const.c`
  178-207: base entries the FE lists after or between data members) is
  reached only with `--gnu_version` below 80000 in C++17 since main's GH
  #260 (`writer-cxx-designated-base-{last,mid}` run at 70400).
- The sampled name check (the first 2000 functions in full, then 1 in 16;
  `ECIL_CHECK_ALL_NAMES=1` for all) sees a mangler bug that only large TUs
  have late (H4 §5); the full check over the C++ corpus and two
  `bits/stdc++.h` units was clean after the M5.1b mangler change.
- The unit harness `unit/run_unit.sh` does not build on the Mac (headers
  lack `ec_cir_error`, BSD `od`); run it on clang4 with
  `UNIT_BUILD=build/gcc-cir CXX=g++`. `run_cases.sh` and `cases/*.dump`
  are stale. `asm_cmp.sh` and `perf_ir.sh` default to 12 jobs.
- The prefix bit `ecil_inline_specifier`: if the local-class keep is ever
  narrowed, check `template_pattern_has_inline_specifier` (H4 §5).
- `loop_condition` drops a comma's left operands only when
  `expression_is_pure`; revisit if that changes (H4 §5).
- Region keeps cost about 17% of FE peak on the template TU and no single
  reason is removable (`research/m5_writer.md` §6). The C++17 purity rule
  spills more than needed (a cost, not a defect) (H2 §5).
- The `builtin_defs.h` generator does not regenerate from checked-in data
  (`research/builtin_table_regen.md`; FE maintenance) (H2 §5).
- A C initializer differential fuzz against gcc (C11 6.7.9) was never done
  (H1 §5).

**Linkage**
- CWG2387, settled in M5.1b: under CIR, GNU mode 15 and later gives const
  variable-template instances external linkage, as g++ 15 and clang do
  (`GNU_CONST_VAR_TEMPL_INTERNAL`, `templates.c`); other configurations
  and older GNU modes keep internal linkage, and CIR writes no `L` for an
  internal instance in GNU mode. It stays CIR-only (decision 28a).

**Library**
- L2's call-site sort (`order_always_inline_uses`) is exercised only by
  the pinned chunk-identity runs; small lit streams already have use
  lists in program order (M5.1b).
- A new lowering that reads a callee's body must be told to the chunker
  (`inline_bodies_`, `chunk.cpp`; the catch-copy thunk is the one case)
  (H4 §5).
- `a_entity::op` points at erased operations after chunking; nothing
  dereferences it (H4 §5).
- Strict validation: the validator does not track operand types, so some
  contract checks live only in the emitter; dropped lazy bodies get only
  the reader's checks (H1 §5, H4 §1.1).

**Tests and gates**
- Since M6.R the CIR expectation files are deltas on `edg_x86_64` (main's
  expectation inheritance): an explicit `PASS` line where `edg_x86_64`
  fails and CIR passes, only for the cases the CIR configs run. Read them
  only through `edgtest.expectations` (`overlay_expectations`), never as
  full lists; a new reader that does reports PASS lines as failures.
- `imported/gnu/c/pr78468` is a stable FAIL(COMPILE) since M6.1a (the
  alloca diagnosis). A recording copy-back once dropped its line from
  `edg_x86_64_cir/imported.log`: check expectation diffs after every
  record.
- lit is not in the cir-env image, and the gate's `ctest` step does not
  run lit: the test is registered only with `-DEDG_CIR_LIT`, which only
  the `cir_src/scripts/box_*.sh` scripts pass (found 2026-11-12). Until
  M6.1b (item A3) fixes the step, lit runs by hand (`box_lit.sh`).
- `src/error_msg.txt` is append-only, so `cir_error` and `cir_warning`
  renumber on every rebase (3601 and 3602 since M6.R). Refer to them by tag
  only.
- Writer goldens check structure only; semantic coverage comes from
  `cmp_run.sh`, probes and the corpus (H1 §5).
- Recordings over 100 KB have no output coverage; `_2p` expectations exist
  for the smoke list only (H1 §5).
- The excused-crash heuristic excuses an FE assertion the writer triggers
  inside shared FE code if the reference aborts there too (H2 §5).
- The `-j` flake of `slow-below-threshold` (random streams about +0.3%,
  about 68K instructions) was the detached guard worker's thread exit
  racing process exit under valgrind; fixed in M5.1 (the worker is never
  woken or destroyed at exit, `pipeline.cpp`). Any new detached thread
  needs the same care.
- LeakSanitizer noise: "Unable to get registers from thread" under
  `linux-cir-asan` in containers is ptrace noise (H4 §5).
- Keep these gate-tool properties: `rto-diff --documented` attributes only
  the exact documented status and variant; an `edg-cir-fe-facts` timeout
  fails; container labels are unique per run (H3 §5).

**Spec leftover** (H1 §5): the `va_list` role is never emitted.

## 6. Roadmap

### M5.1: done (2026-11-06)

Three sessions. Session 1: this document set, clang4, the re-derived baselines.
Session 2 (M5.1b): attr-diff facts (L1, L2, B2, W1, W2), the ungated mangler
and `__has_builtin` (W3, W4), CWG2387 under CIR. Session 3 (M5.1): the
size-based default chunk decision (L3, decision 14); the test hook
`EDG_CIR_TEST_NO_ALWAYS_INLINE`, which attributed every M5.1b asm
difference (C: 14 files L2; C++: 57 tests = 43 L2 + 14 W3/CWG2387, the
changed names equal g++ 15's); the guard-worker exit fix (§5); identity
moved to `identity-m51b`, the allow-list emptied; day-base re-taken at
`4902ad4e5e` (pre-rebase; now `ac210ca02b`); the R2/R3 tooling follow-ups; S1 met (§0).

### M6: remaining gaps and documentation

M6 is every gap that does not fit an earlier milestone, plus the user and
developer documentation (user, H2 §6). It was planned on 2026-11-07 in
the meta session M6.0. The scope and order are decision 55.

**Rules for the sub-milestones**

- **One session each**, about 16 to 20 sessions in all. A session that
  overflows or grows splits into a/b without asking, and the user hears
  about it at its gate (decision 59).
- **Plans.** Each session runs from `plans/<id>-<name>.md`. Where a plan
  and the table below differ, the plan wins, and the gate updates the
  table.
- **Evidence.** Each gate updates the `support/` rows it changes
  (decision 51).
- **ECIL 1.4.**
  - The orchestrator owns it.
  - It opens at M6.2 with a reviewed contract change and stays
    additive.
  - Options that change meaning travel in the stream header (C API 3 if
    needed).
  - It freezes at M6.12.
- **Rebase first** (decision 56). M6.R rebases `cir` onto `main`
  before any other M6 work. Its re-derived baselines replace
  `m51-base-*`.
- **Pin.** M6.1b takes an inventory of the CIR ops the pin has. At most
  one bump follows, before M6.3. It re-derives the baselines (the
  re-look list is §5).
- **Security.** The audits SEC-1 to SEC-4 and their policy are
  `security.md` §3 (decision 53).

Done: M6.0 (2026-11-07), M6.R and M6.1a (2026-11-08; §0, §9). Next: M6.1b.

| Id | Theme | Items | Exit |
| --- | --- | --- | --- |
| M6.0 | Plan, support table, security model | This section; `support/`; `security.md`; the `cir-security` agent | `--check` passes; Sphinx `-W` builds; cir-review approves |
| M6.R | Rebase onto `main` | Rebase the local branch `cir` (never pushed) onto `main` and resolve the conflicts (the CIR-gated hooks, `cmd_line.c`, the tooling, `src/Changes`). Move the CIR entries of M0 to M5.1b into `src/pending_changes/*.txt` with open tags; `edg-collect-changes --check` passes except for the open tags. Check whether `main` already fixed the two §4.8 mangler defects. Re-derive every baseline on clang4 (base, identity, day-base) and tighten the RSS C rows (decision 56). Prune the listed clang4 trees. If it overflows, split into a/b. | The full milestone gate on the rebased tree; differences from M5.1 are attributed to `main`'s changes |
| M6.1a | Probes and negative tests; SEC-1 starts | First the clang4 sandbox wrapper for cir-security (`security.md` §3). Then: the mid-list designated base fix from M6.R (§5 Writer; patch ready); a diagnostic for `__builtin_alloca_with_align` above 16 bytes, before the gate (§5, `pr78468`); one probe per `unverified` row in `tests/tests/cir/features/`, including structured bindings in conditions (writer.md §9.12 calls them unsupported, but no diagnose site exists); a negative test for every `no` and `partial` row; a libstdc++ header sweep (self-checking usage tests at -O0 and -O2); a `support-sweep` gate step. Reject `--set_flag=coroutines` under CIR: today it bypasses `COROUTINE_ENABLING_POSSIBLE` (`cmd_line.c:2533`, honored at :3148), and only the writer's catch-all (`ecil_code.c:10180`) stops it. The checker follow-ups of review 2: per-case type overrides, `xfail` mixed-tu cases, the content of negative tests, only FAIL(COMPILE) or a link failure as `failing` on `no` rows, the mode rule on `partial` rows. | `make_support.py --check --gate M6.1a` passes; the sweep is in the gate |
| M6.1b | SEC-1 triage, housekeeping, fuzzing | The items are in `plans/M6.1b-sec1-triage-housekeeping-fuzzing.md` (L1 to L7, W1 to W7, A0 to A9), with its corrections to the earlier text of this row. In short: the Mythos delta review of M6.1a's reader, validator and emitter changes (standing trigger); fixes for SEC-1-3, SEC-1-4, SEC-1-5, P-1 and P-4, and the real fix of P-5, all inside ECIL 1.3 (what needs a format change moves to M6.2); `EDG_CIR_TEE` and `EDG_CIR_NO_VERIFY` behind `EDG_CIR_TEST_HOOKS`; `-z nodelete`; lit in the gate's `ctest` step; a UBSan preset and the fuzz harness (two targets); the review follow-ups; the `std::__shared_ptr` copy constructor that is not emitted (§4.8); the inventory of the pin's ops; the scope of M6.1c; the SEC-1-6 triage. If it overflows, UBSan and fuzzing move to M6.1c. | 1 clean fuzz core-hour; the register is triaged |
| M6.1c | Extended floating types | `_Float16`, `_Float128` and `bfloat16` (decision 59): encode their constants, map the types to LLVM's half, bfloat and fp128, and diagnose what the pin cannot lower; scoped by M6.1b (W5). The float kinds are already in ECIL 1.3; if it needs a format addition after all, it becomes M6.2b. Also whatever M6.1b cut. | The C++23 header sweep no longer fails on `<numbers>` |
| (M6.Pb) | Pin bump, only if needed | Decided by the M6.1b inventory; before M6.2 | Gate against the new baselines |
| M6.2 | Builtins the optimizer evaluates | ECIL 1.4 opens. `__builtin_constant_p` → `llvm.is.constant`. `__builtin_object_size` and `__builtin_dynamic_object_size` → `llvm.objectsize`. FORTIFY probes (`__builtin___*_chk`, `__builtin_va_arg_pack`, `gnu_inline`). The placement-new form Boost container needs. | The sort kernel builds; the FORTIFY probes abort; the real-world unit count rises (6/15) |
| M6.3 | Atomics | `__atomic_*`, `__sync_*`, C11 `_Atomic`, memory orders. `__builtin_clear_padding`. The x86 add-with-carry builtins. | `<atomic>`, `<memory>` and `<regex>` pass the sweep; Boost format and multiprecision build |
| M6.4a | Nothing silent; SEC-2 starts | `section`, visibility and `transparent_union`: implement where cheap, otherwise diagnose; must-understand in 1.4. Diagnose `ms_abi`. `returns_twice` for the setjmp family and `vfork`. `-fwrapv` and `-fno-strict-overflow` as options. eccp warns on every gcc codegen flag it ignores (CIR-gated), for example `-g` until M6.8 and `-p`/`-pg`. | 0 `ignored` |
| M6.4b | Hardening codegen | `-fstack-protector*`, `-fstack-clash-protection`, `-fcf-protection`, `-ftrivial-auto-var-init`. If auto-var-init does not fit, ask the user about M7. | Hardening rows green, or diagnosed |
| M6.5 | Code quality, attributes, mangling | `llvm.mem*` intrinsics (revisits decision 35). GNU `nonnull` into the mask. `memcmp` facts. `FINAL`. The 27 `_ZdlPvm`/`_ZdlPv` cases and `_ZdaPvm`. Destructor NOTHROW under `--no_exceptions`. Extern `thread_local` purity. Clang-mode `sr`. The §4.8 mangler defects, fixed for every back end unless `main` already has them (decision 56). | Fewer attr-diff checked rows; codequality 1.00× |
| M6.6a | `_Complex` | Arithmetic, conversions, the SysV ABI through CallConvLowering | `_Complex` rows green |
| M6.6b | Other C gaps | VLAs, `_BitInt` parameters and bit-fields, a wide string followed by elements, address constants through casts, the `va_list` role. Optional: the "unsequenced" diagnostic. GNU range designators and flexible-array initializers are `Ext`. | Those rows green |
| M6.7 | Standard C++ gaps | Namespace-scope `thread_local` with dynamic initialization, `std::source_location`, `__builtin_bit_cast`. The writer gaps the header sweep found: a class conditional that does not initialize a variable (`std::map`), a delegating constructor that does not call a constructor (`chrono`), an anonymous-union member with a destructor (`std::format`), an assignment to a class with reusable tail padding (`std::regex`). The gaps in `new (std::nothrow)` and array destroying delete (the array-of-class copy moved to M6.1b, P-5). Constant initialization of a `[[no_unique_address]]` member. (The extended floating types moved to M6.1c.) | Those rows green |
| M6.8 | Debug info I | Design first (library work; skippable supp keys; chunk identity, decision 13). Then `-g` in the stream header, line tables, one `DICompileUnit` per TU, `-fdebug-prefix-map`. | gdb steps through the smoke tests |
| M6.9 | Debug info II | Types, variables, scopes; a gdb harness | gdb prints locals and classes |
| M6.10 | Correctness and robustness | Csmith or YARPGen differential runs against gcc at -O0 and -O2. The C initializer differential fuzz (§5). Operand types in the validator. | No unexplained differences |
| M6.11 | Documentation; SEC-3 starts | User guide in `doc/source/cir_be.rst` (use, options, the support table, diagnostics, limits, security); developer guide from `design.md` and `cookbook.md` | `make -C doc dirhtml SPHINXOPTS="-W"` passes; cir-review approves |
| M6.12 | SEC-4 and the M6 gate | Release audit and its fixes; 1.4 freezes; the full milestone gate | Full gate; 0 open critical or high findings |

### M7: release and platforms

Microsoft mode, native Windows, the release FE macro pins (decision 40),
macOS universal and `-static` builds; AArch64 and other targets once
upstream CallConvLowering supports them (H1 §6, H2 §6). Also: what a
release build of `libedg_cir` is (then `EDG_CIR_TEST_HOOKS` defaults to
OFF).

### After M7

Coroutines (EDG's C-generating back end cannot do them either) and C++20
modules (`cpfe-cir` rejects `--modules`) (decision 55).

### Extensions

After standard C++: GNU extensions, then MSVC extensions (brief). Each
needs its own format addition and review (H1 §6, H2 §6).

## 7. Process

### 7.1 Team and ownership

Agent types: `cir-config`, `cir-writer`, `cir-lib` (Sonnet implementers),
`cir-review` (read-only Opus reviewer) and `cir-security` (read-only Mythos
auditor), defined in `~/.claude/agents/cir-*.md`. Continue an agent with
`SendMessage`. The definitions do not name the checkout: every brief gives
its absolute path (§0). The orchestrator's Fable advisor is the `advisor`
tool; it takes no parameters and reads the orchestrator's transcript.

| Owner | Files (exclusive write) |
| --- | --- |
| orchestrator | `cir_src/doc/{brief,project,design,ecil_format}.md`, `cir_src/doc/archive/**`, `cir_src/doc/{research/README.md,cookbook.md}`, the contracts `cir_src/include/edg_cir/{ecil_format.h,edg_cir.h}`, `cir_src/LLVM_REVISION`; outside the repo `~/.claude/agents/cir-*.md` and the memory notes |
| cir-config | `dev_tools/bin/{cpfe-cir-2p,edg-cir-*}`, the CIR files in `tests/.edgy/**`, `tests/expectations/edg_x86_64_cir*/**` and any non-CIR `tests/expectations/edg_x86_*` re-recording an approved ungated FE change needs, `bases/docker/cir-env/**`, `dev_tools/docker/{llvm-cir,cir-env}/**`, the CIR helpers in `dev_tools/pylibs/edgdocker/__init__.py`, the CIR hunks of `util/eccp.sh`, the CIR options in `src/cmd_line.{c,h}`, the clang4 trees and images |
| cir-writer | `src/ecil_*`, `src/cir_be.*`, the CIR-gated FE hooks (`mem_manage.c`, `scope_stk.c`, `fe_wrapup.c`, `error.c`, `class_decl.c`, `decl_spec.c`, `decls.c`, `symbol_tbl.{h,c}`, `mem_tables.h`, the CIR helpers in `lower_name.c`, the triple call in `fe_init.c`); in M5.1 also `attribute.c`, the `lower_name.c` mangler, the `macro.c` `__has_builtin` values (ungated) and the CWG2387 macro in `templates.c`; `dev_tools/builtins/generate-libc-builtins.py` and `src/builtin_libc_attrs.h`; `cir_src/test/writer/**`, `tests/tests/cir/writer/**`, the new mangling test under `tests/tests/edg/`, `cir_src/doc/writer.md` |
| cir-lib | `cir_src/**` except the orchestrator's files, `cir_src/test/writer/**` and the M6 carve-outs below; `tests/tests/cir/cirgen/**` |

M6 carve-outs (user, 2026-11-07; the `features/` split and the sandbox, 2026-11-08):

- **Orchestrator:** `cir_src/doc/{security.md,support/README.md}`, `cir_src/doc/{plans,reports}/**` (user, 2026-11-12) and `cir_src/doc/exsum/**` except `exsum/support.svg`; outside the repo `~/.claude/hooks/*cir_security*`, `~/.claude/hooks/cir-security-*` and the cir-security PreToolUse entry in `~/.claude/settings.json` (deleting it silently turns the guard off).
- **cir-config:**
  - `cir_src/doc/support/make_support.py` and its rendered outputs: `support.md`, `doc/source/cir_support.inc` and `exsum/support.svg`.
  - `doc/source/cir_*.rst`, `doc/source/_static/cir_support.css`, and the CIR lines in `doc/source/{conf.py,index.rst}`.
  - `tests/tests/cir/features/libstdcxx/**` and `cir_src/doc/support/libstdcxx.json` (user, 2026-11-08).
  - `dev_tools/bin/edg-cir-sandbox` and its install and scratch on clang4 (`~/cir-sec/`, `~/cir-sec-scratch/`).
- **cir-writer:** `cir_src/doc/support/cxx.json`; `tests/tests/cir/features/cxx/**` (user, 2026-11-08).
- **cir-lib:** the other `cir_src/doc/support/*.json`; `tests/tests/cir/features/{c,gnu,builtins,codegen,hardening,platforms}/**` (user, 2026-11-08).
- **cir-security** (read-only, Mythos): nothing.

Changes entries: since M5.1 they follow main's workflow (user,
2026-11-06): one file per entry in `src/pending_changes/*.txt` (main's
`CONTRIBUTING.md` "Changes Entries"; checked with main's
`edg-collect-changes --check`), never a direct edit of `src/Changes`; one
CIR entry per gate, written by cir-config (cir-writer adds to it).
Ungated FE changes get their own ordinary entries written as release
notes. A CIR entry has no GitHub issue yet, so its tag block is `[]`,
which main's checker rejects until it names a `GH #N`. The older CIR
entries move into pending files in M6.R; their tags stay open (decision
56). Every `Changes` file and entry stays valid UTF-8.

### 7.2 Rules

- AGENTS.md and `doc/source/code_style.rst` apply (`cir_src` uses the EDG
  style adapted to C++17; MLIR calls stay camelCase).
- Implementers stop and report any question that changes the contract,
  changes FE behavior outside CIR gating, touches shared tooling, changes
  scope, or has more than one reasonable answer. The orchestrator batches
  these for the user.
- Prototypes and spikes stay out of every commit.
- Never touch the git stash or files another session left in the working
  tree; stage explicit paths only.

### 7.3 Reviews

- Every review reads a frozen snapshot under `build/review-snap-*`.
- Every slice and every hardening round gets an incremental review; fixes
  are re-reviewed; the final review covers only the delta since the last
  one.
- Run the ASan preset for every library change; measure the release FE.

### 7.4 Context budget (every session)

- The orchestrator reads `brief.md` and this file (§6 holds the session's
  items and gate criteria); `cookbook.md` and anything else on demand, in
  line ranges.
- Assignments point at files and sections instead of pasting them.
- Agent reports are at most 40 lines (outcome, files changed, commands
  with one-line results, open issues); details go to
  `.tmp/<milestone>/s<N>/<agent>-<item>.md` (not committed).
- cir-review's report is at most one page; pass its path on, do not
  restate it.
- The orchestrator reads only the step table and summary of gate
  evidence; the owning agent triages its own failures.
- Long jobs run detached on clang4 with exit files; wait with a monitor,
  never by polling in the conversation.
- Per session: one gate commit (session 1 of M5.1: two), at most three
  reviews by cir-review plus at most one by cir-security (decision 53),
  at most two fix rounds per assignment before asking the user. The
  advisor is called at the points the plan names, with the deciding facts
  stated in the conversation first.
- From M6, scratch notes go to `.tmp/m6/s<N>/` in the `cir` checkout
  (M6.1b is `s3`). They are deleted when the next session is planned
  (decision 59): what must survive goes into a tracked doc at the gate.
- When context runs low: write `.tmp/<milestone>/s<N>/resume.md` (state, running
  jobs, next steps) and an "in progress" line in §0 first.

## 8. Open questions for the user

None. The M6.1a questions were answered on 2026-11-12 (decision 59).

## 9. Latest gate evidence

**M6.1a** (lean gate, clang4, tree `m6r-int`; evidence
`m6r-int/.tmp/checkpoint-20261108-134910`, asm in
`m6r-int/.tmp/gate-asm/checkpoint-20261108-134910` and
`gate-cpp-asm/checkpoint-20261108-134910`). `edg-cir-checkpoint --lean
writer,fe-options,shared-src,library,tests,tests-triage,goldens,tooling,support
--lean-base 70b1a2e326` against `m6r-base-*` (fingerprint `93009c5f…`,
equal on both hosts; images equal to the baselines'). 20 of 22 steps
passed outright: build, identity (720 files, 0 problems), isolation,
cir-suite (798/798 in both configurations), corpus (1663/2561, the one
change `pr78468`, triaged), modes, writer-stress, day and day32 (45492
cases per configuration, identical), ctest (4 presets), cpp-corpus
(1472/1823), cpp-modes, cpp-writer-stress, poison, release-smoke (40/40,
98/98), symbols, mixed-tu (70/70 with g++ and clang), opt-corpus (no
unattributed change; the `pr78468` triage line is gone), tools-test (358),
support-sweep (820 cases of 576 tests in `edg_x86_64_cir`, 593 of 349 in
`_2p`; ctest, mixed-tu and `make_support.py --check --gate M6.1a` clean).
Attributed: asm-diff (2563 identical, 0 other differences) and
cpp-asm-diff (1956 identical) fail only on status changes, which the step
does not allow: `pr78468` (the alloca diagnosis) and the
`-DTEST_NUMBER=2` cases of `edg/rma/ctor_init26` and `edg/rma/init33`,
which already failed and now stop at the array-copy diagnosis (§4.1). No
corpus test uses `__thread` in a way that changes its assembly. The next
gate shows the same three changes until the asm baselines move past M6.1a.
After the gate only data and docs changed (R3's follow-ups:
the P1061 row to `ignored`, notes, docs); `make_support.py --check --gate
M6.1a` was re-run on them. The session's scratch (the gate report, the
three reviews, the resume note) was deleted on 2026-11-12; the evidence
directories on clang4 remain, and the SEC-1 report is
`.tmp/m6/s3/inputs/sec-1.md` in the `cir` checkout. M6.R's evidence is in
git (`70b1a2e326`, this section).
