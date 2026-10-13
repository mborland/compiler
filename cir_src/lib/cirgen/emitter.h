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
#include "abi_builder.h"
#include "abi_vtable.h"
#include "cir_module.h"
#include "ecil_reader.h"
#include "session.h"

namespace edg_cir {

/* Types. */

enum class a_type_kind : uint8_t {
  none, void_type, bool_type, int_type, float_type, pointer_type,
  array_type, function_type, record_type, opaque_type,
  vptr_type,   /* TY_VPTR (1.2): !cir.vptr. */
  data_member_type,   /* TY_DATA_MEMBER (1.2): !cir.data_member. */
  method_type         /* TY_METHOD (1.2): !cir.method. */
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
  bool         is_vbase;      /* A virtual base (VIRTUAL_BASE, 1.2). */
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
  uint64_t                 nvsize;          /* CLASS_INFO; else data_size. */
  uint64_t                 align;
  uint64_t                 gid = 0;         /* The record type's G-id. */
  bool                     awaiting_info;   /* A dynamic class whose CLASS_INFO
                                               (12.2) completes its layout. */
  int64_t                  primary_virtual = -1;   /* The VIRTUAL_BASE field
                                               that is the primary base (it
                                               lives in the non-virtual part
                                               and has no storage of its own
                                               in the virtual part). */
  std::vector<a_field>     fields;
  /* Per field: the CIR member index (a bit-field: of its access unit). */
  std::vector<uint32_t>    member_index;
  /* The same for the ".base" record (valid with base_type): the member
     indices of a data member's path that goes down through a base. */
  std::vector<uint32_t>    base_member_index;
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
  uint64_t                 base_type_size;  /* The nvsize base_type was made
                                               for. */
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
  bool                       has_vtt = false;      /* The base variant of a
                                                      has_vbases pair: a
                                                      hidden VTT parameter
                                                      follows `this`. */
  bool                       retracted = false;    /* RETRACT seen. */
  bool                       lowered_form = false; /* Emitted with lowered
                                                      method pointers. */
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
  bool       has_vbases;
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

/* A DECL_ABI_DATA (1.2): a vtable, a VTT or a typeinfo global.  The entity
   is a variable entity of its own (the final names and the linkage rules of
   the other variables apply to it); its type and initializer are built at
   finish, when the class facts are complete. */
struct a_abi_entity {
  uint64_t   gid = 0;
  unsigned   kind = 0;         /* an_ecil_abi_data_kind. */
  uint64_t   type = 0;         /* The class (or type) G-id. */
  uint32_t   flags = 0;        /* The latest DECL_ABI_DATA flags. */
  a_entity  *entity = nullptr;
  a_entity  *name_entity = nullptr;   /* Typeinfo: its _ZTS global. */
  /* The typeinfo of a type that is not a class (TYPEINFO_DEF). */
  bool       has_def = false;
  unsigned   def_kind = 0;     /* an_ecil_typeinfo_kind. */
  uint32_t   def_flags = 0;    /* ECIL_TYPEINFO_*: the pbase masks. */
  uint64_t   def_a = 0;        /* The pointee's typeinfo G-id (0: none). */
  uint64_t   def_b = 0;        /* The class's typeinfo G-id (0: none). */
  std::string symbol;          /* Non-class typeinfo: its name (sid). */
};  /* a_abi_entity */

/* A vtable address point whose symbol and indices are chosen at finish (the
   vtable of the class is declared at the end of the translation unit). */
struct a_vptr_use {
  mlir::Operation  *op;            /* The cir.vtable.address_point. */
  uint64_t          vtable_class;  /* Whose vtable group. */
  uint64_t          base_class;    /* The base subobject (class, offset). */
  int64_t           base_offset;
};  /* a_vptr_use */

/* A number or symbol inside an op that depends on the layouts of the class
   facts, which are complete only at finish (the VTT index of a subobject,
   the place of a virtual base offset in a vtable): the op is made with a
   placeholder and patched there. */
enum class a_late_kind : uint8_t {
  dyn_cast_hint,      /* cir.dyn_cast: the offset hint of the cast from class
                         `owner` to class `sub`. */
  vtt_sub,            /* vtt.address_point: the sub-VTT of a base or virtual
                         base (class `sub`, offset) of class `owner`. */
  vtt_secondary,      /* vtt.address_point: the secondary virtual pointer of
                         the subobject (class `sub`, offset). */
  vbase_offset_offset /* A 64-bit constant: where the offset of virtual base
                         `sub` is stored in the vtable of class `owner`. */
};  /* a_late_kind */

struct a_late_use {
  a_late_kind       kind;
  mlir::Operation  *op;
  uint64_t          owner;       /* The class whose VTT or vtable. */
  uint64_t          sub;         /* 0: the VTT itself (index 0). */
  int64_t           offset;
  bool              names_vtt;   /* The op names the VTT global of `owner`. */
};  /* a_late_use */

/* The key of a thunk: its target and the four adjustment numbers. */
struct a_thunk_key {
  const void  *target;
  int64_t      this_nv;
  int64_t      this_vcall;
  int64_t      ret_nv;
  int64_t      ret_vbase;

  bool operator<(const a_thunk_key &o) const
    {
      if (target != o.target)
        return target < o.target;
      if (this_nv != o.this_nv)
        return this_nv < o.this_nv;
      if (this_vcall != o.this_vcall)
        return this_vcall < o.this_vcall;
      if (ret_nv != o.ret_nv)
        return ret_nv < o.ret_nv;
      return ret_vbase < o.ret_vbase;
    }
};  /* a_thunk_key */

/* A STRUCTOR_DELETING: D0 is synthesized at finish. */
struct a_deleting_dtor {
  a_entity  *d0;
  a_entity  *d1;
  a_entity  *del;
  uint32_t   flags;
  uint64_t   class_gid;
};  /* a_deleting_dtor */

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
  try_body,   /* TRY_BEGIN .. CATCH_BEGIN | TRY_END: the try region. */
  catch_body, /* CATCH_BEGIN .. CATCH_BEGIN | TRY_END: a handler. */
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
  /* cleanup: a DEACTIVATABLE cleanup (mode 4: its code is guarded by the
     flag, which the scope body sets). */
  cir::IfOp        deact_guard;
  mlir::Value      deact_flag;
  bool             deactivatable = false;
  size_t           cond_index = 0;     /* cleanup mode 1: the cleanups_
                                          index of its hoisted scope. */
  /* try_body, catch_body: the index in tries_. */
  size_t           try_index = 0;
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

/* A DEACTIVATABLE cleanup that is still active: CLEANUP_END stores false in
   its flag.  It lives as long as the cleanup scope that holds it is open
   (cleanups_.size() >= depth). */
struct a_deact {
  mlir::Value  flag;
  size_t       depth;
};  /* a_deact */

/* A handler of a try being built (ECIL 1.2 TRY .. CATCH .. TRY_END).  The
   try body and every handler are emitted into placeholder cir.scope regions;
   at TRY_END the cir.try is made and the regions move into it. */
struct a_handler {
  cir::ScopeOp  scope;          /* The placeholder. */
  mlir::Attribute  type;        /* #cir.global_view of the typeinfo, or null
                                   for catch (...). */
  a_abi_entity *ti = nullptr;   /* The typeinfo entity, if any. */
  bool          head_pending = false;   /* begin_catch is not made yet. */
};  /* a_handler */

struct a_try {
  cir::ScopeOp           body;      /* The placeholder of the try region. */
  std::vector<a_handler> handlers;
  bool                   function_try = false;
};  /* a_try */

/* A THROW_BEGIN .. THROW_END in progress: the exception object. */
struct a_throw {
  mlir::Value  object;     /* The cir.alloc.exception pointer. */
  mlir::Value  slot;       /* Where the pointer is kept for the cleanup. */
  bool         materialized = false;   /* The cleanup that frees the object
                                          exists (see materialize_throws). */
  mlir::Value  flag;       /* Its activity flag: THROW_END clears exactly
                              this one, not the innermost active cleanup. */
};  /* a_throw */

/* An entry of the constant stack: the attribute and whether it is all-zero
   (tracked so that zero detection never recurses through nested attrs). */
struct a_const {
  a_const() : is_zero(false) {}
  a_const(mlir::Attribute a, bool zero) : attr(a), is_zero(zero) {}

  mlir::Attribute  attr;
  bool             is_zero;
  uint32_t         mark_begin = 0;   /* The vptr markers (vptr_markers_) of
                                        the objects inside the constant. */
  uint32_t         mark_end = 0;
  mlir::Type       lowered_type;     /* Set when the constant holds pointers
                                        to member functions in their lowered
                                        {i64, i64} form (G2): its type. */
};  /* a_const */

/* The vptr of an object in a constant: the address point of a base subobject
   (class, offset) in the vtable group of the complete object's class.  The
   complete object is known only when the constant is used, and the vtable is
   declared at the end of the translation unit: the constant holds a
   placeholder #cir.global_view that finish replaces. */
struct a_vptr_marker {
  uint64_t  class_gid;      /* The subobject that has the vptr. */
  int64_t   offset;         /* In the aggregate built so far. */
  uint64_t  vtable_class;   /* The complete object's class (0: not yet). */
};  /* a_vptr_marker */

struct a_const_aggregate {
  uint64_t                     type;
  bool                         have_pending = false;
  uint64_t                     pending_index = 0;
  uint64_t                     pending_count = 1;
  size_t                       base;        /* Constant stack height. */
  uint32_t                     marker_base = 0;  /* vptr_markers_ size. */
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
  bool finish_entities();
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

  /* classes.cpp: the C++ class machinery (virtual functions, vtables,
     typeinfo, thunks). */
  bool class_info_record(const ecil::a_record &r);
  bool structor_deleting_record(const ecil::a_record &r);
  bool abi_data_record(const ecil::a_record &r);
  bool vptr_init_record();
  bool early_vptr_store();
  bool vptr_init_with_vbases();
  /* VPTR_INIT in a class with virtual bases (clang's getVTablePointers and
     initializeVTablePointer). */
  bool vbase_cast_record(const ecil::a_record &r);
  bool vtt_addr_record(const ecil::a_record &r);
  mlir::Value virtual_base_address(mlir::Value object, uint64_t der,
                                   uint64_t vbase, mlir::Type result_type,
                                   bool null_check);
  mlir::Value virtual_base_offset(mlir::Value object, uint64_t der,
                                  uint64_t vbase);
  /* The offset of a virtual base from the vtable of the object (an s64). */
  cir::FuncType with_vtt_parameter(cir::FuncType type);
  mlir::Type vtt_parameter_type();
  bool patch_late_uses(edg_abi::a_vtable_context &ctx,
                       const std::map<uint64_t, edg_abi::a_class_id> &by_key);
  bool finish_vtt(a_abi_entity &abi, edg_abi::a_vtable_context &ctx,
                  edg_abi::a_class_db &db,
                  const std::map<uint64_t, edg_abi::a_class_id> &by_key,
                  const std::string &mangled);
  bool vtable_value(edg_abi::a_class_db &db,
                    const edg_abi::a_vtable_layout &layout,
                    cir::RecordType &record, mlir::Attribute &value);
  a_entity *construction_vtable(
      edg_abi::a_vtable_context &ctx, edg_abi::a_class_db &db,
      edg_abi::a_class_id complete, const edg_abi::a_vtt_vtable &which,
      const a_entity &vtt,
      std::unique_ptr<edg_abi::a_vtable_layout> &layout, bool &ok);
  bool init_zero_base_record();
  bool virtual_call(a_entity &callee, llvm::SmallVectorImpl<mlir::Value> &args,
                    uint64_t slot, llvm::ArrayRef<mlir::NamedAttribute> attrs,
                    cir::CallOp &out);
  bool virtual_delete(uint64_t g_dtor, mlir::Value pointer,
                      a_type *object_type);
  void resolve_vptr_markers(const a_const &c, mlir::Type object_type);
  /* The constant is used for an object of this type: its vptr markers
     without a complete class get it. */
  bool patch_vptr_constants(edg_abi::a_vtable_context &ctx,
                            edg_abi::a_class_db &db,
                            const std::map<uint64_t, edg_abi::a_class_id>
                                &by_key);
  bool finish_classes();
  /* finish: decode and check the class facts, then everything the facts
     produce (the types of the ABI data, vtables, typeinfo, thunks, D0). */
  bool finish_abi_entity(a_abi_entity &abi, edg_abi::a_vtable_context &ctx,
                         edg_abi::a_class_db &db,
                         const std::map<uint64_t, edg_abi::a_class_id>
                             &by_key);
  bool finish_class_typeinfo(
      a_abi_entity &abi, edg_abi::a_vtable_context &ctx,
      edg_abi::a_class_db &db,
      const std::map<uint64_t, edg_abi::a_class_id> &by_key,
      const std::string &mangled);
  mlir::Attribute vtable_component(
      edg_abi::a_class_db &db, const edg_abi::a_vtable_layout &layout,
      size_t index,
      const std::map<size_t, const edg_abi::a_thunk_info *> &thunks,
      bool &ok);
  mlir::Attribute rtti_pointer(edg_abi::a_class_db &db,
                               const edg_abi::a_component &component,
                               bool &ok);
  a_entity *thunk_entity(const edg_abi::a_thunk_info &info, a_entity &target,
                         bool &ok);
  void build_thunk_body(a_entity &thunk, a_entity &target,
                        const edg_abi::a_thunk_info &info);
  void build_deleting_dtor(a_deleting_dtor &d);
  a_entity *define_library_function(std::string_view want, mlir::Type type,
                                    unsigned linkage, bool comdat);
  a_entity *library_entity(std::string_view name, bool is_function,
                           mlir::Type type, bool constant);
  mlir::Attribute symbol_pointer(a_entity &e);
  mlir::Attribute pointer_constant(int64_t value);
  bool defined_here(const a_entity &e) const;
  a_abi_entity *abi_entity_of(unsigned kind, uint64_t class_gid);
  a_entity *function_of_key(uint64_t g);

  /* members.cpp: pointers to members and the null constants (12.7). */
  bool zero_initializable(mlir::Type type);
  mlir::Attribute null_attribute(mlir::Type type);
  bool data_member_path(uint64_t class_gid, uint64_t offset,
                        mlir::Type member_type, std::vector<int32_t> &path,
                        bool *empty_member = nullptr);
  mlir::Attribute data_member_constant(a_type &t,
                                       const ecil::an_operand &offset,
                                       bool &ok);
  mlir::Type method_type_in_class_of(a_entity &fn);
  mlir::Attribute method_constant(a_type &t, const ecil::a_record &r,
                                  bool in_function, bool &ok, bool &lowered);
  mlir::Type lowered_method_type();
  mlir::Attribute lowered_method_attr(mlir::Attribute attr);
  mlir::Type lowered_type_of(mlir::Type type);
  mlir::Attribute lower_constant(mlir::Attribute attr, mlir::Type type);
  bool member_pointer_record(const ecil::a_record &r);
  bool pm_call_record(const ecil::a_record &r);

  /* rtti.cpp: the typeinfo of types that are not classes, typeid and
     dynamic_cast. */
  bool typeinfo_def_record(const ecil::a_record &r);
  void name_class_abi_entities();
  bool finish_plain_typeinfo(a_abi_entity &abi, edg_abi::a_class_db &db);
  a_entity *typeinfo_name_global(a_entity &typeinfo,
                                 const std::string &symbol,
                                 const std::string &text);
  bool typeid_record(const ecil::a_record &r);
  bool dynamic_cast_record(const ecil::a_record &r);

  /* eh.cpp: exceptions (ECIL 1.2, M4 slice S4). */
  bool exception_record(const ecil::a_record &r);
  bool try_begin_record(const ecil::a_record &r);
  bool catch_begin_record(const ecil::a_record &r);
  bool catch_param_record(const ecil::a_record &r);
  bool try_end_record();
  bool throw_begin_record(const ecil::a_record &r);
  bool throw_end_record(const ecil::a_record &r);
  bool materialize_throws(bool skip_last = false);
  bool rethrow_record();
  bool eh_spec_record(const ecil::a_record &r);
  bool array_partial_dtor_record(const ecil::a_record &r);
  bool cleanup_end_record();
  bool flush_catch_head(const ecil::a_record &next);
  /* The head of a handler waits for the LOCAL and CATCH_PARAM that may
     follow CATCH_BEGIN; any other record makes it now. */
  bool make_catch_head(a_handler &h, a_local *param, uint64_t kind,
                       a_entity *copy);
  bool assemble_try(a_try &t);
  bool close_eh_spec();
  a_entity *catch_copy_thunk(a_handler &h, a_entity &copy, uint64_t class_gid);
  mlir::Type eh_token_type();
  size_t try_depth() const
    { return tries_.size() + (eh_spec_active_ ? 1u : 0u); }
  void deactivate_cleanup();
  void deactivate_flag(mlir::Value flag);
  void prune_deacts();
  bool begin_cleanup_code(uint64_t kind, bool deactivatable);
  bool end_cleanup_code();
  cir::CleanupKind cleanup_kind(uint64_t kind) const;
  bool is_nothrow_call(uint64_t flags, const a_entity *callee) const;

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
  bool terminate_region(mlir::Region &region, bool keep_dead = false);
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
  bool deletable_type(const a_type &t) const
    { return t.kind != a_type_kind::void_type && type_is_complete(t.cir); }
  bool arm_terminated(size_t value_base);

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
  std::deque<a_abi_entity>                abi_store_;
  std::vector<a_abi_entity *>             abi_order_;
  std::vector<a_vptr_use>                 vptr_uses_;
  std::vector<a_late_use>                 late_uses_;
  bool                                    have_vtable_facts_ = false;
  std::unordered_map<const void *, bool>  zero_init_cache_;
  std::unordered_map<const void *, mlir::Type>
                                          lowered_cache_;
  std::unordered_map<const void *, mlir::Attribute>
                                          null_cache_;
  std::vector<a_deleting_dtor>            deleting_dtors_;
  std::vector<a_vptr_marker>              vptr_markers_;
  std::unordered_map<std::string_view, a_entity *>
                                          library_names_;
  std::unordered_map<std::string_view, a_entity *>
                                          stream_names_;
  bool                                    library_names_seeded_ = false;
  std::map<a_thunk_key, a_entity *>       thunks_;
  std::unordered_set<const a_entity *>    complete_defined_;
  uint64_t                                pending_info_gid_ = 0;
  uint64_t                                this_class_ = 0;
  mlir::Value                             vtt_arg_;  /* The hidden VTT
                                                        parameter of a C2/D2
                                                        of a class with
                                                        virtual bases. */
  /* A C2/D2 of a class with virtual bases whose base initializers convert
     `this` to a virtual base before VPTR_INIT: the vtable pointer must
     already be set (PR 9629; clang stores VTT[0] at entry).  The mark is
     the last op of the entry block when the body starts. */
  mlir::Operation                        *entry_mark_ = nullptr;
  bool                                    entry_mark_set_ = false;
  bool                                    vptr_set_early_ = false;
  bool                                    vptr_init_seen_ = false;
  a_local                                *this_local_ = nullptr;
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
  /* Exceptions. */
  bool                                    exceptions_ = false;
  std::vector<a_try>                      tries_;      /* Open TRYs. */
  std::vector<a_deact>                    deacts_;     /* Active deactivatable
                                                          cleanups. */
  std::vector<a_throw>                    throws_;     /* Open THROWs. */
  bool                                    eh_spec_active_ = false;
  unsigned                                eh_spec_kind_ = 0;
  std::vector<mlir::Attribute>            eh_spec_types_;
  cir::ScopeOp                            eh_spec_body_;
  int64_t                                 pending_head_ = -1; /* tries_ index
                                                with a handler head not yet
                                                made, or -1. */
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
