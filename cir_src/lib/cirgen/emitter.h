/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
emitter.h -- EDG-CIRGen: the non-recursive ECIL-to-CIR emitter (v0, C subset).

a_emitter consumes decoded ECIL records one at a time (ecil_reader.h) and
builds the MLIR module with CIR ops.  There is no recursion anywhere: nested
constructs are bracketed in the stream and tracked with a frame stack, values
with a value stack and constants with a constant stack, all heap vectors.  It
runs on the calling thread and never runs the verifier, a pass, a printer or a
recursive symbol walk; those happen at finish() on the worker thread.

Derived in part from llvm-project clang/lib/CIR/CodeGen (Apache-2.0 WITH
LLVM-exception); see CIRCompat.h for the upstream API surface used.
*/

#ifndef EDG_CIR_EMITTER_H
#define EDG_CIR_EMITTER_H 1

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "CIRCompat.h"
#include "cir_module.h"
#include "ecil_reader.h"
#include "session.h"

namespace edg_cir {

/* Types. */

enum class a_type_kind : uint8_t {
  none, void_type, bool_type, int_type, float_type, pointer_type,
  array_type, function_type, record_type, opaque_type
};  /* a_type_kind */

/* The CIR member index of a field that has no storage of its own (an empty
   base, an empty potentially-overlapping member). */
static const uint32_t no_member = 0xFFFFFFFFu;

struct a_field {
  uint64_t     type;         /* Type gid of the declared field type. */
  uint64_t     byte_offset;  /* Offset in bytes (bit-fields: of the byte). */
  uint32_t     bit_offset;   /* Bit-fields: bit within that byte. */
  uint32_t     bit_width;
  bool         is_bitfield;
  bool         is_unnamed;
  bool         empty_for_abi;
  bool         bitfield_signed;
  bool         flexible_array;
  bool         is_base;       /* A direct non-virtual base (BASE). */
  bool         overlapping;   /* POTENTIALLY_OVERLAPPING: base subobject. */
  bool         omitted;       /* Empty base or member: no storage in CIR. */
  std::string  name;
};  /* a_field */

/* How a bit-field is reached: its access unit and the bits inside it. */
struct a_bitfield_info {
  std::string  name;
  mlir::Type   storage;      /* The access unit's integer (or byte array). */
  mlir::Type   declared;     /* The field's declared (converted) type. */
  uint32_t     unit_index;   /* CIR member index of the access unit. */
  uint64_t     unit_offset;  /* Byte offset of the access unit. */
  unsigned     offset;       /* Bit offset inside the unit, from the LSB. */
  unsigned     size;         /* Width in bits (clamped to the unit width). */
  bool         is_signed;
};  /* a_bitfield_info */

struct a_record_info {
  bool                     is_union;
  bool                     is_class;
  bool                     nontrivial_abi;  /* Passed/returned indirectly. */
  bool                     nontrivial_dtor;
  bool                     complete;
  bool                     packed;        /* As derived by the lowering. */
  std::string              name;
  uint64_t                 size;
  uint64_t                 data_size;
  uint64_t                 align;
  std::vector<a_field>     fields;
  /* Per field: the CIR member index (a bit-field: of its access unit). */
  std::vector<uint32_t>    member_index;
  /* Per field: valid when the field is a bit-field. */
  std::vector<a_bitfield_info>
                           bitfields;
  /* Per CIR member: the type and kind (for constant building). */
  std::vector<mlir::Type>  members;
  std::vector<cir::RecordMemberKind>
                           kinds;
  mlir::Type               union_padding;
  /* The base-subobject type "<name>.base" (sized to data_size) when
     data_size < size and a base or overlapping member needed it. */
  mlir::Type               base_type;
  uint32_t                 pending_fields;  /* REC_FIELD records awaited. */
};  /* a_record_info */

struct a_type {
  a_type_kind      kind = a_type_kind::none;
  mlir::Type       cir;
  uint64_t         size = 0;
  uint64_t         align = 1;
  unsigned         bits = 0;       /* integer width. */
  bool             is_signed = false;
  uint64_t         target = 0;     /* pointee, element or return type gid. */
  uint64_t         count = 0;
  uint32_t         flags = 0;
  bool             variadic = false;
  bool             no_proto = false;
  std::vector<uint64_t>
                   params;
  a_record_info   *record = nullptr;
  unsigned         float_format = 0;
};  /* a_type */

/* Entities (functions and variables). */

struct a_entity {
  bool                       is_function = false;
  uint64_t                   gid = 0;
  uint64_t                   type = 0;
  unsigned                   linkage = 0;
  uint32_t                   flags = 0;
  uint32_t                   align = 0;
  std::string_view           symbol;     /* The current MLIR symbol: unique
                                            among the ops, never changed
                                            before finish. */
  std::string_view           reason;     /* The writer's reason for a
                                            dropped definition (1.1). */
  std::string_view           want;       /* The effective name (spec 7),
                                            valid when named. */
  bool                       named = false;
  mlir::Operation           *op = nullptr;
  bool                       has_body = false;
  bool                       has_init = false;
  bool                       referenced = false;
  bool                       had_linkonce = false;  /* Was linkonce_odr. */
  bool                       in_structor = false;  /* In a STRUCTOR pair. */
  bool                       is_alias = false;     /* C1 as aliasee of C2. */
  bool                       retracted = false;    /* RETRACT seen. */
  bool                       dyn_init = false;     /* Has a STATIC_INIT. */
  bool                       init_inline = false;  /* STATIC_INIT inline_var. */
  bool                       init_template = false;
  bool                       init_exit_only = false;
  std::vector<cir::CallOp>   calls;      /* Direct calls created. */
  std::vector<cir::GetGlobalOp>
                             addresses;  /* get_global ops created. */
};  /* a_entity */

/* A STRUCTOR pair: the complete (C1/D1) and the base (C2/D2) variant. */
struct a_structor_pair {
  a_entity  *complete;
  a_entity  *base;
  bool       is_dtor;
};  /* a_structor_pair */

/* A symbol to rename at finish (a null op: only references are redirected). */
struct a_symbol_change {
  mlir::Operation  *op;
  mlir::StringAttr  from;
  mlir::StringAttr  to;
};  /* a_symbol_change */

/* A library-generated symbol whose name derives from another symbol's final
   name (spec: every name-derived name is computed at finish, after the
   substitution of the final names). */
struct a_generated_symbol {
  cir::GlobalOp  op;
  uint64_t       function_gid;   /* The function the name derives from. */
  unsigned       index;
};  /* a_generated_symbol */

/* Values on the value stack. */

enum class a_value_kind : uint8_t { prvalue, glvalue, bitfield };

struct a_value {
  a_value_kind     kind = a_value_kind::prvalue;
  mlir::Value      value;          /* prv: the value; glv: the address; bit-
                                      field: the address of the access unit. */
  uint32_t         align = 1;      /* glv/bit-field: derived alignment. */
  const a_bitfield_info *bitfield = nullptr;
  bool             omitted = false;  /* glv: the address of an omitted empty
                                        field or base, which may share its
                                        bytes with another member: nothing
                                        is ever written through it. */
};  /* a_value */

/* Frames. */

enum class a_frame_kind : uint8_t {
  function, scope, if_stmt, while_stmt, do_stmt, for_stmt, switch_stmt,
  ternary, land, lor, agg_init, meminit,
  cleanup,    /* CLEANUP_BEGIN .. CLEANUP_BODY: the cleanup code. */
  fullexpr,   /* FULLEXPR_BEGIN .. FULLEXPR_END. */
  array_body, /* ARRAY_CTOR/DTOR body. */
  static_init, /* STATIC_INIT_BEGIN .. STATIC_INIT_END. */
  dropped     /* A construct the library drops (eh cleanups). */
};  /* a_frame_kind */

struct a_frame {
  a_frame_kind     kind;
  mlir::Operation *op = nullptr;       /* The structured op, if any. */
  mlir::Operation *scope_op = nullptr; /* for: the implicit enclosing scope. */
  uint8_t          phase = 0;
  size_t           value_base = 0;
  mlir::Type       result_type;        /* ternary. */
  bool             has_cond = false;   /* for. */
  mlir::Type       switch_type;        /* switch condition type. */
  size_t           target_base = 0;    /* agg_init target stack height. */
  /* cleanup code */
  uint8_t          cleanup_mode = 0;   /* 0 normal, 1 conditional. */
  mlir::OpBuilder::InsertPoint
                   saved_ip;           /* conditional: back to the arm. */
  mlir::Value      flag;               /* conditional: the cleanup.cond. */
  mlir::Operation *cleanup_op = nullptr; /* normal: the scope; conditional:
                                            the cir.if of the cleanup. */
  mlir::Operation *nrvo_guard = nullptr; /* normal: the cir.if that skips the
                                            destruction of the NRVO
                                            variable once it is returned. */
  /* static_init */
  a_entity        *init_entity = nullptr;
  mlir::Block     *saved_entry = nullptr;     /* Global: the function-level
                                                 state it replaced. */
  mlir::Operation *saved_last_alloca = nullptr;
  bool             global_init = false;       /* In F_GLOBALS (no function). */
  /* fullexpr */
  size_t           cleanups_base = 0;  /* Open cleanup scopes at its start. */
  /* Outermost conditional frame: the flag-guarded scope that wraps it. */
  size_t           cond_scope = static_cast<size_t>(-1);  /* cleanups_ index. */
};  /* a_frame */

/* An open cir.cleanup.scope: its body is being emitted, and it closes with the
   scope region it was begun in (owner frame and phase) or, for a conditional
   scope, with its FULLEXPR. */
struct a_cleanup_scope {
  cir::CleanupScopeOp  op;
  size_t               owner;        /* frames_ index of the region frame. */
  uint8_t              owner_phase;
  bool                 conditional;
};  /* a_cleanup_scope */

/* An entry of the constant stack: the attribute and whether it is all-zero
   (tracked so that zero detection never recurses through nested attrs). */
struct a_const {
  mlir::Attribute  attr;
  bool             is_zero;
};  /* a_const */

struct a_const_aggregate {
  uint64_t                     type;
  bool                         have_pending = false;
  uint64_t                     pending_index = 0;
  uint64_t                     pending_count = 1;
  size_t                       base;        /* Constant stack height. */
  struct a_entry {
    uint64_t        index;
    uint64_t        count;
    a_const         value;
  };
  std::vector<a_entry>         entries;
};  /* a_const_aggregate */

struct a_init_target {
  a_value  address;
  uint64_t type;
};  /* a_init_target */

struct a_local {
  mlir::Value  address;
  uint32_t     align = 1;
  uint64_t     type = 0;
  bool         nrvo = false;   /* The NRVO variable (LOCAL RETURN_SLOT). */
};  /* a_local */

class a_emitter {
public:
  explicit a_emitter(an_edg_cir_session &session);

  bool on_record(const ecil::a_record &record,
                 const ecil::a_record_context &context);
  /* Process one record; false means the session now holds an error. */

  bool dispatch_record(const ecil::a_record &record,
                       const ecil::a_record_context &context);

  bool on_frame_end(uint8_t frame_kind);
  /* Check the empty-stack and balanced-state rules at the end of a frame. */

  bool finish();
  bool is_discardable(const a_entity &e) const;
  bool is_dropped_definition(const a_entity &e) const;
  void entity_liveness(
      std::unordered_set<const a_entity *> &live,
      std::unordered_map<const a_entity *, std::vector<a_entity *>> &uses);
  /*
  Run at finish on the worker thread: final names and their one-pass
  substitution, record layout metadata, inline-only and unsupported-entity
  policy.  False: the session has an error.
  */

  uint64_t rename_count() const
    { return renames_applied_; }
  uint64_t walk_count() const
    { return walks_done_; }

private:
  /* Diagnostics. */
  bool fail(an_edg_cir_status status, uint32_t code, const std::string &text);
  bool format_error(const std::string &text);
  bool unsupported(const std::string &text);
  bool v1_flags(const ecil::an_operand &op, uint32_t mask, const char *what);
  /* False (an unsupported error) if the flags operand has C++ bits of mask
     set: ECIL v1 flags that no slice implements yet. */
  bool internal(const std::string &text);
  /* A failed internal check: the validator guarantees what was checked, so
     this is a library bug.  Always on (never an assert); sets
     EDG_CIR_ERROR_INTERNAL. */
  mlir::Location loc() const
    { return loc_; }
  bool uint_operand(const ecil::an_operand &op, uint64_t &out);
  bool int_operand_apint(const ecil::an_operand &op, unsigned bits,
                         llvm::APInt &out);
  std::string_view string_for(const ecil::an_operand &op, bool &ok);

  /* types.cpp */
  a_type *type_at(uint64_t gid);
  a_type *new_type(uint64_t gid);
  bool type_record(const ecil::a_record &r);
  bool record_body(const ecil::a_record &r);
  bool record_field(const ecil::a_record &r);
  bool complete_record_layout(uint64_t gid, a_type &t);
  void lower_struct(a_type &t, a_record_info &rec, bool as_base,
                    bool force_packed);
  void lower_union(a_type &t, a_record_info &rec);
  mlir::Type base_subobject_type(a_type &t);
  /* The type a base or potentially-overlapping member of type t occupies:
     t's ".base" record when its data size is smaller than its size (created
     on first use), else t itself. */
  const a_record_info *record_info_of(mlir::Type t) const;
  bool is_indirect_class(mlir::Type t) const;
  /* True for a record with NONTRIVIAL_ABI. */
  mlir::Attribute constant_for_member(mlir::Attribute attr,
                                      mlir::Type member_type);
  uint64_t type_size(mlir::Type t);
  uint64_t type_align(mlir::Type t, bool natural = false);
  bool type_is_complete(mlir::Type t) const;
  uint64_t field_bit_offset(const a_field &f) const
    { return f.byte_offset * 8 + (f.is_bitfield ? f.bit_offset : 0); }
  uint32_t derived_align(uint32_t base, uint64_t offset) const;
  mlir::Type convert_for_function(const a_type &t);
  std::string unique_record_name(const std::string &hint);
  a_local *local_at(uint64_t l);
  a_local *new_local(uint64_t l);

  /* globals.cpp */
  bool decl_function(const ecil::a_record &r);
  bool decl_variable(const ecil::a_record &r);
  bool var_init(const ecil::a_record &r);
  bool name_bind(const ecil::a_record &r);
  bool retract_record(const ecil::a_record &r);
  bool constant_record(const ecil::a_record &r);
  bool scalar_constant_record(const ecil::a_record &r, a_type *t,
                              uint64_t tgid);
  bool const_aggregate_end();
  bool commit_pending(a_const_aggregate &agg);
  bool build_constant_aggregate(a_const_aggregate &agg, a_const &out);
  mlir::Attribute string_attribute(const uint8_t *data, uint32_t length,
                                   mlir::Type array_type);
  mlir::Attribute zero_attribute(mlir::Type type);
  void push_const(mlir::Attribute attr, bool is_zero);
  mlir::Attribute scalar_int_attribute(mlir::Type type,
                                       const ecil::an_operand &op,
                                       bool &ok);
  mlir::Attribute float_attribute(mlir::Type type, const ecil::an_operand &op,
                                  bool &ok);
  a_entity *entity_at(uint64_t gid);
  a_entity *entity_for(uint64_t gid, bool is_function);
  void retype_function(a_entity &e, mlir::Type new_type);
  void retype_variable(a_entity &e, mlir::Type new_type);
  void convert_call_to_variadic(a_entity &e, cir::CallOp call);
  std::string_view claim_symbol(std::string_view want, uint64_t gid);
  std::string_view keep_string(std::string_view text);
  std::string_view unsupported_reason(const ecil::a_record &r);
  void assign_final_names();
  void apply_substitution();
  void apply_function_attributes(a_entity &e, cir::FuncOp fn);

  /* function.cpp */
  bool func_begin(const ecil::a_record &r);
  bool func_end(const ecil::a_record &r);
  bool param(const ecil::a_record &r);
  bool local_decl(const ecil::a_record &r);
  bool statement(const ecil::a_record &r);
  bool open_frame(a_frame_kind kind, mlir::Operation *op);
  a_frame *top_frame(a_frame_kind kind, const char *what);
  bool pop_frame();
  bool terminate_region(mlir::Region &region, bool value_block_ok = false);
  void terminate_current_block();
  void start_new_block();
  mlir::Value pop_bool_condition();
  bool emit_return(const ecil::a_record &r);
  bool return_from_slot();
  bool cleanup_record(const ecil::a_record &r);
  bool static_init_record(const ecil::a_record &r);
  void apply_dynamic_init();
  bool fullexpr_record(const ecil::a_record &r);
  bool array_record(const ecil::a_record &r);
  void close_cleanups(size_t owner, int phase);
  void close_cleanups_down_to(size_t count);
  bool in_conditional_region(size_t &outermost) const;
  void open_conditional_scope(size_t outermost);
  size_t region_owner() const;
  bool structor_record(const ecil::a_record &r);
  void apply_structors();
  void finish_structors();
  void apply_comdats();
  void implicit_return(bool is_main);
  mlir::Value return_slot();
  mlir::Value nrvo_flag();
  mlir::Value alloca_in(mlir::Block *block, mlir::Type type,
                        const std::string &name, uint32_t align);
  mlir::Value alloca_here(mlir::Type type, const std::string &name,
                          uint32_t align);
  mlir::IntegerAttr alignment_attr(uint64_t align);
  cir::GlobalLinkageKindAttr linkage_attr(cir::GlobalLinkageKind kind);
  mlir::NamedAttribute nothrow_attr();
  std::string label_name(uint64_t l);

  /* expr.cpp */
  bool expression(const ecil::a_record &r);
  bool builtin_operation(uint64_t id, llvm::SmallVectorImpl<mlir::Value> &args,
                         a_type *result_type);
  bool agg_record(const ecil::a_record &r);
  bool pop_value(a_value &out);
  bool pop_prvalue(mlir::Value &out);
  bool pop_glvalue(a_value &out);
  bool push_prvalue(mlir::Value v);
  void push_glvalue(mlir::Value address, uint32_t align);
  mlir::Value load_value(const a_value &v, bool is_volatile);
  bool store_value(const a_value &target, mlir::Value value, bool is_volatile,
                   mlir::Value *result, uint32_t store_flags = 0);
  bool class_cast(const ecil::a_record &r);
  bool new_array_record(const ecil::a_record &r);
  bool delete_record(const ecil::a_record &r);
  bool zero_bytes(const ecil::a_record &r);
  bool cmp3way(const ecil::a_record &r);
  bool check_class_arguments(const llvm::SmallVectorImpl<mlir::Value> &args);
  bool field_address(const a_value &base, uint64_t rec_gid, uint64_t index,
                     a_value &out);
  bool element_address(const a_value &base, uint64_t array_gid,
                       uint64_t index, a_value &out);
  mlir::Value arithmetic(uint8_t op, uint32_t flags, mlir::Value lhs,
                         mlir::Value rhs);
  mlir::Value to_bool(mlir::Value v);
  bool cast_value(uint32_t kind, mlir::Value v, mlir::Type to,
                  mlir::Value &out);
  bool call_record(const ecil::a_record &r, bool indirect);
  bool inc_dec(const ecil::a_record &r);
  bool constant_expression(const ecil::a_record &r);
  mlir::Value function_address(a_entity &e);
  mlir::Value variable_address(a_entity &e);
  void zero_fill(mlir::Value address, mlir::Type type, uint32_t align);
  mlir::Value get_constant_value(mlir::Attribute attr);
  bool materialize_constant_into(const a_value &target, const a_const &c,
                                 mlir::Type type);
  bool tern_record(const ecil::a_record &r);

  an_edg_cir_session                     &session_;
  a_cir_module                           &cm_;
  mlir::MLIRContext                      *ctx_;
  mlir::ModuleOp                          module_;
  compat::a_builder                       builder_;
  mlir::DataLayout                        layout_;
  mlir::Location                          loc_;

  /* Target facts. */
  unsigned                                reg_bits_;
  bool                                    cheap_unaligned_;
  bool                                    c_language_;

  /* Types and entities.  The id tables belong to the stream reader and hold
     the G-id and L-id facts; the emitter keeps its objects in deques (stable
     addresses) and hangs them on the tables' payload slots. */
  ecil::a_id_tables                      &ids_;
  std::deque<a_type>                      type_store_;
  std::deque<a_entity>                    entity_store_;
  std::deque<a_local>                     local_store_;
  std::deque<a_record_info>               records_;
  /* The record info of a CIR record type, keyed by the type's identity. */
  std::unordered_map<const void *, const a_record_info *>
                                          record_of_type_;
  std::unordered_map<std::string, unsigned>
                                          record_names_;
  /* The MLIR symbol names in use (views into the arena). */
  std::unordered_set<std::string_view>    symbols_;
  std::vector<a_entity *>                 entity_order_;
  std::vector<a_generated_symbol>         generated_;
  std::vector<a_structor_pair>            structors_;
  std::vector<a_entity *>                 redirected_;  /* C1s to erase. */
  std::vector<a_symbol_change>            symbol_changes_;
  uint64_t                                renames_applied_ = 0;
  uint64_t                                walks_done_ = 0;
  uint64_t                                pending_body_gid_ = 0;

  /* Per frame / function state. */
  std::vector<a_value>                    values_;
  std::vector<a_const>                    consts_;
  std::vector<a_const_aggregate>          const_aggs_;
  std::vector<a_frame>                    frames_;
  std::vector<a_init_target>              targets_;
  std::vector<a_cleanup_scope>            cleanups_;   /* Open scopes. */
  size_t                                  drop_depth_ = 0;
  size_t                                  record_depth_ = 0;
  /* Attributes built once (their uniquing lookups are not free). */
  mlir::UnitAttr                          unit_attr_;
  mlir::StringAttr                        nothrow_name_;
  mlir::StringAttr                        noreturn_name_;
  mlir::IntegerAttr                       align_attrs_[16];   /* 1 << index */
  cir::GlobalLinkageKindAttr              linkage_attrs_[16]; /* by kind */
  /* The last alloca made in the current function's entry block: the next
     one goes right after it (no backward scan). */
  mlir::Operation                        *last_alloca_ = nullptr;
  cir::FuncOp                             fn_;
  a_entity                               *fn_entity_ = nullptr;
  mlir::Block                            *fn_entry_ = nullptr;
  bool                                    in_function_ = false;
  unsigned                                params_seen_ = 0;
  unsigned                                temp_counter_ = 0;
  uint64_t                                fn_return_type_ = 0;
  mlir::Value                             fn_retval_;  /* __retval slot */
  mlir::Value                             fn_nrvo_flag_;  /* "nrvo" flag. */
};  /* a_emitter */

}  /* namespace edg_cir */

#endif /* EDG_CIR_EMITTER_H */
