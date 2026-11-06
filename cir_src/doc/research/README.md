# Research notes: index

These notes are the evidence behind the design: studies, spikes and
measurements, each written at one milestone. They are not kept up to date.
The current state and the decisions in force are in `../project.md`.

- **live**: still the best description of its subject; read it when you
  work on that subject.
- **historical**: a record of a past milestone; its numbers or pin are
  outdated.
- **superseded**: replaced by the file named.

No file is moved or renamed, because code, tests and tools cite these
paths.

| File | Milestone | Status | Cited by |
| --- | --- | --- | --- |
| `back_end_type_audit.md` | R/M0 | historical (M0 done) | `design.md` |
| `builtin_table_regen.md` | M2.1 | live (an unfixed FE-maintenance finding) | `writer.md` |
| `cir_api_inventory.md` | R | historical (pin 894bfff5; `lib/cirgen/CIRCompat.h` is the live record) | `design.md`, `spike/README.md` |
| `cirgen_map.md` | R/M1 | live (clang CIRGen reuse map) | `design.md`, `lib/cirgen/expr.cpp:13` |
| `cxx_cir_differential.md` | M3c | live (the differential method) | `scripts/cir-{compare,diff-classify,op-compare}.py`, `spike/cxx-shapes/README.md` |
| `cxx_cir_shapes.md` | M3a | live (C++ CIR shapes) | `design.md`, `ecil_format.md`, `spike/cxx-shapes/README.md` |
| `il_to_ecil_map.md` | R/M2 | historical (C subset; `writer.md` is current) | `design.md` §8 errata |
| `il_to_ecil_map_cxx.md` | M3a | historical (the IL facts behind `ecil_format.md` §11) | `ecil_format.md`, `design.md` |
| `il_to_ecil_map_m4.md` | M4a | live §13.2 (M4 diagnosed list) and §13.4 (FE issues); the rest historical | `ecil_format.md`, `writer.md`, `test/writer/tools/il_query.py`, `test/writer/probes/m4/README.md` |
| `lib_refactor_m21.md` | M2.1 | historical | none |
| `m2_triage.md` | M2 | historical | none |
| `m31_reassessment.md` | M3.1 | historical (folded into `design.md` §11) | `design.md`, `scripts/box_pin_stress.sh` |
| `m3_ecil_v1_design.md` | M3a | superseded by `ecil_format.md` §11; its §8 spike verdicts are still cited | `ecil_format.md`, `design.md`, `spike/cxx-shapes/README.md` |
| `m3c_lib_error_triage.md` | M3c | historical | none |
| `m4_cir_differential.md` | M4 | historical (method: `cxx_cir_differential.md`) | none outside research |
| `m4_cir_shapes.md` | M4a | live (spike verdicts, the pin gaps G1–G8) | `lib/abi/README.md`, `spike/m4/README.md`, `design.md`, `ecil_format.md` |
| `m4_ecil_design.md` | M4a | superseded by `ecil_format.md` §12 | `ecil_format.md`, `design.md` |
| `m4_fe_facts.md` | M4a | live (FE layout facts against clang) | `lib/abi/abi_check.cpp`, `lib/abi/README.md`, `edg-cir-fe-facts`, `ecil_format.md` |
| `m4_library_status.md` | M4 | live (§2 library gaps with tests, §3 watch list, §5 unenforced writer assumptions) | `project.md` §4, §5 |
| `m4_stress.md` | M4 | live (watch-list numbers, the stress method) | `edg-cir-gate-evidence`, `test/cirgen/cxx-s4-stress-diamond.test`, `scripts/box_{pin,m4}_stress.sh` |
| `m4_upstream_drift.md` | M4 | superseded by `m5_upstream_drift.md` | `design.md`, `ecil_format.md` |
| `m5_baseline.md` | M4.1 | historical (numbers superseded; §1 still describes the `opt-corpus` method) | `design.md`, `edg-cir-tools-test` (path fixture) |
| `m5_design.md` | M5a | live (S1–S4 design and gate targets) | `lib/driver/chunk.cpp`, `writer.md`, `edg-cir-{perf,lazy-identity,attr-diff}`, `cir-{lazy-triage,attr-known}.txt` |
| `m5_library.md` | M5a | historical (census, prototype and pin study) | `design.md`, `cir-attr-known.txt` |
| `m5_upstream_drift.md` | M4.1/M5p | live (the latest drift study) | `design.md`, `m5_design.md` |
| `m5_writer.md` | M5a | live §6 (region keep) and §7 (the mangler, M5.1); the rest historical | `writer.md`, `m5_design.md`, `test/writer/tools/README.md` |
| `test_plumbing.md` | R to M3c | live §1–§9 and §11.3 (the `edg-cir-record` procedure); historical §10 | `design.md`, `edg-docker-test`, `edg-cir-rto-diff` |
| `writer_hooks.md` | R | historical (`writer.md` and `design.md` are current) | `design.md` |
