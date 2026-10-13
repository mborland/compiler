# M4a spikes (K1 to K7, K6c)

Hand-run experiments at the LLVM pin (`cir_src/LLVM_REVISION`) that show which
CIR shapes the library can emit for virtual functions, RTTI, exceptions and
member pointers.  Nothing here is part of the build or the tests; the results
and verdicts are in `doc/research/m4_cir_shapes.md`.

Run inside the cir-env image from the repository root (box: see
`handoff_2.md` section 3):

    bash cir_src/spike/m4/spike.sh k1_vtables
    bash cir_src/spike/m4/spike.sh k4c_eh_spec -S c++14
    python3 cir_src/spike/m4/k7_layout/layout_m4.py cir_src/spike/m4/k7_layout/classes.cpp

`spike.sh NAME` takes `NAME/cir.cpp` (or `cir.c`) through the pin's CIRGen
(`clang -cc1 -fclangir -emit-cir -clangir-disable-passes`, standing for what
our library would emit), optionally through `NAME/patch.py` (a hand edit that
stands for a shape our library emits differently), then through our pipeline's
pass list (`cir2obj.sh`: canonicalize, target-lowering, cxxabi-lowering,
lowering-prepare, call-conv-lowering, then the direct lowering, which runs
hoist-allocas, flatten-cfg, eh-abi-lowering and goto-solver), links it with
`NAME/host.cpp` compiled by g++, runs it and compares the output and exit code
with an all-g++ build.  `NAME/link.flags` holds linker flags (`--wrap` of the
`__cxa_*` functions to count calls).  It also builds the pin's own full
`clang -fclangir` pipeline for comparison.

| Dir | Spike |
| --- | --- |
| `k1_vtables` | K1 vtables, multiple and virtual inheritance, VTT, construction vtables |
| `k2_thunks` | K2 thunks |
| `k3_rtti` | K3 RTTI, `dyn_cast`, `typeid` |
| `k4_eh`, `k4b_free_exception`, `k4c_eh_spec` | K4 exception handling core |
| `k5a_local_init`, `k5b_array_partial`, `k5c_unwind_c`, `k5c2_unwind_c_nothrow`, `k5d_global_throw` | K5 EH interactions |
| `k6_member_pointers`, `k6b_method_const_adj` | K6 member pointers |
| `k6c_pm_zero`, `k6c2_zero_attr` | K6c zero- and value-initialization of types with data-member pointers (null is -1); K6c2 is the library's current `#cir.zero` form (FAILs, by design) |
| `k6d_base_member_const` | K6d `int D::*` into a non-virtual base (empty base, non-zero base offsets): `#cir.data_member<[path]>` is enough |
| `k10_throw_model` | K10 the final THROW model (implicit deactivatable free, temporaries inside); `spike.sh` reports FAIL against g++ by design (`~A` runs during unwinding in clang, before the throw in g++): compare `mixed.out` with a classic-clang build instead |
| `k11_vptr_const` | K11 vptrs in constants: `#cir.global_view<@_ZTV.., [0, 2], address_point true> : !cir.vptr` in the base (primary) and member subobjects of a constant `D : C, B` and `M { B b; }` (the library's form, `cxx-s1-const-vptr.test`) |
| `k7_layout` | K7 record layout of polymorphic classes (a Python port of CIRRecordLowering driven by the writer-streamable facts) |
