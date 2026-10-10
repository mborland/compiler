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
#include <unordered_map>
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
  std::string                name;
  bool                       named = false;
  std::string                placeholder;
  mlir::Operation           *op = nullptr;
  bool                       has_body = false;
  bool                       has_init = false;
  bool                       referenced = false;
  bool                       renamed = false;
  uint32_t                   reissue_count = 0;
  std::vector<cir::CallOp>   calls;      /* Direct calls created. */
  std::vector<cir::GetGlobalOp>
                             addresses;  /* get_global ops created. */
};  /* a_entity */

/* Values on the value stack. */

enum class a_value_kind : uint8_t { prvalue, glvalue, bitfield };

struct a_value {
  a_value_kind     kind = a_value_kind::prvalue;
  mlir::Value      value;          /* prv: the value; glv: the address; bit-
                                      field: the address of the access unit. */
  uint32_t         align = 1;      /* glv/bit-field: derived alignment. */
  const a_bitfield_info *bitfield = nullptr;
};  /* a_value */

/* Frames. */

enum class a_frame_kind : uint8_t {
  function, scope, if_stmt, while_stmt, do_stmt, for_stmt, switch_stmt,
  ternary, land, lor, agg_init
};  /* a_frame_kind */

struct a_frame {
  a_frame_kind     kind;
  mlir::Operation *op = nullptr;       /* The structured op, if any. */
  mlir::Operation *scope_op = nullptr; /* for: the implicit enclosing scope. */
  uint8_t          phase = 0;
  mlir::Block     *alloca_block = nullptr;
  size_t           value_base = 0;
  mlir::Type       result_type;        /* ternary. */
  bool             has_cond = false;   /* for. */
  mlir::Type       switch_type;        /* switch condition type. */
  size_t           target_base = 0;    /* agg_init target stack height. */
};  /* a_frame */

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
};  /* a_local */

class a_emitter {
public:
  explicit a_emitter(an_edg_cir_session &session);

  bool on_record(const ecil::a_record &record,
                 const ecil::a_record_context &context);
  /* Process one record; false means the session now holds an error. */

  bool on_frame_end(uint8_t frame_kind);
  /* Check the empty-stack and balanced-state rules at the end of a frame. */

  bool finish();
  /*
  Run at finish on the worker thread: late renames, record layout metadata,
  inline-only and unsupported-entity policy.  False: the session has an error.
  */

private:
  /* Diagnostics. */
  bool fail(an_edg_cir_status status, uint32_t code, const std::string &text);
  bool format_error(const std::string &text);
  bool unsupported(const std::string &text);
  mlir::Location loc() const
    { return loc_; }
  bool uint_operand(const ecil::an_operand &op, uint64_t &out);
  bool int_operand_apint(const ecil::an_operand &op, unsigned bits,
                         llvm::APInt &out);
  std::string string_for(const ecil::an_operand &op, bool &ok);

  /* types.cpp */
  a_type *type_at(uint64_t gid);
  a_type *new_type(uint64_t gid);
  bool type_record(const ecil::a_record &r);
  bool record_body(const ecil::a_record &r);
  bool record_field(const ecil::a_record &r);
  bool complete_record_layout(uint64_t gid, a_type &t);
  void lower_struct(a_type &t, a_record_info &rec);
  void lower_union(a_type &t, a_record_info &rec);
  uint64_t type_size(mlir::Type t);
  uint64_t type_align(mlir::Type t, bool natural = false);
  bool type_is_complete(mlir::Type t) const;
  uint64_t field_bit_offset(const a_field &f) const
    { return f.byte_offset * 8 + (f.is_bitfield ? f.bit_offset : 0); }
  uint32_t derived_align(uint32_t base, uint64_t offset) const;
  mlir::Type convert_for_function(const a_type &t);
  std::string unique_record_name(const std::string &hint);

  /* globals.cpp */
  bool decl_function(const ecil::a_record &r);
  bool decl_variable(const ecil::a_record &r);
  bool var_init(const ecil::a_record &r);
  bool name_bind(const ecil::a_record &r);
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
  a_entity *entity_for(uint64_t gid, bool is_function);
  void retype_function(a_entity &e, mlir::Type new_type);
  void retype_variable(a_entity &e, mlir::Type new_type);
  void convert_call_to_variadic(a_entity &e, cir::CallOp call);
  std::string unique_symbol(const std::string &base);
  void apply_function_attributes(a_entity &e, cir::FuncOp fn);

  /* function.cpp */
  bool func_begin(const ecil::a_record &r);
  bool func_end(const ecil::a_record &r);
  bool param(const ecil::a_record &r);
  bool local_decl(const ecil::a_record &r);
  bool statement(const ecil::a_record &r);
  bool open_frame(a_frame_kind kind, mlir::Operation *op,
                  mlir::Block *alloca_block);
  a_frame *top_frame(a_frame_kind kind, const char *what);
  bool pop_frame();
  bool terminate_region(mlir::Region &region, bool value_block_ok = false);
  void terminate_current_block();
  void start_new_block();
  mlir::Value pop_bool_condition();
  bool emit_return(const ecil::a_record &r);
  void implicit_return(bool is_main);
  mlir::Value return_slot();
  mlir::Value alloca_in(mlir::Block *block, mlir::Type type,
                        const std::string &name, uint32_t align);
  mlir::Value alloca_here(mlir::Type type, const std::string &name,
                          uint32_t align);
  mlir::Block *current_alloca_block();
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
                   mlir::Value *result);
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

  /* Types and entities. */
  std::vector<a_type>                     types_;
  std::deque<a_record_info>               records_;
  /* The record info of a CIR record type, keyed by the type's identity. */
  std::unordered_map<const void *, const a_record_info *>
                                          record_of_type_;
  std::vector<uint64_t>                   record_gids_;
  std::unordered_map<std::string, unsigned>
                                          record_names_;
  std::unordered_map<std::string, unsigned>
                                          symbol_names_;
  std::unordered_map<uint64_t, a_entity>  entities_;
  std::vector<uint64_t>                   entity_order_;
  uint64_t                                pending_body_gid_ = 0;

  /* Per frame / function state. */
  std::vector<a_value>                    values_;
  std::vector<a_const>                    consts_;
  std::vector<a_const_aggregate>          const_aggs_;
  std::vector<a_frame>                    frames_;
  std::vector<a_init_target>              targets_;
  std::unordered_map<uint64_t, a_local>   locals_;
  cir::FuncOp                             fn_;
  a_entity                               *fn_entity_ = nullptr;
  mlir::Block                            *fn_entry_ = nullptr;
  bool                                    in_function_ = false;
  unsigned                                params_seen_ = 0;
  unsigned                                temp_counter_ = 0;
  uint64_t                                fn_return_type_ = 0;
  mlir::Value                             fn_retval_;  /* __retval slot */
  std::vector<std::pair<uint64_t, std::string>>
                                          renames_;
  std::vector<cir::GlobalOp>              temporaries_;
};  /* a_emitter */

}  /* namespace edg_cir */

#endif /* EDG_CIR_EMITTER_H */
