# Test plumbing for `edg_x86_64_cir`

Author: cir-config, Phase R, 2026-10-09. All paths relative to the repo root.
Line numbers are for `43e10b90fc`.

## 1. How a config reaches a binary today

* `tests/.edgy/config.json` has `config.c` (variants `edg_x86_64`, `edg_x86_32`;
  `bin_path_env: ["CPFE"]`) and `config.cpp` (variants `*_cp`;
  `bin_path_env: {c: ["CPFE"], cpp: ["CPFE_CP"]}`), `config_sets`, and
  `test_suites.primary` (9 suites) / `secondary` (empty).
* `dev_tools/bin/edgy:174-198` `_load_c_back_end_configs`: one `TestRunConfig`
  per variant, front-end env var `CPFE`, fixed override `EDG_CONFIG=<name>`; it
  exits if `tests/expectations/<name>/` is missing. `:201-227`
  `_load_cpp_back_end_configs` builds `CPFEA`/`CPFEB` and substitutes the
  `cpfe-cp-gen-be` wrapper as `CPFE` (so the cpp variants need that tool).
* `edgy:710-748` `_find_or_copy_front_end`: resolves the binary from the
  config's env var (overrides `TEST_<config>_X`, then the process env), then
  **copies only that file** to `tests/runs/<run>/<config>/bin/` (preserving the
  name) unless `EDG_TEST_NO_COPY_CPFE` is set. `:751-806` `_configure_front_end`
  exports it as the destination env var and runs `<copy> --dump_config` (and
  `--version`) via `edgtest.filter.load_build_constants`
  (`edgtest/filter/__init__.py:46-75`) to get `BuildConstants.config_dump`.
  Because the dump runs from the copy, anything the binary needs at load time
  (e.g. `libedg_cir.so`) must be next to the copy first.
* Exported variables reach a config through `prefix_source TEST_<config>
  <build>/environment.sh` (`edg-docker-test:446-470`): `environment.sh` is
  written by `cmake/wrapper.cmake:9-32` and `edgy:763-770` maps
  `TEST_<config>_X=Y` to `X=Y`. So any `export` added to `environment.sh` is
  visible to that config's tests, including `//script:` commands.
* Per test, `edgtest/exec/__init__.py:99-107` chooses `eccp --cpfe_only` (FE
  test types), `eccp` (link/run types) or `eccp -c`, unless `//script:` replaces
  the command (the script receives the options and test name; stdout/stderr is
  the recorded output, run from the test temp dir). `:1058-1068` skips
  everything with `REQUIREMENTS NOT MET` when `//require:` substrings are not in
  the config dump (`:977-982`, plain `in`). `:1150` sets
  `ASAN_OPTIONS=exitcode=139:log_path=<tmp>/asan`.

## 2. Adding `edg_x86_64_cir`

### 2.1 `tests/.edgy/config.json`

Add a **new top-level `config.cir` section** rather than another variant of `c`
(the `c` loader hard-codes `CPFE` and shares `bin_path_env`):

```json
"cir": {
  "bin_path_env": ["CPFE_CIR"],
  "variants": [ { "name": "edg_x86_64_cir" } ]
}
```

and `"config_sets": { ..., "cir": ["edg_x86_64_cir"] }`. Do **not** add it to
`default` or `x86`, so existing runs and expectations are unchanged. Create
`tests/expectations/edg_x86_64_cir/` (an empty directory is accepted; a missing
suite `.log` is treated as "no expectations", `edgy:262-280`; add a `.gitkeep`).

### 2.2 `edgy` changes (cir-config owns this plumbing)

1. `_load_cir_back_end_configs(loaded_configs, test_home, config_dict)`: same
   shape as the C loader but `config_providing_fe_env='CPFE'`,
   `front_end_env_paths={'CPFE': config_dict['bin_path_env']}` (so `$CPFE` is the
   `cpfe-cir` copy and `eccp`/`edgy` need no further change), env override
   `EDG_CONFIG=edg_x86_64_cir`. Call it from `load_edgy_config` (`:230-250`). No
   `RequiredToolPath` is needed.
2. `_find_or_copy_front_end` (`:710-748`): after `shutil.copy2(fe_path,
   run_fe_path)`, also copy the sidecar libraries named by the config. Proposed:
   `TestRunConfig` gets `sidecar_env: List[str]` (for cir `['EDG_CIR_LIB']`);
   for each such env var (override, then environment) that points to an
   existing file, copy it next to the binary. `environment.sh` exports
   `EDG_CIR_LIB=<build/cir>/lib/libedg_cir.so`. The binary's RPATH is
   `$ORIGIN` first, then the build tree (plan), so the copy is used and rebuilds
   during a run cannot affect it, but a missing copy still falls back to the
   build tree.
3. `environment.sh` / `lazy_environment.sh` (`cmake/wrapper.cmake:9-32`):
   when `cpfe-cir` is configured, add `export CPFE_CIR=...`,
   `export EDG_CIR_LIB=...`, `export ECIL_DUMP=...` (the `ecil-dump` tool) and
   `export EDG_CIR_RT_LIBDIR=...` (where `libedg_cir_rt` lives). cir-config owns
   `cmake/wrapper.cmake`? It is not in the plan's ownership list: ask the
   orchestrator, or emit the lines from `src/cmake/cpfe-cir/CMakeLists.txt`
   into a second file that `edg-docker-test` also sources.
4. No filter change is required for `//require:`; the dump will contain
   `#define BACK_END_TYPE 3` after M0.

### 2.3 Image selection and posture builds (`edg-docker-test`)

`edg-docker-test` always calls `edg-exec` with the default image
(`:330-336`, `:359-365`, `:415-425`, AcknowlEDG at `:520+`), and
`edgdocker.EXECUTABLE_IMAGES = ('dev-env','legacy-dev-env','sphinx-env')`
(`edgdocker/__init__.py:42`), so `edg-exec --img cir-env` is rejected today.

Needed (M1):

* `edgdocker/__init__.py`: add `'cir-env'` to `EXECUTABLE_IMAGES` and
  `'llvm-cir'` to `BUILDABLE_IMAGES`; **no** `IMAGE_PINS` entry (local images,
  built by `build-edg-env cir-env`, which first runs the `llvm-cir` prebuild).
* `TestRunPosture` gets `image: str = 'dev-env'` and every
  `TOOL_PATH_EDG_EXEC` invocation passes `--img <image>` (the AcknowlEDG
  call too, or skipped for `--non-interactive`).
* A new `--posture cir` (or a `@file` posture, which already works with no
  code change: `load_posture_file` takes `builds` and the other fields; the
  `image` key is the only addition):

  ```json
  { "image": "cir-env",
    "builds": [
      { "cmake_preset": "linux-cir-relwithdebinfo", "build_path": "build/cir",
        "targets": ["all"], "test_configs": [],
        "edg_base": "bases/docker/cir-env/gcc" },
      { "cmake_preset": "linux-gcc-debug-cir", "build_path": "build/gcc-cir",
        "targets": ["cpfe-cir", "edg_cir_rt", "ecil-dump"],
        "test_configs": ["edg_x86_64_cir"],
        "edg_base": "bases/docker/cir-env/gcc" } ],
    "test_filters": "imported/gnu/c" }
  ```

  The first entry is the `cir_src` standalone project, which has its own
  `CMakePresets.json`; `configure_build` runs `cmake --preset` from the repo
  root, so it needs a `source_dir` field (`cmake -S cir_src --preset ...`;
  unverified that `-S` with `--preset` picks `cir_src/CMakePresets.json`:
  the safe form is `cwd=cir_src`). Build order matters: library first, then
  the FE (`find_package(EdgCir)` reads `build/cir`).
* `_write_prefixed_env_context` already writes `prefix_source` lines for each
  `test_configs` entry of each build, so `edg_x86_64_cir` picks up
  `build/gcc-cir/environment.sh` with no change.

### 2.4 Tests that exist only for CIR

Adding a test with `//require:BACK_END_TYPE 3` makes **every other config** record
a `REQUIREMENTS NOT MET` line for it (this is what `-W` writes; the memory notes
say the 32-bit logs need the same line). To avoid touching four expectation
sets, put CIR-only tests (ecil-dump goldens, stress tests) in a **new suite**
`tests/tests/cir/`, listed under `test_suites.secondary`. Secondary suites run
only when named (`edgy -c edg_x86_64_cir cir`; `get_selected_test_suite_names_from`,
`edgy:389-402`), so other configs never see it. Its expectations live only in
`tests/expectations/edg_x86_64_cir/cir.log`.

## 3. EDG base `bases/docker/cir-env/gcc/`

`bases/docker/dev-env/gcc` has `edg_eccp_config` plus symlinked `include`
(`include_c++`, the EDG-supplied C++ headers) and `lib*` directories
(`predefined_macros.txt` only). For CIR:

```sh
# bases/docker/cir-env/gcc/edg_eccp_config  (sketch)
. "$(dirname "${BASH_SOURCE:-$0}")/../../dev-env/gcc/edg_eccp_config" 2>/dev/null
```

`eccp.sh:43-45` sources `$EDG_BASE/edg_eccp_config` with `.`, and `eccp.sh` is a
`/bin/sh` script, so no `BASH_SOURCE`: either copy the dev-env file and edit it
(recommended: the base is small and the files already differ per target), or
`. "$EDG_BASE/../../dev-env/gcc/edg_eccp_config"`. The settings that must
differ:

| Variable | dev-env/gcc | cir-env/gcc | Why (eccp.sh) |
| --- | --- | --- | --- |
| `EDG_GEN_C_SUFFIX` | unset (`.int.c`, line 247) | `".int.s"` | generated file is assembly; `gen_o_suffix` becomes `.int.o` (line 249) |
| `EDG_RUNTIME_LIB` | image ENV `edgrt` (`build-base/Dockerfile`, default `C` at line 198) | `""` | `[ ! -z $EDG_RUNTIME_LIB ]` (line 3141) is false for an empty value, so no `-ledgrt`; the config must **assign** the empty string (set but empty beats the image ENV) |
| `EDG_C_TO_OBJ_LIBRARIES` | `-lstdc++ -lgcc_s -lpthread` (line 33) | `-L$EDG_CIR_RT_LIBDIR -ledg_cir_rt -lstdc++ -lgcc_s -lm -lpthread` | libraries follow the objects on the link line (3084-3095: `$object_files ... $EDG_STD_LIBS $EDG_C_TO_OBJ_LIBRARIES`); `-L` is needed because `LIBDIR` (`ECCP_LIBDIR`, line 80) is the EDG lib dir, not `build/cir` |
| `EDG_USE_SYSTEM_HEADERS` | default 0 (EDG headers) | default 0 since M3a (was **1**; see §9.7) | libstdc++/glibc are the runtime, so the matching headers must be used (scrape via `EDG_GCC_INCL_SCRAPE`, exported by `edg-container-exec:67`); C smoke tests have no includes, C++ tests (M3) need this. Per-test `//use_system_includes` already forces 1 (`exec/__init__.py:1035`) |
| `EDG_SUPPRESS_PATCH_MUNCH`, `EDG_STD_LIBS` | 1, empty | same | no munch/patch step (static init is CIR global ctors) |
| `EDG_C_TO_OBJ_DEFAULT_OPTIONS_linux_x86_64` | `-w ... -m64 -march=x86-64` | same | gcc assembles the `.int.s`; `-D`/`-f` flags are harmless to the assembler |
| `-g` | `EDG_C_TO_OBJ_COMPILER_DEBUG_FLAG` | n/a | CIR emits no debug info |

Other `eccp.sh` consequences:

* Line 378: suffix not ending in `.c` => `--old_line_commands` is added to the FE
  options. The FE must accept it under CIR (audit D table).
* `compile_int_c` (754-835) runs `gcc <opts> -o X.o -c X.int.s`; the
  `strip_line_dirs` `sed` is off by default and would be a no-op on assembly.
* Line 2793: `--gen_c_file_name=<tmpdir>/<base>.int.s` is always passed.
* Line 3015: `compile_int_c` is skipped when `fe_only` (`--cpfe_only`), but the FE
  still runs its back end (`--no_code_gen` is the only option setting
  `suppress_back_end`, `cmd_line.c:10698-10704`). So **front-end test types
  (`fp`/`fn`/`fc`) also exercise CIR code generation** under `edg_x86_64_cir`.
  That is why the M2 run is restricted by path filter, below.
* `ECCP_LIBDIR`-based `-L` and the `_$target` rewrite (2489) apply only with
  `--target`.

## 4. `ecil-dump` golden tests

Use `//script:` (the harness runs the named command instead of eccp, in the test
temp dir, with the case options and the test file name as arguments, and records
its combined output; cf. `tests/tests/edg/ccs/coroutines/gnats15431_21`):

```
//type:fp
//options_all:
//require:BACK_END_TYPE 3
//script:./dump_ecil
```

with `dump_ecil` a small `sh` file beside the test:

```sh
#!/bin/sh
"$CPFE" "$@" --ecil_output=out.ecil <stream-only option> || exit $?
exec "$ECIL_DUMP" out.ecil
```

(`$CPFE` and `$ECIL_DUMP` come from `environment.sh`; the exact FE option for
"write the stream, skip codegen" is cir-writer's.) The comparison is textual
against the recording, with the normalizations in `edgtest/filter` (addresses are
irrelevant since the stream has no pointers). Because of the "REQUIREMENTS NOT
MET" fan-out (2.4) these live in the new `cir` suite. Substring matching means
`//require:BACK_END_TYPE 3` is right, but `BACK_END_TYPE 1` would also match
`BACK_END_TYPE 10+`; the values are 1-4 so that is moot.

## 5. ASan and exit codes

* The FE test builds are ASan (`-fsanitize=address`, see `compile_commands`); the
  harness sets `ASAN_OPTIONS=exitcode=139:log_path=<tmp>/asan` (`:1150`).
  `get_test_status_by_code` (`:158-170`) maps any code > 127 to `ABORT`/`RUNTIME
  ABORT`. `exitcode` is a common flag, so it also applies to **LeakSanitizer's
  exit-time failure**: any leak reachable at exit in `libedg_cir.so` (MLIR/LLVM
  global registries, the worker thread's allocations) turns every test into an
  ABORT. Mitigations: define `__lsan_default_suppressions()` in the FE shim
  (plan) and/or add `detect_leaks=0` for the cir config via `TEST_edg_x86_64_cir_ASAN_OPTIONS`
  ... but the harness overwrites `env['ASAN_OPTIONS']` unconditionally at `:1150`,
  so `edgy` needs a one-line change to append config-supplied options
  (e.g. `ASAN_OPTIONS += ':' + env_override.get('EDG_CIR_ASAN_EXTRA','')`), or the
  suppressions must be compiled in (preferred, no harness change).
* `libedg_cir.so` is not ASan-instrumented, which is allowed from an ASan
  executable; heap errors inside it are only seen through the interposed
  `malloc`. The big-stack worker thread is created with
  `pthread_attr_setstacksize`; ASan accepts it (it keeps its own fake stacks only
  with `detect_stack_use_after_return`).
* Programs built by `eccp` (`a.out`) are plain gcc builds: exit status > 127 there
  is a real crash. `//ulimit:` (a header field) is how the stack stress tests set
  `ulimit -s 256`.

## 6. M2 C corpus

`tests/tests/imported/gnu/c` holds 18346 `.c` files imported from the GCC
testsuite (flat directory plus subdirectories; the name pattern matches
`gcc.c-torture/execute` and `gcc.dg`). Types (first `//type:` line), whole
`tests/tests` tree:

| Type | Count |
| --- | ---: |
| `fp` | 11254 |
| `s` (skipped) | 2462 |
| `rp` (run, pass) | 2713 |
| `fn` | 1602 |
| `lp` (link, pass) | 213 |
| `cp` / `rn` | 34 / 8 |

`imported/clang/c` has 1550 `.c` files, all `fp`/`fn`, so it is a front-end
corpus only, not usable for run-pass. Other suites have only a handful of `.c`
files (`edg`: 19 without a type line, `regressions`: 2).

Run-pass candidates in `imported/gnu/c` by subdirectory (`rp` + `lp`, 2926 files):

| Directory | Count |
| --- | ---: |
| `(top level)` | 1153 |
| `torture` | 1062 |
| `tree-ssa` | 279 |
| `ipa` | 70 |
| `cpp` | 48 |
| `atomic` | 47 |
| `sso` | 44 |
| `ubsan` | 35 |
| `fixed-point` | 30 |
| `guality` | 27 |
| `graphite` | 19 |
| `dfp` | 17 |
| (others, 1 each) | ~35 |

**Proposed M2 corpus:** `rp`/`lp` tests in top level, `torture/`, `tree-ssa/` and
`ipa/` = 1153 + 1062 + 279 + 70 = **2564 tests**. Excluded directories:
`atomic*`, `sso`, `ubsan`, `fixed-point`, `guality`, `graphite`, `dfp`, `asan`,
`hwasan`, `simulate-thread`, `debug`, `rtl`, `special`, `analyzer`, `vect`, `dll-*`,
`cpp` (preprocessor tests), and the `.c2x-*` directories (sanitizers, decimal
float, `_Atomic`, GNU debug info, thread simulation are not goals for M2).
`edgy` filters are comma-separated path prefixes (`edgy:523-574`) with no
test-type selector, so `imported/gnu/c` also runs the ~14000 `fp`/`fn`/`s` files
(front-end tests that still push through CIR codegen, per 3.). Options:

1. (recommended, small `edgy` change owned by cir-config) add
   `--types rp,lp` (filter on `TestType` of the loaded description) so the
   M2 command is `edgy -c edg_x86_64_cir --types rp,lp imported/gnu/c/*` style
   prefixes `imported/gnu/c/torture,imported/gnu/c/tree-ssa,imported/gnu/c/ipa,imported/gnu/c/p,imported/gnu/c/2` ...;
   top-level files cannot be selected by directory prefix, only by name prefix.
2. Or run all of `imported/gnu/c` and accept `fp` coverage (stronger "no
   crash" check, more diagnostics, much slower; ~23000 files).

Test options in this corpus (`//options:`): none or `-w` mostly, then
`--c99 [--strict_gnu]`, `--c11`, `--c23`; 56 tests use `//source_files:` (extra
compiled files), 67 have `//filter:`. Expect many tests to need GNU extensions;
unsupported constructs must give diagnostics, not crashes.

### `cir-c-smoke`: 40 curated tests

Selection method: from the 2926 `rp`/`lp` tests, keep those with 8-45 non-comment
lines, a `main`, options empty/`-w`/`--c89`/`--c99`, no `//filter`,
`//source_files`, `//require`; reject any use of `__`, `typeof`, `_Complex`,
`_Atomic`, `asm`, `alloca`, `long double`, statement expressions, computed goto,
`#include`, `#pragma`, `setjmp`, `va_*`, `signal`, `float`/`double`, `printf`,
`memcpy`/`strcpy`/`malloc`, `_Generic`, `_Thread_local`, `_Alignas`,
`_Static_assert`, vectors, and any `[lowercase_identifier];` that could be a VLA;
then keep only those that compile with `cc -std=c99 -pedantic-errors` and run
with exit 0 on the Mac (220 of 262 candidates); drop sanitizer, debug, graphite,
charset, ucnid and cpp directories; take every 4th-5th file (176 left; 40
selected evenly by name). Because none uses headers, no system or EDG headers
are needed. All are `.sft.c`; filter string (comma-separated prefixes, relative
to the suite):

```
imported/gnu/c/20000623-1,
imported/gnu/c/20020210-1,
imported/gnu/c/20030708-1,
imported/gnu/c/20040331-1,
imported/gnu/c/c2y-switch-3,
imported/gnu/c/doloop-2,
imported/gnu/c/nrv1,
imported/gnu/c/pr108308,
imported/gnu/c/pr18241-3,
imported/gnu/c/pr20115,
imported/gnu/c/pr35616,
imported/gnu/c/pr41033,
imported/gnu/c/pr46315,
imported/gnu/c/pr60485-2,
imported/gnu/c/pr97567,
imported/gnu/c/struct-ret-1,
imported/gnu/c/torture/pr100492,
imported/gnu/c/torture/pr105665,
imported/gnu/c/torture/pr114787,
imported/gnu/c/torture/pr27116-2,
imported/gnu/c/torture/pr35634,
imported/gnu/c/torture/pr39132,
imported/gnu/c/torture/pr48124-2,
imported/gnu/c/torture/pr53663-2,
imported/gnu/c/torture/pr57303,
imported/gnu/c/torture/pr58941,
imported/gnu/c/torture/pr59058,
imported/gnu/c/torture/pr60733,
imported/gnu/c/torture/pr61757,
imported/gnu/c/torture/pr66375,
imported/gnu/c/torture/pr68721,
imported/gnu/c/torture/pr79088,
imported/gnu/c/torture/pr80974,
imported/gnu/c/tree-ssa/20040319-1,
imported/gnu/c/tree-ssa/alias-7,
imported/gnu/c/tree-ssa/pr14841,
imported/gnu/c/tree-ssa/pr22018,
imported/gnu/c/tree-ssa/pr41497,
imported/gnu/c/tree-ssa/pr87022,
imported/gnu/c/tree-ssa/tailrecursion-7
```

Full paths (under `tests/tests/`):

```
imported/gnu/c/20000623-1.sft.c
imported/gnu/c/20020210-1.sft.c
imported/gnu/c/20030708-1.sft.c
imported/gnu/c/20040331-1.sft.c
imported/gnu/c/c2y-switch-3.sft.c
imported/gnu/c/doloop-2.sft.c
imported/gnu/c/nrv1.sft.c
imported/gnu/c/pr108308.sft.c
imported/gnu/c/pr18241-3.sft.c
imported/gnu/c/pr20115.sft.c
imported/gnu/c/pr35616.sft.c
imported/gnu/c/pr41033.sft.c
imported/gnu/c/pr46315.sft.c
imported/gnu/c/pr60485-2.sft.c
imported/gnu/c/pr97567.sft.c
imported/gnu/c/struct-ret-1.sft.c
imported/gnu/c/torture/pr100492.sft.c
imported/gnu/c/torture/pr105665.sft.c
imported/gnu/c/torture/pr114787.sft.c
imported/gnu/c/torture/pr27116-2.sft.c
imported/gnu/c/torture/pr35634.sft.c
imported/gnu/c/torture/pr39132.sft.c
imported/gnu/c/torture/pr48124-2.sft.c
imported/gnu/c/torture/pr53663-2.sft.c
imported/gnu/c/torture/pr57303.sft.c
imported/gnu/c/torture/pr58941.sft.c
imported/gnu/c/torture/pr59058.sft.c
imported/gnu/c/torture/pr60733.sft.c
imported/gnu/c/torture/pr61757.sft.c
imported/gnu/c/torture/pr66375.sft.c
imported/gnu/c/torture/pr68721.sft.c
imported/gnu/c/torture/pr79088.sft.c
imported/gnu/c/torture/pr80974.sft.c
imported/gnu/c/tree-ssa/20040319-1.sft.c
imported/gnu/c/tree-ssa/alias-7.sft.c
imported/gnu/c/tree-ssa/pr14841.sft.c
imported/gnu/c/tree-ssa/pr22018.sft.c
imported/gnu/c/tree-ssa/pr41497.sft.c
imported/gnu/c/tree-ssa/pr87022.sft.c
imported/gnu/c/tree-ssa/tailrecursion-7.sft.c
```

Caveat: edgy filters match path prefixes, so `imported/gnu/c/pr41033` also
matches any sibling starting with that string (`pr41033-2.sft.c` ...); run once
with `--cmd` to confirm the list expands to exactly these 40 tests, or add the
`--types` option and exact names. The smoke list is a starting point: cir-lib
should drop or swap any test that needs a feature its v0 op set lacks.

## 7. Differences between the cir-env base and the dev-env base

The M2 gate compares `edg_x86_64_cir` with `edg_x86_64`, so the differences of
`bases/docker/cir-env/gcc/edg_eccp_config` from `bases/docker/dev-env/gcc/`
matter:

* **`EDG_USE_SYSTEM_HEADERS` was 1 until M3a; it is now 0 like dev-env
  (§9.7: parity, and the injected `--gnu_version` broke old EDG language
  modes). Tests that need the system headers say `//use_system_includes`.**
  The original rationale: the generated code links
  against libstdc++/glibc (Itanium ABI), so the front end must see those
  libraries' headers (`EDG_GCC_INCL_SCRAPE`/`EDG_GCC_CINCL_SCRAPE`), not EDG's
  `include_c++` headers written for the `edgrt` runtime. A test that includes a
  system header therefore sees different declarations under the two configs, so
  a difference in status for such a test is not necessarily a CIR bug.
  Tests with no `#include` (the smoke list) are unaffected.
* **The assembler and linker are gcc, as in dev-env**, with the dev-env
  default options. (An earlier M1 draft used the LLVM clang because LLVM's
  assembly used `.prefalign`, which GNU as does not know; `edg-cir --emit=asm`
  now disables LLVM's integrated assembler and emits plain `.p2align`.)
* **`EDG_GEN_C_SUFFIX` is `.int.s`, `EDG_RUNTIME_LIB` is empty** and the link
  line has `-ledg_cir_rt -lstdc++ -lgcc_s -lm -lpthread` (dev-env: `-lstdc++
  -lgcc_s -lpthread` plus `-ledgrt`).
* **Missing runtime directory fails loudly:** the base exits with an error if
  `EDG_CIR_RT_LIBDIR` is not set (it is exported by the build's
  `environment.sh`), instead of failing later at the link.

## 8. `eccp --cpfe_only` still runs the back end: which tests does it matter for?

`edgtest/exec/__init__.py:99-107` runs front-end test types (`fp`, `fn`, `fc`)
with `eccp --cpfe_only` (`-S`). In `eccp.sh` that sets `fe_only=1` and
`keep_int_file=1`: only the C compiler step is skipped. The front end is still
invoked as `cpfe ... --gen_c_file_name=<tmp>/X.int.s`, so under
`edg_x86_64_cir` **the whole CIR back end runs for every `fp`/`fn`/`fc` test**
(the only option that suppresses the back end is `-n`/`--no_code_gen`, which
`eccp` does not pass for these tests; `cmd_line.c:10698`). The same holds for
`edg_x86_64`: its C-generating back end runs too, but it is not expected to fail
on valid input, so nobody notices.

Consequences, by test type (run under a CIR config):

* **`fp` (front end, expect pass):** a valid program that uses a construct the
  CIR back end does not support now fails (compile exit 2 from a "CIR back end:"
  diagnostic) although the front end accepts it. In `imported/gnu/c` this is
  11254 `fp` tests, far more than the 2561
  run/link tests of the M2 corpus. Hence the corpus runs use `--types rp,lp`.
  `fp` tests that exercise only parsing/semantics get an unrelated CIR status.
* **`fn` (front end, expect failure):** a CIR error or a back-end crash cannot
  turn a `fn` test into a pass of the wrong kind unless the front end accepted
  the program (then the test already fails in every configuration); but a
  front-end error followed by the stream's `F_ABORT` is the expected path (the
  library reports "the front end aborted the ECIL stream" only as a note after
  the B2 fix). A crash would appear as `ABORT`, not `PASS`: such crashes count
  in the gate.
* **`fc` (front end, catastrophic error expected):** the stream is aborted the
  same way; no change.
* **`cp`/`cn`/`cc` (`eccp -c`), `lp`/`ln`, `rp`/`rn`/`ra`:** they run the back
  end anyway, so `--cpfe_only` is irrelevant to them.
* **`//script:` tests:** unaffected (they run their own command).
* **Time:** a `--cpfe_only` run in the CIR config costs the library's pipeline
  (MLIR, LLVM) on top of the front end (not measured per test), so a whole-suite run of the
  default suites under `edg_x86_64_cir` is not a good idea: run `edg_x86_64_cir`
  only on the `cir` suite and the corpus filters.

If the back end should be skipped for `fp`/`fn`/`fc` in a CIR config (so those
types test only the front end), the harness would add `--no_code_gen` for them
when `BACK_END_TYPE` is 3: a one-line change in `get_command` plus a config
flag. Not done: the current gate does not run those test types under CIR, and
running the back end on them is useful as a no-crash check on a large input
set (this is how the 11254 `fp` tests of `imported/gnu/c` can be used as an
extra robustness corpus).


## 9. C++ (M3): how a C++ test reaches `cpfe-cir`, the link, and the headers

Findings of cir-config (session 2), from reading `util/eccp.sh`,
`bases/docker/cir-env/gcc/edg_eccp_config`, `edgtest/exec/__init__.py`, the FE
(`statements.c`, `expr.c`) and the test files. Nothing was run: `cpfe-cir`
still rejects C++, so these points need a first real run to confirm.

### 9.1 What decides C or C++, and how `--c++NN` arrives

* `eccp` starts with `c_mode=0` (**C++**, `eccp.sh:397`). A C option (`--c`,
  `--c99`, `--c11`, `--c17`, `--c23`, `--gcc`, `--ms_c11`, ...) sets it to 1; a
  C++ option (`--c++`, `--c++03`, `--c++11` ... `--c++26`, `--g++`, `-b`, ...)
  sets it back to 0 (`eccp.sh:1938-1952`). **The source suffix (`.cpp`, `.cc`,
  `.C`) is not looked at** for this. The language of a C test therefore comes
  from the options of its case (`--c11`, `--gcc`, ...), that of a C++ test from
  its `--c++NN` option (or from nothing, which means C++ with the front end's
  default dialect).
* Per-case options come from the test's `//options:` lines
  (`-DTEST_NUMBER=<n> --c++17 ...`). `eccp` copies unknown options to the front
  end unchanged, so `--c++NN` reaches `cpfe-cir` as written. The bare `cpfe-cir`
  rejects C++ today (`cmd_line.c`); M3 lifts that gate.
* The front end command line that `eccp` builds is
  `$CPFE $EDG_CPFE_DEFAULT_OPTIONS $feoptions --gen_c_file_name=X.int.s
  [--old_line_commands] X.cpp`, where `feoptions` has, for C++, the options of
  the case, `-D__CHAR_BIT__=8 -D_POSIX_SOURCE` (`EDG_DEFAULT_DEFINES`,
  `EDG_DEFAULT_CPP_DEFINES` of the base), `--sys_include=` for every directory of
  `INCLDIR` (see 9.4), and `-tused` (9.2). In the cir-env base
  `EDG_CPFE_DEFAULT_OPTIONS` is `--gnu_version=<scraped GCC version>`, as for C.
* `edgy` runs every C++ test of a CIR config with the same `CPFE`
  (`cpfe-cir`); `CPFE_CP` is not used (the `cir` entry of `config.json` has one
  `bin_path_env`, `CPFE_CIR`), so there is no `cpfe`/`cpfe-cp` split to handle.

### 9.2 Templates, `-tused`, the prelinker

* For a single-source compile-and-link (`rp`, `lp` tests) in C++ mode `eccp`
  appends **`-tused`** (`eccp.sh:2629-2634`), so the front end instantiates every
  used template in the one translation unit. Multi-file (`.mft.cpp`) tests are
  compiled one `cpfe-cir` run per file, with no `-tused`; the CIR configuration
  has `INSTANTIATE_TEMPLATES_EVERYWHERE_USED=1` and
  `AUTOMATIC_TEMPLATE_INSTANTIATION=0`, so every TU must emit the instances it
  uses as `linkonce_odr` (COMDAT) definitions: this matters for `mft` tests and
  for the C1/D1 strategy.
* `eccp` also keeps `automatic_instantiation=1` (`eccp.sh:154`) in C++ mode, so
  at link time it runs **`edg_prelink`** on the object files (the build step
  builds that target; `EDG_PRELINK_PATH` is exported by `environment.sh`, and
  the base adds `-c "nm -og 2>/dev/null"`). `cpfe-cir` writes no `.ii`/`.ti`
  files (no automatic instantiation), so the prelinker should find nothing to
  do. This is **unverified**. If it misbehaves, the base can set
  `EDG_PRELINK_PATH=/bin/true` (the config file is sourced before the variable is
  read, `eccp.sh:53` and `:107`); there is no switch in the config for
  `automatic_instantiation` itself (it is assigned after the config is sourced).
  Native macOS `edg_prelink` fails with templates (see the native workflow
  note); the Linux container build is the target here.

### 9.3 The link line and static initialization

* `eccp` links with `gcc` (not `g++`): `gcc <c_to_obj options> -L<LIBDIR>
  -o exe X.o -L$EDG_CIR_RT_LIBDIR -ledg_cir_rt -lstdc++ -lgcc_s -lm -lpthread`.
  `-lstdc++` is on the line for C and C++ tests alike, so no change is needed
  for C++ itself. The link output is passed through `edg_decode`
  (demangling); an undefined `_ZTV...` therefore shows as a demangled name
  (`vtable for X`) in the test output, which `edg-cir-summary`'s feature table
  also recognizes ("vtable", "typeinfo", ...). Which of the two spellings the
  link messages have must be checked on the first C++ run.
* The `patch`/`munch` step that finds and runs static constructors and
  destructors in the EDG runtime model is **skipped** (`EDG_SUPPRESS_PATCH_MUNCH=1`
  in the base, `eccp.sh` end). Static initialization must therefore come from
  the object file itself: `llvm.global_ctors` lowered to `.init_array` (what
  LoweringPrepare builds from `cir.global` ctor regions) and destructors
  registered with `__cxa_atexit`, run by glibc's `crt1`/`libc`. A C++ test that
  prints from a namespace-scope constructor is the first check of that.
* No EDG runtime library is linked (`EDG_RUNTIME_LIB=""`): `operator new` and
  friends, `__cxa_*`, `__gxx_personality_v0`, and the type-info classes come
  from `libstdc++`/`libsupc++`.

### 9.4 Headers: what the tests include

* **`imported/gnu/cpp` is preprocessed.** The files start with
  `# 0 "./cpp0x/initlist5.C"` / `# 0 "<built-in>"` and contain the expansion of
  every `#include`: 495 of its 1688 run/link tests (`rp`/`lp`) have expanded
  headers (all of them `stdc-predef.h`, 331 `c++config.h`, i.e. libstdc++), made
  by GCC 15.0.0 (snapshot 20250112, `/mds/gnu/build/gcc-15-20250112/...`) on
  glibc. They compile with the preprocessed text as it is, so `cpfe-cir` never
  looks for libstdc++ headers for them; at link time the container's libstdc++
  (GCC 15.3) provides the library, which is ABI-compatible with the 15.0
  headers' symbols as long as the headers' inline code only uses stable
  entry points. (A skew would show up as an undefined or a mismatching symbol
  of `std::`.)
* **`edg/` and `cwg/` are not preprocessed**, but almost nothing includes
  anything: of their 272 run tests (`rp`/`lp`) only 7 have an `#include`
  (`<assert.h>` 2, `<stdio.h>` 2, `<coroutine>`, `<typeinfo>`,
  `<initializer_list>`, `"../coro.h"`); the rest declare `extern "C"`
  functions themselves (`printf`, `abort`) or use `__builtin_*`. Those 7 get
  the system headers only with `//use_system_includes` (the cir-env base
  default is 0 since M3a, §9.7). With it,
  `--sys_include=` points at the GCC C++ include directories scraped at
  container start (`EDG_GCC_INCL_SCRAPE`, from `edg-scrape-compiler gcc --lang
  c++ includes`, set by `edg-container-exec`; C++ mode uses `INCLDIR`, C mode
  `CINCLDIR`). Under `edg_x86_64` the same tests use the EDG headers
  (`EDG_USE_SYSTEM_HEADERS` 0), so a status difference there is not necessarily
  a CIR bug (section 7).
* `<coroutine>` and `<typeinfo>` tests are out of the M3 scope.

### 9.5 FE exceptions stay on (user decision); what that means for the tests

An earlier draft of this section recorded what happens with the front end's
exceptions switched off, which the M3 plan had assumed: with
`exceptions_enabled` false a `try` block is a front-end error
(`pos_error(ec_no_exception_support)`, `statements.c:4841`) and a `throw`
expression too (`expr_pos_error`, `expr.c:38305`), so the whole translation
unit fails with `FE_DIAG`, not just the function. The **preprocessed GNU tests
have the library code expanded** with GCC's default `-fexceptions` already
applied (libstdc++'s `__try`/`__catch`/`__throw_exception_again` are plain
`try`/`catch`/`throw` in the text), and inline bodies are parsed whether used
or not. By word count 325 of the 495 `rp`/`lp` tests with expanded headers and
128 of the other 1233 contain `try`, `throw` or `catch`: about 450 of 1728
tests could never have got past the front end.

**Decision (user, 2026-10-10):** the front end keeps exceptions **on** in
`cpfe-cir`. There is no `--exceptions` rejection and no exceptions-off default.
The writer emits a no-exceptions stream and diagnoses `try`/`throw` per
emitted function (the function is dropped, as for other unsupported
constructs); cleanups needed only for exception paths go out as `eh` cleanups
and are ignored until M4. Consequences for the tests:

* The ~450 tests with `try`/`throw`/`catch` text are **in scope** of
  `cir-cpp-corpus.txt`: they can pass when the `try`/`throw` code is in
  functions that are never emitted (unused inline library code), and are
  `UNSUPPORTED` when it is emitted. `edg-cir-summary`'s "exceptions" feature row
  counts the second group.
* Expanded libstdc++ code that is emitted (it is `linkonce_odr`, so only what a
  test uses) is parsed with the same semantics as in the other configurations
  (`noexcept` and implicit destructor `noexcept` rules, placement `new` /
  `delete` pairing), so the headers parse as they do elsewhere.
* The M3 gate counts for the corpus should be read per feature: the number of
  tests whose only failure reason is "exceptions" is the M4 work list.

### 9.6 Corpus and reference

* `tests/.edgy/filters/cir-cpp-smoke.txt` (46 run tests, each with a feature
  comment) and `cir-cpp-corpus.txt` (the `imported/gnu/cpp` `rp`/`lp` tests
  minus the out-of-scope subdirectories, plus `edg/` and `cwg/`; 1688 test files
  match with `--types rp,lp`; many `edg/` tests have one case per `--c++NN`
  mode: 122 of the 241 `edg/` run tests have more than one case).
  Run them with `--types rp` (smoke) and `--types rp,lp` (corpus), as for C
  (section 8: the `fp`/`fn` types run the back end too).
* The reference configuration for "also fails without CIR" is `edg_x86_64`
  (it compiles C++ through the C-generating back end; its `imported.log` has
  2959 `gnu/cpp` lines). `edg-cir-summary` now matches C++ cases per
  (path, options), because a C++ test has one case per language mode.
* `tests/expectations/edg_x86_64_cir/` has `cir.log` and `imported.log` only;
  M3 adds `edg.log`/`cwg.log` lines when the first runs are recorded
  (recording on the x86-64 box, as for the other expectations).
* The default per-program timeout is now 120 s for every config (C1).

### 9.7 First end-to-end run (M3a, 2026-10-10)

Until the M3 freeze (when C++ became the default of `cpfe-cir` and the option
was removed), with `--ecil_allow_cxx` (a dev-only option of `cpfe-cir`,
undocumented; the default then rejected C++), `DEFAULT_INSTANTIATION_MODE=tim_used` in the
cpfe-cir cmakedef, and the option injected through `EDG_CPFE_DEFAULT_OPTIONS`
of the cir-env base (to be removed at the M3 gate), C++ goes through `eccp`
in both configs (`edg_x86_64_cir` and `edg_x86_64_cir_2p`):

* A program with a namespace, a function template and an aggregate, compiled
  `eccp --c++17 t.cpp -o t`, builds and runs in both modes (output correct).
  The `eccp` flow is as in 9.1-9.3: `cpfe-cir --ecil_allow_cxx --gnu_version=...
  --c++17 --sys_include=<libstdc++ 15 dirs> -tused --gen_c_file_name=t.int.s`,
  then `gcc -c t.int.s`, then **`edg_prelink t.o --` (a no-op: no change needed,
  exit status 0)**, then `gcc ... t.o -ledg_cir_rt -lstdc++ -lgcc_s -lm
  -lpthread`.
* **Names are not mangled yet**: `nm t.o` shows `f`, `twice` and `main`
  as plain `T` symbols (`N::f` and `N::twice<int>` should be `_ZN1N1fEi`
  and a weak `_ZN1N5twiceIiEET_S1_`). The writer sends the source name; that is
  slice S1 (mangled names, `linkonce_odr`).
* Static initialization with a constructor (`A g;`) is still dropped by the
  writer ("non-constant initializer of a static object"), so `.init_array`
  could not be checked yet.
* **The cir-env base's `--gnu_version=<GCC version>` conflicted with the old
  EDG language modes** (`-A`, `--strict`, `--cfront_*`, `-b`, `-r -x`):
  "Command-line error: language modes specified are incompatible" (rc 4,
  before any code is generated) for 154 of the 356 run/link cases of `edg/`
  and `cwg/` (all of `cwg/` has `-A`). Cause: the base copied the dev-env
  rule "with `EDG_USE_SYSTEM_HEADERS=1` inject `--gnu_version`" but made 1 the
  default. **Resolved (option parity with `edg_x86_64`):** the cir-env base now
  defaults `EDG_USE_SYSTEM_HEADERS` to 0 like the dev-env base, so nothing is
  injected, and a test that needs the system headers (and the GNU version they
  need) says `//use_system_includes`, which makes edgy set
  `EDG_USE_SYSTEM_HEADERS=1`. No test of `edg/`, `cwg/` or `imported/gnu/cpp`
  has that tag today. Evidence on the C side: the C smoke list is 40/40 in
  both configs and the C corpus is still 1599/2560 with 0 FE_CRASH/LIB_CRASH;
  two tests changed their recorded status (`torture/c23-stdarg-split-1a` and
  `c2x-stdarg-split-1a`, `FAIL(COMPILE)` to `CATASTROPHE(COMPILE)`: the second
  source file `#include <stdarg.h>` is not found in the EDG include directory;
  `edg_x86_64` records exactly this status), re-recorded on the box. The seven
  `edg/`/`cwg/` tests that had been replaced in the smoke list are back in it.
* `edgy` runs **every case of a test that has one case of the selected type**
  (a `--types rp` run also runs the `cn`/`fn` cases of `edg/rma` tests), so
  `edg-cir-summary --types rp,lp` (new) counts only the cases of those types.

Baseline of `cir-cpp-smoke.txt` (53 tests, `--types rp`; in-process, the 2p
numbers are within 3): 4 PASS (abi/empty8, inherit/conv2, init/dtor2,
template/mem-partial2), 43 UNSUPPORTED (by feature: class parameters and
results 11, expressions and operators 11, locals and temporaries 7, object
initialization 7, dynamic initialization of statics 5), 3 LIB_ERROR
(2 "a function's type changed after its body was emitted", 1 "FUNC_BEGIN:
function G-id defined twice"; `edg-cir-writer-stress --cxx` shows that the
streams of the same three are rejected by ecil-dump), 1 FE_DIAG (init/goto4:
"transfer of control bypasses initialization", which `edg_x86_64` also fails;
it and ext/cond4, a GNU `?:` extension test that is out of the M3 scope, have
since been replaced in the smoke list by expr/cond12 and init/goto1),
1 LINK_ERROR (edg/daveed/g21878a, inline variable templates), **1 RUN_FAIL
(opt/nrv3: a trivial `struct A { int i; }` returned by value from a function
and copied in `main` gives the wrong result, a wrong-code bug)**.


**Two reminders for the M3 gate (done at the M3 freeze).** (1) The
`--ecil_allow_cxx` injection in `bases/docker/cir-env/gcc/edg_eccp_config` and
the option itself (`cmd_line.c`/`cmd_line.h`) were removed once C++ was
accepted by default (section 11.2).
(2) **Before slice S1 the C++ smoke PASS counts mean little:** names are not
mangled yet (section 9.7), so a test passes when its functions and symbols
happen to link with plain C names; the four current PASS tests are of that
kind. Judge progress after S1.

## 10. Slice checkpoints (M3b)

`dev_tools/bin/edg-cir-checkpoint`, run on the Mac from the repository root
with the SSH master socket open, syncs the working tree to the box
(`rsync -a --checksum --no-times`, so changed sources get new modification
times), wipes the CIR build directories, runs `edg-cir-gate-evidence` there
and prints the table. Default steps: build, identity, smoke, corpus, asm-diff,
writer-stress, cpp-smoke, cpp-writer-stress, ctest (about 15 minutes);
`--quick` is build, smoke and cpp-smoke (about 2 minutes). The C assembly is
compared with `~/edg-cir-config/m3a-base-asm` (the corpus assembly of commit
908f7b680a, 1835 files plus manifest.json, deterministic): every difference is
listed with its class (order, labels, layout, symbols, other, status) and a
source hint. The C smoke and corpus must show no differences from the
recorded expectations (the corpus step fails on any). The C++ smoke step fails
on a crash (`--smoke-all-pass` makes any non-passing test a failure); its
tables, per configuration, are in `summary-<config>.txt` of the evidence
directory. The box tree is `~/edg-cir-config/m3b-int` (`--tree` selects
another). Evidence of a run: `~/edg-cir-config/m3b-int/.tmp/checkpoint-<stamp>/`.

Note on re-recording: when `-W` finds that a new output equals the `default`
recording, edgy deletes the per-config file; delete it in the Mac tree too
(this happened for `torture/c23-stdarg-split-1a` and `c2x-stdarg-split-1a`).

The C++ stress steps (`cpp-stress`, `cpp-stress-asan`; select them with
`--steps`, they are off in the default checkpoint) run
`cir_src/scripts/box_stress.sh` over the synthetic C++ streams
(`ecil-stress-gen.py cxx_locals ... cxx_funcs`) through `edg-cir` under a
256 KiB stack, building `build/cir` (or `build/cir-asan`) first: every case
must have rc=0 and run=0 (the streams are self-checking programs), in at most
30 s and 400 MB (ASan build: 1200 MB, smaller sizes). First numbers
(2026-10-11, m3b-int): cxx_locals:2000 2.0 s / 93 MB, cxx_temps:5000 7.3 s /
111 MB, cxx_funcs:6000 1.8 s / 181 MB; the ASan variant's largest is
cxx_funcs:1000 at 452 MB. `edg-cir-perf --stream FILE|KIND:N` measures the
library alone on such a stream (instructions, CPU time, peak RSS), and the
gate's perf step now includes `cxx_funcs:6000` (first measurement: 19,061M
instructions, 1.8 s, 170 MB; a baseline from the M3a library is not possible,
because that library cannot read the C++ opcodes of the stream).

The `poison` step (`edg-cir-poison-check`; region-lifetime safety, on for the
M3 gate): ASan cannot see a front end that reads an IL region after it was
freed, because the blocks of a region are pooled (`free_mem_block` puts them
on `reusable_blocks_list`). What does see it is `OVERWRITE_FREED_MEM_BLOCKS`
(the freed block is filled with 0xdb) plus `EXPENSIVE_CHECKING`. **Both are
already on in the debug `cpfe-cir`** (`build/gcc-cir`; `--dump_configuration`
shows `OVERWRITE_FREED_MEM_BLOCKS 1`, `EXPENSIVE_CHECKING 1`; the debug
build-type fragment and `default/cpfe-cir.cmakedef` set the first, debug
builds the second) and off in `build/gcc-release-cir`. To tell what the
poisoning changes, there is a preset `linux-gcc-debug-cir-nopoison`
(`build/gcc-cir-nopoison`, macro configuration `linux-gcc-nopoison`: the same
front end with `OVERWRITE_FREED_MEM_BLOCKS=0`); the default presets are
unchanged. The step compiles the C corpus (`--types rp,lp`), the C and C++
smoke lists and `tests/tests/cir` (writer and cirgen) with the poisoning
build, the non-poisoning build and the release build, and requires no crash,
assertion or sanitizer report and byte-identical streams (poison vs. not, and
debug vs. release: the second is information only, see below).

**Debug and release front ends differ in their macro configuration, and the
CIR test configurations use the debug one.** `build/gcc-cir` (the preset
`linux-gcc-debug-cir`, macro configuration `linux-gcc`) is what
`edg_x86_64_cir`/`_2p` and the checkpoints run; `build/gcc-release-cir`
(`linux-gcc-release-cir`, configuration `linux-gcc-release`) is the shipping
one. `cpfe-cir --dump_configuration` differs in these macros (debug / release):
the ones that change what the front end builds are `PRAGMA_WEAK_ALLOWED` (1 / 0),
`NEW_CAN_BE_FOLDED_INTO_CTOR` (0 / 1), `ASSIGNMENT_TO_THIS_ALLOWED` (0 / 1),
`TARG_ZERO_WIDTH_BIT_FIELD_AFFECTS_STRUCT_ALIGNMENT` (1 / 0),
`USE_INLINE_EXPANSION` (0 / 1), `USE_INLINING_HINTS` (0 / 1),
`USE_EDG_NAMESPACE` (0 / 1), `MICROSOFT_MODE_TYPE_INFO_IN_NAMESPACE_STD`
(1 / 0), `SUPPRESS_MICROSOFT_ATTRIBUTE_PROCESSING` (0 / 1),
`DEFAULT_MAX_DEPTH_CONSTEXPR_CALL` (256 / 512) and `CHECK_SWITCH_DEFAULT_UNEXPECTED`
(0 / 1); the rest are checking and test aids (`DEBUG`, `CHECKING` is 1 in both,
`EXPENSIVE_CHECKING` 1 / 0, `OVERWRITE_FREED_MEM_BLOCKS` 1 / 0,
`NEED_IL_DISPLAY` 1 / 0, `INCLUDE_EDG_TEST_*`, `SEQUENCING_DIAGNOSTICS_ENABLED`,
`ABORT_ON_INIT_COMPONENT_LEAKAGE`, `ADD_CHECKING_PRAGMAS_FOR_INTERNAL_TESTING`,
`ENABLE_TRANS_UNIT_TEST_MODE`, `INCLUDE_COMMENTS_IN_ASM_FUNC_BODY`,
`DEFAULT_DISPLAY_ERROR_CONTEXT_ON_CATASTROPHE`, `NUM_NAMED_REGISTERS`). So
streams of the two builds can legitimately differ (e.g. `#pragma weak _exit`
and the builtin's noreturn in torture/builtin-noret-1, or the placement of the
`operator new` declaration in cirgen/cxx-s4-new-delete, with
`NEW_CAN_BE_FOLDED_INTO_CTOR`), and release-only IL shapes need their own test:
the `release-smoke` gate step runs the C and C++ smoke lists through the
release front end (`edg-docker-test --posture cir-release`, which builds
`linux-gcc-release-cir` into `build/gcc-release-cir`) in both modes: the C list
must pass completely and no C++ test may pass in the debug front end and not in
the release one. First run (S3/S4 tree): C 40/40 in both configs; C++ 34/53 in
both builds, none only in release.

## 11. M3c: C++ performance input, the freeze patch, the expectations procedure

### 11.1 C++ perf/RSS input
`edg-cir-rss --generate-cxx-functions N -o big.cpp` writes a C++ file of about
N functions (20 per unit: a namespace with a base and a derived class
(constructors, copy constructor, destructor, members), a function template used
with three types, an inline function, a function with a static local, a
namespace-scope variable with a dynamic initializer, a lambda, new/delete of
objects and arrays; no virtuals, no exceptions). `main()` prints a checksum:
with 6000 functions (300 units, 12,008 lines) `g++ -std=c++17` and
`eccp --c++17` through `cpfe-cir` give the same output (`94935 0 2691`) and no
diagnostics. `edg-cir-perf --only cxx` (and the gate's perf step, `--perf-only
cxx`, default) measures it with the release preset in the four configurations
(`2p-fe`, `2p-edg-cir`, `inproc`, `keep`) and the C generating path
(`cgen-fe`: the default release `cpfe` (`build/gcc-release`, built by the step)
on the C++ source; `cgen-total`: `eccp -c` = that front end plus gcc on the
generated C). `edg-cir-rss --cxx FILE` does the same for time and RSS.

First table (2026-10-11, tree m3b-int, S1-S5 state, solo run; instruction
counts are deterministic, times and RSS vary with the load):

| configuration | instructions (M) | CPU s | peak RSS MB |
| --- | ---: | ---: | ---: |
| two-process, front end | 556.5 | 0.15 | 60 |
| two-process, edg-cir | 11,757.7 | 1.30 | 147 |
| in-process | 12,298.4 | 1.48 | 185 |
| `--ecil_keep_regions` | 12,316.7 | 1.57 | 197 |
| C generating: front end (`cpfe`) | 868.2 | 0.20 | 85 |
| C generating: front end + gcc (`eccp -c`) | | 1.54 | 172 |

So for this input the CIR path is as fast as the C generating path with gcc
(1.48 s vs 1.54 s) and uses 7% more memory in process (185 vs 172 MB); the
front end alone is lighter in CIR mode (60 vs 85 MB, 0.15 vs 0.20 s) because of
per-function streaming.

### 11.2 The freeze patch (applied at the M3 freeze, 2026-10-11)
`build/m3-freeze-accept-cxx.patch` (a `git apply` patch) accepted C++ by
default and removed `--ecil_allow_cxx`; it was applied at the M3 freeze: in `src/cmd_line.c` the option description, its `case`,
its reset and the "C++ is not supported yet" check; in `src/cmd_line.h` the
enumerator and the flag; the injection in
`bases/docker/cir-env/gcc/edg_eccp_config`; and a new `src/Changes` entry
(dated 10/11/26). The fp goldens' `//options:` lines that named
`--ecil_allow_cxx` were changed by cir-writer in the same commit.

### 11.3 Recording the C++ expectations (done at the M3 gate)
`dev_tools/bin/edg-cir-record` does the whole procedure on the box and copies
the result back (see its docstring):

1. Sync and build the tree (`edg-cir-checkpoint --steps build`), so the box tree
   equals the Mac tree (the copy back replaces `tests/expectations/<config>/`
   and the changed `.rto` directories).
2. `edg-cir-record --filter @tests/.edgy/filters/cir-cpp-smoke.txt --types rp
   --configs edg_x86_64_cir,edg_x86_64_cir_2p` (both configs for the smoke
   list, whose output differs per config), then `--filter
   @tests/.edgy/filters/cir-cpp-corpus.txt --types rp,lp --configs
   edg_x86_64_cir` (the corpus in the one-process config only).
3. The tool records with `-W` (log lines `gnu/cpp/...`, `edg/...`, `cwg/...` in
   `tests/expectations/edg_x86_64_cir/imported.log`, `edg.log`, `cwg.log`, and
   per-config recordings `.<test>.rto/edg_x86_64_cir.<case>.<command>.txt`).
   A test without a per-config file compares with the `default` recording (the
   C generating output); edgy deletes a per-config file that equals it.
4. Recordings over 100 KB are deleted (M2's rule: 26 tests of the C corpus),
   and the tool patches the log lines of those tests to `--OUTPUT MISMATCH`
   (a second run without `-W`). Check that only a few C++ tests are affected
   (the libstdc++-expanded tests have big diagnostics).
5. Review: `git status --short tests/ | head`, `git diff --stat tests/`; the
   per-config recordings are text, so look at a few; commit nothing here.
6. Verify with the checkpoint: the C++ smoke and the corpus step must show 0
   differences from the recorded expectations (run `edg-cir-gate-evidence
   --only cpp-corpus` for the C++ corpus). Per-config recordings are legitimate
   where output differs between the configs (the library's diagnostics appear
   in a different order in the two modes); status differences between the two
   modes are not (the `cpp-modes` step must stay at 0 different).

Tested on the C side (one test, limit 1 KB): the recording was deleted and its
log line changed to `FAIL--OUTPUT MISMATCH`, the copy back worked.

### 11.4 Smoke list for the M3 gate; Microsoft-mode cases
`cir-cpp-smoke.txt` (56 tests) holds only tests expected to pass within the M3
scope, each also passing in `edg_x86_64` (no reference failure): the tests that
failed there too or hit documented limits were replaced (aligned-new6 by
aligned-new3, copy3 by elide2 and copy5, ref19 by ref22 and ref15, expr/cond6 by
cond1, init/lifetime2 by edg/rma/lifetime22 and init/cleanup4). First run with
the S6 tree: 56/56 in both configurations, in the debug and the release front
end.

A test case whose options select Microsoft mode, C++/CLI or C++/CX
(`--microsoft*` also abbreviated as `--microsoft_v=1930`, `--ms_*`,
`--c++cli`, `--c++cx`) is reported as `REQUIREMENTS NOT MET` when the front end
was built without Microsoft extensions (`MICROSOFT_EXTENSIONS_ALLOWED 0` in its
`--dump_configuration`, as `cpfe-cir` is): `edgtest.exec._case_requests_unsupported_mode`.
The other configurations have the macro set to 1, so nothing changes for them
(46 such cases in the C++ corpus run).
