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

#define ECIL_VERSION_MAJOR 0
#define ECIL_VERSION_MINOR 6

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
  /* Declarations. */                                                        \
  X(decl_func,        0x30, 5)  /* g, t, linkage, flags, sid|nil */          \
  X(decl_var,         0x31, 6)  /* g, t, linkage, flags, align, sid|nil */   \
  X(var_init,         0x32, 1)  /* g; pops one constant */                   \
  /* Bodies and statements. */                                               \
  X(func_begin,       0x40, 2)  /* g, flags */                               \
  X(func_end,         0x41, 1)  /* flags */                                  \
  X(param,            0x42, 5)  /* l, t abi, t decl, sid|nil, flags */       \
  X(local,            0x43, 5)  /* l, t, sid|nil, align, flags */            \
  X(block_begin,      0x44, 0)                                               \
  X(block_end,        0x45, 0)                                               \
  X(if_begin,         0x47, 1)  /* has_else; pops bool */                    \
  X(if_else,          0x48, 0)                                               \
  X(if_end,           0x49, 0)                                               \
  X(while_begin,      0x4A, 0)                                               \
  X(while_body,       0x4B, 0)  /* pops bool */                              \
  X(while_end,        0x4C, 0)                                               \
  X(do_begin,         0x4D, 0)                                               \
  X(do_cond,          0x4E, 0)                                               \
  X(do_end,           0x4F, 0)  /* pops bool */                              \
  X(for_begin,        0x50, 0)                                               \
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
  X(c_agg_end,        0xEA, 0)

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

#define ECIL_FIELD_UNNAMED              0x01u
#define ECIL_FIELD_EMPTY_FOR_ABI        0x02u
#define ECIL_FIELD_BITFIELD_SIGNED      0x04u
#define ECIL_FIELD_FLEXIBLE_ARRAY       0x08u

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

#define ECIL_DECL_VAR_DEFINITION        0x01u
#define ECIL_DECL_VAR_CONST             0x02u
#define ECIL_DECL_VAR_TLS               0x04u
#define ECIL_DECL_VAR_STRING_LITERAL    0x08u
#define ECIL_DECL_VAR_STATIC_LOCAL      0x10u
#define ECIL_DECL_VAR_TENTATIVE         0x20u
#define ECIL_DECL_VAR_UNSUPPORTED       0x40u
                        /* The initializer used a construct ECIL cannot
                           express; the definition was dropped. */

#define ECIL_FUNC_END_REACHABLE         0x01u

#define ECIL_ACCESS_VOLATILE            0x01u

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

/* Supplementary blob keys (all optional; readers skip unknown keys). */
typedef enum an_ecil_supp_key {
  esk_nothrow       = 1,
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
  esk_explicit_align = 12
} an_ecil_supp_key;

#endif /* ECIL_FORMAT_H */
