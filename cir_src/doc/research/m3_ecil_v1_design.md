# M3 design pass: ECIL v1 (core C++) contract outline

Author: the orchestrator's design pass (session 2, 2026-10-10). Status:
**input to gate M3a**. The normative spec is `ecil_format.md` §11, finalized
at M3a after the cir-lib spikes and the cir-writer IL map. Where they
disagree with this note, they win. File and line references are to the
tree at commit 59a33cffa7 and to llvm-project at the pin (894bfff5).

Fixed decisions:
- no exceptions in M3, with cleanups emitted as `normal`;
- virtual functions, virtual bases, RTTI and EH are diagnosed (M4);
- both sides are non-recursive;
- the stream is pointer-free;
- validation is strict;
- output is deterministic;
- the FE sends facts and the library does the Itanium mechanics;
- ECIL 1.0 is declared at the M3 gate.

## Findings at the pin that shape the design

- **Cleanup scopes.** `cir.cleanup.scope` has no results (CIROps.td:1434–1495). CIRGen
  carries a value out of a full-expression through a spill slot,
  `tmp.exprcleanup` (CIRGenCleanup.cpp:300–375). The library does the same.
- **Gotos.** FlattenCFG decides whether a goto leaves a cleanup by checking whether its
  target label lies inside the cleanup body (`gotoTargetsLabelInRegion`,
  FlattenCFG.cpp:984–1040). It handles return/break/continue
  (1096–1120, 1540–1590). `GOTO_STMT.n_cleanups` is therefore not needed.
- **Return hazard.** FlattenCFG moves a return operand that is a load of an entry-block
  alloca after the cleanups (1142–1180). So `int i=0; G g(&i); return i;`
  would return the value `~G` wrote. The fix is to route value returns
  through `__retval` whenever a cleanup is open, which is CIRGen's shape.
- **Guard information.** LoweringPrepare reads the guard name from `dynamic_init_guard` and its
  facts from `dynamic_init_info` (LoweringPrepare.cpp:1600–1640;
  CIRAttrs.td:2190, 2367). It needs the `cir.lowering_lang_options` module
  attribute (LoweringPrepare.cpp:317).
- **`static_local` marking.** Every `get_global` of a guarded local static must carry `static_local`
  (CIRDialect.cpp:2703–2713). The library must know at declaration time that
  the variable is guarded.
- **Init order.** Global init order is the module walk order of `cir.global`
  (LoweringPrepare.cpp:3313–3338, 1892–1975).
- **Ctor/dtor regions.** The ctor regions of `cir.global` and `cir.local_init` are single-block
  (CIROps.td:3492, 4494). A dtor region must start with a `get_global` of the
  variable (LoweringPrepare.cpp:1286–1365).
- **CallConvLowering and copy elision** (CIRABIRewriteContext.cpp):
  - The callee's single `__retval` alloca is rewired to the sret pointer. It
    needs every return to be `cir.return (cir.load __retval)` (1081–1146).
  - At a call site, the destination becomes the sret slot only if the call
    result's single use is a `cir.store` into a dominating address
    (1255–1330).
  - A by-value non-register argument must be a `cir.load` of an alloca
    (1810–1835).
  - The callee's parameter must have exactly one use, its spill alloca
    (785–805).
  - The switch for all of this is `arg_passing_kind` in `cir.record_layouts`
    (CallConvLoweringPass.cpp:85–90).
- **Ctor/dtor variants** (CIRGenItaniumCXXABI.cpp:298–400). With no virtual bases:
  - discardable linkage (linkonce_odr, internal): replace C1 uses with C2;
  - weak_odr: C2 in a `C5`/`D5` comdat, with C1 as an alias;
  - otherwise: C1 is an alias of C2.

  FuncOp `aliasee` lowers to `LLVM::AliasOp` (LowerToLLVM.cpp:2893–2895).
- **Loops** have an optional per-iteration cleanup region (CIROps.td:2350–2420).
  It is not used: the writer rewrites loop condition variables instead.
- **Lambda static invokers** have no body in the IL (`sfk_lambda_entry_point`,
  func_def.c:2626–2721). The writer synthesizes one.
- **`remove_unneeded_instantiations`** only affects linkonce candidates
  (templates.c:20999–21060), so RETRACT can mean "discardable".

## 1. New and changed records

### 1a. New opcodes

| Op | Name (operands) | Stack | Section and rules |
|---|---|---|---|
| 0x33 | `STRUCTOR(g_complete, g_base, flags{dtor, has_vbases})` | — | Declaration section. Both are declared functions of the same type and linkage, not already in a group, introduced earlier in the section. `has_vbases`=0 in v1. A body may be streamed only under `g_base`. The C1/C2 names differ in exactly one character, `1`↔`2`. 0x34 is reserved for D0 (M4) |
| 0x46 | `RETURN_FROM_SLOT` | — | Body statement. The function returns a record or array, and the return object was initialized through `RETURN_SLOT` |
| 0x65 | `CLEANUP_BEGIN(kind, g\|nil, flags=0)` | — (opens code) | Body. The kind is normal=1/eh=2/all=3 (= `cir::CleanupKind`) or exit=4. `g` is required if and only if the kind is exit. The code is stack-neutral and must not consume values pushed before it. LOCAL, LABEL, GOTO, RETURN, BREAK, CONTINUE, nested CLEANUP and FULLEXPR are forbidden inside it. exit is allowed only inside STATIC_INIT |
| 0x66 | `CLEANUP_BODY` | — | Ends the cleanup code. For normal/eh/all the protected range starts here. For exit it records the registration and opens no range |
| 0x67 | `CLEANUP_END` | carries ≤1 value | Explicitly closes the innermost cleanup begun in the current scope region. Not for conditional cleanups. Mostly an M4 tool |
| 0x68 | `FULLEXPR_BEGIN` | — | Opens a scope region |
| 0x69 | `FULLEXPR_END` | carries ≤1 value | Closes all the cleanups begun inside, conditional ones included, innermost first. Values pushed before BEGIN must not be consumed inside. A record prvalue may not be carried |
| 0x6A | `STATIC_INIT_BEGIN(g, flags{inline_var, template_instance})` | — | Body, in a function or in the F_GLOBALS body section. `g` is a static-duration definition with no VAR_INIT. In a function, `g` is `static_local\|guarded`. At most one per g |
| 0x6B | `STATIC_INIT_END` | — | Stack-neutral. No RETURN, GOTO, LABEL, BREAK or CONTINUE inside (single block) |
| 0x6C | `MEMINIT_BEGIN(kind, idx, flags{compiler_generated, default_member_init})` | — | Only at the start of a ctor body. The kind is vbase=0/base=1/field=2/delegation=3 (EDG `cik` order). `idx` is a REC_FIELD index and strictly increases. A delegation must be the only one. vbase is an error in v1 |
| 0x6D | `MEMINIT_END` | — | Stack-neutral |
| 0x92 | `BASE_CAST(t_derived, t_base, offset, flags{nonnull})` | prv ptr → prv ptr | `t_derived` has a REC_FIELD with BASE of type `t_base` at `byte_off==offset` |
| 0x93 | `DERIVED_CAST(t_base, t_derived, offset, flags{nonnull})` | prv ptr → prv ptr | The same check, reversed |
| 0x94 | `RETURN_SLOT` | → glv | The return type is a record or array |
| 0x95 | `ARRAY_CTOR_BEGIN(t_elem, flags{fixed})` | `fixed`: glv array →; otherwise prv elem*, prv size_t count →; then pushes prv elem* inside | The body must consume the pointer and stay stack-neutral. The body is a scope region with no block-creating statements |
| 0x96 | `ARRAY_CTOR_END` | — | |
| 0x97 | `ARRAY_DTOR_BEGIN(t_elem, flags{fixed})` | As 0x95; reverse order | |
| 0x98 | `ARRAY_DTOR_END` | — | |
| 0x99 | `NEW_ARRAY_SIZE(t_elem, cookie)` | prv count (any integer) → prv size_t | `count*sizeof+cookie`; a negative count or overflow gives SIZE_MAX. A non-zero cookie must equal max(sizeof size_t, align t_elem) |
| 0x9A | `NEW_ARRAY_COOKIE(t_elem, cookie)` | prv void*, prv size_t → prv elem* | The same cookie check |
| 0x9B | `DELETE(g_delete_fn, g_dtor\|nil, t_obj, flags{array, sized, aligned})` | prv ptr → | Includes the null check. `array` requires a cookie (a non-trivial dtor, or `sized`) |
| 0x9C | `ZERO_BYTES(bytes)` | glv → | Only `bytes` bytes, for base subobjects whose dsize < size |

0x6E, 0x6F and 0xD7–0xDF stay free for M4 (try/catch/throw).

### 1b. New flag and enum values (no arity changes)

| Record | Addition |
|---|---|
| `TY_RECORD_BODY` flags | `NONTRIVIAL_ABI` 0x04 (= EDG `!construction_by_bitwise_copy_allowed \|\| has_deleted_or_nontrivial_destructor`, il.c:10778–10785), `NONTRIVIAL_DTOR` 0x08 |
| `TY_RECORD_BODY.data_size` | **Meaning change:** Itanium dsize (`data_size_of_type`, types.h:824). Still equal to size for C |
| `REC_FIELD` flags | `BASE` 0x10 (a direct non-virtual base), `POTENTIALLY_OVERLAPPING` 0x20 (the member uses the base-subobject type). Order: bases in declaration order, then data members |
| `PARAM` flags | `THIS` 0x01 |
| `LOCAL` flags | `RETURN_SLOT` 0x01 (NRVO: the variable is `__retval`), `TEMPORARY` 0x02 (naming hint) |
| `STORE` flags | `INIT` 0x02 (the destination is fresh storage: store the call result directly, no agg.tmp), `DSIZE` 0x04 (copy only data_size bytes) |
| `DECL_FUNC` flags | `COMDAT` 0x40 (own comdat; set if and only if the linkage is linkonce_odr/weak_odr) |
| `DECL_VAR` flags | `COMDAT` 0x80, `GUARDED` 0x100 (dynamic init with a guard: static locals, vague-linkage variables) |
| linkage | linkonce_odr=3 and weak_odr=4 already exist; the writer now produces them |

**Other mappings:**
- **References** need no new record. They are `TY_PTR`, including reference
  members; the writer stops using TY_OPAQUE for them. `eok_ref_indirect` is
  `LOAD DEREF`, and binding a reference is `ADDR`.
- **`this`** is `PARAM(l, T*, T*, "this", THIS)`, emitted first. Non-static
  member `TY_FUNC` types put `T*` first.
- **Member calls** are an ordinary `CALL` with the object pointer as
  argument 0.
- **Virtual calls** are diagnosed (M4). A vptr class gets an unnamed
  `TY_OPAQUE(8,8)` field and is therefore ABI-opaque under the existing
  rule.

### 1c. Normative semantics

**Cleanups.**
- The writer emits `CLEANUP_BEGIN … code … CLEANUP_BODY` immediately after
  an object is completely constructed, code first.
- The protected range lasts until CLEANUP_END or the end of the innermost
  enclosing scope region. The scope regions are FUNC, BLOCK, the FOR init
  scope (FOR_BEGIN…FOR_END), an IF arm, a loop body, the SWITCH body,
  FULLEXPR, STATIC_INIT, MEMINIT and the ARRAY loop body.
- **Conditional cleanups.** Ternary arms and the right-hand side of `&&`/`||`
  are *conditional regions*, not scope regions. A CLEANUP_BEGIN inside one
  is conditional and requires an enclosing FULLEXPR in the same function.
  The library defers it to that FULLEXPR's end with an active flag
  (CIRGen's mechanism, CIRGenCleanup.cpp:104–130, 221–300).
- **Loop conditions.** A CLEANUP_BEGIN directly in a loop condition or step
  region outside a FULLEXPR is an error; the writer rewrites those loops.
- **Exits** through a protected range run its code (FlattenCFG).
  `GOTO_STMT.n_cleanups` must be 0.
- **The M3 library** (exceptions=0) maps `all` to `normal`, and decodes `eh`
  and drops it. M4 needs no format change.

**Copy elision.**
- A prvalue of a `NONTRIVIAL_ABI` record may only be consumed by one of:
  - `STORE(INIT)` into a destination pushed before the call's arguments;
  - `MATERIALIZE_TEMP`;
  - `DROP`.
- As a call argument it must be `LOAD` of a glv naming local storage
  (`LREF` of a LOCAL, PARAM or temporary).
- `DUP`, a TERN result and `RETURN_STMT(true)` on such types are errors.
  Returns use `RETURN_SLOT … RETURN_FROM_SLOT`.
- The library guarantees the store/load shapes CallConvLowering needs, so
  the ABI decision stays in the library.

**RETRACT(g).**
- Allowed only on internal or linkonce_odr entities.
- It marks g discardable and clears its unsupported obligation at finish.
- It never deletes; GlobalDCE drops the entity if it is unreferenced.

**NAME_BIND.**
- It only binds G-ids introduced with a nil name. The linkage may change at
  the same time (an unnamed class that gets a name for linkage purposes).
- The writer uses nil plus a pending list when a predicate mirroring
  `must_wait_for_mangling_reasons` (scope_stk.c:9166–9239) holds:
  - an originally-unnamed class without a discriminator;
  - a closure with a pending parent;
  - a lambda in a default argument;
  - a structured-binding container.
- C++ streams always set LATE_NAMES; F_NAMES comes before F_END.
- CHECKING builds re-mangle every named non-internal G-id at the end of the
  TU and assert that it did not change.

## 2. Worked encodings (symbolic)

**Notation:**
- The class is `struct A{int x; A(); A(int); A(const A&); ~A();}`, sent
  with NONTRIVIAL_ABI|NONTRIVIAL_DTOR.
- C1, C2 and D1 are A's G-ids.
- `fV` is `void(A*)`.
- `nae` is kind all.

**(1) A loop with a local and `break`**

`for(int i=0;i<n;++i){ A a; if(i==3) break; g(a.x); }`
```
FOR_BEGIN  LOCAL l2 int "i"  LREF l2 CONST_INT int 0 STORE INIT none
FOR_COND true  LREF l2 LOAD LREF l1 LOAD CMP lt
FOR_STEP true  LREF l2 INC_DEC pre_inc nsw DROP
FOR_BODY BLOCK_BEGIN
  LOCAL l3 A "a"  LREF l3 ADDR CALL C1 fV 1 0
  CLEANUP_BEGIN nae nil 0  LREF l3 ADDR CALL D1 fV 1 0  CLEANUP_BODY
  LREF l2 LOAD CONST_INT int 3 CMP eq IF_BEGIN false BREAK_STMT IF_END
  LREF l3 FIELD A 0 LOAD CALL g_g fI 1 0
BLOCK_END            -- closes a's cleanup, then the block
FOR_END              -- the init scope (i) closes here
```

**(2) A full-expression temporary**

`if (f(A()).x) h();`, where `B f(const A&)` returns a trivial B.
```
FULLEXPR_BEGIN
  LOCAL l2 A nil 4 TEMPORARY   LREF l2 ADDR CALL C1 fV 1 0
  CLEANUP_BEGIN nae nil 0  LREF l2 ADDR CALL D1 fV 1 0  CLEANUP_BODY
  LREF l2 ADDR CALL g_f fB 1 0  EXTRACT B 0
FULLEXPR_END         -- int carried out (tmp.exprcleanup), ~A() runs
TO_BOOL IF_BEGIN false CALL g_h fv 0 0 IF_END
```
In `int r = f(A()).x;` the `STORE INIT` into r comes before FULLEXPR_END, so
nothing is carried out.

**(3) A conditional temporary**

`int v = b ? A().x : 0;`
```
FULLEXPR_BEGIN LREF l_v  LREF l_b LOAD TO_BOOL TERN_BEGIN int
  LOCAL l2 A nil 4 TEMPORARY  LREF l2 ADDR CALL C1 fV 1 0
  CLEANUP_BEGIN nae nil 0  LREF l2 ADDR CALL D1 fV 1 0  CLEANUP_BODY  -- conditional: deferred
  LREF l2 FIELD A 0 LOAD
TERN_ELSE CONST_INT int 0 TERN_END  STORE INIT none
FULLEXPR_END
```
The library produces the shape in clang's `cleanup-conditional.cpp:14-40`:
- a `cleanup.cond` flag set to false before the ternary and to true in the
  arm;
- a cleanup region `if(flag) D1`.

**(4) A constructor with a base, a member and a default member initializer**

`struct D : B { M m; int k = 7; D(); }; D::D() : B(1) {}`

The declaration section has `DECL_FUNC Dc1 … "_ZN1DC1Ev"`,
`DECL_FUNC Dc2 … "_ZN1DC2Ev"`, `STRUCTOR Dc1 Dc2 0`, and the same for B. D's
REC_FIELDs are: [0] B BASE @0, [1] M @4, [2] int @8.
```
FUNC_BEGIN Dc2 0  PARAM l1 pD pD "this" THIS
MEMINIT_BEGIN base 0 0   LREF l1 LOAD BASE_CAST D B 0 nonnull CONST_INT int 1 CALL Bc2 fBi 2 0  MEMINIT_END
MEMINIT_BEGIN field 1 compiler_generated  LREF l1 LOAD DEREF FIELD D 1 ADDR CALL Mc1 fM 1 0  MEMINIT_END
MEMINIT_BEGIN field 2 default_member_init LREF l1 LOAD DEREF FIELD D 2 CONST_INT int 7 STORE INIT none MEMINIT_END
BLOCK_BEGIN BLOCK_END  FUNC_END reachable
```
The library emits `@_ZN1DC2Ev` with the body. The linkage is external, so it
also emits `@_ZN1DC1Ev alias(@_ZN1DC2Ev)`.

**(5) A destructor body**

`D2 : B { M m; ~D2(){ log(); } }`. The cleanups are pushed before the body so
that they also run on early returns, which matches CIRGen.
```
FUNC_BEGIN D2d2 0  PARAM l1 pD2 pD2 "this" THIS
CLEANUP_BEGIN nae nil 0  LREF l1 LOAD BASE_CAST D2 B 0 nonnull CALL Bd2 fBv 1 0  CLEANUP_BODY
CLEANUP_BEGIN nae nil 0  LREF l1 LOAD DEREF FIELD D2 1 ADDR CALL Md1 fMv 1 0  CLEANUP_BODY
BLOCK_BEGIN CALL g_log fv 0 0 BLOCK_END
FUNC_END reachable        -- runs ~M(), then ~B()
```

**(6) A guarded local static**

`static A a = g();`, where `g` returns A.
```
decl: DECL_VAR ga A internal definition|static_local|guarded 4 "_ZZ1fvE1a"
STATIC_INIT_BEGIN ga 0
  GREF ga  CALL g_g fA 0 0  STORE INIT none
  CLEANUP_BEGIN exit ga 0  GREF ga ADDR CALL D1 fV 1 0  CLEANUP_BODY
STATIC_INIT_END
```
This becomes `cir.local_init static_local @_ZZ1fvE1a ctor{…} dtor{…}`. The
global gets `dynamic_init_guard<"_ZGVZ1fvE1a">` and
`dynamic_init_info<local=true,…>`.

**(7) Namespace scope**

`A a(1); const A& r = A();` (in F_GLOBALS)
```
decl: DECL_VAR ga A external definition 4 "a"
      DECL_VAR gt A internal definition 4 "_ZGR1r_"
      DECL_VAR gr pA external definition 8 "r"
body: STATIC_INIT_BEGIN ga 0  GREF ga ADDR CONST_INT int 1 CALL C1i fVi 2 0
        CLEANUP_BEGIN exit ga 0 GREF ga ADDR CALL D1 fV 1 0 CLEANUP_BODY  STATIC_INIT_END
      STATIC_INIT_BEGIN gr 0  GREF gt ADDR CALL C1 fV 1 0
        CLEANUP_BEGIN exit gt 0 GREF gt ADDR CALL D1 fV 1 0 CLEANUP_BODY  -- cir.register_exit_dtor @_ZGR1r_
        GREF gr GREF gt ADDR STORE INIT none  STATIC_INIT_END
```
This is the shape in clang's `global-temp-dtor.cpp:31-41`. For a local
`const A& r = A();`, the temporary is constructed inside the FULLEXPR and
its `CLEANUP_BEGIN nae` is emitted after FULLEXPR_END, so it lives until the
end of the block.

**(8) `p = new A[n]` and `delete[] p`**
```
LOCAL lc int nil 4 TEMPORARY  LREF lc LREF ln LOAD STORE INIT none
LREF lp  LREF lc LOAD NEW_ARRAY_SIZE A 8  CALL g_Znam fNew 1 0
LREF lc LOAD CAST integral size_t  NEW_ARRAY_COOKIE A 8
DUP LREF lc LOAD CAST integral size_t  ARRAY_CTOR_BEGIN A 0  CALL C1 fV 1 0  ARRAY_CTOR_END
STORE 0 none
LREF lp LOAD  DELETE g_ZdaPv D1 A array
```
The delete becomes `if(p!=null){ cir.delete_array %p delete_fn(@_ZdaPv)
element_dtor(@_ZN1AD1Ev) element_align(4) }`, with the cookie handled by
CXXABILowering.

**(9) A capturing lambda called locally**

`[&x, y](int z){ return x+y+z; }(2)`, inside one F_REGION.
```
FUNC_BEGIN gf … LOCAL l3 L "l" 8 0
  LREF l3 AGG_INIT_BEGIN L 0 AGG_SEL 0 LREF l2 ADDR STORE INIT none
                             AGG_SEL 1 LREF l1 LOAD STORE INIT none AGG_INIT_END
  LREF l3 ADDR CONST_INT int 2 CALL gop fL 2 0 RETURN_STMT true … FUNC_END 0
FUNC_BEGIN gop 0  PARAM l1 pL pL "this" THIS  PARAM l2 int int "z" 0
  LREF l1 LOAD DEREF FIELD L 0 LOAD DEREF LOAD  LREF l1 LOAD DEREF FIELD L 1 LOAD ADD nsw
  LREF l2 LOAD ADD nsw RETURN_STMT true  FUNC_END 0
```
Bodies follow the region's scope order and are never nested. The static
invokers, which the writer synthesizes, come last.

**(10) A template instantiated twice**

Each instantiation is its own F_REGION:
- `DECL_FUNC g1 fII linkonce_odr inline|defined_here|comdat "_Z5twiceIiET_S0_"`
  plus its body;
- `DECL_FUNC g2 fDD linkonce_odr inline|defined_here|comdat "_Z5twiceIdET_S0_"`
  plus its body.

The caller's region CALLs `g1` and `g2`. If `remove_unneeded_instantiations`
clears one of them, F_GLOBALS carries `RETRACT g`.

## 3. Arity

`ECIL_MAX_ARITY` stays 6 (ecil_reader.h:38). The largest new arity is 4,
and every new fact uses an existing flags operand. M4's vtable data should
use flat arrays rather than new operands.

No existing arity changes. The meaning changes are:
- FOR_BEGIN's init scope spans the loop (done in 0.7);
- `data_size` = dsize;
- `GOTO.n_cleanups` must be 0;
- `MATERIALIZE_TEMP` stores directly, never through agg.tmp;
- NAME_BIND only binds nil names;
- RETRACT as defined above;
- F_GLOBALS may have a body section starting at the first STATIC_INIT_BEGIN;
- `HDR_LANG.exceptions` must be 0 in v1.

## 4. Library mapping

| Records | CIR at the pin |
|---|---|
| Cleanups | `cir.cleanup.scope` with `normal`. The code fills the cleanup region and the body continues in the body region. This is a new `a_frame` kind `cleanup`. Closing a scope region pops the cleanup frames innermost first, spilling and reloading carried values. Conditional cleanups: wrap the outermost conditional op in a cleanup scope, either eagerly at TERN/LAND/LOR begin inside a FULLEXPR or lazily by moving the op, with a `cleanup.cond` alloca at entry |
| Returns | While any cleanup is open, store into `__retval` and emit `return(load __retval)`. `RETURN_FROM_SLOT` always uses that shape. A `LOCAL(RETURN_SLOT)` is bound to `__retval` |
| STATIC_INIT | In a function: `cir.local_init static_local` with ctor/dtor regions. In F_GLOBALS: the `cir.global` ctor region (allocas in the region block) and dtor region (with a leading `get_global` added by the library). An exit cleanup on another g becomes `cir.register_exit_dtor`. A guarded variable gets `dynamic_init_guard`, `dynamic_init_info` and `static_local` on every `get_global` of a local. Each initialized global is moved to the end of the module in record order. The module attribute `cir.lowering_lang_options` carries exceptions=false and threadsafe from HDR_LANG |
| STRUCTOR | Body under C2. At finish, the C1 strategy applies: redirect C1 to C2 through the M2.1 finish-time substitution map (`symbol_changes_` + `apply_substitution`, one `AttrTypeReplacer` walk; the per-entity `calls`/`addresses` lists miss `C_ADDR` references and are only for retyping), a C5/D5 comdat plus `aliasee`, or a plain `aliasee`. `aliasee` and comdat names are computed at finish from final names |
| BASE/DERIVED_CAST | `cir.base_class_addr` / `cir.derived_class_addr`, with `nonnull` from the flag |
| REC_FIELD BASE | The member has the base-subobject type `!rec_B.base`, sized to dsize, when dsize < size. `empty_for_abi` bases are omitted. `record_layouts` uses the real `arg_passing_kind` and `has_trivial_dtor` |
| ARRAY_* | `cir.array.ctor` / `cir.array.dtor`; the block argument is the element pointer |
| NEW_ARRAY_* | Overflow builtins plus select, then bitcast, store and `ptr_stride` (clang's `new-array-cookie.cpp` shape) |
| DELETE | `cir.if(p!=null)` around `cir.delete_array` (with `delete_params`), or around D1 then the operator delete call |
| STORE INIT/DSIZE | A direct `cir.store`; DSIZE becomes `cir.libc.memcpy` of dsize bytes |
| ZERO_BYTES | `cir.libc.memset` |
| MEMINIT | A frame marker only in M3 (validation). M4 uses it to skip virtual-base inits in the base variant |

## 5. Writer mapping (all on the existing heap work stacks)

**New `a_code_item` kinds:** `cik_fullexpr`, `cik_cleanup`, `cik_meminit` (a
cursor over `constructor_inits`), `cik_dtor_cleanups` (a cursor), `cik_new`
and `cik_static_init`. Each uses phases; none recurses.

- **`emit_function`** (ecil_code.c:2922):
  - the `this` PARAM;
  - for a structor, STRUCTOR and the C2 G-id;
  - for a ctor, the `cik_meminit` cursor before the body, including
    `compiler_generated` entries;
  - for a dtor, the `cik_dtor_cleanups` before the body;
  - NRVO from `return_value_variable` (il_def.h:18433), with a
    writer-explicit nrvo bool and a `!flag` guard in the cleanup.
- **`do_stmt`** (2615):
  - `stmk_init` (2649): after `cik_init`, push `cik_cleanup` if
    `di->destructor && di->lifetime` (il_def.h:4007, 4020);
  - `stmk_for` (2727): no BLOCK around the init (0.7);
  - condition variables are rewritten as `while(1){decl; if(!c) break;
    body}` (or `for(;;step)`);
  - `stmk_return` with `return_dynamic_init` (16738) becomes
    `RETURN_SLOT`, the init, then `RETURN_FROM_SLOT`;
  - a static local with dynamic init, from the scope's
    `local_static_variable_inits` (18542), becomes `cik_static_init`.
- **`do_expr`** (2141):
  - `enk_object_lifetime` becomes `cik_fullexpr`. Temporaries with
    `has_temporary_lifetime==FALSE` (4088) are collected, and their cleanups
    are emitted after END.
  - `enk_temp_init` and `enk_lambda` (15387–15420) become a temporary
    LOCAL, `cik_init`, an optional `cik_cleanup`, and an LREF.
  - `eok_base/derived_class_cast` becomes `[ADDR] *_CAST [DEREF]`.
  - `eok_ref_indirect` becomes `LOAD DEREF`.
  - `enk_new_delete` becomes `cik_new`. The cookie replicates the
    lowering-only `get_array_new_padding_size` (lower_init.c:3145–3191) on
    top of `new_or_delete_type_requires_array_handling` (expr.c:22673).
- **`do_call`** (1444):
  - the object argument comes first;
  - C1 is used for construction;
  - NONTRIVIAL_ABI arguments are materialized as temporaries and passed as
    `LREF … LOAD`;
  - call results into a destination use `STORE INIT`.
- **`do_init`** (2369): the `dik_constructor`, `class_result_via_ctor`,
  `bitwise_copy` (DSIZE for bases), `dik_lambda` and `dik_zero`
  (ZERO_BYTES for overlapping bases) mappings.
- **`ecil_type.c`:**
  - `emit_record_fields` (686): bases with BASE, references as TY_PTR, the
    vptr as an opaque field, dsize, the record flags;
  - `finish_function_type` (576): prepend `this`;
  - `ecil_function_id` (1442):
    - C1 and C2 names, from a new CIR-gated `lower_name.c` helper that
      passes `cdk_subobject` through `mangled_function_base_name`
      (12766–12816);
    - linkonce_odr/weak_odr + COMDAT;
    - the late-name predicate.
- **`ecil_emit_globals`** (ecil_const.c:1359):
  - walks namespace and class scopes;
  - emits STATIC_INIT in `dynamic_inits` order (18533);
  - emits RETRACT.
- **`ecil_stream_function_region`** (3036): appends synthesized static
  invokers from a queue.
- **`ecil_stream_finish`**: F_NAMES and the CHECKING re-mangle.

## 6. Spikes to run first (cir-lib, M3a)

1. **sret and non-byval identity through CallConvLowering.** Hand-write CIR
   for:
   - `A a = make();`
   - a `return make();` chain
   - NRVO with `__retval`
   - `take(A)` with a temporary, plus the callee's parameter slot

   Run each through the pin's pipeline and check it with a ctor/dtor
   address-logging runtime test. **Fallback:** the library lowers
   NONTRIVIAL_ABI signatures itself, with explicit `!cir.ptr` params and
   `llvm.sret`/`noalias` (the shape in clang's `nrvo.cpp:57`).
2. **Return sinking past cleanups** (FlattenCFG.cpp:1142–1180), with
   `G g(&i); return i;`. **Fallback:** always return through `__retval`.
3. **Conditional cleanups**, wrapped eagerly vs lazily, against clang's
   `cleanup-conditional.cpp` shapes and multi-exit FlattenCFG.
   **Fallback:** writer-explicit flags. Every destructible temporary goes in
   a single cleanup opened at FULLEXPR_BEGIN, in `destructions` order; this
   needs a pre-scan for unordered entries (il_def.h:4078).
4. **Alias × CallConvLowering:** a C1 alias where C2 has a coerced record
   parameter. **Fallback:** a forwarding C1 body for external linkage, and
   C1→C2 use replacement elsewhere.
5. **Single-block ctor regions and init order** (op moving), including a
   ternary and `&&` inside static inits.
6. **`cir.delete_array`** with sized delete, and cookie consistency.
7. **Base-subobject `.base` layouts** against clang, with the cir_src
   layout scripts.
8. **libstdc++ coverage.** vptr classes (`ios_base`, `ctype::do_widen`) make
   many vague-linkage bodies unsupported in M3; they link under KEEP_GOING
   only if libstdc++ exports them. The C++ corpus filters must account for
   this.

## 7. Notes from the M2.1 library (cir-lib, 2026-10-10)

- **Closing scope regions.** Cleanup frames close from
  `a_record_context.scope.closed`/`closed_kind`, innermost first.
  - `FOR_END` closes two regions: the loop body, then the for-init scope.
    `IF_ELSE` closes one region and opens one.
  - The reader's `commit()` runs after the handler, so payloads are intact
    when a region closes.
  - M3's cleanup-range rules go in the region rule table in
    `ecil_brackets.cpp`.
- **Allocas.** The `cleanup.cond` flag allocas and `LOCAL(RETURN_SLOT)` go
  through `alloca_in`, so they stay in the entry block. Entry tracking
  (`last_alloca_`) does not anchor on `cir.label`; that is harmless while no
  label is in the entry block.
- **`__retval`.** It is created lazily by `return_slot()`. A
  `LOCAL(RETURN_SLOT)` must create it at the LOCAL record, before
  `fn_retval_` can be read on the implicit-return path.
- **`static_local`.** Every `get_global` of a guarded local carries
  `static_local`. `variable_address` gets the guarded state from the
  entity's declaration flags, which are known at declaration time.
- **Lambda captures.** Nested bodies are format errors from minor 7. Each
  lambda body is its own function frame, so captures never reference the
  parent's L-ids.

## 8. Spike verdicts (cir-lib, `cxx_cir_shapes.md`): changes to this design

All seven spikes work at the pin, and no fallback was needed. These changes
supersede the sections above. The M3a contract carries them.

1. **`DROP` of a `NONTRIVIAL_ABI` prvalue** is allowed only when the record
   lacks `NONTRIVIAL_DTOR`. Otherwise the writer materializes the prvalue
   into a temporary with a cleanup, as clang does with `agg.tmp.ensured`.
2. **Copy-elision shapes** (exact rules; the consumer enforces them, and a
   violation is a CallConvLowering error or a silent miscompile):
   - a call result has exactly one use, a store into a dominating address;
   - a by-value argument is a direct `load` of an alloca;
   - each by-value parameter is spilled exactly once;
   - every return of such a record goes through `__retval`.
3. **Returns.** While any cleanup is open, every value return goes through
   `__retval`. A bare load of an entry alloca is otherwise sunk past the
   cleanups, which silently returns the wrong value. Exits through cleanup
   scopes need no `n_cleanups`.
4. **Conditional cleanups.**
   - The ECIL encoding is unchanged. A `CLEANUP_BEGIN` inside a TERN or
     LAND/LOR arm stays the same.
   - The protected range still extends to the end of the enclosing
     FULLEXPR, and destruction is in reverse construction order across all
     of that full-expression's cleanups.
   - The consumer must not open the guarded scope at `FULLEXPR_BEGIN`: that
     gave the wrong order in `sum(T(1).v, a ? T(2).v : 0)`. It opens the
     guarded `cir.cleanup.scope` at the outermost conditional op (the
     record that begins the TERN or LAND/LOR containing it), with a
     per-cleanup `cleanup.cond` flag:
     - `false` at scope start;
     - `true` after construction;
     - `cir.if (flag)` around the cleanup code.
   - A carried value is spilled through `tmp.exprcleanup`.
5. **C1/C2.**
   - External linkage: C1 is `aliasee` of C2. This works with coerced
     record parameters.
   - `weak_odr`: C2 is in comdat `C5`/`D5`, and C1 is `weak_odr` with
     `alias(@C2)`.
   - Discardable linkage: only C2 is emitted, and C1 references are
     substituted by the finish-time map.
6. **Static init.**
   - `dynamic_init_guard` and `dynamic_init_info` always come together.
   - A `cir.global` dtor region starts with a `get_global` of the variable.
   - `?:` and `&&` work inside ctor regions.
   - Lifetime-extended static temporaries are `_ZGR…` globals with
     `cir.register_exit_dtor`.
7. **Arrays and delete.** Use clang's shapes: the overflow-checked size
   with `select`, `cir.array.ctor` over the C2 symbol, and
   `cir.delete_array` with `delete_params`/`element_dtor`/`element_align`,
   lowered by CXXABILowering.
8. **Layouts.**
   - The M2 record lowering handles `.base` subobject types, empty bases
     and tail-padding reuse unchanged. 15 of 22 types match clang exactly;
     the other differences are cosmetic tail padding.
   - `empty_for_abi` is for empty *bases* and C zero-size members only. An
     empty C++ data member (without `[[no_unique_address]]`) is an
     ordinary field of size 1, which clang keeps as `data E1`.
