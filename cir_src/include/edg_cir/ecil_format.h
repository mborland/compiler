/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
ecil_format.h -- the ECIL (EDG CIR IL) stream format contract.

This header is shared by the front-end writer (src/ecil_write.c) and the
CIR library (cir_src/).  It must stay dependency-free: it includes only
<stdint.h> and <stddef.h>, contains no EDG front-end or LLVM names, and
compiles as C99, C++14 and C++17.  It is owned by the orchestrator; see
cir_src/doc/ecil_format.md for the full specification.  Any change bumps
ECIL_VERSION_MAJOR or ECIL_VERSION_MINOR (section 9 of the spec).
*/

#ifndef ECIL_FORMAT_H
#define ECIL_FORMAT_H 1

#include <stddef.h>
#include <stdint.h>

#define ECIL_VERSION_MAJOR 1
#define ECIL_VERSION_MINOR 3

/*
The stream preamble: 8 magic bytes, then u16 major, u16 minor and
u32 features, all little-endian (16 bytes in total).
*/
#define ECIL_MAGIC_0 0x45  /* 'E' */
#define ECIL_MAGIC_1 0x43  /* 'C' */
#define ECIL_MAGIC_2 0x49  /* 'I' */
#define ECIL_MAGIC_3 0x4C  /* 'L' */
#define ECIL_MAGIC_4 0x0D
#define ECIL_MAGIC_5 0x0A
#define ECIL_MAGIC_6 0x1A
#define ECIL_MAGIC_7 0x0A
#define ECIL_PREAMBLE_SIZE 16

/*
Feature bits (preamble u32).  Bits 0-15 are ignorable: a reader that does
not know one proceeds.  Bits 16-31 are required: a reader that does not
know one rejects the stream.
*/
#define ECIL_FEATURE_REQUIRED_MASK 0xFFFF0000u
#define ECIL_FEATURE_LOCATIONS  0x00000001u
                        /* LOC records may appear. */
#define ECIL_FEATURE_SUPP       0x00000002u
                        /* Some frames carry supplementary blobs. */
#define ECIL_FEATURE_REFS       0x00000004u
                        /* F_REGION frames end with a REFS record. */
#define ECIL_FEATURE_LATE_NAMES 0x00000008u
                        /* NAME_BIND records may appear. */

/*
Frame header: u32 payload_len, u8 kind, u8 flags, u16 reserved (0).  The
payload follows immediately and holds whole records only.
*/
#define ECIL_FRAME_HEADER_SIZE 8

typedef enum an_ecil_frame_kind {
  efk_header    = 0x01,  /* HDR_* records; must be the first frame. */
  efk_decls     = 0x02,  /* Introductions not tied to a body. */
  efk_region    = 0x03,  /* One function memory region. */
  efk_globals   = 0x04,  /* File-scope definitions and initializers. */
  efk_names     = 0x05,  /* Late NAME_BIND records. */
  efk_abort     = 0x0E,  /* Front-end error: discard everything. */
  efk_end       = 0x0F,  /* END record; must be the last frame. */
  efk_first_ext = 0x80   /* 0x80-0xFF: extension frames, skippable. */
} an_ecil_frame_kind;

#define ECIL_FRAME_FLAG_HAS_SUPP 0x01u
                        /* Records in this frame may carry a supp blob. */

/*
Operand tags.  Multi-byte payloads are little-endian.  Arrays are flat:
their elements are scalars only (never arrays, bins or supp blobs).
*/
#define ECIL_TAG_FIXUINT_MAX  0x7F  /* 0x00-0x7F: value 0-127. */
#define ECIL_TAG_FIXBIN       0x80  /* 0x80-0x9F: 0-31 bytes follow. */
#define ECIL_TAG_FIXARRAY     0xA0  /* 0xA0-0xBF: 0-31 elements follow. */
#define ECIL_TAG_NIL          0xC0
#define ECIL_TAG_POISON       0xC1  /* Never valid. */
#define ECIL_TAG_FALSE        0xC2
#define ECIL_TAG_TRUE         0xC3
#define ECIL_TAG_BIN8         0xC4
#define ECIL_TAG_BIN16        0xC5
#define ECIL_TAG_BIN32        0xC6
#define ECIL_TAG_SUPP8        0xC7
#define ECIL_TAG_SUPP16       0xC8
#define ECIL_TAG_SUPP32       0xC9
#define ECIL_TAG_F32          0xCA
#define ECIL_TAG_F64          0xCB
#define ECIL_TAG_U8           0xCC
#define ECIL_TAG_U16          0xCD
#define ECIL_TAG_U32          0xCE
#define ECIL_TAG_U64          0xCF
#define ECIL_TAG_I8           0xD0
#define ECIL_TAG_I16          0xD1
#define ECIL_TAG_I32          0xD2
#define ECIL_TAG_I64          0xD3
#define ECIL_TAG_U128         0xD4
#define ECIL_TAG_I128         0xD5
#define ECIL_TAG_FBITS        0xD6  /* u8 n, then n bytes of float bits. */
#define ECIL_TAG_ARR16        0xDC
#define ECIL_TAG_ARR32        0xDD
#define ECIL_TAG_NEGFIXINT    0xE0  /* 0xE0-0xFF: -32 to -1. */

/*
Opcodes.  Each entry is X(name, value, arity), where arity is the fixed
number of operands that follow the opcode byte.  A record may be followed
by one supp blob (tags C7-C9) only in a frame with
ECIL_FRAME_FLAG_HAS_SUPP set.  Opcode values 0x00, 0xC0, 0xC1 and C7-C9
are never assigned, so a desync landing on nil or poison is detected and a
supp blob is unambiguous.  Stack effects are documented in the spec.
*/
#define ECIL_OPCODES(X)                                                      \
  /* Meta and header. */                                                     \
  X(str,              0x01, 2)  /* sid, bin */                               \
  X(loc,              0x02, 3)  /* sid|nil file, line, col */                \
  X(name_bind,        0x03, 3)  /* g, sid, linkage */                        \
  X(retract,          0x04, 1)  /* g */                                      \
  X(ext,              0x05, 2)  /* kind, bin */                              \
  X(refs,             0x06, 1)  /* array g */                                \
  X(end,              0x07, 2)  /* n_frames, status */                       \
  X(hdr_target,       0x08, 6)  /* sid triple, sid|nil abi, sid|nil dl,   */ \
                                /* ptr_bits, reg_bits, flags              */ \
  X(hdr_lang,         0x09, 3)  /* lang, std, flags */                       \
  X(hdr_producer,     0x0A, 2)  /* sid producer, sid|nil source */           \
  X(hdr_type_role,    0x0B, 2)  /* role, t */                                \
  /* Types. */                                                               \
  X(ty_void,          0x10, 1)  /* g */                                      \
  X(ty_bool,          0x11, 3)  /* g, size, align */                         \
  X(ty_int,           0x12, 5)  /* g, bits, signed, size, align */           \
  X(ty_float,         0x13, 4)  /* g, fmt, size, align */                    \
  X(ty_ptr,           0x14, 2)  /* g, t pointee */                           \
  X(ty_array,         0x15, 4)  /* g, t elem, count|nil, flags */            \
  X(ty_func,          0x16, 4)  /* g, t ret, flags, array t params */        \
  X(ty_record_decl,   0x17, 3)  /* g, kind, sid|nil */                       \
  X(ty_record_body,   0x18, 6)  /* g, size, data_size, align, flags, n */    \
  X(rec_field,        0x19, 6)  /* t, byte_off, bit_off|nil, bit_width|nil,*/\
                                /* sid|nil, flags                         */ \
  X(ty_opaque,        0x1A, 3)  /* g, size, align */                         \
  X(ty_vptr,          0x1B, 1)  /* g (1.2) */                                \
  X(class_info,       0x1E, 5)  /* t, nvsize, nvalign, primary_idx|nil,   */ \
                                /* nslots (1.2)                           */ \
  X(class_bases,      0x1F, 2)  /* t, array (field_idx, flags) (1.2) */      \
  X(class_vfuncs,     0x20, 2)  /* t, array (g, slot, flags, sig) (1.2) */   \
  X(class_overrides,  0x21, 2)  /* t, array (g_fn, sub_off, g_ovr,        */ \
                                /* ovr_off) (1.2)                         */ \
  X(ty_data_member,   0x23, 3)  /* g, t member, t class (1.2) */             \
  X(ty_method,        0x24, 3)  /* g, t func, t class (1.2) */               \
  X(class_mangle,     0x25, 2)  /* t, sid (1.2) */                           \
  /* Declarations. */                                                        \
  X(decl_func,        0x30, 5)  /* g, t, linkage, flags, sid|nil */          \
  X(decl_var,         0x31, 6)  /* g, t, linkage, flags, align, sid|nil */   \
  X(var_init,         0x32, 1)  /* g; pops one constant */                   \
  X(structor,         0x33, 3)  /* g complete, g base, flags (v1) */         \
  X(structor_deleting,0x34, 4)  /* g d0, g d1, g delete, flags (1.2) */      \
  X(decl_abi_data,    0x35, 6)  /* g, kind, t, linkage, flags, sid|nil    */ \
                                /* (1.2)                                  */ \
  X(typeinfo_def,     0x36, 5)  /* g ti, kind, flags, g a|nil, g b|nil    */ \
                                /* (1.2)                                  */ \
  /* Bodies and statements. */                                               \
  X(func_begin,       0x40, 2)  /* g, flags */                               \
  X(func_end,         0x41, 1)  /* flags */                                  \
  X(param,            0x42, 5)  /* l, t abi, t decl, sid|nil, flags */       \
  X(local,            0x43, 5)  /* l, t, sid|nil, align, flags */            \
  X(block_begin,      0x44, 0)                                               \
  X(block_end,        0x45, 0)                                               \
  X(return_from_slot, 0x46, 0)  /* v1 */                                     \
  X(if_begin,         0x47, 1)  /* has_else; pops bool */                    \
  X(if_else,          0x48, 0)                                               \
  X(if_end,           0x49, 0)                                               \
  X(while_begin,      0x4A, 0)                                               \
  X(while_body,       0x4B, 0)  /* pops bool */                              \
  X(while_end,        0x4C, 0)                                               \
  X(do_begin,         0x4D, 0)                                               \
  X(do_cond,          0x4E, 0)                                               \
  X(do_end,           0x4F, 0)  /* pops bool */                              \
  X(for_begin,        0x50, 0)  /* opens the init scope, to FOR_END */       \
  X(for_cond,         0x51, 1)  /* has_cond */                               \
  X(for_step,         0x52, 1)  /* has_step; pops bool if has_cond */        \
  X(for_body,         0x53, 0)                                               \
  X(for_end,          0x54, 0)                                               \
  X(switch_begin,     0x55, 0)  /* pops int */                               \
  X(case_value,       0x56, 1)  /* int */                                    \
  X(case_range,       0x57, 2)  /* lo, hi */                                 \
  X(case_default,     0x58, 0)                                               \
  X(switch_end,       0x59, 0)                                               \
  X(break_stmt,       0x5A, 0)                                               \
  X(continue_stmt,    0x5B, 0)                                               \
  X(return_stmt,      0x5C, 1)  /* has_value */                              \
  X(goto_stmt,        0x5D, 2)  /* l label, n_cleanups */                    \
  X(label,            0x5E, 2)  /* l label, sid|nil */                       \
  X(unreachable,      0x5F, 0)                                               \
  /* Object initialization. */                                               \
  X(init_const,       0x60, 0)  /* glv; pops one constant */                 \
  X(init_zero,        0x61, 0)  /* glv */                                    \
  X(agg_init_begin,   0x62, 2)  /* t, flags; glv */                          \
  X(agg_sel,          0x63, 1)  /* idx; -> glv of sub-object */              \
  X(agg_init_end,     0x64, 0)                                               \
  X(cleanup_begin,    0x65, 3)  /* kind, g|nil, flags (v1) */                \
  X(cleanup_body,     0x66, 0)  /* v1 */                                     \
  X(cleanup_end,      0x67, 0)  /* 1.2: deactivates a cleanup */           \
  X(fullexpr_begin,   0x68, 0)  /* v1 */                                     \
  X(fullexpr_end,     0x69, 0)  /* carries <= 1 value (v1) */                \
  X(static_init_begin,0x6A, 2)  /* g, flags (v1) */                          \
  X(static_init_end,  0x6B, 0)  /* v1 */                                     \
  X(meminit_begin,    0x6C, 3)  /* kind, idx, flags (v1) */                  \
  X(meminit_end,      0x6D, 0)  /* v1 */                                     \
  X(vptr_init,        0x6E, 0)  /* 1.2 */                                    \
  X(func_eh_spec,     0x6F, 2)  /* kind, array g ti; header (1.2) */         \
  /* References, memory and values. */                                       \
  X(lref,             0x70, 1)  /* l -> glv */                               \
  X(gref,             0x71, 1)  /* g -> glv */                               \
  X(load,             0x72, 1)  /* flags; glv -> prv */                      \
  X(store,            0x73, 2)  /* flags, result; glv prv -> result */       \
  X(field,            0x74, 2)  /* t rec, idx; glv -> glv */                 \
  X(extract,          0x75, 2)  /* t rec, idx; prv -> prv */                 \
  X(deref,            0x76, 0)  /* prv ptr -> glv */                         \
  X(addr,             0x77, 0)  /* glv -> prv ptr */                         \
  X(addr_offset,      0x78, 2)  /* t ptr, bytes; prv ptr -> prv ptr */       \
  X(dup,              0x79, 0)                                               \
  X(drop,             0x7A, 0)                                               \
  X(const_int,        0x7B, 2)  /* t, int -> prv */                          \
  X(const_float,      0x7C, 2)  /* t, float -> prv */                        \
  X(const_null,       0x7D, 1)  /* t -> prv */                               \
  X(const_ptr,        0x7E, 2)  /* t, int -> prv */                          \
  X(materialize_temp, 0x7F, 2)  /* l, t; prv -> glv */                       \
  /* Operators (operands already converted to a common type). */             \
  X(add,              0x80, 1)  /* flags */                                  \
  X(sub,              0x81, 1)  /* flags */                                  \
  X(mul,              0x82, 1)  /* flags */                                  \
  X(div,              0x83, 1)  /* flags */                                  \
  X(rem,              0x84, 1)  /* flags */                                  \
  X(shl,              0x85, 1)  /* flags */                                  \
  X(shr,              0x86, 1)  /* flags */                                  \
  X(bit_and,          0x87, 1)  /* flags */                                  \
  X(bit_or,           0x88, 1)  /* flags */                                  \
  X(bit_xor,          0x89, 1)  /* flags */                                  \
  X(cmp,              0x8A, 1)  /* kind; prv prv -> bool */                  \
  X(neg,              0x8B, 1)  /* flags */                                  \
  X(bit_not,          0x8C, 0)                                               \
  X(log_not,          0x8D, 0)  /* bool -> bool */                           \
  X(to_bool,          0x8E, 0)  /* scalar -> bool */                         \
  X(ptr_stride,       0x8F, 1)  /* flags; prv ptr prv int -> prv ptr */      \
  X(ptr_diff,         0x90, 1)  /* t result; prv ptr prv ptr -> prv */       \
  X(inc_dec,          0x91, 2)  /* kind, flags; glv -> prv */                \
  X(base_cast,        0x92, 4)  /* t der, t base, offset, flags (v1) */      \
  X(derived_cast,     0x93, 4)  /* t base, t der, offset, flags (v1) */      \
  X(return_slot,      0x94, 0)  /* -> glv (v1) */                            \
  X(array_ctor_begin, 0x95, 2)  /* t elem, flags (v1) */                     \
  X(array_ctor_end,   0x96, 0)  /* v1 */                                     \
  X(array_dtor_begin, 0x97, 2)  /* t elem, flags (v1) */                     \
  X(array_dtor_end,   0x98, 0)  /* v1 */                                     \
  X(new_array_size,   0x99, 2)  /* t elem, cookie (v1) */                    \
  X(new_array_cookie, 0x9A, 2)  /* t elem, cookie (v1) */                    \
  X(delete,           0x9B, 4)  /* g fn, g dtor|nil, t, flags (v1) */        \
  X(zero_bytes,       0x9C, 1)  /* bytes; glv (v1) */                        \
  X(cmp3way,          0x9D, 5)  /* t result, lt, eq, gt, unordered|nil;  */  \
                                /* prv prv -> prv (1.1)                */    \
  X(vbase_cast,       0x9E, 3)  /* t der, t vbase, flags; prv -> prv (1.2) */\
  X(vtt_addr,         0x9F, 2)  /* kind, idx|nil; -> prv (1.2) */            \
  X(dynamic_cast,     0xA0, 4)  /* g src ti, g dst ti|nil, t result,      */ \
                                /* flags; prv -> prv (1.2)                */ \
  X(typeid,           0xA1, 3)  /* g ti|nil, t result, flags;             */ \
                                /* [glv] -> glv (1.2)                     */ \
  X(pm_field,         0xA2, 1)  /* t dm; prv obj prv dm -> glv (1.2) */      \
  X(pm_call,          0xA3, 3)  /* t call, nargs, flags; prv obj,         */ \
                                /* prv method, args -> result (1.2)       */ \
  X(pm_cast,          0xA4, 4)  /* kind, t from, t to, offset;            */ \
                                /* prv -> prv (1.2)                       */ \
  X(const_data_member,0xA5, 2)  /* t, offset|nil -> prv (1.2) */             \
  X(const_method,     0xA6, 4)  /* t, g|nil, adj, flags -> prv (1.2) */      \
  X(array_partial_dtor,0xA7, 1) /* g dtor (1.2) */                           \
  X(init_zero_base,   0xA8, 0)  /* glv; nvsize bytes (1.2) */               \
  X(new_array_size_throw,0xA9, 2) /* t elem, cookie; count -> size_t;  */ \
                                /* bad_array_new_length (1.2)             */ \
  /* Calls, casts and varargs (C0, C1 and C7-C9 are never assigned). */     \
  X(cast,             0xC2, 2)  /* kind, t; prv -> prv */                    \
  X(va_start,         0xC3, 0)  /* prv ap */                                 \
  X(va_arg,           0xC4, 1)  /* t; prv ap -> prv */                       \
  X(va_end,           0xC5, 0)  /* prv ap */                                 \
  X(va_copy,          0xC6, 0)  /* prv dst prv src */                        \
  X(builtin,          0xCA, 3)  /* id, nargs, t|nil */                       \
  X(call,             0xCB, 4)  /* g, t call, nargs, flags */                \
  X(call_indirect,    0xCC, 3)  /* t call, nargs, flags; callee prv ptr */   \
  /* Bracketed expressions. */                                               \
  X(tern_begin,       0xD0, 1)  /* t|nil; pops bool */                       \
  X(tern_else,        0xD1, 0)                                               \
  X(tern_end,         0xD2, 0)                                               \
  X(land_begin,       0xD3, 0)  /* pops bool */                              \
  X(land_end,         0xD4, 0)  /* -> bool */                                \
  X(lor_begin,        0xD5, 0)  /* pops bool */                              \
  X(lor_end,          0xD6, 0)  /* -> bool */                                \
  /* Exceptions (1.2; 0xDA is unassigned). */                                \
  X(try_begin,        0xD7, 1)  /* flags */                                  \
  X(catch_begin,      0xD8, 1)  /* g ti|nil */                               \
  X(catch_param,      0xD9, 3)  /* l, kind, g copy|nil */                    \
  X(try_end,          0xDB, 0)                                               \
  X(throw_begin,      0xDC, 1)  /* t; -> glv */                              \
  X(throw_end,        0xDD, 2)  /* g ti, g dtor|nil; pops nothing */         \
  X(rethrow,          0xDE, 0)                                               \
  /* Constant initializers (constant stack). */                              \
  X(c_int,            0xE0, 2)  /* t, int */                                 \
  X(c_float,          0xE1, 2)  /* t, float */                               \
  X(c_null,           0xE2, 1)  /* t */                                      \
  X(c_zero,           0xE3, 1)  /* t */                                      \
  X(c_string,         0xE4, 2)  /* t, bin */                                 \
  X(c_addr,           0xE5, 3)  /* t, g, bytes */                            \
  X(c_ptr,            0xE6, 2)  /* t, int */                                 \
  X(c_agg_begin,      0xE7, 1)  /* t */                                      \
  X(c_sel,            0xE8, 1)  /* idx */                                    \
  X(c_repeat,         0xE9, 2)  /* idx, count */                             \
  X(c_agg_end,        0xEA, 0)                                               \
  X(c_data_member,    0xEB, 2)  /* t, offset|nil (1.2) */                    \
  X(c_method,         0xEC, 4)  /* t, g|nil, adj, flags (1.2) */

#define ECIL_DEFINE_OPCODE_ENUMERATOR(name, value, arity) \
  eop_##name = value,

typedef enum an_ecil_opcode {
  ECIL_OPCODES(ECIL_DEFINE_OPCODE_ENUMERATOR)
  eop_invalid = 0x00
} an_ecil_opcode;

#undef ECIL_DEFINE_OPCODE_ENUMERATOR

/* Values of operands that are enumerations. */

typedef enum an_ecil_linkage {
  elk_external             = 0,
  elk_internal             = 1,
  elk_private              = 2,  /* Unnamed constants (string literals). */
  elk_linkonce_odr         = 3,
  elk_weak_odr             = 4,
  elk_weak                 = 5,
  elk_available_externally = 6,
  elk_extern_weak          = 7,
  elk_common               = 8
} an_ecil_linkage;

typedef enum an_ecil_record_kind {
  erk_struct = 0,
  erk_union  = 1,
  erk_class  = 2
} an_ecil_record_kind;

typedef enum an_ecil_float_format {
  eff_ieee_half   = 0,
  eff_bfloat16    = 1,
  eff_ieee_single = 2,
  eff_ieee_double = 3,
  eff_x87_80      = 4,
  eff_ieee_quad   = 5,
  eff_ibm_double  = 6
} an_ecil_float_format;

typedef enum an_ecil_type_role {
  etr_size_t    = 1,
  etr_ptrdiff_t = 2,
  etr_int       = 3,
  etr_va_list   = 4,
  etr_wchar_t   = 5,
  etr_char      = 6
} an_ecil_type_role;

typedef enum an_ecil_language {
  elang_c   = 0,
  elang_cxx = 1
} an_ecil_language;

typedef enum an_ecil_cmp_kind {
  ecmp_lt = 0,
  ecmp_le = 1,
  ecmp_gt = 2,
  ecmp_ge = 3,
  ecmp_eq = 4,
  ecmp_ne = 5,
  ecmp_one = 6,  /* Floating ordered-and-unequal (islessgreater). */
  ecmp_uno = 7   /* Floating unordered (isunordered). */
} an_ecil_cmp_kind;

/* Mirrors the CIR cast kinds the v0 writer can produce. */
typedef enum an_ecil_cast_kind {
  eck_integral         = 0,
  eck_int_to_bool      = 1,
  eck_float_to_bool    = 2,
  eck_ptr_to_bool      = 3,
  eck_bool_to_int      = 4,
  eck_bool_to_float    = 5,
  eck_int_to_float     = 6,
  eck_float_to_int     = 7,
  eck_floating         = 8,
  eck_int_to_ptr       = 9,
  eck_ptr_to_int       = 10,
  eck_bitcast          = 11,
  eck_array_to_ptrdecay = 12
} an_ecil_cast_kind;

/* CLEANUP_BEGIN kinds (v1).  1-3 equal cir::CleanupKind. */
typedef enum an_ecil_cleanup_kind {
  ecl_normal = 1,  /* Normal exits only. */
  ecl_eh     = 2,  /* Exception exits only (dropped while exceptions=0). */
  ecl_all    = 3,  /* Both. */
  ecl_exit   = 4   /* Program exit of static-duration object g. */
} an_ecil_cleanup_kind;

/* MEMINIT_BEGIN kinds (v1), in EDG's constructor-initializer order. */
typedef enum an_ecil_meminit_kind {
  emk_vbase      = 0,  /* Virtual base (1.2; C1 of a has_vbases pair). */
  emk_base       = 1,
  emk_field      = 2,
  emk_delegation = 3
} an_ecil_meminit_kind;

typedef enum an_ecil_inc_dec_kind {
  eidk_pre_inc  = 0,
  eidk_pre_dec  = 1,
  eidk_post_inc = 2,
  eidk_post_dec = 3
} an_ecil_inc_dec_kind;

typedef enum an_ecil_store_result {
  esr_none = 0,  /* Nothing is pushed. */
  esr_glv  = 1,  /* The target glvalue is pushed (C++). */
  esr_prv  = 2   /* The stored value is pushed (C). */
} an_ecil_store_result;

/*
Builtins.  Operands are prvalues already converted to the builtin's
parameter types; the result type is the BUILTIN record's t.
*/
typedef enum an_ecil_builtin {
  ebi_expect       = 1,   /* (v, expected) -> v */
  ebi_unreachable  = 2,   /* () */
  ebi_trap         = 3,   /* () */
  ebi_alloca       = 4,   /* (size) -> void*; function-lifetime storage */
  ebi_bswap        = 5,   /* (x) -> x's type */
  ebi_clz          = 6,   /* (x) -> int; undefined for 0 */
  ebi_ctz          = 7,   /* (x) -> int; undefined for 0 */
  ebi_popcount     = 8,   /* (x) -> int */
  ebi_parity       = 9,   /* (x) -> int */
  ebi_ffs          = 10,  /* (x) -> int */
  ebi_clrsb        = 11,  /* (x) -> int */
  ebi_add_overflow = 12,  /* (a, b, T *res) -> bool; *res = wrapped a+b */
  ebi_sub_overflow = 13,  /* (a, b, T *res) -> bool */
  ebi_mul_overflow = 14,  /* (a, b, T *res) -> bool */
  ebi_is_fp_class  = 15,  /* (x, mask) -> bool; mask is a constant int of
                             ECIL_FP_CLASS_* bits (LLVM FPClassTest) */
  ebi_signbit      = 16   /* (x) -> bool */
} an_ecil_builtin;

/* DECL_ABI_DATA kinds (1.2). */
typedef enum an_ecil_abi_data_kind {
  eadk_vtable   = 0,
  eadk_vtt      = 1,
  eadk_typeinfo = 2
} an_ecil_abi_data_kind;

/* TYPEINFO_DEF kinds (1.2): the non-class std::type_info classes. */
typedef enum an_ecil_typeinfo_kind {
  etik_fundamental    = 0,  /* External declaration only. */
  etik_pointer        = 1,  /* g_a: pointee typeinfo. */
  etik_member_pointer = 2,  /* g_a: pointee typeinfo; g_b: class one. */
  etik_function       = 3,
  etik_array          = 4,
  etik_enum           = 5
} an_ecil_typeinfo_kind;

/* FUNC_EH_SPEC kinds (1.2). */
typedef enum an_ecil_eh_spec_kind {
  eesk_noexcept = 1,  /* Terminate if the body throws. */
  eesk_dynamic  = 2   /* throw(T...): filter, then unexpected. */
} an_ecil_eh_spec_kind;

/* VTT_ADDR kinds (1.2). */
typedef enum an_ecil_vtt_addr_kind {
  evak_self  = 0,  /* The VTT of the function's own class. */
  evak_base  = 1,  /* idx: a BASE field. */
  evak_vbase = 2   /* idx: a VIRTUAL_BASE field. */
} an_ecil_vtt_addr_kind;

/* PM_CAST kinds (1.2). */
typedef enum an_ecil_pm_cast_kind {
  epck_to_derived = 0,  /* Base-to-derived member pointer: adds offset. */
  epck_to_base    = 1   /* Derived-to-base member pointer: subtracts. */
} an_ecil_pm_cast_kind;

/* CATCH_PARAM kinds (1.2).  Equal to cir::InitCatchKind; 3 is invalid. */
typedef enum an_ecil_catch_kind {
  ecat_reference                   = 0,
  ecat_pointer                     = 1,
  ecat_scalar                      = 2,
  ecat_trivial_copy                = 4,
  ecat_non_trivial_copy            = 5,  /* g_copy: the C1 copy ctor. */
  ecat_reference_to_pointer        = 6,
  ecat_reference_to_record_pointer = 7
} an_ecil_catch_kind;

/* Bits of the ebi_is_fp_class mask (same values as LLVM's FPClassTest). */
#define ECIL_FP_CLASS_SNAN           0x001u
#define ECIL_FP_CLASS_QNAN           0x002u
#define ECIL_FP_CLASS_NEG_INF        0x004u
#define ECIL_FP_CLASS_NEG_NORMAL     0x008u
#define ECIL_FP_CLASS_NEG_SUBNORMAL  0x010u
#define ECIL_FP_CLASS_NEG_ZERO       0x020u
#define ECIL_FP_CLASS_POS_ZERO       0x040u
#define ECIL_FP_CLASS_POS_SUBNORMAL  0x080u
#define ECIL_FP_CLASS_POS_NORMAL     0x100u
#define ECIL_FP_CLASS_POS_INF        0x200u

/* Flag bits of operands named "flags". */

#define ECIL_TARGET_BIG_ENDIAN          0x01u
#define ECIL_TARGET_CHAR_SIGNED         0x02u
#define ECIL_TARGET_CHEAP_UNALIGNED_BF  0x04u

#define ECIL_LANG_WRAPV                 0x01u
#define ECIL_LANG_EXCEPTIONS            0x02u
#define ECIL_LANG_RTTI                  0x04u
#define ECIL_LANG_THREADSAFE_STATICS    0x08u

#define ECIL_ARRAY_INCOMPLETE           0x01u
#define ECIL_ARRAY_ZERO_LENGTH          0x02u

#define ECIL_FUNC_TYPE_VARIADIC         0x01u
#define ECIL_FUNC_TYPE_NO_PROTO         0x02u

#define ECIL_RECORD_PACKED              0x01u
#define ECIL_RECORD_FLEXIBLE_ARRAY      0x02u
#define ECIL_RECORD_NONTRIVIAL_ABI      0x04u
                        /* v1: passed and returned indirectly (not
                           bitwise copyable, a deleted or non-trivial
                           destructor, or all copy and move constructors
                           deleted). */
#define ECIL_RECORD_NONTRIVIAL_DTOR     0x08u  /* v1 */

#define ECIL_FIELD_UNNAMED              0x01u
#define ECIL_FIELD_EMPTY_FOR_ABI        0x02u
#define ECIL_FIELD_BITFIELD_SIGNED      0x04u
#define ECIL_FIELD_FLEXIBLE_ARRAY       0x08u
#define ECIL_FIELD_BASE                 0x10u
                        /* v1: a direct non-virtual base subobject. */
#define ECIL_FIELD_POTENTIALLY_OVERLAPPING 0x20u
                        /* v1: laid out with the base-subobject type
                           (data_size bytes). */
#define ECIL_FIELD_VIRTUAL_BASE         0x40u
                        /* 1.2: a virtual base, direct or indirect, at
                           its complete-object offset. */

#define ECIL_CLASS_BASE_PUBLIC          0x01u  /* 1.2 (CLASS_BASES) */

#define ECIL_VFUNC_PURE                 0x01u  /* 1.2 (CLASS_VFUNCS) */
#define ECIL_VFUNC_DELETED              0x02u  /* 1.2 */
#define ECIL_VFUNC_IMPLICIT             0x04u  /* 1.2 */
#define ECIL_VFUNC_REF_RESULT           0x08u
                        /* 1.2: the function returns a reference (a
                           covariant return adjustment is not null-checked). */

#define ECIL_DECL_FUNC_DEFINED_HERE     0x01u
#define ECIL_DECL_FUNC_INLINE           0x02u
#define ECIL_DECL_FUNC_NORETURN         0x04u
#define ECIL_DECL_FUNC_IS_MAIN          0x08u
#define ECIL_DECL_FUNC_INLINE_ONLY      0x10u
                        /* C99 inline definition without an external
                           definition in this TU (final status is
                           re-issued in F_GLOBALS). */
#define ECIL_DECL_FUNC_UNSUPPORTED      0x20u
                        /* The body used a construct ECIL cannot express;
                           only the declaration is available. */
#define ECIL_DECL_FUNC_COMDAT           0x40u
                        /* v1: own comdat named after the symbol (iff the
                           linkage is linkonce_odr or weak_odr). */
#define ECIL_DECL_FUNC_NOTHROW          0x80u
                        /* 1.2: the function cannot throw (front end
                           predicate only). */

#define ECIL_DECL_VAR_DEFINITION        0x01u
#define ECIL_DECL_VAR_CONST             0x02u
#define ECIL_DECL_VAR_TLS               0x04u
#define ECIL_DECL_VAR_STRING_LITERAL    0x08u
#define ECIL_DECL_VAR_STATIC_LOCAL      0x10u
#define ECIL_DECL_VAR_TENTATIVE         0x20u
#define ECIL_DECL_VAR_UNSUPPORTED       0x40u
                        /* The initializer used a construct ECIL cannot
                           express; the definition was dropped. */
#define ECIL_DECL_VAR_COMDAT            0x80u  /* v1: as for functions. */
#define ECIL_DECL_VAR_GUARDED           0x100u
                        /* v1: dynamically initialized under the Itanium
                           guard (spec 11.2 gives the guard name). */

#define ECIL_STRUCTOR_DTOR              0x01u  /* v1: D1/D2, not C1/C2. */
#define ECIL_STRUCTOR_HAS_VBASES        0x02u
                        /* 1.2 (0 before): both variants are streamed;
                           the base variant takes the VTT. */

#define ECIL_STRUCTOR_DELETING_SIZED    0x01u  /* 1.2 */
#define ECIL_STRUCTOR_DELETING_ALIGNED  0x02u  /* 1.2 */
#define ECIL_STRUCTOR_DELETING_DESTROYING 0x04u
                        /* 1.2: g_delete is a destroying operator delete;
                           D0 calls it and does not run D1. */

#define ECIL_ABI_DATA_DEFINED_HERE      0x01u  /* 1.2 (DECL_ABI_DATA) */
#define ECIL_ABI_DATA_COMDAT            0x02u  /* 1.2 */
#define ECIL_ABI_DATA_TU_LOCAL          0x04u
                        /* 1.2 (typeinfo): the type is TU-local; the
                           name string starts with '*'. */

/* TYPEINFO_DEF flags (1.2): the __pbase_type_info masks. */
#define ECIL_TYPEINFO_CONST             0x01u
#define ECIL_TYPEINFO_VOLATILE          0x02u
#define ECIL_TYPEINFO_RESTRICT          0x04u
#define ECIL_TYPEINFO_INCOMPLETE        0x08u
#define ECIL_TYPEINFO_INCOMPLETE_CLASS  0x10u
#define ECIL_TYPEINFO_TRANSACTION_SAFE  0x20u
#define ECIL_TYPEINFO_NOEXCEPT          0x40u

#define ECIL_PARAM_THIS                 0x01u  /* v1 */

#define ECIL_LOCAL_RETURN_SLOT          0x01u  /* v1: NRVO variable. */
#define ECIL_LOCAL_TEMPORARY            0x02u  /* v1: naming hint. */

#define ECIL_FUNC_END_REACHABLE         0x01u

#define ECIL_ACCESS_VOLATILE            0x01u
#define ECIL_STORE_INIT                 0x02u
                        /* v1 (STORE): the target is fresh storage; a call
                           result is stored directly (copy elision). */
#define ECIL_STORE_DSIZE                0x04u
                        /* v1 (STORE): copy only data_size bytes. */

#define ECIL_ARITH_NSW                  0x01u
#define ECIL_ARITH_NUW                  0x02u
#define ECIL_ARITH_EXACT                0x04u

#define ECIL_INC_DEC_NSW                0x01u
#define ECIL_INC_DEC_VOLATILE           0x02u

#define ECIL_PTR_STRIDE_NEGATE          0x01u
#define ECIL_PTR_STRIDE_INDEX_UNSIGNED  0x02u

#define ECIL_AGG_INIT_ZERO_FILL         0x01u

#define ECIL_CALL_VARIADIC_CALLEE       0x01u
#define ECIL_CALL_NORETURN              0x02u
#define ECIL_CALL_RESULT_UNUSED         0x04u
#define ECIL_CALL_NOTHROW               0x08u
                        /* 1.2 (CALL, CALL_INDIRECT, PM_CALL): the callee
                           or call type cannot throw. */
#define ECIL_CALL_VIRTUAL               0x10u
                        /* 1.2 (CALL): through the vtable slot of g. */
#define ECIL_CALL_VTT                   0x20u
                        /* 1.2 (CALL): a VTT_ADDR operand follows
                           argument 0. */

#define ECIL_CAST_NONNULL               0x01u
                        /* v1 (BASE_CAST, DERIVED_CAST; 1.2 VBASE_CAST):
                           the operand is known non-null; no null check. */
#define ECIL_VBASE_CAST_COMPLETE        0x02u
                        /* 1.2: the static complete-object offset. */

#define ECIL_DYNAMIC_CAST_REF           0x01u  /* 1.2: throws bad_cast. */
#define ECIL_TYPEID_NULL_CHECK          0x01u  /* 1.2: typeid(*p). */
#define ECIL_METHOD_VIRTUAL             0x01u
                        /* 1.2 (CONST_METHOD, C_METHOD). */

#define ECIL_TRY_FUNCTION_TRY           0x01u  /* 1.2 (TRY_BEGIN) */

#define ECIL_CLEANUP_DEACTIVATABLE      0x01u
                        /* 1.2 (CLEANUP_BEGIN, eh or all): may be ended
                           early by CLEANUP_END. */

#define ECIL_STATIC_INIT_INLINE_VAR     0x01u  /* v1 */
#define ECIL_STATIC_INIT_TEMPLATE_INSTANCE 0x02u  /* v1 */
#define ECIL_STATIC_INIT_PARTIAL        0x04u
                        /* 1.1: g has a VAR_INIT with its constant
                           elements; the STATIC_INIT stores only the
                           dynamic ones (no zero-fill). */

#define ECIL_MEMINIT_COMPILER_GENERATED 0x01u  /* v1 */
#define ECIL_MEMINIT_DEFAULT_MEMBER_INIT 0x02u  /* v1 */

#define ECIL_ARRAY_LOOP_FIXED           0x01u
                        /* v1 (ARRAY_CTOR/DTOR_BEGIN): the operand is a glv
                           of an array type, not a pointer and a count. */

#define ECIL_DELETE_ARRAY               0x01u  /* v1 */
#define ECIL_DELETE_SIZED               0x02u  /* v1 */
#define ECIL_DELETE_ALIGNED             0x04u  /* v1 */
#define ECIL_DELETE_VIRTUAL             0x08u
                        /* 1.2: D0 through the vtable. */
#define ECIL_DELETE_GLOBAL              0x10u
                        /* 1.2 (with VIRTUAL): ::delete of a polymorphic
                           object: D1 through the vtable, then the global
                           operator delete on the complete object. */
#define ECIL_DELETE_DESTROYING          0x20u
                        /* 1.2 (without VIRTUAL): a destroying operator
                           delete; no destructor runs (g dtor is nil). */

/* Supplementary blob keys (all optional; readers skip unknown keys). */
typedef enum an_ecil_supp_key {
  esk_nothrow       = 1,  /* A hint only. */
  esk_branch_hint   = 2,
  esk_typedef_name  = 3,
  esk_pure          = 4,
  esk_const         = 5,
  esk_align_assumed = 6,
  esk_nrvo_var      = 7,
  esk_readable_name = 8,
  esk_section       = 9,
  esk_visibility    = 10,
  esk_used          = 11,
  esk_explicit_align = 12,
  esk_unsupported_reason = 13, /* 1.1: bin, UTF-8, at most 255 bytes; on a
                                  DECL_FUNC or DECL_VAR with UNSUPPORTED. */
  esk_noinline      = 14  /* 1.3: true; on a DECL_FUNC.  The function must
                             not be inlined (GNU noinline).  Sticky. */
} an_ecil_supp_key;

#endif /* ECIL_FORMAT_H */
