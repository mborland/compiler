# Security of the CIR back end

This file holds the threat model, the audit schedule and the findings
register of the ClangIR (CIR) back end. The orchestrator owns it. Its
policy is project.md decisions 52 and 53. Reproducers and proof-of-concept
inputs stay in `.tmp/m6/` and are never committed.

## 1. Threat model

| Asset or input | Trust | Requirement |
| --- | --- | --- |
| Source code compiled by `cpfe-cir` | Untrusted | Neither the FE writer nor the library corrupts memory, recurses without bound, or hangs without bound on any input the FE accepts. Unsupported constructs are diagnosed (decision 16), never guessed. |
| ECIL bytes given to the reader (a two-process `.ecil` file, or `edg_cir_process_*`) | Untrusted as bytes | The reader and validator are memory-safe on arbitrary bytes. Malformed input is a format error, never undefined behavior. |
| Facts in a well-formed ECIL stream (NOTHROW, nonnull, noundef, dereferenceable, `CAST_NONNULL`, linkage) | Trusted, like the contents of an object file | A wrong fact may make the generated code wrong. It must never make the library memory-unsafe. The writer's facts are reviewed whenever it changes them (project.md §5). |
| The embedder of `libedg_cir` and the process environment | Trusted | Release builds still carry no hook that writes files or skips the verifier. The C API states its lifetime rules, including whether the library may be unloaded. |
| Generated code | Its users rely on it | No construct and no hardening flag is silently ignored: it is honored, or it is diagnosed. Hardening reaches parity with the distribution defaults (M6.4b). |
| The toolchain and the gate tooling | Trusted after review | The pinned llvm-project SHA; base images pinned by digest; scripts that run tests on clang4 confine them (timeouts, orphan killing, no network where possible). |

Not in scope: denial of service by inputs that are legitimately huge (the
upstream scaling limits in project.md §5 are tracked as performance items);
a malicious embedder; Microsoft mode and other targets until M7.

## 2. Severity rubric

| Severity | Meaning | Examples |
| --- | --- | --- |
| Critical | Memory corruption or code execution reachable from untrusted source or ECIL bytes; a silent miscompile that removes a safety check in correct programs | A reader out-of-bounds write; a wrong `nonnull` that deletes a null check |
| High | A silent miscompile of a correct program that does not remove a check | Missing `returns_twice` on `setjmp`; an `ms_abi` or `transparent_union` call with the wrong ABI; a wrong memory order |
| High | Memory-safety read, unbounded resource use from small inputs, a hardening flag silently ignored, or a crash recovery that hides corruption | An out-of-bounds read in the validator; `-D_FORTIFY_SOURCE` giving no protection without a warning |
| Medium | Robustness or hardening gaps that need an unusual setup or a trusted party's mistake | A debug environment hook that writes a file in release builds |
| Low | Defense in depth | Storing an explicit end bound where an invariant already holds |

Each finding gets a CWE id where one fits.

## 3. Audits

The auditor is the agent `cir-security` (`~/.claude/agents/cir-security.md`,
on the Mythos model; the user supplies the model id). Its scope:

- It reads a frozen `build/review-snap-*` and never edits anything.
- It may build and run proof-of-concept inputs only in a throwaway
  container on clang4. The container runs from the `cir-env` image, with
  `--rm --network none`, CPU and memory caps, under `nice -n 10`, on a
  copy of the snapshot. It never touches shared trees or other people's
  containers.
- **Before SEC-1 (M6.1a), the sandbox becomes a wrapper on clang4.**
  Membership in the docker group is root-equivalent on that shared host, so
  instructions alone are not enough. cir-config owns the wrapper
  (`dev_tools/bin/edg-cir-sandbox`), and the agent may call only it.
  - Enforcement (user, 2026-11-08; project.md decision 58): a PreToolUse
    guard (`~/.claude/hooks/cir-security-guard.py`, the orchestrator's; it
    runs as a user-level settings hook scoped to `agent_type` cir-security
    and as the agent's frontmatter hook, and either one blocks) allows
    only Read, Grep and Glob inside the repository, plus a restricted local
    `grep`, since this harness has no Grep or Glob tools (paths checked as
    Claude Code expands them: no `~`, no `..`, no surrounding white space,
    known input keys only), a few read-only commands, and exactly one ssh
    form that calls the wrapper, and only while the orchestrator's flag
    file `cir-security-sandbox.enabled` exists. It logs every call. Its
    fixtures are `~/.claude/hooks/test_cir_security_guard.py`; canaries
    prove it before each audit. clang4's `authorized_keys` is unchanged.
  - Limits of the guard. It fails closed on its own errors and has its
    own 10 s time limit, but a hook *timeout* lets the call through, and a
    call whose hook input carries no `agent_type` is not checked (both are
    Claude Code behavior). An allowed call still goes through the normal
    permission checks. The audit log is the cross-check.
  - The wrapper takes no docker options from its caller. A PoC script
    arrives on stdin; each run gets a fresh directory, and the wrapper
    reads results back without following symlinks. The container has two
    mounts: the built snapshot (read-only) and the run's own work
    directory. A run is killed when the scratch filesystem's free space or
    inodes run low or the run's growth passes its cap.
  - Flags: `--rm --network none --pids-limit --cap-drop ALL --security-opt
    no-new-privileges --user --init`, CPU and memory caps, and a `timeout`
    inside the container, so a killed client cannot orphan it.
  - Mounts: no docker.sock. Two host mounts: the built snapshot
    (read-only) and the run's work directory; the toolchain is the
    image's own.
  - It documents how the snapshot reaches clang4.
- It reports each finding confirmed, with its reproducer.

If Mythos is unavailable, cir-review runs the audit with the same brief,
and the register marks that audit "non-Mythos". SEC-4 must run on Mythos.

| Audit | When | Scope |
| --- | --- | --- |
| SEC-1 Baseline | At the start of M6.1a, on a snapshot of the rebased tree (after M6.R); triaged in M6.1b | The whole surface: the reader and validator; the C API (lifetime, dlclose); crash recovery (decision 15: audit it, do not change it); file I/O and environment hooks; trusted facts; the FE writer on untrusted source (count truncation, recursion); `util/eccp.sh` and `cpfe-cir-2p`; the gate tooling on clang4; the supply chain |
| SEC-2 Semantics | At the start of M6.4a | The builtins and facts of M6.2 and M6.3 (`llvm.is.constant`, `llvm.objectsize`, memory orders, `__builtin_clear_padding`); the M6.1b fixes; the fuzzer's findings |
| SEC-3 Format | At the start of M6.11 | The reader delta of ECIL 1.4 (builtins, atomics, must-understand facts, the C gaps, debug supp keys); the M6.10 validator; the M6.4b hardening output; DWARF privacy (paths, prefix maps) |
| SEC-4 Release | M6.12; it blocks the M6 gate | Everything since SEC-1; the security claims in the docs; the supply chain again |

**Standing trigger.** These areas get a delta review by cir-security:

- the reader and validator;
- the C API;
- crash recovery;
- file I/O;
- the stream header.

A session gets at most one Mythos review. A delta made in the same session
as a scheduled audit is reviewed at the start of the next session, or
folded into the next scheduled audit.

**Policy.**

- **Scheduled gaps.** A finding that matches a gap already scheduled has
  that gap's planned gate as its disposition, and is tracked to it. The
  scheduled gaps are a support table row (`ignored`, `no` or
  `unverified`, with `planned`) or an observation in §4. Example: the
  `_FORTIFY_SOURCE` gap is High, and M6.2 closes it.
- **Preemption.** Any other critical or high finding preempts the next
  session.
- **Gates.**
  - Every M6.x gate needs 0 open critical or high findings without a
    scheduled disposition.
  - The milestone gate (M6.12, the full gate) needs 0 open critical or
    high findings at all.
- **Medium findings** need a disposition the user accepts.
- Every fix gets a regression test, normally the reproducer turned into a
  test by the owning implementer.

## 4. Observations before SEC-1

These come from the M6.0 research (three Sonnet agents, checked by
cir-review). They are input for SEC-1, not findings.

| Area | Observation | First estimate |
| --- | --- | --- |
| Fuzzing | No fuzz harness exists. 491 fixture files test malformed input, but nothing mutates bytes. The harness must run with `EDG_CIR_NO_GUARD`, because crash recovery turns crashes into diagnostics (M6.1b). libFuzzer is not in the llvm-cir image; the `EDG_CIR_FORMAT_ONLY` build needs no LLVM. | High (missing assurance) |
| Validator | It does not track operand types, so some contract checks live only in the emitter (project.md §5). Memory safety on arbitrary bytes rests on the emitter, the always-on MLIR verifier and an assertions-enabled LLVM (M6.10). | Medium |
| Generated code | `__builtin_object_size` folds to "unknown", so `-D_FORTIFY_SOURCE` gives no protection (M6.2). There is no stack protector, CET or stack-clash protection (M6.4b). `-fwrapv` cannot be set from the command line (M6.4a). `section`, visibility, `transparent_union` and `ms_abi` are ignored without a diagnostic. `setjmp`, `sigsetjmp` and `vfork` get no `returns_twice` (M6.4a). eccp passes `-g` only to the assembler step, silently. eccp rejects other unknown `-f*` flags (`util/eccp.sh:2341`), which is diagnosed, not silent. | High |
| Environment hooks | `EDG_CIR_TEE` (`session.cpp:303`) opens any path with `fopen(path, "wb")` in every build. `EDG_CIR_NO_VERIFY` (`pipeline.cpp:487`) skips the verifier. Neither is behind `EDG_CIR_TEST_HOOKS`, whose default is ON; only `linux-cir-release` turns it off (M6.1b gates both; the default flips once M7 defines a release build). | Medium |
| C API | Nothing says "never dlclose" and there is no `-z nodelete`, but a detached worker thread and the process-global handlers outlive any session (M6.1b). The header says decoding runs on the caller's thread, while `session.cpp` decodes on the worker. | Medium |
| Crash recovery | It longjmps out of signal handlers (LLVM `CrashRecoveryContext`, decision 15), and it changes sigactions without atomicity. A poisoned session is leaked on purpose. | Audit item |
| Reader | `next_array_element` re-decodes elements that `decode_operand` already bounded (cir-review: safe). The 1 GiB `max_frame` buffers only as bytes arrive, so it cannot amplify memory. | Low |
| Supply chain | `edgcpp/dev-env:latest` and `fedora:43` are not pinned by digest. | Medium |
| Two-process files | `cpfe-cir-2p` uses `mktemp`. `src/ecil_sink.c` opens `--ecil_output` with `O_CREAT\|O_TRUNC` and no `O_NOFOLLOW`, which is normal for a compiler output path. | Low |

## 5. Findings register

SEC-1 ran on Mythos (`claude-mythos-5-1`) on 2026-11-08 against the M6.R
tree (`70b1a2e326`); its report and reproducers are in `.tmp/m6/s2/`
(`sec-1.md`); its line numbers are those of the audited tree. `P-n` are
findings of the M6.1a feature probes. "Proposed"
dispositions are triaged in M6.1b (§3); a medium finding needs the user's
disposition.

| Id | Severity | CWE | Area | Summary | Status | Fixed in | Test |
| --- | --- | --- | --- | --- | --- | --- | --- |
| SEC-1-1 | Critical | CWE-121 | Library, ECIL bytes | `string_attribute` writes 16 bytes into a `uint64_t` for a `C_STRING` with 128-bit elements (`globals.cpp:978`); the validator accepts the stream; a release build exits 0 | Fixed (user, 2026-11-08) | M6.1a | `cir_src/test/cirgen/bad-c-string-int128.test`, `cir_src/test/format/fixtures/bad_c_string_int128.txt` |
| SEC-1-2 | Critical | CWE-758, CWE-476 | Library, trusted facts | The nonnull mask is shifted by 64 or more for a function with 64 or more parameters, so parameter 63 of a function that returns a reference gets `nonnull`; -O2 deletes its null check; reachable from C++ source (`signature.cpp:189`, `:326`) | Fixed (user, 2026-11-08) | M6.1a | `cir_src/test/cirgen/sec1-nonnull-param64.test`, `tests/tests/cir/cirgen/cxx-sec1-nonnull-param64.sft.cpp` |
| SEC-1-3 | High | CWE-770 | Library, ECIL bytes | `C_REPEAT` expands to the array count with no bound (`globals.cpp:1123`, `:1174`): a 134-byte stream requests 8 TiB | Open; proposed M6.1b | | |
| SEC-1-4 | High | CWE-20 | Validator | Unchecked layout numbers (alignments, bit-field widths, record alignment, locals of function type, zero initializers) reach LLVM assertions; a release `cir_src` build compiles out the header-inlined ones (`llvm::Align`, `APInt`), so there they are undefined behavior (raised from Medium by review R2) | Open; proposed M6.1b | | |
| SEC-1-5 | High | CWE-407 | Validator | No bracket-depth limit: a 200 KB stream of 100,000 nested blocks runs for more than 300 s (§2: unbounded resource use from a small input; raised from Medium by review R2) | Open; proposed M6.1b | | |
| SEC-1-6 | Medium | CWE-250 | Gate tooling | edgdocker runs the test containers `--privileged` as root with the tree mounted (`edgdocker/__init__.py:451`; main's shared tooling) | Open; M6.1b triage (user, 2026-11-08); needs the user's acceptance | | |
| SEC-1-7 | Low | — | Validator, emitter | A name id 0 is "no name" to the validator but not to the emitter: a misleading internal error | Open | | |
| SEC-1-8 | Low | CWE-829 | Supply chain | `fedora:43` is not pinned in five Dockerfiles under `dev_tools/docker/` (§4) | Open | | |
| SEC-1-9 | Low | — | Crash recovery | A poisoned session still destroys its reader (`session.cpp:320-349`) | Open | | |
| P-1 | High | — | Generated code | `#pragma STDC FENV_ACCESS ON` is ignored without a diagnostic (row `c99.fenv_access`, `ignored`) | Open; proposed M6.1b | | |
| P-2 | Medium | — | Generated code | `scalar_storage_order` is ignored with only the FE's "unknown attribute" warning | Open; proposed M6.4a (its row); needs the user's acceptance | | |
| P-3 | Medium | — | Generated code | `trivial_abi` is ignored with only the FE's "unknown attribute" warning; the ABI differs from clang objects | Open; proposed M6.4a (its row); needs the user's acceptance | | |
| P-4 | High | — | Generated code | The initialization store of a volatile object is emitted non-volatile, so -O2 can drop it (row `c89.volatile_init`, `ignored`) | Open; proposed M6.1b | | |
| P-5 | High | — | Generated code | The implicit copy constructor of a class with an array of class objects calls the element copy constructor without its source (`struct S { std::string a[2]; }` copies crash) | Diagnosed in M6.1a (user, 2026-11-08); fix proposed M6.1b | M6.1a (diagnosis) | `tests/tests/cir/writer/writer-cxx-array-copy-negative.sft.cpp` |
| P-6 | High | — | Generated code | Every GNU `__thread` variable was emitted without TLS (the FE records the keyword only as a declaration modifier), so a definition was one global shared by all threads; the support row's evidence did not use `__thread` | Fixed | M6.1a | `tests/tests/cir/writer/writer-gnu-thread-keyword.sft.cpp`, `writer-gnu-thread-keyword-c.sft.c` |
| P-7 | High | — | Front end (every configuration) | A structured binding pack bound by reference (`auto &[...r]`, C++26 P1061) gives wrong values in every configuration, the reference cpfe included (row `cxx26.structured_binding_packs`, `ignored`) | Open; proposed M6.1b triage (an FE defect; may belong to `main`) | | |
