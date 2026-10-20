/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
ecil_schema.cpp -- the opcode schema: names and arities come from the
ECIL_OPCODES table in ecil_format.h; operand classes, flag masks and
enumerator ranges below are the operand rules of the spec (section 8).
*/

#include <cctype>
#include <string>

#include "ecil_reader.h"

namespace ecil {

namespace {

typedef an_operand_spec s;

const s s_id    = {an_operand_class::id, 0, 0};
const s s_idn   = {an_operand_class::id_or_nil, 0, 0};
const s s_uint  = {an_operand_class::uinteger, 0, 0};
const s s_uintn = {an_operand_class::uinteger_or_nil, 0, 0};
const s s_int   = {an_operand_class::integer, 0, 0};
const s s_intn  = {an_operand_class::integer_or_nil, 0, 0};
const s s_bool  = {an_operand_class::boolean, 0, 0};
const s s_bin   = {an_operand_class::bin, 0, 0};
const s s_arr   = {an_operand_class::array, 0, 0};
const s s_flt   = {an_operand_class::floating, 0, 0};

s s_flags(uint32_t mask)
/*
Return an operand spec for a flags operand with the given allowed bits.
*/
{
  s result = {an_operand_class::flags, 0, mask};
  return result;
}  /* s_flags */

s s_enum(uint32_t lo, uint32_t hi)
/*
Return an operand spec for an enumerator in [lo, hi].
*/
{
  s result = {an_operand_class::enumerator, lo, hi};
  return result;
}  /* s_enum */

struct a_spec_row {
  uint8_t  opcode;
  uint8_t  section;
  s        operands[ECIL_MAX_ARITY];
};  /* a_spec_row */

#define SM eos_meta
#define SH eos_header
#define ST eos_type
#define SD eos_decl
#define SB eos_body
#define SC eos_const
#define SS (eos_body | eos_stmt)
#define V1 eos_v1

/* One row per opcode; the operand count must equal the table arity. */
const a_spec_row spec_rows[] = {
  {eop_str, SM, {s_id, s_bin}},
  {eop_loc, SM, {s_idn, s_uint, s_uint}},
  {eop_name_bind, SM, {s_id, s_id, s_enum(0, elk_common)}},
  {eop_retract, SM, {s_id}},
  {eop_ext, SM, {s_uint, s_bin}},
  {eop_refs, SM, {s_arr}},
  {eop_end, SM, {s_uint, s_uint}},
  {eop_hdr_target, SH, {s_id, s_idn, s_idn, s_uint, s_uint, s_flags(7)}},
  {eop_hdr_lang, SH, {s_enum(0, elang_cxx), s_uint, s_flags(0xF)}},
  {eop_hdr_producer, SH, {s_id, s_idn}},
  {eop_hdr_type_role, SM, {s_enum(etr_size_t, etr_char), s_id}},
  {eop_ty_void, ST, {s_id}},
  {eop_ty_bool, ST, {s_id, s_uint, s_uint}},
  {eop_ty_int, ST, {s_id, s_uint, s_bool, s_uint, s_uint}},
  {eop_ty_float, ST, {s_id, s_enum(0, eff_ibm_double), s_uint, s_uint}},
  {eop_ty_ptr, ST, {s_id, s_id}},
  {eop_ty_array, ST, {s_id, s_id, s_uintn, s_flags(3)}},
  {eop_ty_func, ST, {s_id, s_id, s_flags(3), s_arr}},
  {eop_ty_record_decl, ST, {s_id, s_enum(0, erk_class), s_idn}},
  {eop_ty_record_body, ST, {s_id, s_uint, s_uint, s_uint, s_flags(0xF),
                            s_uint}},
  {eop_rec_field, ST, {s_id, s_uint, s_uintn, s_uintn, s_idn,
                       s_flags(0x7F)}},
  {eop_ty_opaque, ST, {s_id, s_uint, s_uint}},
  {eop_decl_func, SD, {s_id, s_id, s_enum(0, elk_common), s_flags(0x3FF),
                       s_idn}},
  {eop_decl_var, SD, {s_id, s_id, s_enum(0, elk_common), s_flags(0x1FF),
                      s_uint, s_idn}},
  {eop_var_init, SD, {s_id}},
  {eop_structor, SD | V1, {s_id, s_id, s_flags(3)}},
  {eop_func_begin, SS, {s_id, s_flags(0)}},
  {eop_func_end, SS, {s_flags(1)}},
  {eop_param, SS, {s_id, s_id, s_id, s_idn, s_flags(1)}},
  {eop_local, SS, {s_id, s_id, s_idn, s_uint, s_flags(3)}},
  {eop_block_begin, SS, {}},
  {eop_block_end, SS, {}},
  {eop_return_from_slot, SS | V1, {}},
  {eop_if_begin, SS, {s_bool}},
  {eop_if_else, SS, {}},
  {eop_if_end, SS, {}},
  {eop_while_begin, SS, {}},
  {eop_while_body, SS, {}},
  {eop_while_end, SS, {}},
  {eop_do_begin, SS, {}},
  {eop_do_cond, SS, {}},
  {eop_do_end, SS, {}},
  {eop_for_begin, SS, {}},
  {eop_for_cond, SS, {s_bool}},
  {eop_for_step, SS, {s_bool}},
  {eop_for_body, SS, {}},
  {eop_for_end, SS, {}},
  {eop_switch_begin, SS, {}},
  {eop_case_value, SS, {s_int}},
  {eop_case_range, SS, {s_int, s_int}},
  {eop_case_default, SS, {}},
  {eop_switch_end, SS, {}},
  {eop_break_stmt, SS, {}},
  {eop_continue_stmt, SS, {}},
  {eop_return_stmt, SS, {s_bool}},
  {eop_goto_stmt, SS, {s_id, s_uint}},
  {eop_label, SS, {s_id, s_idn}},
  {eop_unreachable, SS, {}},
  {eop_init_const, SB, {}},
  {eop_init_zero, SB, {}},
  {eop_agg_init_begin, SB, {s_id, s_flags(1)}},
  {eop_agg_sel, SB, {s_uint}},
  {eop_agg_init_end, SB, {}},
  {eop_cleanup_begin, SS | V1, {s_enum(ecl_normal, ecl_exit), s_idn,
                                s_flags(1)}},
  {eop_cleanup_body, SS | V1, {}},
  {eop_cleanup_end, SS | V1, {}},
  {eop_fullexpr_begin, SS | V1, {}},
  {eop_fullexpr_end, SS | V1, {}},
  {eop_static_init_begin, SS | V1, {s_id, s_flags(7)}},
  {eop_static_init_end, SS | V1, {}},
  {eop_meminit_begin, SS | V1, {s_enum(emk_vbase, emk_delegation), s_uint,
                                s_flags(3)}},
  {eop_meminit_end, SS | V1, {}},
  {eop_lref, SB, {s_id}},
  {eop_gref, SB, {s_id}},
  {eop_load, SB, {s_flags(1)}},
  {eop_store, SB, {s_flags(7), s_enum(0, esr_prv)}},
  {eop_field, SB, {s_id, s_uint}},
  {eop_extract, SB, {s_id, s_uint}},
  {eop_deref, SB, {}},
  {eop_addr, SB, {}},
  {eop_addr_offset, SB, {s_id, s_int}},
  {eop_dup, SB, {}},
  {eop_drop, SB, {}},
  {eop_const_int, SB, {s_id, s_int}},
  {eop_const_float, SB, {s_id, s_flt}},
  {eop_const_null, SB, {s_id}},
  {eop_const_ptr, SB, {s_id, s_int}},
  {eop_materialize_temp, SB, {s_id, s_id}},
  {eop_add, SB, {s_flags(7)}},
  {eop_sub, SB, {s_flags(7)}},
  {eop_mul, SB, {s_flags(7)}},
  {eop_div, SB, {s_flags(7)}},
  {eop_rem, SB, {s_flags(7)}},
  {eop_shl, SB, {s_flags(7)}},
  {eop_shr, SB, {s_flags(7)}},
  {eop_bit_and, SB, {s_flags(7)}},
  {eop_bit_or, SB, {s_flags(7)}},
  {eop_bit_xor, SB, {s_flags(7)}},
  {eop_cmp, SB, {s_enum(0, ecmp_uno)}},
  {eop_neg, SB, {s_flags(7)}},
  {eop_bit_not, SB, {}},
  {eop_log_not, SB, {}},
  {eop_to_bool, SB, {}},
  {eop_ptr_stride, SB, {s_flags(3)}},
  {eop_ptr_diff, SB, {s_id}},
  {eop_inc_dec, SB, {s_enum(0, eidk_post_dec), s_flags(3)}},
  {eop_base_cast, SB | V1, {s_id, s_id, s_int, s_flags(1)}},
  {eop_derived_cast, SB | V1, {s_id, s_id, s_int, s_flags(1)}},
  {eop_return_slot, SB | V1, {}},
  {eop_array_ctor_begin, SB | V1, {s_id, s_flags(1)}},
  {eop_array_ctor_end, SB | V1, {}},
  {eop_array_dtor_begin, SB | V1, {s_id, s_flags(1)}},
  {eop_array_dtor_end, SB | V1, {}},
  {eop_new_array_size, SB | V1, {s_id, s_uint}},
  {eop_new_array_cookie, SB | V1, {s_id, s_uint}},
  {eop_new_array_size_throw, SB | V1, {s_id, s_uint}},
  {eop_delete, SB | V1, {s_id, s_idn, s_id, s_flags(0x3F)}},
  {eop_zero_bytes, SB | V1, {s_uint}},
  {eop_cmp3way, SB | V1, {s_id, s_int, s_int, s_int, s_intn}},
  {eop_cast, SB, {s_enum(0, eck_array_to_ptrdecay), s_id}},
  {eop_va_start, SB, {}},
  {eop_va_arg, SB, {s_id}},
  {eop_va_end, SB, {}},
  {eop_va_copy, SB, {}},
  {eop_builtin, SB, {s_enum(ebi_expect, ebi_signbit), s_uint, s_idn}},
  {eop_call, SB, {s_id, s_id, s_uint, s_flags(0x7F)}},
  {eop_call_indirect, SB, {s_id, s_uint, s_flags(0xF)}},
  {eop_tern_begin, SB, {s_idn}},
  {eop_tern_else, SB, {}},
  {eop_tern_end, SB, {}},
  {eop_land_begin, SB, {}},
  {eop_land_end, SB, {}},
  {eop_lor_begin, SB, {}},
  {eop_lor_end, SB, {}},
  {eop_c_int, SC, {s_id, s_int}},
  {eop_c_float, SC, {s_id, s_flt}},
  {eop_c_null, SC, {s_id}},
  {eop_c_zero, SC, {s_id}},
  {eop_c_string, SC, {s_id, s_bin}},
  {eop_c_addr, SC, {s_id, s_id, s_int}},
  {eop_c_ptr, SC, {s_id, s_int}},
  {eop_c_agg_begin, SC, {s_id}},
  {eop_c_sel, SC, {s_uint}},
  {eop_c_repeat, SC, {s_uint, s_uint}},
  {eop_c_agg_end, SC, {}},
  {eop_ty_vptr, ST, {s_id}},
  {eop_class_info, SD, {s_id, s_uint, s_uint, s_uintn, s_uint}},
  {eop_class_bases, SD, {s_id, s_arr}},
  {eop_class_vfuncs, SD, {s_id, s_arr}},
  {eop_class_overrides, SD, {s_id, s_arr}},
  {eop_ty_data_member, ST, {s_id, s_id, s_id}},
  {eop_ty_method, ST, {s_id, s_id, s_id}},
  {eop_class_mangle, SD, {s_id, s_id}},
  {eop_structor_deleting, SD, {s_id, s_id, s_id, s_flags(7)}},
  {eop_decl_abi_data, SD, {s_id, s_enum(eadk_vtable, eadk_typeinfo), s_id,
                           s_enum(0, elk_common), s_flags(7), s_idn}},
  {eop_typeinfo_def, SD, {s_id, s_enum(etik_fundamental, etik_enum),
                          s_flags(0x7F), s_idn, s_idn}},
  {eop_vptr_init, SS, {}},
  {eop_func_eh_spec, SS, {s_enum(eesk_noexcept, eesk_dynamic), s_arr}},
  {eop_vbase_cast, SB, {s_id, s_id, s_flags(3)}},
  {eop_vtt_addr, SB, {s_enum(evak_self, evak_vbase), s_uintn}},
  {eop_dynamic_cast, SB, {s_id, s_idn, s_id, s_flags(1)}},
  {eop_typeid, SB, {s_idn, s_id, s_flags(1)}},
  {eop_pm_field, SB, {s_id}},
  {eop_pm_call, SB, {s_id, s_uint, s_flags(0xF)}},
  {eop_pm_cast, SB, {s_enum(epck_to_derived, epck_to_base), s_id, s_id,
                     s_uint}},
  {eop_const_data_member, SB, {s_id, s_uintn}},
  {eop_const_method, SB, {s_id, s_idn, s_uint, s_flags(1)}},
  {eop_array_partial_dtor, SB, {s_id}},
  {eop_init_zero_base, SB, {}},
  {eop_try_begin, SS, {s_flags(1)}},
  {eop_catch_begin, SS, {s_idn}},
  {eop_catch_param, SS, {s_id, s_enum(ecat_reference,
                                      ecat_reference_to_record_pointer),
                         s_idn}},
  {eop_try_end, SS, {}},
  {eop_throw_begin, SB, {s_id}},
  {eop_throw_end, SB, {s_id, s_idn}},
  {eop_rethrow, SB, {}},
  {eop_c_data_member, SC, {s_id, s_uintn}},
  {eop_c_method, SC, {s_id, s_idn, s_uint, s_flags(1)}}
};

#undef SM
#undef SH
#undef ST
#undef SD
#undef SB
#undef SC
#undef SS
#undef V1

struct a_schema {
  an_opcode_info info[256];
  bool           present[256];
  std::string    names[256];
  std::string    problem;

  a_schema();
};  /* a_schema */

a_schema::a_schema()
/*
Build the table: names and arities from ECIL_OPCODES, operand rules from
spec_rows, then cross-check the two.
*/
{
  bool has_row[256];

  for (int i = 0; i < 256; ++i) {
    info[i].name = nullptr;
    info[i].value = 0;
    info[i].arity = 0;
    info[i].section = 0;
    info[i].min_minor = 0;
    present[i] = false;
    has_row[i] = false;
    for (int j = 0; j < ECIL_MAX_ARITY; ++j) {
      info[i].operands[j].cls = an_operand_class::none;
      info[i].operands[j].lo = 0;
      info[i].operands[j].mask = 0;
    }
  }
#define ECIL_FILL_BASE(name, opvalue, oparity)                               \
  names[opvalue] = #name;                                                    \
  info[opvalue].value = opvalue;                                             \
  info[opvalue].arity = oparity;                                             \
  present[opvalue] = true;
  ECIL_OPCODES(ECIL_FILL_BASE)
#undef ECIL_FILL_BASE
  for (int i = 0; i < 256; ++i) {
    if (present[i]) {
      for (size_t k = 0; k < names[i].size(); ++k)
        names[i][k] = static_cast<char>(std::toupper(
                                  static_cast<unsigned char>(names[i][k])));
      info[i].name = names[i].c_str();
    }
  }
  /* The records of ECIL 1.2 (CLEANUP_END existed but was rejected). */
  static const uint8_t minor2[] = {
    eop_ty_vptr, eop_class_info, eop_class_bases, eop_class_vfuncs,
    eop_class_overrides, eop_ty_data_member, eop_ty_method, eop_class_mangle,
    eop_structor_deleting, eop_decl_abi_data, eop_typeinfo_def,
    eop_cleanup_end, eop_vptr_init, eop_func_eh_spec, eop_vbase_cast,
    eop_vtt_addr, eop_dynamic_cast, eop_typeid, eop_pm_field, eop_pm_call,
    eop_pm_cast, eop_const_data_member, eop_const_method,
    eop_array_partial_dtor, eop_init_zero_base, eop_try_begin, eop_catch_begin, eop_catch_param,
    eop_try_end, eop_throw_begin, eop_throw_end, eop_rethrow,
    eop_c_data_member, eop_c_method, eop_new_array_size_throw
  };
  for (uint8_t op : minor2) {
    if (present[op])
      info[op].min_minor = 2;
  }
  for (const a_spec_row &row : spec_rows) {
    if (!present[row.opcode]) {
      problem = "schema row for an unassigned opcode";
    } else {
      has_row[row.opcode] = true;
      info[row.opcode].section = row.section;
      for (int j = 0; j < ECIL_MAX_ARITY; ++j)
        info[row.opcode].operands[j] = row.operands[j];
    }
  }
  for (int i = 0; i < 256 && problem.empty(); ++i) {
    if (present[i]) {
      if (!has_row[i]) {
        problem = "no schema row for opcode " + names[i];
      } else {
        for (int j = 0; j < ECIL_MAX_ARITY && problem.empty(); ++j) {
          bool used = info[i].operands[j].cls != an_operand_class::none;
          if (used != (j < info[i].arity))
            problem = "schema operand count differs from the arity of " +
                      names[i];
        }
      }
    }
  }
}  /* a_schema::a_schema */

const a_schema &schema()
/*
Return the lazily built schema (thread-safe since C++11).
*/
{
  static const a_schema instance;
  return instance;
}  /* schema */

struct a_frame_name {
  uint8_t     kind;
  const char *name;
};  /* a_frame_name */

const a_frame_name frame_names[] = {
  {efk_header, "HEADER"}, {efk_decls, "DECLS"}, {efk_region, "REGION"},
  {efk_globals, "GLOBALS"}, {efk_names, "NAMES"}, {efk_abort, "ABORT"},
  {efk_end, "END"}
};

}  /* anonymous namespace */

const an_opcode_info *find_opcode_info(uint8_t opcode)
/*
Return the schema entry for opcode, or nullptr if it is unassigned.
*/
{
  const a_schema &sc = schema();
  const an_opcode_info *result = nullptr;

  if (sc.present[opcode])
    result = &sc.info[opcode];
  return result;
}  /* find_opcode_info */

const an_opcode_info *find_opcode_by_name(const std::string &name)
/*
Case-insensitive lookup by spelling, or nullptr.
*/
{
  const a_schema &sc = schema();
  const an_opcode_info *result = nullptr;
  std::string upper(name);

  for (size_t k = 0; k < upper.size(); ++k)
    upper[k] = static_cast<char>(std::toupper(
                                  static_cast<unsigned char>(upper[k])));
  for (int i = 0; i < 256 && result == nullptr; ++i) {
    if (sc.present[i] && sc.names[i] == upper)
      result = &sc.info[i];
  }
  return result;
}  /* find_opcode_by_name */

const char *frame_kind_name(uint8_t kind)
/*
"HEADER", "DECLS", ... or nullptr for an extension or unknown kind.
*/
{
  const char *result = nullptr;

  for (const a_frame_name &entry : frame_names) {
    if (entry.kind == kind)
      result = entry.name;
  }
  return result;
}  /* frame_kind_name */

bool schema_is_consistent(std::string &problem)
/*
Self-check: every opcode has an operand row whose count equals its arity.
*/
{
  const a_schema &sc = schema();

  problem = sc.problem;
  return sc.problem.empty();
}  /* schema_is_consistent */

}  /* namespace ecil */
