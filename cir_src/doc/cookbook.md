# EDG ClangIR back end — cookbook

How to build, run, test and gate the CIR back end. Read on demand; the
state, decisions and process are in `project.md`.

## 1. Commands

### Host access (x86-64 = clang4; Mac = edit and drive only)
- `ssh -o BatchMode=yes -i ~/.ssh/id_rsa matt@clang4.cpp.al`; rsync `-e 'ssh -o BatchMode=yes -i ~/.ssh/id_rsa'`
- Ubuntu 26.04, 172 cores, 251 GB RAM, ~430 GB free at planning; matt is in `docker`, no sudo; shared host.
- Never touch `~/cerastes-m4.5d1` or other people's containers. Old box 192.168.1.167: retired for CIR.
- Trees `~/edg-cir-{config,writer,lib}`; checkpoint trees `~/edg-cir-config/<tree>`; `~/edg-cir-config/llvm-project` at the pin.
- Settings: `EDG_CIR_HOST` (default `matt@clang4.cpp.al`), `EDG_CIR_SSH_KEY` (default `~/.ssh/id_rsa`); the ControlPath socket is optional.
- `docker` works directly for matt; the tools run it without `sg docker` (set `EDG_CIR_SG_DOCKER=1` to bring it back).
- Upload from the Mac runs at about 1 MB/s (a full tree took 25 min): never upload `build/`, `tests/runs/` or `.tmp/`; seed a new
  tree on clang4 by copying a built one (`cp -a ~/edg-cir-config/m5-int ~/edg-cir-<agent>`), then rsync deltas (`-z`).
- `/tmp` on clang4 is tmpfs (RAM): set `TMPDIR` to a directory on disk for image builds and large jobs.
- `edg-docker-test --jobs N` caps the build and edgy jobs; without it a run uses all 172 CPUs (the gate and `edg-cir-record` pass it).
- Never record goldens or expectations on the Mac; record on clang4 (same cir-env image).
- Mac-only loops: `macos-arm-clang-debug-cir` (writer, aarch64 triple, not authoritative), `cir-format-only` (no LLVM).
- Sync a tree: `rsync -a --checksum --no-times` (what `edg-cir-checkpoint` does); see §7 for why.
- `cir_src/scripts/box.sh ssh|sync|run JOB CMD|chown|test JOB` (`EDG_CIR_HOST`, `EDG_CIR_SSH_KEY`, optional `EDG_BOX_SOCKET`, `EDG_CIR_SG_DOCKER`, `EDG_BOX_TREE`; run names `cir-lib-JOB-pid`)
- `cir_src/test/writer/tools/box.sh ssh|sync|run LOG SCRIPT|wait LOG [N]|fetch REMOTE DIR` (same variables plus `BOX_TREE`; detached via nohup + `~/LOG.exit`; `run` sets `TMPDIR=$HOME/tmp` and `nice -n 10`)
- `cmp_run.sh`, `rt_run.sh`, `cmp_box.sh`, `rt_box.sh` read `BOX_TREE` (default edg-cir-writer)

### Toolchain and images
- Pin: `cir_src/LLVM_REVISION` = `2d6f180e7dbad81db52f5163a34502c1b8058012`. Bump: change it, rebuild, `ninja check-clang-cir`, rebuild images, run the `toolchain` lean gate (+`library` if cir_src changed).
- Images on clang4 (built 2026-11-05): `edgcpp/dev-env` is pulled by digest and tagged (`dev-init.py docker` builds no
  image; it needs PATH and PYTHONPATH set to the tree); `edgcpp/llvm-cir` and `edgcpp/cir-env` are built by `build-edg-env`
  (`~/edg-cir-config/build-images.sh`; `EDG_LLVM_CIR_JOBS` caps the LLVM build, 64 jobs took 12 min, cir-env 6 min more).
  `~/edg-cir-config/llvm-work/build` keeps the LLVM build for incremental rebuilds.
- `dev_tools/bin/edg-build-llvm-cir [--check] [--jobs N] <llvm-project> <build> <prefix>`: run inside the llvm-cir image build, or inside dev-env with bind mounts (keeps it incremental). ~27 min on the old 16-thread box; use `--jobs 64`.
- Images `edgcpp/llvm-cir` (`dev_tools/docker/llvm-cir`) and `edgcpp/cir-env` (`dev_tools/docker/cir-env`, FROM dev-env, prefix `/edg/llvm-cir`, LLVM bin not on PATH). Tag `:latest` and `:2d6f180e`.
- cir-env EDG base: `bases/docker/cir-env/gcc`. Track the pin as it is; the `release/24.x` move is retired (project.md §2 Retired).
- Library data after an LLVM bump (diff the NOTHROW set; the generator reports exclusions):
  `python3 dev_tools/builtins/generate-libc-builtins.py --clang-builtins-inc /edg/llvm-cir/include/clang/Basic/Builtins.inc --llvm-sha 2d6f180e --attrs-output src/builtin_libc_attrs.h`
  Only unconditional letters U, c, r, n (not e/g: Linux math-errno on); `src/ecil_kind_names.h`: regenerate with `cir_src/test/writer/tools/gen_kind_names.py` when `il_def.h` enums change.

### Builds (all in Docker: `edg-exec --img cir-env -- ...`)
- Library: `cmake -S cir_src --preset linux-cir-relwithdebinfo` -> `build/cir`; also `linux-cir-release`, `linux-cir-asan`. Test hooks are the CMake option `EDG_CIR_TEST_HOOKS`.
- FE: `linux-gcc-debug-cir` (`build/gcc-cir`, ASan, links the library); `linux-gcc-release-cir` (`build/gcc-release-cir`, no ASan: timing and RSS); `linux-gcc-debug-cir-nopoison` (made by `poison`).
- Fresh FE in a scratch tree: rsync `src cmake include_* lib_src util nt_util bases dev_tools CMakeLists.txt CMakePresets.json`, `cmake --preset linux-gcc-release-cir && ninja -C build/gcc-release-cir cpfe-cir` (30 s); link `build/gcc-cir` to it for dev_tools.
- Library on clang4 (`~/edg-cir-lib`, cir-env): `cir_src/scripts/box_allpresets.sh` (3 presets + ctest), `box_all.sh [preset]`, `box_lit.sh [-a "--filter=name -v"]`, `box_ninja.sh`; lit comes from `~/edg-cir-config/llvm-project/llvm/utils/lit`.
- `edg-docker-test --non-interactive --posture cir [filter]` builds library + `cpfe-cir` + util tools; `--posture cir-release` uses the release FE.

### Running
- In-process: `eccp --c11 x.c` or `eccp --c++17 x.cpp` (cir-env base; mode flag required, `.cpp` is C without one), or `cpfe-cir --c11 --gen_c_file_name=x.s x.c`.
- Two-process: `cpfe-cir --c++17 --ecil_output=x.ecil x.cpp`, then `edg-cir [-o out] [--emit=cir|cir-lowered|llvm|bc|asm|obj] [-O0..-O3] x.ecil`; `dev_tools/bin/cpfe-cir-2p` does both for the `_2p` config (takes `--cir_opt_level=N` as `-ON`).
- edg-cir flags: `--verify-each --keep-going --stats --trace --llvm-arg=ARG --triple=T --version`;
  `--lazy-symbols` (list deferred bodies: replayed/dropped); `--eager-bodies` (disable lazy emission; oracle for `lazy-identity`);
  `--no-llvm-passes` (CIR -> LLVM only; `edg-cir-attr-diff` uses it); `--chunk-functions=N` (default 500); `--chunk-threshold=N` (default 4000 bodies; 1 forces; 4294967295 never).
- Peak memory: the `peak RSS KiB` line of `edg-cir --stats --emit=asm` (forced: `--chunk-functions=500 --chunk-threshold=1`).
- Inspect: `ecil-dump x.ecil` (validates); `cir_src/test/writer/tools/stackcheck.py`; `edg-cir-ecil-dump` (goldens).
- eccp in a scratch tree: export the `edg-scrape-compiler` variables, `CPFE=$CPFE_CIR`, `EDG_BASE=bases/docker/cir-env/gcc`, `EDG_USE_SYSTEM_HEADERS=1`, then `util/eccp.sh -O2 --g++ file.cpp`.

### Tests and gates (run on the Mac in the repo root; PATH has dev_tools/bin, PYTHONPATH dev_tools/pylibs)
- Per-program timeout is 120 s (edgtest exec default); posture timeout 300 s; the comment at `edg-docker-test:268` still says 60 s (stale).
- Slice: `edg-cir-checkpoint --tree T --lean AREAS --lean-base REF [--detach] [--jobs 48]` (areas checked against `git diff REF` + untracked before any sync; uncovered or empty set refused; `--allow-empty-change-set`, `--no-area-check` need a stated reason)
- Quick: `--quick` (build, smoke, cpp-smoke). Default steps: build identity smoke corpus asm-diff writer-stress cpp-smoke cpp-writer-stress crash-sweep ctest.
- Milestone: `edg-cir-checkpoint --milestone --tree m51-int --jobs 48 --detach` (about 3.5 h on clang4, perf about 90 min; `--stress-timeout` defaults to 3600 s: the 20000-rung else-if ladder takes about 1400 s there): every step, `--perf-realworld pinned --perf-levels 0,2 --perf-targets s1`, plus the day baseline. The day baseline is the link `day-base/baseline-64`.
- Re-run one step on the same evidence: `--extra '--only STEP --out <evidence dir>'`; resume with `--out`; `--force` re-runs PASSed steps; `--steps-for AREAS` prints the set.
- Direct (on clang4 as matt, no sg): `edg-cir-gate-evidence --tree T --lean AREAS | --milestone | --steps ...`; `--with-cpp`, `--with-m4`, `--smoke-all-pass`, `--allow-status-changes`, `--jobs 48`.
- Time bounds: `--command-timeout` (7200 s default), `--step-timeout STEP=SECONDS`; a timeout kills the process group and stops containers by `edg.job` label (names `cir-config-<tree>-<step>-*` via `EDG_DOCKER_NAME_PREFIX`). `EDG_CIR_STRESS_TIMEOUT`, `EDG_CIR_STRESS_STACK_KB` (256 KB cpp-stress, 8 MB cpp-stress-asan).
- Crash rules: library crash always fails; FE crash excused only if the reference has the same class and no `ecil_*`/`cir_be*` frame (`tests/.edgy/filters/cir-excused-crashes.txt`); kill and timeout always fail.
- Triage files (`tests/.edgy/filters/`): `cir-status-triage.txt`, `cir-opt-triage.txt`, `cir-lazy-triage.txt`, `cir-attr-known.txt`, `cir-documented-gaps.txt`, `cir-eh-probes-skip.txt`; a line for a case that did not regress is stale and fails.
- Baselines (clang4, `~/edg-cir-config`, taken 2026-11-06 at `991737c19a`, code = M5; each has `<name>.images.txt`):
  `identity-m0c` (`--identity-baseline`, captured at the M0 commit `fee14b0c21`), `day-base/baseline-64` (`edg_x86_64`,
  `_cp`; the `day` step) and `day-base/baseline-32` (`edg_x86_32`, `_32_cp`, run with `@posture32.json`), `m5-base-asm`
  (`--asm-baseline`), `m5-base-cpp-asm`, `m5-base-status` (`c_*`, `cpp_*`; `--status-baseline`, `--opt-baseline-status`),
  `m5-base-perf.json` (`--perf-baseline`, regression), `m5a-base-perf.json` (`--perf-targets-baseline`, S1 targets, built
  from `77a24448fa`); raw data `m5-base-rss.txt`, `m5-base-chunk-rss.json`, `m5-base-attr-diff/`. Boost headers 1.92.0
  in `m5-int/.tmp/boost-inc`. Images: dev-env `1c1cf78d322b` (`:cir-1c1cf78d322b`), llvm-cir `e92da938d7cc`, cir-env
  `cd5c7f63e579`; a comparison against a baseline taken with other images says so (`IMAGES`).
  asm baseline: `edg-cir-compare-modes @tests/.edgy/filters/cir-c-corpus.txt --types rp,lp -j 12 --save-asm D --in-process-only` (C++: `--cxx @...cir-cpp-corpus.txt`) in a built tree of the baseline commit. Identity: `edg-macro-identity-check configure|capture --binaries|compare` (docstring recipe) at `fee14b0c21`.
  A missing or unusable baseline stops the gate (status 2) unless `--allow-missing-baselines`; `edg-cir-checkpoint --check-baselines` lists what the selected steps need.
- `edg-cir-summary <run>` buckets a run (overall and CIR-specific vs `tests/expectations/edg_x86_64`); `edg-cir-rto-diff --auto [--documented]` attributes status changes.
- Record C++/CIR expectations: `edg-cir-record --tree T ...` (-W on clang4, 100 KB rule, fetch back; `research/test_plumbing.md` §11.3); then `edg-cir-rto-diff --auto`.
- Writer goldens: `tests/tests/cir/writer` (`//script:edg-cir-ecil-dump`, `//require:BACK_END_TYPE 3`); cirgen run tests `tests/tests/cir/cirgen`; ctest/lit `cir_src/test/lit.cfg.py`, `capi_tests`.
- Tools self-test (no box, no Docker): `python3 dev_tools/bin/edg-cir-tools-test [-v]`; on both hosts.
- Prune: `edg-cir-checkpoint --prune N | --prune-only | --prune-evidence`; it keeps trees with a KEEP file, entries under 6 h, and `PROTECTED_TREES` (`day-base`, `m5-int`); the default tree is `m51-int`.

### Harnesses and box scripts (each has a usage header)
- `edg-cir-perf` (cachegrind Ir; `--only cxx`, `--stream KIND:N`, `--targets s1`), `edg-cir-rss` (`--generate-cxx-functions N -o big.cpp`, `--fe-build`), `edg-cir-stress`, `edg-cir-writer-stress[ --cxx]`, `edg-cir-compare-modes [--cxx] [--opt-level N]`, `edg-cir-check-isolation`, `edg-cir-poison-check`.
- `edg-cir-abi-diff batch [--vbases]`, `edg-cir-symbols [--also g++]`, `edg-cir-mixed-tu`, `edg-cir-fe-facts`, `edg-cir-run-diff` (g++ execution differential, `-O<N>`), `edg-cir-attr-diff`, `edg-cir-codequality`, `edg-cir-opt-triage`, `edg-cir-lazy-identity`, `edg-cir-chunk-identity`, `edg-cir-chunk-rss`, `edg-cir-chunk-ir`, `edg-cir-names-check`.
- cir_src/scripts: `box_stress.sh cxx_locals:2000 ...` (kinds in `ecil-stress-gen.py`), `box_m4_stress.sh`, `box_attrdiff2.sh TAG`, `box_chunk_identity.sh TAG --sets pinned`, `box_chunk_o2.py DIR 2`, `box_diff.sh`/`box_side.sh`/`box_diff_m4.sh` (clang shape diff), `box_realfe.sh`, `box_runall.py`/`box_corpus.py`/`box_ccorpus.py`, `box_reasons.py`, `cir-compare.py [--cxx]`, `cir-op-compare.py`, `cir-diff-classify.py`, `ecil-m4-stress-gen.py`.
- Writer tools: `stream_sweep.py [--body-hash|--reasons]`, `cg_writer.py`, `gen_perf_tus.py`, `cmp_cxx_box.sh` (`FE_BUILD=gcc-release-cir`), `cmp_cxx_multi_box.sh`, `unit/run_unit.sh`; fixtures `cir_src/test/format/gen_v12_fixtures.py`, `cir_src/test/cirgen/Inputs/gen_eh.py`.

### Debugging variables
- Library: `EDG_CIR_TEE=<file>`, `EDG_CIR_NO_VERIFY=1`, `EDG_CIR_DEBUG_NOTES=1`, `EDG_CIR_NO_GUARD`, `EDG_CIR_WORKER_STACK_MB`, `EDG_CIR_CHUNK_DUMP`.
- Test hooks (only in builds with `EDG_CIR_TEST_HOOKS`): `EDG_CIR_TEST_LAZY_MISS=<sym>` (lazy-missed-edge.test), `EDG_CIR_TEST_CHUNK_FAIL=N` (chunk-failure.test), also `EDG_CIR_TEST_CRASH`, `_CONSUME_CRASH`, `_NO_WORKER`, `_UPSTREAM_LOWERING`.
- Writer: `ECIL_GID_STRESS=1` (move and poison the G-id table), `ECIL_CHECK_ALL_NAMES=1` (full late-name check; default samples 2000 + 1 in 16), `edg-cir --stats`, ASan poisons pooled FE regions.

## 2. Lean gates: area → steps

Read from `AREA_STEPS` in `edg-cir-gate-evidence` (about line 392); `build` is added to every area except `tooling`.

| Area | Steps added |
| --- | --- |
| writer | cir-suite corpus cpp-corpus asm-diff cpp-asm-diff modes cpp-modes poison opt-corpus writer-stress cpp-writer-stress symbols mixed-tu |
| library | ctest cir-suite corpus cpp-corpus asm-diff cpp-asm-diff opt-corpus modes cpp-modes |
| fe-options / linkage | release-smoke / symbols mixed-tu |
| eccp | cir-suite corpus cpp-corpus opt-corpus mixed-tu |
| tests / tests-triage / tests-crashes | cir-suite corpus cpp-corpus / opt-corpus / crash-sweep |
| tests-smoke / tests-reference | smoke cpp-smoke opt-corpus release-smoke / day |
| tooling / pylibs / twop | tools-test / smoke day / smoke cpp-smoke modes cpp-modes |
| goldens / summary | cir-suite / smoke cpp-smoke cir-suite corpus cpp-corpus |
| toolchain | ctest smoke cpp-smoke cir-suite corpus cpp-corpus opt-corpus asm-diff cpp-asm-diff mixed-tu symbols eh-probes m4-stress cpp-stress crash-sweep perf rss stress release-smoke (TOOLCHAIN_SKIPS for the rest) |
| shared-src | identity isolation day |
| lazy / chunk / names / attr / crashes | lazy-identity / chunk-identity chunk-rss slow-below-threshold / names-check / attr-diff codequality / crash-sweep |
| speed | perf rss stress cpp-stress m4-stress crash-sweep |
Declared, not derived: lazy speed crashes attr chunk names. Expensive steps (crash-sweep perf rss stress cpp-stress m4-stress) run at toolchain and milestone gates only; say in the report why a slice cannot affect them. File -> area rules: `AREA_RULES`; an unmapped file fails the check (add a rule). Step lists: `--with-cpp` = CPP_STEPS, `--with-m4` = M4_STEPS, `--milestone` = both.

## 3. Host hygiene and lessons

- Every command is time-bounded (`timeout`, `--step-timeout`, `--command-timeout`). A hang once cost 2 h (ASan symbolizer in a 256 KB stack): bound compilers and programs, not just steps.
- Parallelism at most 64 jobs (gate `--jobs 48`), prefix `nice -n 10`; run `df -h ~` before large jobs (warn under 40 GB); tell the orchestrator before any job over ~10 min of CPU.
- Containers: `docker run --name cir-<agent>-<job>`; record every PID/ID; stop only by exact ID. Never `pkill -f`, `killall`, `docker ps -q | xargs docker stop`, `docker system prune`. Other people's containers share the account.
- Never touch `~/cerastes-m4.5d1`. Scratch goes under the tree's `build/` or `.tmp/` (or `~/edg-cir-<agent>`), never a shared `/tmp` (agents clobbered each other); `/tmp` inside a container is private to one `run`.
- Long jobs: `nohup ... & ; echo $? > exitfile`, detached, then one Monitor/until-loop; never block or poll in the conversation.
- Root-owned files: posture containers leave them in `build/`; remove via a container (`box.sh chown`, or `docker run --rm -v tree:/w edgcpp/cir-env chown -R $(id -u):$(id -g) /w/build`); no sudo.
- rsync mtime pitfall: `-a`/`--delete` keeps Mac mtimes, so ninja may see "no work" and test old code. Use `--checksum --no-times` or wipe `build/cir*`, `build/gcc-cir`, `build/gcc-release-cir`; `touch` after a revert. Each checkpoint records and compares a tree fingerprint (src, cir_src, dev_tools/bin, tests/.edgy, cmake).
- Disk: each checkpoint tree is 10-15 GB (ENOSPC killed a run once); prune `build/` and `tests/runs/`, keep `.tmp` evidence.
- ASan: quarantine distorts RSS; measure with `linux-gcc-release-cir` or `ASAN_OPTIONS=quarantine_size_mb=0`. LeakSanitizer "Unable to get registers from thread" is ptrace noise; real reports start `ERROR: AddressSanitizer`. Run `linux-cir-asan` ctest for every library change.
- Mac zsh/BSD: no word splitting of a quoted list, `sed -i ''`, no `timeout`; unquoted globs error (`no matches found`); put nested commands in a script and scp it; `echo =====` fails in zsh (use printf).
- Measure the release FE, not only debug (test-feature pins cost 5.7% FE instructions). CHECKING is TRUE in every build.
- Wait for the implementer's "tree green" before a checkpoint sync; review on a frozen `build/review-snap-*`.
- Check the reference expectation before calling a run failure a miscompile (`rto-diff --auto`); check a count before using it in a decision.
