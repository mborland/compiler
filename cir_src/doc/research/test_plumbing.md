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
| `EDG_USE_SYSTEM_HEADERS` | default 0 (EDG headers) | default **1** | libstdc++/glibc are the runtime, so the matching headers must be used (scrape via `EDG_GCC_INCL_SCRAPE`, exported by `edg-container-exec:67`); C smoke tests have no includes, C++ tests (M3) need this. Per-test `//use_system_includes` already forces 1 (`exec/__init__.py:1035`) |
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

* **`EDG_USE_SYSTEM_HEADERS` is 1 (dev-env: 0).** The generated code links
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
