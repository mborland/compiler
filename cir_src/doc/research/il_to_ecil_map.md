# IL to ECIL map, v0 (C subset)

Author: cir-writer (Phase R). Input for the orchestrator's format spec. Every
claim is tagged: **V** = read in `src/il_def.h` or other source (file:line),
**P** = observed in the non-lowered IL dump of the native debug
`build/clang/bin/cpfe-cp --c11 --il_display` (probe sources and a summariser are
in `cir_src/test/writer/probes/`; `run_probe.sh x.c` then `x.sum`),
**I** = proposal.

Opcode names are proposals in the plan's vocabulary. Notation:
`OP a b` = opcode with operands; `[x y] -> [z]` = effect on the consumer's
value stack; `T` = type id (G), `G` = global id, `L` = frame-local id,
`S` = string id, `F` = flags. "Writer" = `ecil_write.c`, "consumer" = EDG-CIRGen.

## 0. Ground rules the probes established

1. **IL types are structural but not canonical.** Every `a_type` entry
   (including each typedef/qualifier `tk_typeref`) is a separate node; function
   types are not shared (each declaration/definition has its own `tk_routine`).
   ECIL type ids are therefore assigned to *canonical* types (typerefs and
   qualifiers stripped, enums mapped to their integer type, pointers/arrays by
   structure, records by identity) and cached in the id slot of *every* IL type
   entry that maps to them (V: `ecil_id` in the prefix). Equal canonical ids
   make a cast a no-op.
2. **Qualifiers do not exist in ECIL types.** `const` becomes a `constant` flag
   on a global; `volatile` becomes a flag on `LOAD`/`STORE` (from the lvalue
   node's type qualifiers or `an_expr_node::volatile_fetch`, `il_def.h` expr
   node); `restrict` and `_Atomic` are unsupported v0 (atomic) or a supp hint
   (restrict). `TQ_*`: `il_def.h:6277-6314`.
3. **Lvalue-to-rvalue conversion is implicit in the IL (V/P).** There is no
   load node. A node whose `is_lvalue` is FALSE but whose kind is
   "rvalueable" (`il.c:25250-25400`: `enk_variable`, `enk_temp_init`,
   `eok_indirect`, `eok_subscript`, `eok_points_to_field`, `eok_dot_field` with
   a glvalue base, ...) means *address computation + load*. Probe: `a + c` has
   operand `enk_variable a` with `orig_lvalue_type` set and no `is_lvalue`.
   Writer rule `emit(node)`:
   * address-producing kinds emit the address;
   * if the node is not a glvalue and the type is not array/function: append
     `LOAD T F(volatile)`;
   * glvalue nodes (assignment targets, `&x`, ++/--, member bases that need an
     address) never get a `LOAD`.
   Aggregate rvalues (`s = *ps`, call arguments, `return s`) are `LOAD` of the
   record type; the consumer may fuse `store(load)` to a copy.
4. **Implicit conversions are explicit `eok_cast` nodes** (P, usually with
   `compiler_generated`): usual arithmetic conversions on binary operators,
   assignment right-hand sides (`r = uc` gets a cast to int), default argument
   promotions on variadic arguments (`float -> double`, `uchar -> int`),
   switch condition promotion, shift operands (each operand promoted
   independently, no balancing). **Exceptions that are not cast in the IL:**
   compound assignment operands (`l += f` with `long l`, `float f` has *no*
   cast, P) and comparison/logical results (`int`).
5. **Boolean contexts are raw (P).** `if (p)`, `while (d)`, `for (;x;)`, `!i`,
   `&&`, `||`, `?:` conditions arrive with their own int/pointer/float type, not
   normalised; only `(_Bool)x` and conversions to `_Bool` use `eok_bool_cast`.
   The writer wraps conditions: `<expr> TOBOOL(kind)`.
6. **Folding is aggressive and complete (P).** `sizeof`, `alignof`, enum
   constants, `(unsigned)-1`, `1.0*2` appear as one `enk_constant` node whose
   backing expression (`a_constant::expr`) must be *ignored*. Null-pointer
   constants appear as `ck_integer` constants with a pointer type; `ga` as an
   address constant (`ck_address`, `abk_variable`) with a byte `offset`.
7. **Break/continue are gotos (P/V).** `stmk_goto` to a compiler-generated label
   with `a_label::break_label` / `switch_break_label` / `continue_label`
   (`il_def.h:13489-13545`); the `stmk_label` for it sits right after the
   loop/switch (break) or at the end of the body (continue).
8. **Declarations and initialisations are separate statements (P).** A local
   `int x = e;` is `stmk_decl` (lists entities) followed by `stmk_init`
   (`dynamic_init`); a `for (int i...)` init is a `stmk_block` holding both.
9. **Names in C need no mangling** (see `writer_hooks.md` section 2).
10. **Targets facts travel in the stream.** Each type entry already carries
    authoritative `size` and `alignment` (`il_def.h:9795-9806`), each record
    field its offset. Send them; do not recompute in the consumer.

## 1. Types

All `TY_*` records introduce a G-id; a type is introduced before first use
(post-order, with records declared before bodies). Incomplete records get
`TY_RECORD_DECL`; the writer completes them with `TY_RECORD_BODY` as soon as
the IL type is complete (scan of a pending list at each hand-off, or `F_GLOBALS`).

| IL (`a_type_kind`, `il_def.h:5573`) | ECIL | notes |
| --- | --- | --- |
| `tk_void` | `TY_VOID` | |
| `tk_integer`, not enum, not bool (`variant.integer.int_kind`; `an_integer_kind` at `il_def.h:5646`) | `TY_INT width signed [size align]` | `ik_char` signedness: `int_kind_is_signed[ik_char]` (`il_def.h:5666`), mirrored by `il_header.plain_chars_are_signed` (`il_def.h:18823`); `char`, `signed char`, `unsigned char` map to s8/s8/u8 (distinct C types, one CIR type). `ik_short..ik_unsigned_long_long`, `ik_int128` (GNU, `INT128_EXTENSIONS_ALLOWED`), width from `type->size*targ_char_bit`. `ik_bit_precise` (`_BitInt`, `bit_width`): unsupported v0. |
| `tk_integer` with `enum_type` | same `TY_INT` as its `int_kind` | enumerator list not needed (supp name only). `has_explicit_enum_base`, `is_scoped_enum`: C++. |
| `tk_integer` with `bool_type` (`_Bool`) | `TY_BOOL size=1 align=1` | P: `int_kind` unsigned char, `bool_type` TRUE. |
| `tk_float` (`a_float_kind`, `il_def.h:5769`) | `TY_FLOAT fmt size align` | v0: `fk_float`, `fk_double`, `fk_long_double`. `fmt` in {ieee_single, ieee_double, x87_80, ieee_quad, ...} from kind + `type->size` (`TARG_SIZEOF_LONG_DOUBLE` 8/12/16 by target, `targ_def.h:2265-2281`; x86-64 SysV: 16 -> x87_80). `fk_float128`, `_Float16`, bfloat: GNU later. |
| `tk_complex`, `tk_imaginary` | unsupported v0 | |
| `tk_pointer` (`variant.pointer.type`, `is_reference` FALSE in C) | `TY_PTR T_pointee` | pointee may be an incomplete record (declare first). Function pointer = `TY_PTR TY_FUNC`. C: address space/`__ptr32` only with MS/UPC: unsupported. |
| `tk_array` (`variant.array`, `il_def.h:10257-10335`) | `TY_ARRAY T_elem count F` | `count = number_of_elements`; `F.incomplete` when `type->incomplete` (extern `int a[]`), `F.zero_len` when `bound_is_zero` (GNU `[0]`), `F.flexible_member` handled in the record. `is_vla`/`is_variable_size_array`: unsupported v0 (detection: section 7). Array `qualifiers`/`is_static` (C99 parameter forms): drop; the *parameter* type is already adjusted to a pointer (`a_param_type::type` vs `declared_type`, `il_def.h:6392-6396`). |
| `tk_struct`, `tk_union` (`class_struct_union`) | `TY_RECORD_DECL kind name` then `TY_RECORD_BODY T size align F n {MEM...}` | C never has `tk_class`. `F.packed` from `is_packed`, `size`/`alignment` authoritative (`type->size` is 0 while incomplete; zero-size complete types only for GNU empty structs/flexible arrays: `is_empty_class`, `bound_is_zero`). `max_member_alignment` (`#pragma pack`) is already baked into field offsets. |
| `tk_routine` (`variant.routine`, `extra_info`) | `TY_FUNC T_ret n F {T_param}` | `F.variadic = has_ellipsis`, `F.unprototyped = !prototyped`. Parameter types from `extra_info->param_type_list` (`a_param_type::type`). **K&R definition** (P: `kr(a,b) int a; char b;`, `prototyped:FALSE`, params `int`,`char` unpromoted): the *ABI* signature uses `default_argument_promotion` (`types.h:1065`: char/short -> int, float -> double); the writer emits the promoted `TY_FUNC` and, in the prologue, a narrowing `CAST integral` into the parameter local. Unprototyped *calls* arrive with promotions already applied as casts. `does_not_return` (type attribute) -> supp `noreturn`. Calling conventions: `calling_convention` default only (v0). |
| `tk_typeref` | transparent | `skip_typerefs`; typedef name -> supp `TY_NAME T S` hint; `qualifiers` -> not in type (rule 2). `trk_*` kinds other than `trk_none`: `trk_is_typeof_*` and `trk_for_type_attributes` (GNU `typeof`, type attributes) are transparent too; the rest are C++. `trk_is_deduced_auto` etc. C++. |
| `tk_error`, `tk_unknown` | internal error diagnostic | cannot reach a back end |
| `tk_vector`, `tk_scalable_vector`, `tk_riscv_vector`, `tk_mfp8`, ... | unsupported v0 | |
| `tk_class`, `tk_ptr_to_member`, `tk_template_param`, `tk_nullptr`, `tk_reflection` | C++ (M3+) | |

### 1.1 Record layout facts the FE supplies

`a_field` (`il_def.h:11898-12110`):

| fact | field | ECIL |
| --- | --- | --- |
| byte offset | `offset` (bit-fields: the byte component) | `MEM offset` |
| bit offset within that byte | `offset_bit_remainder` (0..`targ_char_bit`-1) | `MEM.bit_off` |
| bit width | `bit_size` (`declared_bit_size` can be larger in C++) | `MEM.bit_size` |
| is bit-field / signed | `is_bit_field`, `bit_field_is_signed` | flags |
| field type | `type` (for bit-fields the *base* integer type) | `MEM.T` |
| explicit alignment / packed | `alignment`, `is_packed` | supp |
| unnamed / anonymous | `source_corresp.name == NULL`, `is_anonymous_parent_object`, `compiler_generated` | name = nil; anonymous struct/union members are ordinary members of unnamed record type (P: `pa->i` is `dot_field(points_to_field(pa, <anon union field>), i)`) |
| record size/alignment | `type->size`, `type->alignment` | `TY_RECORD_BODY` |
| flexible member | last field array, `contains_flexible_array_member` | `MEM` of `TY_ARRAY count=0 incomplete` |
| MS container offsets | `offset_in_container` | not used (SysV only, `TARG_MICROSOFT_BIT_FIELD_ALLOCATION`=0) |

The writer sends raw offsets (including bit-field byte/bit pairs). The
consumer builds the CIR record with explicit padding and bit-field storage
units (as clang's CGRecordLayoutBuilder) and the `cir.record_layouts` attribute.
Member index for `FIELD`: position in `field_list` order (`next` chain); the
field's id slot stores `index+1`.

## 2. Variables and globals

`a_variable` (`il_def.h:11210-11869`), scopes (`il_def.h:18295-18629`).

| IL | ECIL |
| --- | --- |
| file-scope variable, `storage_class` `sc_unspecified`/`sc_extern`/`sc_static` (`a_storage_class`, `il_def.h:408`) | `VAR_DECL G S T linkage F` at first reference (decl section of the referencing frame), `VAR_DEF G ...` in `F_GLOBALS` |
| linkage | `source_corresp.name_linkage`: `nlk_external` -> external, `nlk_internal` -> internal (`il_def.h:511-523`); `sc_extern` with `init_kind == initk_none` -> declaration only |
| tentative definition | P: `int tent;` has `sc_unspecified` (or `sc_static`), `initk_none`. Definition iff not `sc_extern` (or has initializer). Emit zero-initialised external/internal global at `F_GLOBALS` (no `common` in v0). Duplicates are merged by the FE into one entry. |
| block-scope `extern int x;` | variable with `sc_extern` on a scope `variables` list; same as a file-scope extern; type refinements (`extern int a[5]` over `int a[]`) are undone at pop (`scope_stk.c:10290-10312`): use the node type for the access, the declaration type for the global |
| `static` local (including nested blocks) | `sc_static` variable on a scope's `variables` list (file-scope region); defined *inside the function frame* because its initializer (`initk_function_local`) lives in the function region (P: `static int *sp = &cnt` -> `initk_function_local` + `a_local_static_variable_init`). Fetch with `get_variable_initializer(var, scope, ...)` (`il.c:17279`; needs `innermost_function_scope` set, see hand-off). Emit at the `stmk_decl` that lists it as `VAR_DEF G name=<fn>.<var> internal F.static_local`. Simple constants use `initk_static` (P: `static int cnt = 5`). |
| `auto`/`register` local | `LOCAL_VAR L T align name` in the frame decl section (alloca hoisted), emitted for every `stmk_decl` entity and every parameter; `register` ignored |
| parameter | `a_scope::variant.routine.parameters` list (`il_def.h:18530-18534`), `is_parameter`; `PARAM L T name`, followed in the prologue by `STORE` into its `LOCAL_VAR` (consumer allocas, as clang) |
| thread local | `is_thread_local`/`is_effective_thread_local` (`il_def.h:11878-11880`): flag `tls`; v0 may leave unsupported |
| alignment | `alignment` (explicit `aligned`) -> supp |
| section, visibility, `used`, `weak`, `asm("name")` | `section`, `ELF_visibility`, `has_gnu_used_attribute`, `is_weak`, `asm_name_or_reg.name` with `asm_name_is_valid`; supp keys; `asm` name replaces the symbol name; `is_gnu_alias`, `is_weakref`: unsupported v0 |
| `const` global | from `var->type` qualifiers -> `constant` flag (only valid with a constant initializer) |
| compound literal at file scope | `sc_static` variable with `is_compound_literal` (P) -> ordinary internal global; name `.compoundliteral.N` |
| initializer kinds | `init_kind` (`il_def.h:11009`): `initk_none`, `initk_static` (constant), `initk_zero`, `initk_dynamic`, `initk_function_local`; `initk_binding` is C++ |

File-scope `initk_dynamic` or a `ck_dynamic_init` constant inside a global's
aggregate does not occur in ISO C; if it does (extension) diagnose
"non-constant initializer at file scope (unsupported v0)".

### 2.1 Constants and initializers

`a_constant` (`il_def.h:4741-5362`), kinds (`il_def.h:3618`):

| `ck_*` | in C | ECIL (initializer or value context) |
| --- | --- | --- |
| `ck_integer` | integers, enum, bool, chars, **null/int-to-pointer** | `CONST_INT T v` ; with pointer type: `CONST_NULL T` (value 0) or `CONST_PTR T v` (`an_integer_value`, up to 128 bit, low `type->size` bytes) |
| `ck_float` | float/double/long double | `CONST_FLOAT T bytes`; recommended payload: f32/f64 as IEEE bits (host double/float is exact for them), x87/quad as `fp_to_hex_constant_string` text (`float_pt.h:161`) because the host `long double` differs between the Mac loop and Linux; consumer parses with APFloat |
| `ck_string` | string literals (`value`, `length` incl. NUL, `character_kind`) | `STR_LIT G elem_width n bytes` introduces an anonymous private constant array global (dedupe by (kind, bytes) within the TU); expression use is `GREF G` (the IL gives an `is_lvalue` array constant then `eok_array_to_pointer`, P); in initializers of arrays `K_STRING bytes pad_to_n` (P: `char name[8] = "ab"` is `ck_string` typed `char[8]`, `length=3`, zero padded) |
| `ck_address` | `&var`, arrays, function addresses, strings | `K_ADDR G byte_offset` / expression `GREF G` + `ADDR_OFFSET bytes`. `address.kind`: `abk_variable`, `abk_routine`, `abk_constant` (string literal) supported; `abk_temporary`, `abk_label` (computed goto), `abk_typeid`... unsupported v0. Use `offset` (bytes, P: `&arr[1]` -> 4); ignore `subobject_path` and `one_past_the_end`. |
| `ck_aggregate` | struct/union/array initializers (list `first_constant`.. `next`) | `K_AGGR T n {entry}` with explicit selectors, see 2.2 |
| `ck_designator` | `.f =`, `[i] =` inside aggregates (P: stays in non-lowered IL) | resolved by the writer, never emitted |
| `ck_init_repeat` | `[a ... b] = x` and skipped array runs | `K_REPEAT idx count value` |
| `ck_dynamic_init` | automatic aggregate initializers with non-constant members (P: `int a[3] = {i, 2}`) | the element becomes an *expression* (see 2.3) |
| `ck_complex`, `ck_imaginary`, `ck_fixed_point` | unsupported v0 | |
| `ck_label_difference`, `ck_template_param`, `ck_ptr_to_member`, `ck_void`, `ck_reflection`, `ck_error` | not C / not emitted | |

### 2.2 Aggregate initializers (normalisation by the writer)

P: the FE already applies brace elision, converts each leaf to the field type
(`(char)3`), and leaves missing trailing elements implicit
(`partial_aggr_value`, `is_partially_initialized`). Designators remain
(`ck_designator` nodes in the list; nested designators become nested aggregates:
`.i.b[1] = 5` is `{.b = {[1] = 5}}`; `[5 ... 6] = 7` is a designator followed by
`ck_init_repeat`). The writer implements C11 6.7.9 "current object" on a work stack
and emits:

```
K_AGGR  T  n_entries { sel  value }...      sel = member_index | array_index
K_REPEAT sel count value
```

with all unlisted members/elements implicitly zero (consumer zero-fills;
clang does the same for `const_record`/`const_array`). A union initializer has
exactly one entry (the active member; `ck_designator` kept even for the first
member in non-lowered IL, P `un = { .f = 1.0f }`). `value` is a nested `K_*`,
`CONST_*`, or `K_ADDR`. Bit-field members in a constant aggregate: the entry
carries the member index; the consumer packs. v0 restriction: `ck_init_repeat`
whose repeated constant is a *leaf* (the "count is the number of leaf elements"
multi-dimensional form, `il_def.h:5326-5362`, and the
`multidimensional_aggr_tail_not_repeated` flag) is diagnosed unsupported unless the
repeat covers one array level.

### 2.3 Dynamic initialisation of locals (`stmk_init`)

`a_dynamic_init::kind` (`il_def.h:3957-3990`, struct `:3993`), reachable in C:

| `dik_*` | ECIL |
| --- | --- |
| `dik_none` | nothing (variable declared, no initialiser needed) |
| `dik_zero` | `INIT_ZERO L` (`STORE` of `CONST_ZERO T`) |
| `dik_constant` | `INIT_CONST L K_*` (scalar: `CONST_*` + `STORE`; aggregate/string: `K_AGGR`/`K_STRING` stored as a record/array value; unlisted tail zero) |
| `dik_expression` | `<expr> STORE`-style: `LREF L` `<value expr>` `STORE T` (value already cast to the variable type; for a struct-valued call (`struct P q = mk(i)`) the call result is stored, no sret reasoning in the writer) |
| `dik_nonconstant_aggregate` | `INIT_AGGR L T` then per entry: `<expr for ck_dynamic_init>` with explicit selectors, constants inline; unlisted members zero (the `ck_aggregate` has `ck_dynamic_init` children whose `dynamic_init` is a `dik_expression`, P) |
| `dik_bitwise_copy`, `dik_constructor`, `dik_class_result_via_ctor`, `dik_lambda` | C++ only |

## 3. Routines

`a_routine` (`il_def.h:12228-13266`).

| fact | field | ECIL |
| --- | --- | --- |
| definition present | `defined`, `function_def_number`, `memory_region`, `il_header.function_def_table` | `FN_DEF_BEGIN G ... FN_DEF_END` inside `F_REGION` |
| linkage | `storage_class`: `sc_unspecified`+`nlk_external` external; `sc_static`/`nlk_internal` internal | |
| C99 inline | `is_inline`, `definition_for_inlining_only`, `suppress_inline_body`, `explicit_extern_inline`, `need_out_of_line_copy`, `gnu_c89_inline` (P: `cinl` has `definition_for_inlining_only` + `suppress_inline_body`; `einl` and `sinl` do not) | `definition_for_inlining_only`/`suppress_inline_body`: `FN_DECL` only (no body; region still freed). Otherwise emit the definition; `static inline` is internal. v1: `available_externally` with body. |
| `main` | `il_header.main_routine` | normal; if the body's end is reachable (`a_block::end_of_block_reachable` of `scope->assoc_block->variant.block.extra_info`) the consumer appends `return 0` (C99 5.1.2.2.3). Writer sends `FN_DEF_END F.end_reachable F.is_main`. |
| fall off end of non-void | same flag | consumer inserts unreachable/trap |
| attributes | `always_inline`, `never_inline`, `is_naked`, `is_pure`, `is_weak`, `is_weakref`, `is_gnu_alias`, `is_ifunc`, `has_gnu_used_attribute`, `decl_modifiers`, `gnu_extra_info` (section, visibility, ctor/destructor priority `has_ctor_priority`/`has_dtor_priority`), `type->...does_not_return`, `fp_contract`/`fenv_access`/`cx_limited_range` | supp keys; `__attribute__((constructor/destructor))` -> v0 unsupported (needs global ctor list) |
| parameters | scope `parameters` list, `a_param_type` | `PARAM` records (name only as hint) |
| builtins | `variant.builtin_function_kind` (`BUILTIN_FUNCTIONS_ENABLED`) | section 6.4 |
| filtered | `ignore_routine_in_back_end` set (`writer_hooks.md` section 5) | not emitted |
| asm functions | `storage_class == sc_asm`, `stmk_asm_func_body` | unsupported v0 |
| nested functions | the FE rejects them (P: syntax error) | n/a |
| K&R | `type->routine.extra_info->prototyped == FALSE` | see 1 |

## 4. Statements (`a_statement_kind`, `il_def.h:15802`, struct `:16516`)

Statement lists are linked by `next` (iterate, never recurse). Reachable from C
and their ECIL (value stack is empty between statements; `stmk_expr` ends with
`DISCARD` unless the expression is `void`):

| `stmk_*` | ECIL |
| --- | --- |
| `stmk_expr` | `<expr> [DISCARD]` |
| `stmk_empty` | nothing |
| `stmk_block` | `BLOCK_BEGIN ... BLOCK_END` (P: the FE adds compiler-generated blocks around `if`/`while` bodies and `for` init; harmless); scope variables are already in the decl section. `stmk_block.extra_info->assoc_scope` only matters for GNU `__label__`. |
| `stmk_decl` | for each entity in `decl.entities` (`an_il_entity_list_entry` tagged pointers): variable auto -> `DECL L` marker (alloca already hoisted); static local -> `VAR_DEF` (2); typedef/struct -> nothing; block-scope function declaration -> nothing (introduced on use); `stmk_decl` for VLA is `stmk_vla_decl` |
| `stmk_init` | section 2.3 |
| `stmk_if` | `<cond expr> TOBOOL IF_BEGIN <then> [IF_ELSE <else>] IF_END`. `else if` ladders nest in `else_statement`: iterate, do not recurse (deep shape, section 8). |
| `stmk_while` | `WHILE_BEGIN <cond> TOBOOL COND_END <body> WHILE_END` |
| `stmk_end_test_while` | `DO_BEGIN <body> DO_COND <cond> TOBOOL COND_END DO_END` |
| `stmk_for` | `FOR_BEGIN <init stmt> FOR_COND [<cond> TOBOOL] COND_END FOR_STEP [<increment expr> DISCARD] FOR_BODY <body> FOR_END`; `a_for_loop::initialization` is a statement (block with decl+init), `increment` an expression, `expr` the optional condition (P) |
| `stmk_switch` + `stmk_switch_case` | `SWITCH_BEGIN <cond (promoted int, already cast)> SWITCH_BODY <body: CASE v | CASE_RANGE lo hi | DEFAULT inline at each stmk_switch_case position> SWITCH_END`. Values from `a_switch_case_entry::case_value`/`range_end` (`il_def.h:16246`); `reachable_by_fall_through` available as hint. v0: case/default must be direct children (or plain-block descendants) of the switch body; a case nested under a loop/if inside the switch (Duff's device) is diagnosed unsupported (writer checks the `parent` chain). |
| `stmk_goto`, `stmk_label` | label with `break_label`/`switch_break_label` -> `BREAK`; with `continue_label` -> `CONTINUE`; the matching `stmk_label` emits nothing. Other labels: `GOTO L` / `LABEL L` (label id introduced at first mention; `a_label::exec_stmt` in the same region). `GOTO` carries `n_cleanups` operand = 0 in C. |
| `stmk_return` | `[<expr>] RETURN F.has_value`; expression is already cast to the return type (struct returns: `LOAD` of record, ABI by the consumer) |
| `stmk_asm`, `stmk_asm_func_body` | unsupported v0 |
| `stmk_assigned_goto` (computed goto), `stmk_stmt_expr_result` (statement expression) | unsupported v0 |
| `stmk_set_vla_size`, `stmk_vla_decl` | unsupported v0 (VLA) |
| `stmk_constexpr_if`, `stmk_if_consteval`, `stmk_if_not_consteval`, `stmk_range_based_for`, `stmk_for_each`, `stmk_try_block`, `stmk_microsoft_try`, `stmk_coroutine*`, `stmk_upc_*` | C++/MS/UPC; unreachable in C |

Object lifetimes (`lifetime` fields) are always NULL in C: no `FULLEXPR`,
`CLEANUP`, or `TEMP` records in v0.

Source locations: `a_statement::position` / `an_expr_node::position`
(`a_source_position` = sequence number + column); convert with
`conv_seq_to_physical_file_and_line` (`il.h:3472`) into a `LOC` supp record;
the lookup tables are in the file-scope region and valid mid-TU.

## 5. Expressions

Node kinds (`an_expr_node_kind`, `il_def.h:13570`) reachable from C:
`enk_operation`, `enk_constant`, `enk_variable`, `enk_routine`, `enk_field`
(operand of field selection), `enk_temp_init` (compound literals),
`enk_c11_generic` (use `.result` only), `enk_sizeof`/`enk_alignof`/
`enk_runtime_sizeof` (only if not folded: VLA, so unsupported),
`enk_statement` (GNU, unsupported), `enk_builtin_operation` (`bok_offsetof`,
`bok_types_compatible`, `bok_intaddr`: normally folded; others unsupported),
`enk_builtin_choose_expr` (use the chosen arm), `enk_address_of_ellipsis`
(only with obsolete `va_start`), `enk_type_operand` (_Generic selector only).
C++-only: `enk_lambda`, `enk_new_delete`, `enk_throw`, `enk_condition`,
`enk_object_lifetime`, `enk_typeid`, `enk_reuse_value` (also GNU `?:`),
`enk_gcnew`, `enk_param_ref`, `enk_braced_init_list`, `enk_yield`/`enk_await`,
`enk_fold`, `enk_initializer`, `enk_concept_id`, `enk_requires`,
`enk_compound_req`, `enk_nested_req`, `enk_const_eval_deferred`,
`enk_template_name`, `enk_token_sequence`, `enk_pack_index`, `enk_sizeof_pack`.

### 5.1 Leaves

| IL | ECIL |
| --- | --- |
| `enk_variable`, auto/param | `LREF L` (+`LOAD` if rvalue, rule 3) |
| `enk_variable`, file-scope/static | `GREF G` (+`LOAD`) |
| `enk_routine` (rvalue, type pointer-to-function; or lvalue with function type) | `GREF G` (a function address; no load). A direct call's callee node is *not* emitted (see calls). |
| `enk_constant` | per 2.1: `CONST_*`; string array lvalue -> `GREF Gstr`; `ck_address` -> `GREF G` + `ADDR_OFFSET bytes`; ignore `a_constant::expr`/`name_reference` |
| `enk_temp_init` (lvalue, `dynamic_init` `dik_constant`/`dik_nonconstant_aggregate`) | allocate a frame temp `LOCAL_VAR L T` (writer-allocated, appended to the decl section), `INIT_*` as 2.3, then `LREF L` (+`LOAD` if rvalue). File scope: static compound literal variable, see 2. |
| `enk_field` | only as operand 2 of a field operation; never emitted alone |

### 5.2 Operators (`an_expr_operator_kind`, `il_def.h:13855-14365`)

`type_kind` (`operation.type_kind`) tells the operation's domain (`tk_integer`,
`tk_float`, `tk_pointer`, ...). Which operands are lvalues is listed per row.
`bin(kind)` emits the operand types' canonical kind (the consumer knows
signed/unsigned/float from the type ids).

| IL | operands (slots) | ECIL (post-order) |
| --- | --- | --- |
| `eok_add/subtract/multiply` | 2 rvalues, same type (explicit casts) | `<a> <b> ADD|SUB|MUL F(nsw if signed int)` |
| `eok_divide/remainder` | 2 rvalues | `DIV|REM` (consumer picks signed/unsigned/float) |
| `eok_shiftl/shiftr` | 2 rvalues, types may differ (each promoted independently) | `<a> <b cast to a's type if width differs> SHL|SHR` |
| `eok_and/or/xor` | 2 rvalues | `AND|OR|XOR` |
| `eok_eq/ne/lt/le/gt/ge` | 2 rvalues (pointers allowed) | `<a> <b> CMP_<op>`: result is the consumer's bool; then `CAST bool_to_int` to the node type `int` (mirrors clang; the consumer folds `int_to_bool(bool_to_int(x))`) |
| `eok_negate` | 1 | `NEG` (nsw if signed) |
| `eok_complement` | 1 | `NOT` (bitwise) |
| `eok_unary_plus` | 1 | nothing |
| `eok_not` | 1, raw scalar | `<x> TOBOOL LNOT` then `CAST bool_to_int` |
| `eok_land` / `eok_lor` | 2, raw scalar, evaluated left to right | `<a> TOBOOL LAND_BEGIN <b> TOBOOL LAND_END` (bracketed; likewise `LOR_*`), then `CAST bool_to_int` |
| `eok_question` | cond (raw scalar), then, else (same type after casts); void and struct results possible | `<c> TOBOOL TERN_BEGIN T <then> TERN_ELSE <else> TERN_END` (`T` = result type; void arms yield nothing). GNU `a ?: b` (`is_gnu_two_operand_question_mark`): unsupported v0 (`enk_reuse_value`). |
| `eok_comma` | 2 | `<a> DISCARD <b>` (`a` has `result_is_not_used`) |
| `eok_address_of` | glvalue (function designator or array allowed) | emit operand as address (no `LOAD`); result is that pointer |
| `eok_indirect` | pointer rvalue | `<ptr>`; address-producing; `LOAD` added if the node is an rvalue. If the pointee is a function type (`(*fp)(x)`, P: node type is `int (*)(...)`) it is a no-op returning the pointer. |
| `eok_subscript` | pointer rvalue + integer (either order; `pointer_operand_is_second`; `1[p]` P) | `<ptr> <idx> PTR_STRIDE F(idx signedness from its type)` -> address (+ `LOAD`). Multi-dim: inner subscript is a glvalue of array type then `eok_array_to_pointer` (P: `a[1][2]`). |
| `eok_padd` | pointer + integer, either order; integer unpromoted | `<ptr> <idx> PTR_STRIDE` (consumer scales by pointee size; `void*` element size 1, GNU) |
| `eok_psubtract` | pointer - integer | `<ptr> <idx> NEG? PTR_STRIDE F.neg` |
| `eok_pdiff` | pointer - pointer | `<a> <b> PTR_DIFF` (result type `long`/`ptrdiff_t`; consumer divides by pointee size) |
| `eok_dot_field` | base glvalue (address) or prvalue struct; op2 `enk_field` | glvalue base: `<base addr> FIELD T_rec idx` (+ `LOAD` if rvalue); bit-field: `BFIELD T_rec idx` (`LOAD`/`STORE` on it are bit-field get/set); prvalue base (`f().x`, P): `<base> EXTRACT idx` |
| `eok_points_to_field` | pointer rvalue; op2 `enk_field` | `<ptr> FIELD T_rec idx` / `BFIELD` (+ `LOAD`); anonymous union/struct members appear as an extra compiler-generated field hop (P) |
| `eok_array_to_pointer` | array glvalue | `<array addr> CAST array_to_ptrdecay`; if the operand is a *prvalue* array (P: `f().arr[1]`), first `MATERIALIZE_TEMP T` (writer-allocated temp, init from the value) then decay |
| `eok_cast`, `eok_bool_cast` | 1 | cast classification below |
| `eok_assign` | lvalue, rvalue (converted to the lvalue type by explicit cast) | `<addr> <value> STORE T F(volatile, keep_value if !result_is_not_used)`; struct assignment copies `sizeof(struct)` (C); bit-field targets: `BFIELD` then `STORE` (consumer: set_bitfield; the kept value is the truncated stored value) |
| `eok_add_assign` ... `eok_xor_assign`, `eok_shiftl/r_assign`, `eok_padd_assign`, `eok_psubtract_assign` | lvalue, rvalue **not converted** (P) | expansion owned by the writer so casts stay classified there: `<addr> DUP LOAD T_l CAST->comp <rhs> CAST->comp <op> CAST->T_l STORE T_l F(keep)`; `comp` = usual arithmetic conversion of (`T_l`, `T_r`) for `+ - * / % & | ^`, promoted `T_l` for shifts (rhs cast to `comp` width), pointer forms `<addr> DUP LOAD <idx> PTR_STRIDE(neg) STORE`; bit-fields: `BFIELD` item in place of the address. (`DUP` is the only stack-shuffling op needed.) |
| `eok_pre_incr/decr`, `eok_post_incr/decr` | lvalue | `<addr> INC|DEC pre|post F` : result is the new/old value (type of lvalue; for pointers add/sub 1 element; for bit-fields consumer handles; `_Bool` ++ unsupported) |
| `eok_call` | callee, args... | `<callee> <args in IL order> CALL_INDIRECT n F` or, when operand 1 is `enk_routine` (rvalue pointer-to-function with a routine), `<args...> CALL G n F`. Arguments are already default-promoted/cast; struct arguments are `LOAD`ed records (consumer ABI). `F.variadic_callee`, `F.noreturn` (type `does_not_return`), `F.result_unused`. Unprototyped callee (P: `int (*)()`): no signature check; consumer uses the argument value types. |
| `eok_va_start/va_arg/va_end/va_copy` | `va_list` lvalue operand(s): array-typed glvalue -> its address; pointer rvalue (parameter) -> its value | `<ap addr> [<last param addr>] VA_START`, `<ap> VA_ARG T` (type = node type; struct types need consumer ABI support, risk for cir-lib), `<ap> VA_END`, `<dst> <src> VA_COPY`; `eok_va_start_single_operand` (old `<varargs.h>`) same without the second operand |
| `eok_lvalue_cast`, `eok_lvalue_adjust`, `eok_ref_cast`, `eok_class_rvalue_adjust`, `eok_lvalue` | GNU lvalue casts / C++ | unsupported v0 (diagnose; `eok_lvalue_adjust` between canonically equal types is a no-op) |
| `eok_real_part/imag_part/xconj`, `eok_jmultiply...`, `eok_fj*` | complex | unsupported v0 |
| `eok_vector_*`, `eok_gnu_min/max`, `eok_spaceship`, `eok_box...`, `eok_base_class_cast...`, `eok_dynamic_cast`, `eok_pm_*`, `eok_dot_pm_*`, `eok_*_member_call`, `eok_*_vacuous_destructor_call`, `eok_noexcept`, `eok_assume`, `eok_virtual_function_ptr`, `eok_cli_subscript`, `eok_dot_static`, `eok_points_to_static`, `eok_bassign`, `eok_await/yield/splice`, `eok_parens` (see below) | C++/MS | not reachable in C, except `eok_parens`: transparent (`PARENS_IN_IL`; skip the node, keep `is_parenthesized` as a hint) |

### 5.3 Cast classification (writer, from canonical src/dst ECIL types)

Cast nodes: `eok_cast`, `eok_bool_cast`, `eok_array_to_pointer`. Classes: VOID,
BOOL, INT(w,s), FLOAT(fmt), PTR, ARRAY, FUNC, RECORD.

| src -> dst | emit |
| --- | --- |
| any -> VOID | `DISCARD` |
| same canonical id | nothing |
| INT -> INT | `CAST integral` (trunc/ext/sign change decided by the consumer from the ids) |
| INT -> BOOL / FLOAT -> BOOL / PTR -> BOOL | `CAST int_to_bool` / `float_to_bool` / `ptr_to_bool` (`eok_bool_cast`, and `TOBOOL`) |
| BOOL -> INT | `CAST bool_to_int` |
| BOOL -> FLOAT/PTR | `CAST bool_to_int` (to unsigned) then the next row |
| INT -> FLOAT / FLOAT -> INT | `CAST int_to_float` / `float_to_int` |
| FLOAT -> FLOAT | `CAST floating` |
| INT -> PTR / PTR -> INT | `CAST int_to_ptr` / `ptr_to_int` |
| PTR -> PTR (different canonical pointee) | `CAST bitcast` (pointee qualifier-only changes are the same canonical id, so nothing) |
| ARRAY -> PTR (`eok_array_to_pointer`) | `CAST array_to_ptrdecay` (operand is the array's address) |
| FUNC -> PTR | nothing (the node already denotes the address) |
| RECORD -> RECORD (same) | nothing; different record or union cast (GNU) unsupported |
| constants with pointer type (`ck_integer`) | `CONST_NULL` / `CONST_PTR` (no cast record) |

`TOBOOL` is the same table with dst BOOL (+ the int `!= 0` form for ints being
the consumer's `int_to_bool`). Arithmetic promotions use the writer's own
`usual_arithmetic_conversions` over canonical types (cross-checked against
`exprutil.h:1881` and `types.h:1064-1065` in CHECKING builds, to avoid FE type
allocation during hand-off).

### 5.4 Calls, varargs, builtins

* Direct call: `E#routine` node typed pointer-to-function (P). Indirect: any
  other pointer rvalue (P: `fpt("x")`, `(*fpt)("y")`).
* A variadic callee carries `F.variadic_callee`; the fixed parameter count is in
  the callee `TY_FUNC` (direct) or the operand's pointee type (indirect).
* `__func__`: a string-literal lvalue constant (P: `"call"`, `char[5]`).
* Builtin routines (`variant.builtin_function_kind`; P: `__builtin_expect` is
  an ordinary `enk_routine` with prototype `long(long,long)`):
  v0 minimal set mapped by name: `__builtin_expect` (value of arg 0),
  `__builtin_unreachable`, `__builtin_trap`, `__builtin_abort` ->
  `abort`, `__builtin_memcpy/memmove/memset/memcmp/strlen/strcmp` -> plain
  library call by the name without the prefix, `__builtin_va_*` (already
  `eok_va_*`). Everything else (`__builtin_popcount`, `__atomic_*`, vector, ...)
  is diagnosed unsupported in v0 (the writer never emits a call to a symbol that
  does not exist at link time).

## 6. Unsupported constructs in v0: detection

Policy (plan): "unsupported in CIR back end" diagnostic; the function becomes a
declaration (`FN_DECL`), the frame is not partially emitted (writer discards the
frame buffer), never a crash. Detection is cheap because the first marker is
visible before any traversal, or during the single traversal.

| construct | detection | when |
| --- | --- | --- |
| VLA | any `stmk_set_vla_size`/`stmk_vla_decl`; `a_scope::vla_dimensions != NULL`; type with `variant.array.is_vla`/`is_variable_size_array`; `a_variable::is_vla`/`has_variably_modified_type`; `il_header.vla_used` as a TU hint | statement/type visit; also at function start (`vla_dimensions`) |
| `_Complex`, `_Imaginary` | `tk_complex`/`tk_imaginary` type, `ck_complex`/`ck_imaginary`, `eok_real_part/imag_part/xconj/jmultiply/...`, `bok_builtin_complex` | type introduction, operator visit |
| `_Atomic` | `TQ_C11_ATOMIC` on a typeref/array qualifier (`il_def.h:6287`; P prints `_Atomic(int)`) | type introduction |
| GNU statement expression | `enk_statement`, `stmk_stmt_expr_result`, `a_routine::contains_statement_expression` | function start flag + visit |
| computed goto | `stmk_assigned_goto`, `ck_address`/`abk_label`, `a_label::address_taken` | statement/constant visit |
| inline `asm` / asm functions / file-scope asm | `stmk_asm`, `stmk_asm_func_body`, `sc_asm`, `scope->asm_entries` | statement visit; file scope at `F_GLOBALS` |
| vector types | `tk_vector`, `tk_scalable_vector`, ..., `eok_vector_*`, `bok_builtin_shuffle*` | type/operator visit |
| nested functions | rejected by the FE (P) | n/a |
| `__builtin_*` beyond the minimal set | routine with `builtin_function_kind != bfk_none` not in the table | call visit |
| case in nested control flow | `stmk_switch_case` whose `parent` chain (to the switch body) crosses a loop/if/switch | statement visit |
| non-constant file-scope initializers | `initk_dynamic`, `ck_dynamic_init` in a global | `F_GLOBALS` |
| GNU lvalue casts, `?:` omitted operand | `eok_lvalue_cast`, `is_gnu_two_operand_question_mark` | operator visit |
| `_BitInt`, `__int128`, `_Float16`, `__float128` | `ik_bit_precise`, (`ik_int128` is supported: plain 128-bit int), `fk_float16/fp16`, `fk_float128` | type introduction |
| thread-local | `is_effective_thread_local` | decl (may be supported; see 2) |
| weak aliases, ifunc, constructor/destructor attributes | `is_gnu_alias`/`is_weakref`/`is_ifunc`, `has_ctor_priority`/`has_dtor_priority` | decl visit |
| PCH, modules | command line | `cmd_line` (cir-config) |

## 7. Deep-recursion shapes and the writer's work stack

### 7.1 Shapes for C (all heap-stack iterations)

1. Left-deep binary chains `a+b+c+...` (also `,` chains, `&&`/`||` chains,
   `|` flag lists): depth = N through `operands`.
2. Right-deep `?:` chains and `else if` ladders (each `else_statement` is the
   next `stmk_if`), 20k realistic in generated code.
3. Deeply nested blocks, loops, parenthesised expressions, `(((((a)))))`
   (`eok_parens`).
4. Nested aggregate initializers (`{{{...}}}`, 5k) and *wide* flat ones (1M
   elements): the latter must stream with a cursor, never materialise the list.
5. Call nesting `f(f(f(...)))`, member/indirection chains `p->a->b->c`,
   `****p`, subscript chains.
6. Type graphs: pointer/array/function-return chains (`int ****...`), typedef
   chains, self-referential records through pointers (cycles broken by
   `TY_RECORD_DECL`).
7. Flat but long: statement lists (100k), switch case lists, parameter lists:
   iterate over `next`.
8. A function with a huge body: the frame is built in memory (decl + body
   buffers); a `F_REGION` continuation flag (`part` frames) is available if
   needed, not in v0.

### 7.2 Design (model: `interpret.c`)

`interpret.c:1409` `an_interpreter_work_item`, `:1473` `a_work_stack`,
`:2004` `push_work`, `:6237` `process_work_item`, `:6273` `run_pushed_work`:
slot-reusing doubly linked items (`below`, `above`, `depth`), `kind`, `phase`,
`il_entry`, a payload union per kind; a handler runs once per phase and pushes
children; items hand over by changing `kind` and resetting `phase`.
`Dyn_array` (`util.h:1607`) is the alternative when items need not stay valid
across pushes; the interpreter's linked-slot form keeps item pointers stable,
which the writer wants too (payload pointers into IL, not into the array).

```c
enum an_ecil_work_kind {
  ewk_type,       /* canonicalise + introduce a type (post-order) */
  ewk_record,     /* TY_RECORD_BODY once member types are introduced */
  ewk_expr,       /* post-order value expression (payload: node, ctx) */
  ewk_cond,       /* expr then TOBOOL; used by if/while/for/?:/&&/||/! */
  ewk_addr,       /* expr in address context (lvalue); no final LOAD */
  ewk_const,      /* constant; aggregates use a cursor over first_constant..next */
  ewk_init,       /* stmk_init / stmk_decl initialiser, designator normaliser */
  ewk_stmt,       /* statement (bracketed control flow) */
  ewk_stmt_list   /* cursor over a next-chain of sibling statements */
};
enum an_ecil_work_phase { ewp_start, ewp_resume1, ewp_resume2, ewp_resume3, ewp_finish };
```

Per-construct scripts (push order is reverse of execution; `S` = this item):

| construct | start | resume1 | resume2 / finish |
| --- | --- | --- | --- |
| binary op | push S(r1), op2, op1 | emit op | pop |
| `&&`/`||` | push S(r1), cond(op1) | emit `LAND_BEGIN`; push S(r2), cond(op2) | emit `LAND_END`; cast |
| `?:` | push S(r1), cond(op1) | emit `TERN_BEGIN`; push S(r2), expr(op2) | emit `TERN_ELSE`; push S(r3), expr(op3); resume3: `TERN_END` |
| call | evaluate callee unless direct; payload cursor = first arg | one resume per arg (cursor), no extra phases | emit `CALL` |
| compound assign | push S(r1), addr(op1) | emit `DUP LOAD cast`; push S(r2), expr(op2) | emit `cast op cast STORE` |
| `if` | push S(r1), cond(expr) | emit `IF_BEGIN`; push S(r2), stmt(then) | emit `IF_ELSE`+push stmt(else) (the else is an `stmk_if`: handed over by changing the item, not nesting) / `IF_END` |
| loops | analogous with `COND_END` brackets | | |
| `stmk_block` | emit `BLOCK_BEGIN`; push S(finish), then `ewk_stmt_list` | | emit `BLOCK_END` |
| `stmk_stmt_list` | each visit processes one statement and re-pushes itself with `stmt->next` (cursor), so a 100k list uses O(1) stack | | |
| aggregate init | cursor item over constants: one visit per constant; nested `ck_aggregate` pushes a child cursor | | `K_AGGR` count patch |
| type | push component types; if record: emit `TY_RECORD_DECL`, queue `ewk_record` | post-order emit `TY_*` | |

Type/decl introduction needed while an expression is on the stack runs a nested
loop of the same machinery (`run_pushed_work` pattern: call depth bounded by
the dependency of the *construct*, not by input depth): the entity's records go
to the frame's declaration buffer, body records to the body buffer; the frame is
`decl ++ body` (plan: declarations before first use). `REFS` trailer = ids seen.
Per-entity "emitted" state is the `ecil_id` slot (prefix); pending records list
for completion; streamed bitmap by `function_def_number`.

Rules that keep it non-recursive: no function in `ecil_write.c` that takes an
IL node and calls itself or a peer of the same traversal; per-node helpers are
leaf functions; every walk is a handler on an item. CI check: call-graph cycle
detection (`-fcallgraph-info`), plus the debug stack watermark described in
`writer_hooks.md` section 8.

## 8. Open questions for the orchestrator

1. **Compound assignment and ++/--**: this map has the writer expand compound
   assignment into `DUP/LOAD/cast/op/cast/STORE` (keeps cast classification in
   one place) and keeps `INC/DEC` as dedicated ops. Confirm the opcode set
   includes `DUP`, `DISCARD`, `TOBOOL`, `MATERIALIZE_TEMP`, `EXTRACT`,
   `BFIELD`, `ADDR_OFFSET`, `PTR_STRIDE`, `PTR_DIFF`.
2. **Comparison/`!`/`&&` results**: writer emits `bool_to_int` after a CMP/LNOT/
   LAND (clang CIR shape) and the consumer folds. Alternative: let CMP carry
   the result type. Pick one.
3. **C99 inline definitions**: v0 sends declaration only; confirm.
4. **Float constants**: IEEE bits for f32/f64, hex text for x87/quad; confirm
   the consumer can parse hex via APFloat.
5. **Frame size**: no continuation frames in v0.
6. **Static locals** are defined inside the function frame (initializer lives
   in the function region); the consumer must accept a global definition that
   arrives inside `F_REGION` (not only in `F_GLOBALS`).
7. **Tentative definitions** are emitted at `F_GLOBALS` as zero-init (no
   `common`).
8. **`G` var/fn type changes** between first reference and definition
   (`int a[]` then `int a[10]`; unprototyped declaration then prototyped
   definition): the consumer must replace the global/function symbol, as clang
   does.
