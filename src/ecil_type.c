/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*

ecil_type.c -- ECIL types and declarations: the canonical type mapping,
               record layout, DECL_FUNC / DECL_VAR and their re-issue rules.

IL types are structural but not canonical (each typedef, qualified type and
function type is its own entry).  ECIL types are canonical: typerefs and
qualifiers are stripped, enums map to their integer type, and pointers,
arrays and function types are interned by structure, so equal ECIL types
have one G-id.  The id is cached in the id slot of every IL type entry that
maps to it.  Types are introduced post-order with an explicit work stack
(never recursion on the type graph); a record is declared before its fields
are visited, which breaks cycles.

*/

/* Header files common to all files. */
#include "fe_common.h"

#ifdef PCH_PRAGMA_GUARD
/* Mark the end of the sequence of headers subject to precompiled header
   processing. */
#pragma hdrstop
#endif /* ifdef PCH_PRAGMA_GUARD */

/* Additional header files. */
#include "ecil_write.h"
#if BACK_END_TYPE == BACK_END_TYPE_CIR
#include "lower_name.h"
#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */
/* Defines builtin_libc_names: the libc/libm functions that are builtins,
   sorted and NULL-terminated, generated from the builtin tables. */
#include "builtin_libc.h"

#if ECIL_WRITER_ENABLED

#include "class_decl.h"

/* Conditionally open the "edg" namespace. */
BEGIN_EDG_NAMESPACE

/* The phases of an item of the type work stack. */
enum an_type_phase {
  tph_start,        /* Classify the type. */
  tph_children,     /* Pointer, array, function: the children are known. */
  tph_fields        /* Record: visit the field types, then emit the body. */
};

/* An item of the type work stack. */
typedef struct a_type_work_item {
  a_type_ptr    type;             /* A type with typerefs skipped. */
  enum an_type_phase
                phase;
  a_boolean     ret_done;         /* Function: return type is known. */
  a_boolean     this_done;        /* Function: this type is known. */
  a_base_class_ptr
                base_cursor;      /* Record: next direct base to visit. */
  a_param_type_ptr
                param_cursor;     /* Function: next parameter to visit. */
  a_field_ptr   field_cursor;     /* Record: next field to visit. */
  a_boolean     promote_params;   /* Function: K&R promoted form. */
  a_boolean     decl_only;        /* Record: only its declaration is needed
                                     now (it is a pointee); the body comes
                                     after the types in progress. */
} a_type_work_item;

/* A record declared while incomplete: its G-id and its IL type. */
typedef struct an_ecil_pending_record {
  uint32_t      gid;
  a_type_ptr    type;
} an_ecil_pending_record;

/* The type work stack and the list of records still awaiting a body. */
STATIC_THREAD a_type_work_item
                *type_stack;
STATIC_THREAD size_t
                type_stack_size;
STATIC_THREAD size_t
                type_stack_capacity;
STATIC_THREAD an_ecil_pending_record
                *pending_records;
STATIC_THREAD size_t
                num_pending_records;
STATIC_THREAD size_t
                num_deferred_records;  /* Pending only for the order. */
STATIC_THREAD size_t
                pending_records_capacity;
STATIC_THREAD a_boolean
                pending_records_dirty;  /* A class was completed, or a
                                           complete record is pending only
                                           for the order, since the pending
                                           records were last scanned. */

/* The classes whose facts (layout, vtable) are being sent, innermost last
   (explicit: the inheritance depth scales with the input). */
STATIC_THREAD a_type_ptr
                *class_stack;
STATIC_THREAD size_t
                class_stack_size;
STATIC_THREAD size_t
                class_stack_capacity;

/* The types whose typeinfo is being introduced, innermost last (explicit: a
   pointer chain scales with the input). */
STATIC_THREAD a_type_ptr
                *typeinfo_stack;
STATIC_THREAD size_t
                typeinfo_stack_size;
STATIC_THREAD size_t
                typeinfo_stack_capacity;

STATIC_THREAD uint32_t
                bad_array_function_gid;   /* __cxa_throw_bad_array_new_length,
                                             once introduced. */

/* The signatures of virtual functions (CLASS_VFUNCS sig): the interned
   routines, in order, with the next entry of the same hash chain, and the
   chain heads by hash. */
typedef struct a_sig_entry {
  a_routine_ptr rout;             /* NULL for a dead entry (below). */
  uint32_t      next;             /* Index + 1 of the next entry, or 0. */
  a_boolean     local;            /* The routine is in a function's region:
                                     its IL is freed with it, and the entry
                                     is dead from then on (ecil_forget_local_
                                     signatures); the id stays taken. */
} a_sig_entry;

STATIC_THREAD a_sig_entry
                *sig_entries;
STATIC_THREAD size_t
                num_sig_entries;
STATIC_THREAD size_t
                sig_entries_capacity;
STATIC_THREAD uint32_t
                *sig_heads;
STATIC_THREAD size_t
                sig_heads_size;

/* The classes whose vtable was marked used since the log was last
   committed (so that a dropped body or constant can take the mark back). */
STATIC_THREAD uint32_t
                *used_log;
STATIC_THREAD size_t
                used_log_size;
STATIC_THREAD size_t
                used_log_capacity;

/* The dynamic classes whose records were sent, in order (their vtables are
   decided at the end: ecil_emit_class_abi_data), and the function-local ones
   (decided when their function's region ends). */
STATIC_THREAD uint32_t
                *dynamic_classes;
STATIC_THREAD size_t
                num_dynamic_classes;
STATIC_THREAD size_t
                dynamic_classes_capacity;

/* A name bound at the end (NAME_BIND). */
typedef struct a_late_binding {
  uint32_t      gid;
  uint32_t      sid;
  unsigned int  linkage;
} a_late_binding;

STATIC_THREAD a_late_binding
                *late_bindings;
STATIC_THREAD size_t
                num_late_bindings;
STATIC_THREAD size_t
                late_bindings_capacity;

/* The reason a function or variable was dropped as unsupported, kept so that
   every re-issue of its unsupported declaration carries it (spec 6,
   unsupported_reason). */
typedef struct a_drop_reason {
  uint32_t      gid;
  char          *text;
} a_drop_reason;

STATIC_THREAD a_drop_reason
                *drop_reasons;
STATIC_THREAD size_t
                num_drop_reasons;
STATIC_THREAD size_t
                drop_reasons_capacity;


static void free_name_table(void);

void ecil_type_reset(void)
/*
Forget the writer's type state at the end of a compilation.
*/
{
  free(type_stack);
  free(pending_records);
  free(class_stack);
  free(typeinfo_stack);
  typeinfo_stack = NULL;
  bad_array_function_gid = 0;
  typeinfo_stack_size = typeinfo_stack_capacity = 0;
  free(used_log);
  used_log = NULL;
  used_log_size = used_log_capacity = 0;
  free(dynamic_classes);
  free(sig_entries);
  free(sig_heads);
  sig_entries = NULL;
  sig_heads = NULL;
  num_sig_entries = sig_entries_capacity = sig_heads_size = 0;
  class_stack = NULL;
  dynamic_classes = NULL;
  class_stack_size = class_stack_capacity = 0;
  num_dynamic_classes = dynamic_classes_capacity = 0;
  free(late_bindings);
  late_bindings = NULL;
  num_late_bindings = late_bindings_capacity = 0;
  while (num_drop_reasons != 0) free(drop_reasons[--num_drop_reasons].text);
  free(drop_reasons);
  drop_reasons = NULL;
  drop_reasons_capacity = 0;
  free_name_table();
  type_stack = NULL;
  pending_records = NULL;
  type_stack_size = type_stack_capacity = 0;
  num_pending_records = pending_records_capacity = 0;
  num_deferred_records = 0;
  pending_records_dirty = FALSE;
}  /* ecil_type_reset */


const char *ecil_entity_name(a_source_correspondence *scp)
/*
Return the plain (C) name of an entity, or NULL if it has none.
*/
{
  return scp->name;
}  /* ecil_entity_name */


/* Target types the writer needs that the front end does not hand it. */

unsigned int ecil_int_bits(void)
/*
The width of the target's int.
*/
{
  return (unsigned int)(targ_sizeof_int * targ_char_bit);
}  /* ecil_int_bits */


uint32_t ecil_target_int_type(an_ecil_frame *frame)
/*
The G-id of the target's int.
*/
{
  return ecil_intern_int_type(frame, ecil_int_bits(), TRUE,
                              (unsigned int)targ_sizeof_int,
                              (unsigned int)targ_alignof_int);
}  /* ecil_target_int_type */


uint32_t ecil_target_double_type(an_ecil_frame *frame)
/*
The G-id of the target's double (IEEE binary64).
*/
{
  return ecil_intern_float_type(frame, (unsigned int)eff_ieee_double,
                                (unsigned int)targ_sizeof_double,
                                (unsigned int)targ_alignof_double);
}  /* ecil_target_double_type */


uint32_t ecil_target_uintptr_type(an_ecil_frame *frame)
/*
The G-id of the unsigned integer type as wide as a pointer.
*/
{
  return ecil_intern_int_type(frame,
                       (unsigned int)(targ_sizeof_pointer * targ_char_bit),
                       FALSE, (unsigned int)targ_sizeof_pointer,
                       (unsigned int)targ_sizeof_pointer);
}  /* ecil_target_uintptr_type */


uint32_t ecil_promote_gid(an_ecil_frame *frame, uint32_t gid,
                          a_type_ptr il_type)
/*
The type a value of ECIL type gid has after the integer promotions (bool and
integers narrower than int become int) and, if il_type is not NULL, after
the default argument promotions of an unprototyped call or a K&R definition
(the float of the IL type il_type, which must be plain float, becomes
double).  The G-id is unchanged if no promotion applies.  The caller must
not hold an an_ecil_gid_info across the call (it can assign a G-id).
*/
{
  an_ecil_gid_info  *info = ecil_gid_info(gid);

  if (info->kind == egk_bool ||
      (info->kind == egk_int && info->bits < ecil_int_bits())) {
    gid = ecil_target_int_type(frame);
  } else if (il_type != NULL && info->kind == egk_float &&
             ecil_is_plain_float_type(il_type)) {
    gid = ecil_target_double_type(frame);
  }  /* if */
  return gid;
}  /* ecil_promote_gid */


unsigned int ecil_linkage_for(a_storage_class    storage_class,
                              unsigned int       name_linkage,
                              a_boolean          is_weak)
/*
The ECIL linkage of a function or variable (an an_ecil_linkage value):
internal for a static or an entity with internal linkage, weak for a weak
one, else external.
*/
{
  unsigned int  linkage = (unsigned int)elk_external;

  if (storage_class == (a_storage_class)sc_static ||
      name_linkage == (unsigned int)nlk_internal) {
    linkage = (unsigned int)elk_internal;
  } else if (is_weak) {
    linkage = (unsigned int)elk_weak;
  }  /* if */
  return linkage;
}  /* ecil_linkage_for */


a_boolean ecil_gid_has_data_member_pointer(uint32_t gid)
/*
Return TRUE if the type is a pointer to a data member or contains one (an
array element, a member or a base, at any depth).
*/
{
  return gid != 0 && (ecil_gid_info(gid)->flags & ECIL_GF_HAS_DMP) != 0;
}  /* ecil_gid_has_data_member_pointer */


static void remember_one_drop_reason(uint32_t gid, const char *text)
/*
Keep text as the reason for the dropped declaration of G-id gid, replacing an
earlier one.  Nothing is kept if memory runs out: the reason is a courtesy.
*/
{
  size_t  i;
  char    *copy;

  for (i = 0; i < num_drop_reasons && drop_reasons[i].gid != gid; i++) {
  }  /* for */
  if (i == num_drop_reasons && num_drop_reasons == drop_reasons_capacity) {
    size_t         new_capacity = (drop_reasons_capacity == 0) ?
                                  16 : drop_reasons_capacity * 2;
    a_drop_reason  *new_list = (a_drop_reason *)realloc(drop_reasons,
                                   new_capacity * sizeof(a_drop_reason));

    if (new_list != NULL) {
      drop_reasons = new_list;
      drop_reasons_capacity = new_capacity;
    }  /* if */
  }  /* if */
  if (i < num_drop_reasons || num_drop_reasons < drop_reasons_capacity) {
    size_t  length = ecil_utf8_prefix_length(text, ECIL_REASON_MAX - 1);

    copy = (char *)malloc(length + 1);
    if (copy != NULL) {
      memcpy(copy, text, length);
      copy[length] = '\0';
      if (i == num_drop_reasons) {
        drop_reasons[i].gid = gid;
        drop_reasons[i].text = NULL;
        num_drop_reasons++;
      }  /* if */
      free(drop_reasons[i].text);
      drop_reasons[i].text = copy;
    }  /* if */
  }  /* if */
}  /* remember_one_drop_reason */


void ecil_remember_drop_reason(uint32_t gid, const char *text)
/*
Remember why the function or variable with G-id gid is being dropped, for the
unsupported_reason supp blob of its unsupported declaration.  For a
constructor or destructor the reason is kept for both variants.
*/
{
  if (gid != 0) {
    uint32_t  partner = ecil_gid_info(gid)->partner;

    remember_one_drop_reason(gid, text);
    if (partner != 0) remember_one_drop_reason(partner, text);
    if (ecil_gid_info(gid)->variant == ECIL_VARIANT_COMPLETE &&
        ecil_gid_info(gid)->aux != 0) {
      remember_one_drop_reason(ecil_gid_info(gid)->aux, text);   /* D0. */
    }  /* if */
  }  /* if */
}  /* ecil_remember_drop_reason */


static void add_drop_reason(an_ecil_supp *supp, uint32_t gid)
/*
Add to the supp blob of the DECL_FUNC or DECL_VAR that is being written, which
is unsupported, the reason that was remembered for its G-id (never another
G-id's), if any.
*/
{
  size_t  i;

  for (i = 0; i < num_drop_reasons && drop_reasons[i].gid != gid; i++) {
  }  /* for */
  if (i < num_drop_reasons) {
    ecil_supp_text(supp, esk_unsupported_reason, drop_reasons[i].text);
  }  /* if */
}  /* add_drop_reason */


void ecil_emit_decl_var(an_ecil_frame *frame,
                        uint32_t      gid,
                        uint32_t      type,
                        unsigned int  linkage,
                        unsigned int  flags,
                        uint32_t      alignment,
                        uint32_t      name_sid)
/*
Emit DECL_VAR (an introduction or a re-issue) into the declaration section
and remember its facts as the last ones emitted for the G-id.
*/
{
  an_ecil_buffer  *saved = ecil_frame_enter_decl(frame);
  an_ecil_gid_info
                  *info;

  ecil_rec_begin(frame, eop_decl_var);
  ecil_arg_uint(frame, gid);
  ecil_arg_uint(frame, type);
  ecil_arg_uint(frame, linkage);
  ecil_arg_uint(frame, flags);
  ecil_arg_uint(frame, alignment);
  if (name_sid != 0) {
    ecil_arg_uint(frame, name_sid);
  } else {
    ecil_arg_nil(frame);
  }  /* if */
  if ((flags & ECIL_DECL_VAR_UNSUPPORTED) != 0) {
    an_ecil_supp  supp;

    ecil_supp_begin(&supp);
    add_drop_reason(&supp, gid);
    ecil_rec_supp_end(frame, &supp);
  }  /* if */
  ecil_rec_end(frame);
  ecil_frame_leave_decl(frame, saved);
  info = ecil_gid_info(gid);
  info->decl_type = type;
  info->decl_linkage = linkage;
  info->decl_flags = flags;
  info->decl_name = name_sid;
  info->decl_emitted = TRUE;
}  /* ecil_emit_decl_var */


void ecil_emit_dropped_decl_var(an_ecil_frame *frame,
                                uint32_t      gid,
                                uint32_t      type,
                                unsigned int  linkage,
                                unsigned int  flags,
                                uint32_t      alignment,
                                uint32_t      name_sid)
/*
Re-issue a variable whose definition cannot be expressed as an unsupported
declaration: it is no definition (no definition, tentative, COMDAT or guard
flags), and a vague linkage becomes external as for a dropped function
(spec 11.2).
*/
{
  if (linkage == (unsigned int)elk_linkonce_odr ||
      linkage == (unsigned int)elk_weak_odr) {
    linkage = (unsigned int)elk_external;
  }  /* if */
  flags &= ~(unsigned int)(ECIL_DECL_VAR_DEFINITION | ECIL_DECL_VAR_TENTATIVE |
                           ECIL_DECL_VAR_COMDAT | ECIL_DECL_VAR_GUARDED);
  ecil_emit_decl_var(frame, gid, type, linkage,
                     flags | ECIL_DECL_VAR_UNSUPPORTED, alignment, name_sid);
}  /* ecil_emit_dropped_decl_var */


/* Small introductions. */

static uint32_t ecil_intern_simple(an_ecil_frame     *frame,
                                   an_ecil_gid_kind  kind,
                                   const uint32_t    *key,
                                   size_t            key_count,
                                   unsigned int      bits,
                                   a_boolean         is_signed,
                                   unsigned int      size,
                                   unsigned int      alignment,
                                   unsigned int      float_format)
/*
Introduce a scalar type (void, bool, int, float) once per key and return its
G-id.
*/
{
  uint32_t  gid = ecil_word_lookup(key, key_count);

  if (gid == 0) {
    gid = ecil_new_gid(kind);
    if (gid != 0) {
      an_ecil_gid_info  *info = ecil_gid_info(gid);
      an_ecil_buffer    *saved;

      info->bits = bits;
      info->is_signed = is_signed;
      info->size = size;
      info->align = alignment;
      info->float_format = float_format;
      ecil_word_register(key, key_count, gid);
      saved = ecil_frame_enter_decl(frame);
      switch (kind) {
        case egk_void:
          ecil_rec1(frame, eop_ty_void, gid);
          break;
        case egk_bool:
          ecil_rec_begin(frame, eop_ty_bool);
          ecil_arg_uint(frame, gid);
          ecil_arg_uint(frame, size);
          ecil_arg_uint(frame, alignment);
          ecil_rec_end(frame);
          break;
        case egk_int:
          ecil_rec_begin(frame, eop_ty_int);
          ecil_arg_uint(frame, gid);
          ecil_arg_uint(frame, bits);
          ecil_arg_bool(frame, is_signed);
          ecil_arg_uint(frame, size);
          ecil_arg_uint(frame, alignment);
          ecil_rec_end(frame);
          break;
        default:
          ecil_rec_begin(frame, eop_ty_float);
          ecil_arg_uint(frame, gid);
          ecil_arg_uint(frame, float_format);
          ecil_arg_uint(frame, size);
          ecil_arg_uint(frame, alignment);
          ecil_rec_end(frame);
          break;
      }  /* switch */
      ecil_frame_leave_decl(frame, saved);
    }  /* if */
  }  /* if */
  return gid;
}  /* ecil_intern_simple */


uint32_t ecil_void_type(an_ecil_frame *frame)
/*
Return the G-id of void.
*/
{
  uint32_t  key[1] = { (uint32_t)egk_void };

  return ecil_intern_simple(frame, egk_void, key, 1, 0, FALSE, 0, 0, 0);
}  /* ecil_void_type */


uint32_t ecil_bool_type(an_ecil_frame *frame)
/*
Return the G-id of _Bool.
*/
{
  uint32_t  key[1] = { (uint32_t)egk_bool };

  return ecil_intern_simple(frame, egk_bool, key, 1, 1, FALSE, 1, 1, 0);
}  /* ecil_bool_type */


uint32_t ecil_intern_int_type(an_ecil_frame *frame,
                              unsigned int  bits,
                              a_boolean     is_signed,
                              unsigned int  size,
                              unsigned int  alignment)
/*
Return the G-id of the integer type with this width and signedness.
*/
{
  uint32_t  key[3];

  key[0] = (uint32_t)egk_int;
  key[1] = bits;
  key[2] = is_signed ? 1u : 0u;
  return ecil_intern_simple(frame, egk_int, key, 3, bits, is_signed, size,
                            alignment, 0);
}  /* ecil_intern_int_type */


uint32_t ecil_intern_float_type(an_ecil_frame *frame,
                                unsigned int  format,
                                unsigned int  size,
                                unsigned int  alignment)
/*
Return the G-id of the floating-point type with this format.
*/
{
  uint32_t  key[3];

  key[0] = (uint32_t)egk_float;
  key[1] = format;
  key[2] = size;
  return ecil_intern_simple(frame, egk_float, key, 3, 0, TRUE, size,
                            alignment, format);
}  /* ecil_intern_float_type */


uint32_t ecil_ptr_type(an_ecil_frame *frame, uint32_t pointee)
/*
Return the G-id of the pointer to the type with G-id pointee.
*/
{
  uint32_t  key[2];
  uint32_t  gid;

  key[0] = (uint32_t)egk_ptr;
  key[1] = pointee;
  gid = ecil_word_lookup(key, 2);
  if (gid == 0) {
    gid = ecil_new_gid(egk_ptr);
    if (gid != 0) {
      an_ecil_gid_info  *info = ecil_gid_info(gid);
      an_ecil_buffer    *saved;

      info->ref = pointee;
      info->size = (uint32_t)targ_sizeof_pointer;
      info->align = (uint32_t)targ_sizeof_pointer;
      ecil_word_register(key, 2, gid);
      saved = ecil_frame_enter_decl(frame);
      ecil_rec2(frame, eop_ty_ptr, gid, pointee);
      ecil_frame_leave_decl(frame, saved);
    }  /* if */
  }  /* if */
  return gid;
}  /* ecil_ptr_type */


void ecil_note_unsupported_type(uint32_t gid, const char *fallback)
/*
Note an unsupported use of a value of the type with G-id gid, naming the
kind of type when it is a vector or complex type.
*/
{
  unsigned int  flags = gid != 0 ? ecil_gid_info(gid)->flags : 0u;

  if (flags & ECIL_GF_VECTOR) {
    ecil_note_unsupported("a vector type (vector types are not supported)");
  } else if (flags & ECIL_GF_COMPLEX) {
    ecil_note_unsupported("a complex type (complex types are not "
                          "supported)");
  } else {
    ecil_note_unsupported(fallback);
  }  /* if */
}  /* ecil_note_unsupported_type */


static uint32_t ecil_opaque_type(an_ecil_frame *frame, a_type_ptr type)
/*
Introduce TY_OPAQUE for a type ECIL v0 cannot represent: laid out as
size bytes with the right alignment, so records containing it keep their
layout.  One G-id per IL type entry.
*/
{
  uint32_t  gid = ecil_new_gid(egk_opaque);

  if (gid != 0) {
    an_ecil_gid_info  *info = ecil_gid_info(gid);
    an_ecil_buffer    *saved;

    info->flags = ECIL_GF_OPAQUE | ECIL_GF_ABI_OPAQUE;
#if GNU_VECTOR_TYPES_ALLOWED
    if (type->kind == tk_vector) info->flags |= ECIL_GF_VECTOR;
#endif /* GNU_VECTOR_TYPES_ALLOWED */
    if (type->kind == tk_complex || type->kind == tk_imaginary) {
      info->flags |= ECIL_GF_COMPLEX;
    }  /* if */
    info->size = (uint64_t)type->size;
    info->align = (uint32_t)(type->alignment == 0 ? 1 : type->alignment);
    saved = ecil_frame_enter_decl(frame);
    ecil_rec_begin(frame, eop_ty_opaque);
    ecil_arg_uint(frame, gid);
    ecil_arg_uint(frame, info->size);
    ecil_arg_uint(frame, info->align);
    ecil_rec_end(frame);
    ecil_frame_leave_decl(frame, saved);
  }  /* if */
  return gid;
}  /* ecil_opaque_type */


static unsigned int ecil_float_format_of(a_type_ptr type)
/*
Return the an_ecil_float_format of a floating type, or 0xFFFF if v0 has
none for it.  The size of the IL type is authoritative.
*/
{
  unsigned int  format = 0xFFFF;
  a_targ_size_t size = type->size;

  switch (type->variant.float_kind) {
    case fk_float16:
    case fk_fp16:
    case fk_std_float16:
      format = (unsigned int)eff_ieee_half;
      break;
    case fk_std_float32:
      format = (unsigned int)eff_ieee_single;
      break;
    case fk_std_float64:
      format = (unsigned int)eff_ieee_double;
      break;
    case fk_std_bfloat16:
      format = (unsigned int)eff_bfloat16;
      break;
    case fk_float128:
    case fk_std_float128:
      format = (unsigned int)eff_ieee_quad;
      break;
    case fk_float80:
      format = (unsigned int)eff_x87_80;
      break;
    default:
      if (size == 4) {
        format = (unsigned int)eff_ieee_single;
      } else if (size == 8) {
        format = (unsigned int)eff_ieee_double;
      } else if (size == 12 || size == 16) {
        format = (unsigned int)eff_x87_80;
      }  /* if */
      break;
  }  /* switch */
  return format;
}  /* ecil_float_format_of */


/* The type work stack. */

static void check_type_chain(a_type_ptr type)
/*
Walk the typeref chain of a type, noting constructs ECIL v0 rejects
(_Atomic).
*/
{
  while (type->kind == tk_typeref) {
    if ((type->variant.typeref.qualifiers & TQ_C11_ATOMIC) != 0) {
      ecil_note_unsupported("an _Atomic type");
    }  /* if */
    type = type->variant.typeref.type;
  }  /* while */
}  /* check_type_chain */


static a_type_work_item *push_type(a_type_ptr type, enum an_type_phase phase)
/*
Push a work item for an IL type (typerefs are skipped) and return it, or
return NULL if the stack could not grow.
*/
{
  a_type_work_item  *item = NULL;

  check_type_chain(type);
  if (type_stack_size == type_stack_capacity) {
    size_t            new_capacity = (type_stack_capacity == 0) ?
                                     64 : type_stack_capacity * 2;
    a_type_work_item  *new_stack = (a_type_work_item *)realloc(
                                   type_stack,
                                   new_capacity * sizeof(a_type_work_item));

    if (new_stack != NULL) {
      type_stack = new_stack;
      type_stack_capacity = new_capacity;
    }  /* if */
  }  /* if */
  if (type_stack_size < type_stack_capacity) {
    item = &type_stack[type_stack_size++];
    item->type = skip_typerefs(type);
    item->phase = phase;
    item->ret_done = FALSE;
    item->this_done = FALSE;
    item->base_cursor = NULL;
    item->param_cursor = NULL;
    item->field_cursor = NULL;
    item->promote_params = FALSE;
    item->decl_only = FALSE;
  } else {
    ecil_note_unsupported("out of memory in the ECIL writer");
  }  /* if */
  return item;
}  /* push_type */


static uint32_t known_type_id(a_type_ptr type)
/*
Return the G-id already assigned to a type (typerefs skipped), or 0.  The
typeref chain is checked for unsupported qualifiers even when the type is
known, since a qualified type shares the id of its unqualified type.
*/
{
  check_type_chain(type);
  return ecil_gid_of(skip_typerefs(type));
}  /* known_type_id */


static a_base_class_ptr record_direct_bases(a_type_ptr t);

static a_boolean info_of_gid_is_bodyless_record(uint32_t gid)
/*
Return TRUE for a record that has been declared but whose body has not been
sent (and did not fail).
*/
{
  const an_ecil_gid_info  *info = ecil_gid_info(gid);

  return info->kind == egk_record &&
         (info->flags & (ECIL_GF_COMPLETE | ECIL_GF_FAILED)) == 0 &&
         info->entity != NULL;
}  /* info_of_gid_is_bodyless_record */


static a_boolean type_is_in_progress(a_type_ptr t)
/*
Return TRUE if a work item for the type is on the type stack: its body is
being built (a record that a function type or a field mentions again must not
be started a second time).
*/
{
  size_t  i;
  a_boolean  found = FALSE;

  for (i = 0; i < type_stack_size && !found; i++) {
    found = type_stack[i].type == t;
  }  /* for */
  return found;
}  /* type_is_in_progress */


static a_boolean need_type(a_type_ptr child)
/*
Return TRUE if the child type has an id; otherwise push it (and return FALSE)
so the caller re-visits its own item afterwards.
*/
{
  uint32_t   gid = known_type_id(child);
  a_boolean  known = gid != 0;

  if (!known) {
    (void)push_type(child, tph_start);
  } else if (!C_mode() && info_of_gid_is_bodyless_record(gid)) {
    /* A record that was only declared (a pointee seen first) is needed by
       value: its body comes before its container's. */
    a_type_ptr         t = skip_typerefs(child);
    a_type_work_item   *item = (t->incomplete || type_is_in_progress(t)) ?
                               NULL : push_type(t, tph_fields);

    if (item != NULL) {
      item->base_cursor = record_direct_bases(t);
      item->field_cursor = t->variant.class_struct_union.field_list;
      known = FALSE;
    }  /* if */
  }  /* if */
  return known;
}  /* need_type */


static a_boolean need_pointee(a_type_ptr child)
/*
Like need_type for the pointee of a pointer: a record only has to be
declared (the pointer needs no body), so that a class that contains the
pointer's own class by value does not wait for it (CRTP: E<B> has a W<B> member
that points to B, and B derives from E<B>).  The body is sent after the
types in progress (ecil_type_id).
*/
{
  a_boolean  known = known_type_id(child) != 0;

  if (!known) {
    a_type_work_item  *item = push_type(child, tph_start);
    a_type_ptr        t = skip_typerefs(child);

    if (item != NULL && !C_mode() && is_immediate_class_type(t)) {
      item->decl_only = TRUE;
    }  /* if */
  }  /* if */
  return known;
}  /* need_pointee */


static uint32_t finish_member_pointer(an_ecil_frame *frame, a_type_ptr t)
/*
Make TY_DATA_MEMBER(member type, class) or TY_METHOD(function type with the
object pointer first, class) of a pointer to a member whose member type and
class have ids (spec 12.2).  Its null is -1 for a data member, which makes
every type that contains one not zero-initializable (ECIL_GF_HAS_DMP,
spec 12.7).
*/
{
  uint32_t    mem = known_type_id(pm_member_type(t));
  uint32_t    cls = known_type_id(pm_class_type(t));
  a_boolean   is_method = skip_typerefs(pm_member_type(t))->kind ==
                          tk_routine;
  uint32_t    key[3];
  uint32_t    gid;

  key[0] = (uint32_t)(is_method ? egk_method : egk_data_member);
  key[1] = mem;
  key[2] = cls;
  gid = ecil_word_lookup(key, 3);
  if (gid == 0) {
    gid = ecil_new_gid(is_method ? egk_method : egk_data_member);
    if (gid != 0) {
      an_ecil_gid_info  *info = ecil_gid_info(gid);
      an_ecil_buffer    *saved;

      info->ref = mem;
      info->aux = cls;
      info->size = (uint64_t)t->size;
      info->align = (uint32_t)(t->alignment == 0 ? 1 : t->alignment);
      if (!is_method) info->flags |= ECIL_GF_HAS_DMP;
      ecil_word_register(key, 3, gid);
      saved = ecil_frame_enter_decl(frame);
      ecil_rec_begin(frame, is_method ? eop_ty_method : eop_ty_data_member);
      ecil_arg_uint(frame, gid);
      ecil_arg_uint(frame, mem);
      ecil_arg_uint(frame, cls);
      ecil_rec_end(frame);
      ecil_frame_leave_decl(frame, saved);
    }  /* if */
  }  /* if */
  return gid;
}  /* finish_member_pointer */


static uint32_t finish_pointer(an_ecil_frame *frame, a_type_ptr t)
/*
Make the ptr type of a pointer whose pointee has an id.
*/
{
  return ecil_ptr_type(frame, known_type_id(t->variant.pointer.type));
}  /* finish_pointer */


static uint32_t intern_array(an_ecil_frame *frame,
                             uint32_t      elem,
                             uint64_t      count,
                             unsigned int  flags,
                             uint64_t      size)
/*
Return the G-id of the array type of elem with these facts, introducing it
once per key.  An incomplete array has count 0 and ECIL_ARRAY_INCOMPLETE and
is sent with a nil count.
*/
{
  uint32_t  key[4];
  uint32_t  gid;

  key[0] = (uint32_t)egk_array;
  key[1] = elem;
  key[2] = (uint32_t)(count & 0xFFFFFFFFu);
  key[3] = (uint32_t)(count >> 32) ^ (flags << 24);
  gid = ecil_word_lookup(key, 4);
  if (gid == 0) {
    gid = ecil_new_gid(egk_array);
    if (gid != 0) {
      an_ecil_gid_info  *info = ecil_gid_info(gid);
      an_ecil_gid_info  *einfo = ecil_gid_info(elem);
      an_ecil_buffer    *saved;

      info->ref = elem;
      info->count = count;
      info->size = (uint64_t)size;
      info->align = einfo->align;
      info->flags = einfo->flags & (ECIL_GF_OPAQUE | ECIL_GF_ABI_OPAQUE |
                                    ECIL_GF_FAILED);
      if (info->flags != 0) {
        info->flags = ECIL_GF_ABI_OPAQUE | (info->flags & ECIL_GF_FAILED);
      }  /* if */
      info->flags |= einfo->flags & ECIL_GF_HAS_DMP;
      ecil_word_register(key, 4, gid);
      saved = ecil_frame_enter_decl(frame);
      ecil_rec_begin(frame, eop_ty_array);
      ecil_arg_uint(frame, gid);
      ecil_arg_uint(frame, elem);
      if ((flags & ECIL_ARRAY_INCOMPLETE) != 0) {
        ecil_arg_nil(frame);
      } else {
        ecil_arg_uint(frame, count);
      }  /* if */
      ecil_arg_uint(frame, flags);
      ecil_rec_end(frame);
      ecil_frame_leave_decl(frame, saved);
    }  /* if */
  }  /* if */
  return gid;
}  /* intern_array */


uint32_t ecil_array_of(an_ecil_frame *frame, uint32_t elem, uint64_t count)
/*
Return the G-id of the complete array type of count elements of type elem
(for a type the front end does not have, such as the one-element array of a
tentative definition with an incomplete array type, C11 6.9.2p5).
*/
{
  return intern_array(frame, elem, count, 0u,
                      (uint64_t)ecil_gid_info(elem)->size * count);
}  /* ecil_array_of */


static uint32_t finish_array(an_ecil_frame *frame, a_type_ptr t)
/*
Make the array type of an array whose element type has an id.
*/
{
  a_boolean     incomplete = t->incomplete &&
                             !t->variant.array.bound_is_zero;
  uint64_t      count = incomplete ? 0 :
                        (uint64_t)t->variant.array.variant.number_of_elements;
  unsigned int  flags = 0;

  if (incomplete) flags |= ECIL_ARRAY_INCOMPLETE;
  if (t->variant.array.bound_is_zero) flags |= ECIL_ARRAY_ZERO_LENGTH;
  return intern_array(frame, known_type_id(t->variant.array.element_type),
                      count, flags, (uint64_t)t->size);
}  /* finish_array */


a_boolean ecil_is_plain_float_type(a_type_ptr type)
/*
Return TRUE for the type float itself: only it is promoted to double by the
default argument promotions (not _Float32 and the like).
*/
{
  a_type_ptr  t = skip_typerefs(type);

  return t->kind == tk_float &&
         t->variant.float_kind == (a_float_kind)fk_float;
}  /* ecil_is_plain_float_type */


static uint32_t finish_function_type(an_ecil_frame *frame,
                                     a_type_ptr    t,
                                     a_boolean     promote_params)
/*
Make the ECIL function type of a routine type whose return and parameter
types all have ids.  For an unprototyped declaration the parameter list is
empty; for an unprototyped (K&R) definition it holds the default-promoted
parameter types (spec 8.2).
*/
{
  a_routine_type_supplement_ptr supp = rout_type_supp(t);
  uint32_t                      ret = known_type_id(t->variant.routine.
                                                    return_type);
  uint32_t                      *key;
  uint32_t                      *params;
  size_t                        n = 0;
  uint32_t                      gid = 0;
  unsigned int                  flags = 0;
  a_param_type_ptr              p;
  a_boolean                     no_proto = !supp->prototyped;
  a_boolean                     list_params = !no_proto || promote_params;
  a_boolean                     has_this = !C_mode() &&
                                  routine_type_is_nonstatic_member_function(t);

  if (list_params) {
    for (p = supp->param_type_list; p != NULL; p = p->next) n++;
  }  /* if */
  if (has_this) n++;
  key = (uint32_t *)malloc((n + 3) * sizeof(uint32_t));
  params = (uint32_t *)malloc((n + 1) * sizeof(uint32_t));
  if (key == NULL || params == NULL) {
    ecil_note_unsupported("out of memory in the ECIL writer");
  } else {
    size_t  i = 0;

    if (supp->has_ellipsis) flags |= ECIL_FUNC_TYPE_VARIADIC;
    if (no_proto) flags |= ECIL_FUNC_TYPE_NO_PROTO;
    if (has_this) {
      /* The object pointer comes first (spec 11.3). */
      params[i++] = known_type_id(f_implicit_this_param_type_of(t));
    }  /* if */
    if (list_params) {
      for (p = supp->param_type_list; p != NULL; p = p->next) {
        uint32_t  pg = known_type_id(p->type);

        if (no_proto) pg = ecil_promote_gid(frame, pg, p->type);
        params[i++] = pg;
      }  /* for */
    }  /* if */
    key[0] = (uint32_t)egk_func_type;
    key[1] = ret;
    key[2] = flags;
    for (i = 0; i < n; i++) key[3 + i] = params[i];
    gid = ecil_word_lookup(key, n + 3);
    if (gid == 0) {
      gid = ecil_new_gid(egk_func_type);
      if (gid != 0) {
        an_ecil_gid_info  *info = ecil_gid_info(gid);
        an_ecil_buffer    *saved;

        info->ref = ret;
        info->count = n;
        ecil_word_register(key, n + 3, gid);
        saved = ecil_frame_enter_decl(frame);
        ecil_rec_begin(frame, eop_ty_func);
        ecil_arg_uint(frame, gid);
        ecil_arg_uint(frame, ret);
        ecil_arg_uint(frame, flags);
        ecil_arg_array(frame, params, n);
        ecil_rec_end(frame);
        ecil_frame_leave_decl(frame, saved);
      }  /* if */
    }  /* if */
  }  /* if */
  free(key);
  free(params);
  return gid;
}  /* finish_function_type */


/* Records. */

static void note_pending_record(uint32_t gid, a_type_ptr type)
/*
Remember a record that was declared while still incomplete.
*/
{
  if (num_pending_records == pending_records_capacity) {
    size_t                  new_capacity = (pending_records_capacity == 0) ?
                                           32 : pending_records_capacity * 2;
    an_ecil_pending_record  *new_list = (an_ecil_pending_record *)realloc(
                              pending_records,
                              new_capacity * sizeof(an_ecil_pending_record));

    if (new_list != NULL) {
      pending_records = new_list;
      pending_records_capacity = new_capacity;
    }  /* if */
  }  /* if */
  if (num_pending_records < pending_records_capacity) {
    pending_records[num_pending_records].gid = gid;
    pending_records[num_pending_records].type = type;
    num_pending_records++;
  } else {
    ecil_note_unsupported("out of memory in the ECIL writer");
  }  /* if */
}  /* note_pending_record */


static a_boolean field_is_unnamed(a_field_ptr field)
/*
Return TRUE if the field has no name.
*/
{
  return field->source_corresp.name == NULL;
}  /* field_is_unnamed */


static a_base_class_ptr record_direct_bases(a_type_ptr t)
/*
The first of the direct base classes of a C++ class in declaration order, or
NULL (C records, and classes without bases).  The list of base classes also
has the indirect ones: follow next_direct from here.
*/
{
  a_base_class_ptr  bases = NULL;

  if (!C_mode() && is_immediate_class_type(t) &&
      class_type_supp(t) != NULL) {
    bases = class_type_supp(t)->direct_base_classes;
    while (bases != NULL && !bases->direct) bases = bases->next;
  }  /* if */
  return bases;
}  /* record_direct_bases */


a_boolean ecil_class_is_dynamic(a_type_ptr type)
/*
Return TRUE for a dynamic class (spec 12.2): a C++ class with a virtual
function of its own or in a base class, or with a virtual base class.  Its
record then has a TY_VPTR field, a VIRTUAL_BASE field or a BASE field of a
dynamic class.  (needs_virtual_function_table is not this: it looks at the
class's own virtual functions only.)
*/
{
  a_type_ptr  t = skip_typerefs(type);

  return !C_mode() && is_immediate_class_type(t) &&
         class_type_supp(t) != NULL &&
         (t->variant.class_struct_union.
                      any_virtual_functions_including_in_base_classes ||
          t->variant.class_struct_union.any_virtual_base_classes);
}  /* ecil_class_is_dynamic */


a_boolean ecil_class_has_own_vptr(a_type_ptr type)
/*
Return TRUE for a dynamic class that has a vptr of its own (a TY_VPTR field at
offset 0): one without a primary base.  A class whose primary base is dynamic
(a non-virtual base, or a virtual one) shares that base's vptr.
*/
{
  a_type_ptr  t = skip_typerefs(type);

  return ecil_class_is_dynamic(t) &&
         class_type_supp(t)->primary_base_class == NULL;
}  /* ecil_class_has_own_vptr */


static a_targ_size_t base_layout_size(a_type_ptr type)
/*
Where a base class subobject ends from its offset (spec 12.2): the nvsize of
its class if its CLASS_INFO was sent, else its data size (a class without
CLASS_INFO is laid out with nvsize = data_size), else its size.
*/
{
  uint32_t       bg = known_type_id(type);
  a_targ_size_t  size = size_of_type(type);

  if (bg != 0 && (ecil_gid_info(bg)->flags & ECIL_GF_COMPLETE) != 0) {
    size = (ecil_gid_info(bg)->flags & ECIL_GF_LAYOUT_FACTS) != 0 ?
           ecil_gid_info(bg)->nvsize : ecil_gid_info(bg)->data_size;
  }  /* if */
  return size;
}  /* base_layout_size */


a_targ_size_t ecil_data_size_of_type(a_type_ptr type)
/*
The Itanium C++ ABI data size of a type (dsize): the size, except for a class
that is not POD for the purpose of layout, where it is the end of the last
non-empty subobject: the vptr, a direct non-virtual base or a virtual base
(by its nvsize: spec 12.2), or a data member (by its size, or by its data size
for a [[no_unique_address]] member).  A class with virtual bases has as its
data size the largest end of any of them.  The front end's data_size_of_type
recurses into every class-type member, which gives a short value for a member
of a POD class type.  The data sizes of the subobject types are those sent
already.
*/
{
  a_type_ptr    t = skip_typerefs(type);
  a_targ_size_t result = size_of_type(type);

  if (!C_mode() && is_immediate_class_type(t) && !t->incomplete &&
      class_type_supp(t) != NULL &&
      !class_symbol_supp(symbol_for(t))->is_cpp03_POD) {
    a_class_type_supplement_ptr  ctsp = class_type_supp(t);
    a_base_class_ptr             base;
    a_field_ptr                  field;

    result = 0;
    if (ecil_class_has_own_vptr(t)) {
      result = (a_targ_size_t)ctsp->virtual_function_info_offset +
               targ_sizeof_pointer;
    }  /* if */
    for (base = record_direct_bases(t); base != NULL;
         base = base->next_direct) {
      if (!base->is_virtual && !base->is_optimized_empty_base) {
        a_targ_size_t  bsize = base_layout_size(base->type);
        a_targ_size_t  end = (a_targ_size_t)base->offset + bsize;

        if (bsize != 0 && end > result) result = end;
      }  /* if */
    }  /* for */
    for (field = t->variant.class_struct_union.field_list; field != NULL;
         field = field->next) {
      a_targ_size_t  size;
      a_targ_size_t  end;

      if (field->is_bit_field) {
        size = (a_targ_size_t)((field->offset_bit_remainder +
                                field->bit_size + 7) / 8);
      } else if (field->has_no_unique_address_attribute &&
                 is_immediate_class_type(skip_typerefs(field->type))) {
        uint32_t  fg = known_type_id(field->type);

        size = (fg != 0 && (ecil_gid_info(fg)->flags &
                            ECIL_GF_COMPLETE) != 0) ?
               ecil_gid_info(fg)->data_size : size_of_type(field->type);
        if (field->is_optimized_empty_class) size = 0;
      } else {
        size = size_of_type(field->type);
      }  /* if */
      end = (a_targ_size_t)field->offset + size;
      /* A zero-width bit-field ends the data too, at its (aligned) offset
         (Itanium; spec 11.3). */
      if ((size != 0 || field->is_bit_field) && end > result) result = end;
    }  /* for */
    if (t->variant.class_struct_union.any_virtual_base_classes) {
      /* The virtual bases (direct and indirect) end at their offset in the
         complete object plus their nvsize, if they are not empty. */
      for (base = ctsp->base_classes; base != NULL; base = base->next) {
        if (base->is_virtual && !is_empty_class_type(base->type)) {
          a_targ_size_t  bsize = base_layout_size(base->type);
          a_targ_size_t  end = (a_targ_size_t)base->offset + bsize;

          if (bsize != 0 && end > result) result = end;
        }  /* if */
      }  /* for */
    }  /* if */
  }  /* if */
  return result;
}  /* ecil_data_size_of_type */


static uint32_t ecil_vptr_type(an_ecil_frame *frame)
/*
The G-id of TY_VPTR (spec 12.2): the type of the vptr field, as big and as
aligned as a pointer.
*/
{
  uint32_t  key[2];
  uint32_t  gid;

  key[0] = (uint32_t)egk_vptr;
  key[1] = (uint32_t)targ_sizeof_pointer;
  gid = ecil_word_lookup(key, 2);
  if (gid == 0) {
    gid = ecil_new_gid(egk_vptr);
    if (gid != 0) {
      an_ecil_gid_info  *info = ecil_gid_info(gid);
      an_ecil_buffer    *saved;

      info->size = (uint32_t)targ_sizeof_pointer;
      info->align = (uint32_t)targ_sizeof_pointer;
      ecil_word_register(key, 2, gid);
      saved = ecil_frame_enter_decl(frame);
      ecil_rec1(frame, eop_ty_vptr, gid);
      ecil_frame_leave_decl(frame, saved);
    }  /* if */
  }  /* if */
  return gid;
}  /* ecil_vptr_type */


/* Class facts (spec 12.2). */

static a_boolean type_is_function_local(a_type_ptr type);

static a_boolean push_class(a_type_ptr t)
/*
Push a class on the class work stack; FALSE if memory ran out (the reason is
noted).
*/
{
  a_boolean  ok = TRUE;

  if (class_stack_size == class_stack_capacity) {
    size_t      new_capacity = (class_stack_capacity == 0) ?
                               32 : class_stack_capacity * 2;
    a_type_ptr  *new_stack = (a_type_ptr *)realloc(class_stack,
                                      new_capacity * sizeof(a_type_ptr));

    if (new_stack == NULL) {
      ecil_note_unsupported("out of memory in the ECIL writer");
      ok = FALSE;
    } else {
      class_stack = new_stack;
      class_stack_capacity = new_capacity;
    }  /* if */
  }  /* if */
  if (ok) class_stack[class_stack_size++] = t;
  return ok;
}  /* push_class */


static void note_dynamic_class(uint32_t gid)
/*
Remember a dynamic class whose record was sent: its vtable is decided later
(at the end of its function's region for a local class, else at the end of
the translation unit).
*/
{
  if (num_dynamic_classes == dynamic_classes_capacity) {
    size_t    new_capacity = (dynamic_classes_capacity == 0) ?
                             64 : dynamic_classes_capacity * 2;
    uint32_t  *new_list = (uint32_t *)realloc(dynamic_classes,
                                      new_capacity * sizeof(uint32_t));

    if (new_list != NULL) {
      dynamic_classes = new_list;
      dynamic_classes_capacity = new_capacity;
    }  /* if */
  }  /* if */
  if (num_dynamic_classes < dynamic_classes_capacity) {
    dynamic_classes[num_dynamic_classes++] = gid;
  } else {
    ecil_note_unsupported("out of memory in the ECIL writer");
  }  /* if */
}  /* note_dynamic_class */


static uint32_t intern_class_mangle(an_ecil_frame *frame, a_type_ptr t)
/*
The sid of the Itanium <type> mangling of a class (what follows _ZTV).
*/
{
  uint32_t  sid = 0;

#if BACK_END_TYPE == BACK_END_TYPE_CIR
  const char  *text = mangled_typeinfo_string(t);

  if (text != NULL) sid = ecil_intern_cstring(frame, text);
#else /* BACK_END_TYPE != BACK_END_TYPE_CIR */
  (void)frame;
  (void)t;
#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */
  return sid;
}  /* intern_class_mangle */


static void emit_class_mangle(an_ecil_frame *frame, uint32_t gid,
                              uint32_t sid)
/*
CLASS_MANGLE(t, sid) (introduction or re-issue).
*/
{
  an_ecil_buffer  *saved = ecil_frame_enter_decl(frame);

  ecil_rec2(frame, eop_class_mangle, gid, sid);
  ecil_frame_leave_decl(frame, saved);
  ecil_gid_info(gid)->decl_name = sid;
}  /* emit_class_mangle */


static void send_layout_facts(an_ecil_frame *frame, a_type_ptr t,
                              uint32_t gid)
/*
Send CLASS_MANGLE, CLASS_INFO and CLASS_BASES of a class whose record body was
sent: the layout facts (spec 12.2).  The facts of a dynamic base were sent with
its body; a plain base's are only sent when something needs them.
*/
{
  an_ecil_gid_info             *info = ecil_gid_info(gid);
  a_class_type_supplement_ptr  ctsp = class_type_supp(t);
  uint32_t                     sid;
  uint32_t                     *bases;
  size_t                       nbases = 0;
  size_t                       i = 0;
  a_base_class_ptr             base;
  a_boolean                    ok = TRUE;
  a_boolean                    has_primary = ctsp->primary_base_class != NULL;
  uint64_t                     primary_idx = 0;
  uint64_t                     nslots = 0;
  an_ecil_buffer               *saved;

  if ((info->flags & ECIL_GF_LAYOUT_FACTS) == 0) {
    for (base = record_direct_bases(t); base != NULL;
         base = base->next_direct) {
      nbases++;
    }  /* for */
    bases = (uint32_t *)malloc((2 * nbases + 1) * sizeof(uint32_t));
    sid = intern_class_mangle(frame, t);
    if (bases == NULL) {
      ecil_note_unsupported("out of memory in the ECIL writer");
      ok = FALSE;
    } else if (sid == 0) {
      ecil_note_unsupported("a class without a mangled name");
      ok = FALSE;
    } else {
      for (base = record_direct_bases(t); base != NULL;
           base = base->next_direct) {
        uint32_t  bg = known_type_id(base->type);

        if (ecil_gid_of(base) == 0 || bg == 0 ||
            (ecil_gid_info(bg)->flags & ECIL_GF_COMPLETE) == 0 ||
            ((ecil_gid_info(bg)->flags & ECIL_GF_DYNAMIC) != 0 &&
             (ecil_gid_info(bg)->flags & ECIL_GF_LAYOUT_FACTS) == 0)) {
          ecil_note_unsupported("a base class without a record body");
          ok = FALSE;
        } else {
          bases[i++] = ecil_gid_of(base) - 1u;
          bases[i++] = base->derivation != NULL &&
                       base->derivation->access ==
                                         (an_access_specifier)as_public ?
                       ECIL_CLASS_BASE_PUBLIC : 0u;
        }  /* if */
      }  /* for */
    }  /* if */
    if (ok && has_primary) {
      uint32_t  pidx = ecil_gid_of(ctsp->primary_base_class);

      if (pidx == 0) {
        ecil_note_unsupported("a primary base class that is not a direct "
                              "base");
        ok = FALSE;
      } else {
        primary_idx = pidx - 1u;
      }  /* if */
    }  /* if */
#if CHECKING
    /* The data size sent with the body is at most nvsize (without virtual
       bases), which is at most the size (spec 12.2: a displaced empty base
       makes nvsize larger). */
    check_assertion(!ok ||
                    ((t->variant.class_struct_union.any_virtual_base_classes ||
                      (uint64_t)ctsp->size_without_virtual_base_classes >=
                           (uint64_t)info->data_size) &&
                     (uint64_t)ctsp->size_without_virtual_base_classes <=
                           (uint64_t)t->size));
#endif /* CHECKING */
    if (ok && ecil_class_is_dynamic(t) &&
        ctsp->highest_virtual_function_number !=
                             (a_virtual_function_number)
                                       VIRTUAL_FUNCTION_NUMBER_NONE) {
      nslots = (uint64_t)ctsp->highest_virtual_function_number + 1u;
    }  /* if */
    if (ok) {
      saved = ecil_frame_enter_decl(frame);
      ecil_rec2(frame, eop_class_mangle, gid, sid);
      ecil_rec_begin(frame, eop_class_info);
      ecil_arg_uint(frame, gid);
      ecil_arg_uint(frame, (uint64_t)ctsp->size_without_virtual_base_classes);
      ecil_arg_uint(frame, (uint64_t)
                           ctsp->alignment_without_virtual_base_classes);
      if (has_primary) {
        ecil_arg_uint(frame, primary_idx);
      } else {
        ecil_arg_nil(frame);
      }  /* if */
      ecil_arg_uint(frame, nslots);
      ecil_rec_end(frame);
      ecil_rec_begin(frame, eop_class_bases);
      ecil_arg_uint(frame, gid);
      ecil_arg_array(frame, bases, i);
      ecil_rec_end(frame);
      ecil_frame_leave_decl(frame, saved);
      info = ecil_gid_info(gid);
      info->flags |= ECIL_GF_LAYOUT_FACTS;
      info->decl_name = sid;
      info->nvsize = (uint32_t)ctsp->size_without_virtual_base_classes;
    }  /* if */
    free(bases);
  }  /* if */
}  /* send_layout_facts */



static a_boolean vbase_is_primary_of_a_base(a_type_ptr t,
                                           a_base_class_ptr vbase)
/*
Return TRUE if the virtual base class vbase of the class t has no storage of
its own because it is the primary virtual base of t or of one of t's bases,
direct or indirect (clang's hasOwnStorage; spec 12.2): it lives inside the
storage of that base.  CHECKING asserts that the field then has the offset of
that base.
*/
{
  a_class_type_supplement_ptr  ctsp = class_type_supp(t);
  a_base_class_ptr             b;
  a_boolean                    primary = FALSE;
  a_type_ptr                   vt = skip_typerefs(vbase->type);

  if (ctsp->primary_base_class != NULL &&
      ctsp->primary_base_class->is_virtual &&
      skip_typerefs(ctsp->primary_base_class->type) == vt) {
    primary = TRUE;
#if CHECKING
    check_assertion(vbase->offset == 0);
#endif /* CHECKING */
  }  /* if */
  for (b = ctsp->base_classes; b != NULL && !primary; b = b->next) {
    a_class_type_supplement_ptr  bsup =
                                   class_type_supp(skip_typerefs(b->type));

    if (bsup != NULL && bsup->primary_base_class != NULL &&
        bsup->primary_base_class->is_virtual &&
        skip_typerefs(bsup->primary_base_class->type) == vt) {
      primary = TRUE;
#if CHECKING
      /* It sits at the offset of the base it is the primary base of (the
         first such base in the layout). */
      {
        a_boolean         found = FALSE;
        a_base_class_ptr  c;

        for (c = ctsp->base_classes; c != NULL && !found; c = c->next) {
          a_class_type_supplement_ptr  csup =
                                      class_type_supp(skip_typerefs(c->type));

          found = csup != NULL && csup->primary_base_class != NULL &&
                  csup->primary_base_class->is_virtual &&
                  skip_typerefs(csup->primary_base_class->type) == vt &&
                  c->offset == vbase->offset;
        }  /* for */
        check_assertion(found);
      }
#endif /* CHECKING */
    }  /* if */
  }  /* for */
  return primary;
}  /* vbase_is_primary_of_a_base */


static void emit_record_fields(an_ecil_frame *frame, a_type_ptr t,
                               uint32_t gid)
/*
Emit TY_RECORD_BODY and the REC_FIELD records of a complete record whose
field types all have ids; assign each field its member index.  A C++ class
lists its vptr (a TY_VPTR field) when it owns one, then its
direct bases in declaration order, then its data members (spec 11.3); the
members' indexes count the bases.
*/
{
  an_ecil_gid_info  *info;
  a_field_ptr       field;
  a_base_class_ptr  base;
  a_base_class_ptr  bases = record_direct_bases(t);
  uint64_t          n = 0;
  uint64_t          idx = 0;
  unsigned int      body_flags = 0;
  uint32_t          vptr_gid = 0;
  a_targ_size_t     data_size = t->size;
  an_ecil_buffer    *saved;

  for (field = t->variant.class_struct_union.field_list; field != NULL;
       field = field->next) {
    n++;
  }  /* for */
  if (!C_mode() && is_immediate_class_type(t) && class_type_supp(t) != NULL) {
    for (base = bases; base != NULL; base = base->next_direct) {
      if (!base->is_virtual) n++;
    }  /* for */
    for (base = class_type_supp(t)->base_classes; base != NULL;
         base = base->next) {
      if (base->is_virtual) n++;           /* A VIRTUAL_BASE field. */
    }  /* for */
    if (ecil_class_has_own_vptr(t)) {
      vptr_gid = ecil_vptr_type(frame);
      n++;
    }  /* if */
    data_size = ecil_data_size_of_type(t);
    if (ecil_class_is_nontrivial_abi(t)) {
      body_flags |= ECIL_RECORD_NONTRIVIAL_ABI;
    }  /* if */
    if (ecil_class_has_nontrivial_dtor(t)) {
      body_flags |= ECIL_RECORD_NONTRIVIAL_DTOR;
    }  /* if */
  }  /* if */
  if (t->variant.class_struct_union.is_packed) {
    body_flags |= ECIL_RECORD_PACKED;
  }  /* if */
  if (t->variant.class_struct_union.contains_flexible_array_member) {
    body_flags |= ECIL_RECORD_FLEXIBLE_ARRAY;
  }  /* if */
  info = ecil_gid_info(gid);
  info->count = n;
  info->data_size = (uint32_t)data_size;
  /* A record declared while incomplete had no size or alignment yet. */
  info->size = (uint64_t)t->size;
  info->align = (uint32_t)t->alignment;
  if (ecil_class_is_dynamic(t)) info->flags |= ECIL_GF_DYNAMIC;
#if CHECKING
  if (!C_mode() && is_immediate_class_type(t)) {
    /* The class is dynamic iff its record has a TY_VPTR field, a VIRTUAL_BASE
       field or a BASE field of a dynamic class (spec 12.2). */
    a_boolean  stream_dynamic = vptr_gid != 0 ||
                    t->variant.class_struct_union.any_virtual_base_classes;

    for (base = bases; base != NULL && !stream_dynamic;
         base = base->next_direct) {
      uint32_t  bg = known_type_id(base->type);

      stream_dynamic = bg != 0 &&
                       (ecil_gid_info(bg)->flags & ECIL_GF_DYNAMIC) != 0;
    }  /* for */
    check_assertion(stream_dynamic == (ecil_class_is_dynamic(t) != 0));
  }  /* if */
#endif /* CHECKING */
  /* Intern the names first: a STR must not fall between the body record and
     its REC_FIELD records. */
  for (field = t->variant.class_struct_union.field_list; field != NULL;
       field = field->next) {
    if (!field_is_unnamed(field)) {
      (void)ecil_intern_cstring(frame, field->source_corresp.name);
    }  /* if */
  }  /* for */
  info = ecil_gid_info(gid);
  saved = ecil_frame_enter_decl(frame);
  ecil_rec_begin(frame, eop_ty_record_body);
  ecil_arg_uint(frame, gid);
  ecil_arg_uint(frame, (uint64_t)t->size);
  ecil_arg_uint(frame, (uint64_t)data_size);
  ecil_arg_uint(frame, (uint64_t)t->alignment);
  ecil_arg_uint(frame, body_flags);
  ecil_arg_uint(frame, n);
  ecil_rec_end(frame);
  if (vptr_gid != 0) {
    /* The vptr: an unnamed TY_VPTR field at offset 0 (spec 12.2). */
    ecil_rec_begin(frame, eop_rec_field);
    ecil_arg_uint(frame, vptr_gid);
    ecil_arg_uint(frame, (uint64_t)class_type_supp(t)->
                                              virtual_function_info_offset);
    ecil_arg_nil(frame);
    ecil_arg_nil(frame);
    ecil_arg_nil(frame);
    ecil_arg_uint(frame, ECIL_FIELD_UNNAMED);
    ecil_rec_end(frame);
    idx++;
  }  /* if */
  for (base = bases; base != NULL; base = base->next_direct) {
    uint32_t      bg = known_type_id(base->type);
    unsigned int  flags = ECIL_FIELD_BASE | ECIL_FIELD_UNNAMED;

    if (base->is_virtual) continue;      /* After the data members. */
    ecil_set_gid(base, (uint32_t)(idx + 1));
    idx++;
    if (bg != 0 && (ecil_gid_info(bg)->flags &
                    (ECIL_GF_OPAQUE | ECIL_GF_ABI_OPAQUE)) != 0) {
      /* A base with opaque storage (a vptr, a _BitInt member, ...) makes
         the class ABI-opaque too (spec 8.11), whatever the depth. */
      ecil_gid_info(gid)->flags |= ECIL_GF_ABI_OPAQUE;
    }  /* if */
    if (bg != 0) {
      ecil_gid_info(gid)->flags |= ecil_gid_info(bg)->flags &
                                   ECIL_GF_HAS_DMP;
    }  /* if */
    if (base->is_optimized_empty_base) flags |= ECIL_FIELD_EMPTY_FOR_ABI;
    ecil_rec_begin(frame, eop_rec_field);
    ecil_arg_uint(frame, bg);
    ecil_arg_uint(frame, (uint64_t)base->offset);
    ecil_arg_nil(frame);
    ecil_arg_nil(frame);
    ecil_arg_nil(frame);
    ecil_arg_uint(frame, flags);
    ecil_rec_end(frame);
  }  /* for */
  for (field = t->variant.class_struct_union.field_list; field != NULL;
       field = field->next) {
    uint32_t      fg = known_type_id(field->type);
    unsigned int  flags = 0;
    uint32_t      name_sid = 0;

    ecil_set_gid(field, (uint32_t)(idx + 1));
    idx++;
    if (field_is_unnamed(field)) {
      flags |= ECIL_FIELD_UNNAMED;
    } else {
      name_sid = ecil_intern_cstring(frame, field->source_corresp.name);
    }  /* if */
    if (field->is_bit_field && field->bit_field_is_signed) {
      flags |= ECIL_FIELD_BITFIELD_SIGNED;
    }  /* if */
    if (fg != 0) {
      an_ecil_gid_info  *finfo = ecil_gid_info(fg);

      info = ecil_gid_info(gid);
      if (finfo->flags & (ECIL_GF_OPAQUE | ECIL_GF_ABI_OPAQUE)) {
        info->flags |= ECIL_GF_ABI_OPAQUE;
      }  /* if */
      info->flags |= finfo->flags & ECIL_GF_HAS_DMP;
      if (field->has_no_unique_address_attribute && !C_mode() &&
          finfo->kind == egk_record) {
        /* A [[no_unique_address]] member is laid out with the base-subobject
           type; an empty one is empty for the ABI as well. */
        flags |= ECIL_FIELD_POTENTIALLY_OVERLAPPING;
        if (field->is_optimized_empty_class) flags |= ECIL_FIELD_EMPTY_FOR_ABI;
      } else if (finfo->kind == egk_record && finfo->size == 0 &&
                 (finfo->flags & ECIL_GF_COMPLETE) && C_mode()) {
        flags |= ECIL_FIELD_EMPTY_FOR_ABI;
      }  /* if */
      if (finfo->kind == egk_array && finfo->size == 0) {
        /* Zero-length arrays are empty for the ABI; the last one is also
           a flexible array member (spec 8.2 layout rules). */
        flags |= ECIL_FIELD_EMPTY_FOR_ABI;
        if (field->next == NULL) flags |= ECIL_FIELD_FLEXIBLE_ARRAY;
      }  /* if */
    }  /* if */
    ecil_rec_begin(frame, eop_rec_field);
    ecil_arg_uint(frame, fg);
    ecil_arg_uint(frame, (uint64_t)field->offset);
    if (field->is_bit_field) {
      ecil_arg_uint(frame, field->offset_bit_remainder);
      ecil_arg_uint(frame, field->bit_size);
    } else {
      ecil_arg_nil(frame);
      ecil_arg_nil(frame);
    }  /* if */
    if (name_sid != 0) {
      ecil_arg_uint(frame, name_sid);
    } else {
      ecil_arg_nil(frame);
    }  /* if */
    ecil_arg_uint(frame, flags);
    ecil_rec_end(frame);
  }  /* for */
  if (!C_mode() && is_immediate_class_type(t) && class_type_supp(t) != NULL) {
    /* The virtual bases, direct and indirect, in the order the front end
       constructs them (its postorder base list: also the order of the
       MEMINIT(vbase) groups of C1), after the data members, at their offsets
       in the complete object (spec 12.2). */
    for (base = class_type_supp(t)->base_classes; base != NULL;
         base = base->next) {
      if (base->is_virtual) {
        uint32_t      bg = known_type_id(base->type);
        unsigned int  flags = ECIL_FIELD_VIRTUAL_BASE | ECIL_FIELD_UNNAMED;

        ecil_set_gid(base, (uint32_t)(idx + 1));
        idx++;
        if (bg != 0 && (ecil_gid_info(bg)->flags &
                        (ECIL_GF_OPAQUE | ECIL_GF_ABI_OPAQUE)) != 0) {
          ecil_gid_info(gid)->flags |= ECIL_GF_ABI_OPAQUE;
        }  /* if */
        if (bg != 0) {
          ecil_gid_info(gid)->flags |= ecil_gid_info(bg)->flags &
                                       ECIL_GF_HAS_DMP;
        }  /* if */
        if (is_empty_class_type(base->type) ||
            vbase_is_primary_of_a_base(t, base)) {
          /* No storage of its own (spec 12.2). */
          flags |= ECIL_FIELD_EMPTY_FOR_ABI;
        }  /* if */
        ecil_rec_begin(frame, eop_rec_field);
        ecil_arg_uint(frame, bg);
        ecil_arg_uint(frame, (uint64_t)base->offset);
        ecil_arg_nil(frame);
        ecil_arg_nil(frame);
        ecil_arg_nil(frame);
        ecil_arg_uint(frame, flags);
        ecil_rec_end(frame);
      }  /* if */
    }  /* for */
  }  /* if */
  ecil_frame_leave_decl(frame, saved);
  ecil_gid_info(gid)->flags |= ECIL_GF_COMPLETE;
  if (ecil_gid_info(gid)->flags & ECIL_GF_DYNAMIC) {
    /* The layout facts of a dynamic class follow its body directly (the
       library needs them to build the record, spec 12.2). */
    note_dynamic_class(gid);
    send_layout_facts(frame, t, gid);
  }  /* if */
}  /* emit_record_fields */


static void emit_record_body(an_ecil_frame *frame, a_type_ptr t, uint32_t gid)
/*
Emit the body of a complete record whose field types all have ids, unless
a member or base is a record that failed: then this record fails too (it keeps
its declaration only, and uses that need it complete are unsupported).
*/
{
  a_field_ptr       field;
  a_base_class_ptr  base;
  a_boolean         failed = FALSE;

  if (ecil_class_has_own_vptr(t) &&
      class_type_supp(t)->virtual_function_info_offset != 0) {
    failed = TRUE;                /* (never seen: the vptr is at 0.) */
  }  /* if */
  for (base = record_direct_bases(t); base != NULL && !failed;
       base = base->next_direct) {
    uint32_t  bg = known_type_id(base->type);

    failed = bg != 0 && (ecil_gid_info(bg)->flags & ECIL_GF_FAILED) != 0;
  }  /* for */
  for (field = t->variant.class_struct_union.field_list;
       field != NULL && !failed; field = field->next) {
    uint32_t  fg = known_type_id(field->type);

    failed = fg != 0 && (ecil_gid_info(fg)->flags & ECIL_GF_FAILED) != 0;
    if (!failed && !C_mode() && field->has_no_unique_address_attribute &&
        fg != 0 && is_union_type(field->type) &&
        ecil_data_size_of_type(field->type) < ecil_gid_info(fg)->size) {
      /* A union with reusable tail padding laid out as a base-subobject
         type is not supported in v1 (spec 11.3). */
      failed = TRUE;
    }  /* if */
  }  /* for */
  if (failed) {
    ecil_gid_info(gid)->flags |= ECIL_GF_FAILED | ECIL_GF_ABI_OPAQUE;
  } else {
    emit_record_fields(frame, t, gid);
  }  /* if */
}  /* emit_record_body */


static a_boolean has_bit_precise_bit_field(a_type_ptr t)
/*
Return TRUE if the class, struct or union has a bit-field declared with a
_BitInt type.
*/
{
  a_field_ptr  field;
  a_boolean    found = FALSE;

  for (field = t->variant.class_struct_union.field_list;
       field != NULL && !found; field = field->next) {
    a_type_ptr  ft = skip_typerefs(field->type);

    found = field->is_bit_field && ft->kind == tk_integer &&
            (ft->variant.integer.int_kind == ik_bit_precise ||
             ft->variant.integer.int_kind == ik_unsigned_bit_precise);
  }  /* for */
  return found;
}  /* has_bit_precise_bit_field */


static void run_type_stack(an_ecil_frame *frame, size_t base)
/*
Process the work stack down to depth base.
*/
{
  while (type_stack_size > base && ecil_unsupported_reason == NULL) {
    size_t            idx = type_stack_size - 1;
    a_type_ptr        t = type_stack[idx].type;
    uint32_t          gid = ecil_gid_of(t);

    if (gid != 0 && type_stack[idx].phase == tph_start) {
      type_stack_size--;          /* Already introduced. */
    } else if (type_stack[idx].phase == tph_start) {
      switch (t->kind) {
        case tk_void:
          ecil_set_gid(t, ecil_void_type(frame));
          type_stack_size--;
          break;
        case tk_integer:
          if (t->variant.integer.bool_type) {
            gid = ecil_bool_type(frame);
          } else if (t->variant.integer.int_kind == ik_bit_precise ||
                     t->variant.integer.int_kind ==
                                              ik_unsigned_bit_precise) {
            gid = ecil_opaque_type(frame, t);
          } else {
            gid = ecil_intern_int_type(frame,
                       (unsigned int)(t->size * (a_targ_size_t)targ_char_bit),
                       int_kind_is_signed[(int)t->variant.integer.int_kind] ?
                                                             TRUE : FALSE,
                       (unsigned int)t->size, (unsigned int)t->alignment);
          }  /* if */
          ecil_set_gid(t, gid);
          type_stack_size--;
          break;
        case tk_nullptr:
          /* C23 nullptr_t: a null pointer, laid out as void *. */
          ecil_set_gid(t, ecil_ptr_type(frame, ecil_void_type(frame)));
          type_stack_size--;
          break;
        case tk_float:
          { unsigned int  format = ecil_float_format_of(t);

            if (format == 0xFFFF) {
              gid = ecil_opaque_type(frame, t);
            } else {
              gid = ecil_intern_float_type(frame, format,
                                           (unsigned int)t->size,
                                           (unsigned int)t->alignment);
            }  /* if */
          }
          ecil_set_gid(t, gid);
          type_stack_size--;
          break;
        case tk_pointer:
          /* A reference is a pointer to its referent (spec 11.3). */
          type_stack[idx].phase = tph_children;
          break;
        case tk_array:
          if (t->variant.array.is_vla ||
              t->variant.array.is_variable_size_array ||
              t->variant.array.is_template_dependent_size_array) {
            ecil_note_unsupported("variable length array");
          } else {
            type_stack[idx].phase = tph_children;
          }  /* if */
          break;
        case tk_routine:
          type_stack[idx].phase = tph_children;
          type_stack[idx].param_cursor =
                                      rout_type_supp(t)->param_type_list;
          break;
        case tk_ptr_to_member:
          /* TY_DATA_MEMBER or TY_METHOD of the member's type and the class
             (both only declared, spec 12.2). */
          type_stack[idx].phase = tph_children;
          break;
        case tk_struct:
        case tk_union:
        case tk_class:
          if (!t->incomplete && has_bit_precise_bit_field(t)) {
            /* The library cannot describe a bit-field of a _BitInt type:
               the record is opaque storage of the right size and
               alignment, and value-level uses are unsupported. */
            ecil_set_gid(t, ecil_opaque_type(frame, t));
            type_stack_size--;
            gid = 0;
          } else {
            gid = ecil_new_gid(egk_record);
          }  /* if */
          if (gid != 0) {
            uint32_t      name_sid = 0;
            an_ecil_gid_info
                          *info = ecil_gid_info(gid);
            an_ecil_buffer
                          *saved;

            info->entity = t;
            info->size = (uint64_t)t->size;
            info->align = (uint32_t)t->alignment;
            if (type_is_function_local(t)) {
              /* Its IL entry is gone when the file scope is complete. */
              info->flags |= ECIL_GF_FUNCTION_LOCAL;
            }  /* if */
            ecil_set_gid(t, gid);
            if (t->source_corresp.name != NULL) {
              name_sid = ecil_intern_cstring(frame, t->source_corresp.name);
            }  /* if */
            saved = ecil_frame_enter_decl(frame);
            ecil_rec_begin(frame, eop_ty_record_decl);
            ecil_arg_uint(frame, gid);
            ecil_arg_uint(frame, t->kind == tk_union ?
                                  (unsigned int)erk_union :
                                  (t->kind == tk_class ?
                                   (unsigned int)erk_class :
                                   (unsigned int)erk_struct));
            if (name_sid != 0) {
              ecil_arg_uint(frame, name_sid);
            } else {
              ecil_arg_nil(frame);
            }  /* if */
            ecil_rec_end(frame);
            ecil_frame_leave_decl(frame, saved);
            if (t->incomplete || type_stack[idx].decl_only) {
              if (!t->incomplete) {
                num_deferred_records++;
                pending_records_dirty = TRUE;
              }  /* if */
              note_pending_record(gid, t);
              type_stack_size--;
            } else {
              type_stack[idx].phase = tph_fields;
              type_stack[idx].base_cursor = record_direct_bases(t);
              type_stack[idx].field_cursor =
                                  t->variant.class_struct_union.field_list;
            }  /* if */
          }  /* if */
          break;
        default:
          ecil_set_gid(t, ecil_opaque_type(frame, t));
          type_stack_size--;
          break;
      }  /* switch */
    } else if (type_stack[idx].phase == tph_children) {
      if (t->kind == tk_pointer) {
        if (need_pointee(t->variant.pointer.type)) {
          idx = type_stack_size - 1;
          ecil_set_gid(t, finish_pointer(frame, t));
          type_stack_size--;
        }  /* if */
      } else if (t->kind == tk_array) {
        if (need_type(t->variant.array.element_type)) {
          idx = type_stack_size - 1;
          ecil_set_gid(t, finish_array(frame, t));
          type_stack_size--;
        }  /* if */
      } else if (t->kind == tk_ptr_to_member) {
        if (need_pointee(pm_member_type(t))) {
          idx = type_stack_size - 1;
          if (need_pointee(pm_class_type(t))) {
            idx = type_stack_size - 1;
            ecil_set_gid(t, finish_member_pointer(frame, t));
            type_stack_size--;
          }  /* if */
        }  /* if */
      } else {
        /* A function type: the return type, then each parameter. */
        a_boolean complete = TRUE;

        if (!type_stack[idx].ret_done) {
          if (need_pointee(t->variant.routine.return_type)) {
            idx = type_stack_size - 1;
            type_stack[idx].ret_done = TRUE;
          } else {
            complete = FALSE;
          }  /* if */
        }  /* if */
        if (complete && !type_stack[idx].this_done) {
          if (!C_mode() && routine_type_is_nonstatic_member_function(t)) {
            if (need_pointee(f_implicit_this_param_type_of(t))) {
              idx = type_stack_size - 1;
              type_stack[idx].this_done = TRUE;
            } else {
              complete = FALSE;
            }  /* if */
          } else {
            type_stack[idx].this_done = TRUE;
          }  /* if */
        }  /* if */
        while (complete && type_stack[idx].param_cursor != NULL) {
          if (need_pointee(type_stack[idx].param_cursor->type)) {
            idx = type_stack_size - 1;
            type_stack[idx].param_cursor =
                                      type_stack[idx].param_cursor->next;
          } else {
            complete = FALSE;
          }  /* if */
        }  /* while */
        if (complete) {
          ecil_set_gid(t, finish_function_type(frame, t, FALSE));
          type_stack_size--;
        }  /* if */
      }  /* if */
    } else {
      /* tph_fields: visit each field type, then emit the body. */
      a_boolean complete = TRUE;

      while (complete && type_stack[idx].base_cursor != NULL) {
        if (need_type(type_stack[idx].base_cursor->type)) {
          idx = type_stack_size - 1;
          type_stack[idx].base_cursor =
                                  type_stack[idx].base_cursor->next_direct;
        } else {
          complete = FALSE;
        }  /* if */
      }  /* while */
      while (complete && type_stack[idx].field_cursor != NULL) {
        if (need_type(type_stack[idx].field_cursor->type)) {
          idx = type_stack_size - 1;
          type_stack[idx].field_cursor = type_stack[idx].field_cursor->next;
        } else {
          complete = FALSE;
        }  /* if */
      }  /* while */
      if (complete) {
        emit_record_body(frame, t, gid);
        type_stack_size--;
      }  /* if */
    }  /* if */
  }  /* while */
  if (ecil_unsupported_reason != NULL) {
    size_t  i;

    /* Records in progress keep their gid but never get a body: mark them,
       so that uses that need a complete record are diagnosed. */
    for (i = base; i < type_stack_size; i++) {
      a_type_ptr  st = type_stack[i].type;

      if ((st->kind == tk_struct || st->kind == tk_union ||
           st->kind == tk_class) && ecil_gid_of(st) != 0) {
        ecil_gid_info(ecil_gid_of(st))->flags |= (ECIL_GF_FAILED |
                                                  ECIL_GF_ABI_OPAQUE);
      }  /* if */
    }  /* for */
    type_stack_size = base;
  }  /* if */
}  /* run_type_stack */


uint32_t ecil_type_id(an_ecil_frame *frame, a_type_ptr type)
/*
Return the G-id of the canonical ECIL type for an IL type, introducing it
(and everything it needs) in the declaration section.  Returns 0 (with
ecil_unsupported_reason set) if the type cannot be represented.
*/
{
  uint32_t  gid = known_type_id(type);

  if (gid == 0 && ecil_unsupported_reason == NULL) {
    size_t  base = type_stack_size;

    if (push_type(type, tph_start) != NULL) run_type_stack(frame, base);
    gid = known_type_id(type);
    if (gid != 0 && skip_typerefs(type) != type) ecil_set_gid(type, gid);
    if (base == 0 && num_deferred_records != 0 &&
        ecil_unsupported_reason == NULL) {
      /* Records that were only declared (pointees) get their bodies now. */
      num_deferred_records = 0;
      ecil_complete_pending_records(frame);
    }  /* if */
  }  /* if */
  return gid;
}  /* ecil_type_id */


static a_boolean record_has_atomic_member(a_type_ptr t)
/*
Return TRUE if a member of the record has an _Atomic-qualified type.
*/
{
  a_field_ptr  field;
  a_boolean    found = FALSE;

  for (field = t->variant.class_struct_union.field_list;
       field != NULL && !found; field = field->next) {
    a_type_ptr  ft = field->type;

    while (ft->kind == tk_typeref && !found) {
      found = (ft->variant.typeref.qualifiers & TQ_C11_ATOMIC) != 0;
      ft = ft->variant.typeref.type;
    }  /* while */
  }  /* for */
  return found;
}  /* record_has_atomic_member */


static a_boolean type_is_function_local(a_type_ptr type)
/*
Return TRUE for a class that lives in a function's memory region: local to a
function, or nested in a class that is.
*/
{
  a_boolean  local = type->source_corresp.is_local_to_function;

  while (!local && type->source_corresp.is_class_member) {
    type = parent_class_of(type);
    local = type->source_corresp.is_local_to_function;
  }  /* while */
  return local;
}  /* type_is_function_local */


void ecil_forget_local_pending_records(void)
/*
A function's memory region is about to be freed: forget the incomplete
records that were declared in it (they cannot be completed later, and their
IL entries are gone).
*/
{
  size_t  i;
  size_t  kept = 0;

  for (i = 0; i < num_pending_records; i++) {
    if (!type_is_function_local(pending_records[i].type)) {
      pending_records[kept++] = pending_records[i];
    }  /* if */
  }  /* for */
  num_pending_records = kept;
}  /* ecil_forget_local_pending_records */


void ecil_note_class_completed(void)
/*
A class was completed (the front end called do_class_layout for it): a record
that was declared incomplete may be complete now.
*/
{
  pending_records_dirty = TRUE;
}  /* ecil_note_class_completed */


void ecil_complete_pending_records(an_ecil_frame *frame)
/*
Emit the body of every record that was declared incomplete and has been
completed since.  Those still incomplete stay pending.  Nothing is scanned
unless a class was completed (or a complete record is waiting for its
order) since the last scan.  A record the stream
cannot describe (an _Atomic member, a _BitInt bit-field, an unsupported
member) is marked failed instead; that never makes the caller's own
construct unsupported, so the reason of the caller is kept aside.  Running a
record's field types can assign G-ids and move the G-id table, so the
record's an_ecil_gid_info is looked up again after it.
*/
{
  size_t                i;
  size_t                kept = 0;
  an_ecil_saved_reason  caller_reason;

#if CHECKING
  if (num_pending_records != 0 && !pending_records_dirty) {
    /* The shortcut is sound: no pending record is complete. */
    for (i = 0; i < num_pending_records; i++) {
      check_assertion(pending_records[i].type->incomplete);
    }  /* for */
  }  /* if */
#endif /* CHECKING */
  if (num_pending_records != 0 && pending_records_dirty) {
    pending_records_dirty = FALSE;
    ecil_save_reason(&caller_reason);
    for (i = 0; i < num_pending_records; i++) {
      uint32_t    gid = pending_records[i].gid;
      a_type_ptr  t = pending_records[i].type;

      if (t->incomplete) {
        pending_records[kept++] = pending_records[i];
      } else if ((ecil_gid_info(gid)->flags &
                  (ECIL_GF_COMPLETE | ECIL_GF_FAILED)) != 0) {
        /* Its body was sent already (it was needed by value first). */
      } else if (has_bit_precise_bit_field(t) || record_has_atomic_member(t)) {
        ecil_gid_info(gid)->flags |= ECIL_GF_FAILED | ECIL_GF_ABI_OPAQUE;
      } else {
        size_t            base = type_stack_size;
        a_type_work_item  *item = push_type(t, tph_fields);

        if (item != NULL) {
          item->base_cursor = record_direct_bases(t);
          item->field_cursor = t->variant.class_struct_union.field_list;
          run_type_stack(frame, base);
        }  /* if */
        if (ecil_unsupported_reason != NULL) {
          /* run_type_stack marked the record failed. */
          ecil_gid_info(gid)->flags |= ECIL_GF_FAILED | ECIL_GF_ABI_OPAQUE;
          ecil_unsupported_reason = NULL;
        }  /* if */
      }  /* if */
    }  /* for */
    num_pending_records = kept;
    ecil_restore_reason(&caller_reason);
  }  /* if */
}  /* ecil_complete_pending_records */


uint32_t ecil_func_type_id(an_ecil_frame *frame,
                           a_type_ptr    routine_type,
                           a_boolean     promote_params)
/*
Return the G-id of the ECIL function type of a routine type.  With
promote_params, an unprototyped type (a K&R definition) lists its
default-promoted parameter types.
*/
{
  a_type_ptr  t = skip_typerefs(routine_type);
  uint32_t    gid = 0;

  if (!promote_params || rout_type_supp(t)->prototyped) {
    gid = ecil_type_id(frame, t);
  } else {
    /* Make sure every component has an id, then build the promoted form. */
    a_param_type_ptr  p;

    gid = ecil_type_id(frame, t);
    (void)ecil_type_id(frame, t->variant.routine.return_type);
    for (p = rout_type_supp(t)->param_type_list; p != NULL; p = p->next) {
      (void)ecil_type_id(frame, p->type);
    }  /* for */
    if (ecil_unsupported_reason == NULL) {
      gid = finish_function_type(frame, t, TRUE);
    }  /* if */
  }  /* if */
  return gid;
}  /* ecil_func_type_id */


/* Declarations. */

a_boolean ecil_routine_is_filtered(a_routine_ptr rout)
/*
Return TRUE if the routine must not be sent (the writer's equivalent of
ignore_routine_in_back_end, which is FALSE without IL lowering).
*/
{
  return rout->is_prototype_instantiation ||
         special_kind_is(rout, sfk_deduction_guide) || rout->is_consteval;
}  /* ecil_routine_is_filtered */


static a_boolean class_copy_move_ctors_all_deleted(a_type_ptr type)
/*
Return TRUE if the class declares copy or move constructors and every one of
them is deleted (Itanium C++ ABI 3.1.2.3: such a class is non-trivial for the
purposes of calls).
*/
{
  a_class_type_supplement_ptr ctsp = class_type_supp(type);
  a_routine_ptr               rp;
  a_boolean                   seen = FALSE;
  a_boolean                   usable = FALSE;

  if (ctsp != NULL && ctsp->assoc_scope != NULL) {
    for (rp = ctsp->assoc_scope->routines; rp != NULL; rp = rp->next) {
      a_type_qualifier_set  qualifiers;

      if (rp->special_kind == (a_special_function_kind)sfk_constructor &&
          is_copy_constructor(rp, type, &qualifiers, /*include_move_ctors=*/
                              TRUE, /*is_declarative_context=*/FALSE)) {
        seen = TRUE;
        if (!rp->is_deleted) usable = TRUE;
      }  /* if */
    }  /* for */
  }  /* if */
  return seen && !usable;
}  /* class_copy_move_ctors_all_deleted */


a_boolean ecil_class_is_nontrivial_abi(a_type_ptr type)
/*
Return TRUE if the type is a class that is passed and returned indirectly:
not bitwise copyable, a deleted or non-trivial destructor, or all copy and
move constructors deleted (ECIL spec 11.3).  The type must be complete to
answer TRUE.
*/
{
  a_boolean                      result = FALSE;
  a_class_symbol_supplement_ptr  cssp;

  type = skip_typerefs(type);
  if (is_immediate_class_type(type) && !type->incomplete &&
      (cssp = symbol_supplement_for_class(type)) != NULL) {
    result = !cssp->construction_by_bitwise_copy_allowed ||
             has_deleted_or_nontrivial_destructor(cssp) ||
             class_copy_move_ctors_all_deleted(type);
  }  /* if */
  return result;
}  /* ecil_class_is_nontrivial_abi */


a_boolean ecil_class_has_nontrivial_dtor(a_type_ptr type)
/*
Return TRUE if the type is a class with a non-trivial destructor.
*/
{
  a_boolean                      result = FALSE;
  a_class_symbol_supplement_ptr  cssp;

  type = skip_typerefs(type);
  if (is_immediate_class_type(type) && !type->incomplete &&
      (cssp = symbol_supplement_for_class(type)) != NULL) {
    result = has_nontrivial_destructor(cssp);
  }  /* if */
  return result;
}  /* ecil_class_has_nontrivial_dtor */


/* The libc/libm functions that are also builtins are listed in
   builtin_libc_names (builtin_libc.h, included above): a call to __builtin_X
   of one of them is an ordinary CALL of X (design.md P2). */


/* The builtins ECIL has records for, by name without the __builtin_ prefix,
   with the id the call is translated by (an an_ecil_builtin value or an
   ECIL_SPECIAL_* id).  The type-suffix variants are listed out. */
typedef struct a_special_name {
  const char    *name;
  unsigned int  id;
} a_special_name;

#define SPECIAL_INT_FAMILY(base, id) \
  { base, id }, { base "l", id }, { base "ll", id }, { base "imax", id }, \
  { base "g", id }
#define SPECIAL_FP_FAMILY(base, id) \
  { base, id }, { base "f", id }, { base "l", id }

static const a_special_name special_names[] = {
  { "expect", (unsigned int)ebi_expect },
  { "unreachable", (unsigned int)ebi_unreachable },
  { "trap", (unsigned int)ebi_trap },
  { "alloca", (unsigned int)ebi_alloca },
  { "bswap16", (unsigned int)ebi_bswap },
  { "bswap32", (unsigned int)ebi_bswap },
  { "bswap64", (unsigned int)ebi_bswap },
  { "bswap128", (unsigned int)ebi_bswap },
  SPECIAL_INT_FAMILY("clz", (unsigned int)ebi_clz),
  SPECIAL_INT_FAMILY("ctz", (unsigned int)ebi_ctz),
  SPECIAL_INT_FAMILY("popcount", (unsigned int)ebi_popcount),
  SPECIAL_INT_FAMILY("parity", (unsigned int)ebi_parity),
  SPECIAL_INT_FAMILY("ffs", (unsigned int)ebi_ffs),
  SPECIAL_INT_FAMILY("clrsb", (unsigned int)ebi_clrsb),
  /* These two override the "g" variants of the families above. */
  { "clzg", ECIL_SPECIAL_CLZG },
  { "ctzg", ECIL_SPECIAL_CTZG },
  { "add_overflow", (unsigned int)ebi_add_overflow },
  { "sub_overflow", (unsigned int)ebi_sub_overflow },
  { "mul_overflow", (unsigned int)ebi_mul_overflow },
  SPECIAL_FP_FAMILY("isnan", ECIL_SPECIAL_FP_ISNAN),
  { "isinf_sign", ECIL_SPECIAL_FP_ISINF_SIGN },
  SPECIAL_FP_FAMILY("isinf", ECIL_SPECIAL_FP_ISINF),
  SPECIAL_FP_FAMILY("isfinite", ECIL_SPECIAL_FP_ISFINITE),
  SPECIAL_FP_FAMILY("isnormal", ECIL_SPECIAL_FP_ISNORMAL),
  SPECIAL_FP_FAMILY("issignaling", ECIL_SPECIAL_FP_ISSIGNALING),
  SPECIAL_FP_FAMILY("signbit", ECIL_SPECIAL_FP_SIGNBIT),
  { "fpclassify", ECIL_SPECIAL_FP_FPCLASSIFY },
  { "isless", ECIL_SPECIAL_FP_ISLESS },
  { "isgreater", ECIL_SPECIAL_FP_ISGREATER },
  { "islessequal", ECIL_SPECIAL_FP_ISLESSEQUAL },
  { "isgreaterequal", ECIL_SPECIAL_FP_ISGREATEREQUAL },
  { "islessgreater", ECIL_SPECIAL_FP_ISLESSGREATER },
  { "isunordered", ECIL_SPECIAL_FP_ISUNORDERED },
  { "c23_va_start", ECIL_SPECIAL_C23_VA_START },
  { "alloca_with_align", ECIL_SPECIAL_ALLOCA_ALIGN },
  { "add_overflow_p", ECIL_SPECIAL_ADD_OVERFLOW_P },
  { "sub_overflow_p", ECIL_SPECIAL_SUB_OVERFLOW_P },
  { "mul_overflow_p", ECIL_SPECIAL_MUL_OVERFLOW_P },
  { NULL, 0 }
};

/* An entry of the name table: what a name is, as a builtin and as a libc
   function. */
typedef struct a_name_entry {
  const char    *name;            /* NULL: an empty slot. */
  unsigned int  special_id;       /* 0 if none. */
  a_boolean     library;
} a_name_entry;

#define NAME_TABLE_SLOTS 2048u    /* A power of two, above twice the names
                                     (the libc names and the specials). */

STATIC_THREAD a_name_entry
                *name_table;


static uint32_t hash_name(const char *name)
/*
FNV-1a of a NUL-terminated name (indexes the name table only).
*/
{
  uint32_t  hash = 2166136261u;

  for (; *name != '\0'; name++) {
    hash ^= (unsigned char)*name;
    hash *= 16777619u;
  }  /* for */
  return hash;
}  /* hash_name */


static a_name_entry *name_entry_slot(a_name_entry *table, const char *name)
/*
Return the slot of the name in the table: its entry, or the empty slot where
it belongs.
*/
{
  uint32_t  slot = hash_name(name) & (NAME_TABLE_SLOTS - 1);

  while (table[slot].name != NULL && strcmp(table[slot].name, name) != 0) {
    slot = (slot + 1) & (NAME_TABLE_SLOTS - 1);
  }  /* while */
  return &table[slot];
}  /* name_entry_slot */


static void free_name_table(void)
/*
Release the name table.
*/
{
  free(name_table);
  name_table = NULL;
}  /* free_name_table */


static a_boolean build_name_table(void)
/*
Build the name table on first use; return FALSE if memory ran out.
*/
{
  if (name_table == NULL) {
    a_name_entry  *table = (a_name_entry *)calloc(NAME_TABLE_SLOTS,
                                                  sizeof(a_name_entry));
    size_t        i;

    if (table != NULL) {
      for (i = 0; builtin_libc_names[i] != NULL; i++) {
        a_name_entry  *e = name_entry_slot(table, builtin_libc_names[i]);

        e->name = builtin_libc_names[i];
        e->library = TRUE;
      }  /* for */
      for (i = 0; special_names[i].name != NULL; i++) {
        a_name_entry  *e = name_entry_slot(table, special_names[i].name);

        e->name = special_names[i].name;
        e->special_id = special_names[i].id;
      }  /* for */
      name_table = table;
    }  /* if */
  }  /* if */
  return name_table != NULL;
}  /* build_name_table */


void ecil_classify_function_name(const char *name, an_ecil_name_class *cls)
/*
Classify a routine name with one hashed lookup.  For __builtin_X, special_id
is the id of the builtin ECIL has a record for (else 0) and library_name is
X when X is a libc function; is_library_function says that the name as
written is a libc function.
*/
{
  cls->special_id = 0;
  cls->library_name = NULL;
  cls->is_library_function = FALSE;
  if (build_name_table()) {
    if (strncmp(name, "__builtin_", 10) == 0) {
      const char          *base = name + 10;
      const a_name_entry  *e = name_entry_slot(name_table, base);

      if (e->name != NULL) {
        cls->special_id = e->special_id;
        if (e->library) cls->library_name = base;
      }  /* if */
    } else {
      const a_name_entry  *e = name_entry_slot(name_table, name);

      cls->is_library_function = e->name != NULL && e->library;
    }  /* if */
  }  /* if */
}  /* ecil_classify_function_name */


/* The facts a DECL_FUNC carries. */
typedef struct a_func_state {
  uint32_t      type;
  unsigned int  linkage;
  unsigned int  flags;
  uint32_t      name;
  a_boolean     late;             /* The name is not final: nil for now. */
  a_boolean     noinline;         /* GNU noinline: supp key 14, sticky. */
} a_func_state;

/* TRUE while the late names are being resolved (the file scope is complete,
   so every name is final). */
STATIC_THREAD a_boolean
                resolving_late_names;


static a_boolean routine_is_function_local(a_routine_ptr rout)
/*
Return TRUE for a routine that lives in a function's memory region: a
function local to a function, or a member of a (possibly nested) class that
is local to one.  Such an entity is gone when the file scope is complete.
*/
{
  a_source_correspondence  *scp = &rout->source_corresp;
  a_boolean                result = scp->is_local_to_function;

  if (!result && scp->is_class_member) {
    a_type_ptr  cls = parent_class_of(rout);

    while (!cls->source_corresp.is_local_to_function &&
           cls->source_corresp.is_class_member) {
      cls = parent_class_of(cls);
    }  /* while */
    result = cls->source_corresp.is_local_to_function;
  }  /* if */
  return result;
}  /* routine_is_function_local */


static const char *routine_symbol_text(a_routine_ptr rout,
                                       unsigned int  variant)
/*
Return the symbol name of a routine: its asm name if it has one, else its
Itanium mangled name in C++ (the plain name for main and extern "C"
routines), else the C name.  For the base variant of a constructor or
destructor it is the C2/D2 name.  The text may be in a temporary buffer: copy
it at once.
*/
{
  const char  *name = ecil_entity_name(&rout->source_corresp);
  a_boolean   has_asm_name = FALSE;

#if GNU_EXTENSIONS_ALLOWED || REDEFINE_EXTNAME_PRAGMA_ENABLED
  if (rout->gnu_extra_info != NULL && rout->gnu_extra_info->asm_name != NULL) {
    name = rout->gnu_extra_info->asm_name;
    has_asm_name = TRUE;
  }  /* if */
#endif /* GNU_EXTENSIONS_ALLOWED || REDEFINE_EXTNAME_PRAGMA_ENABLED */
#if BACK_END_TYPE == BACK_END_TYPE_CIR
  if (!C_mode() && !has_asm_name &&
      (name != NULL || variant != ECIL_VARIANT_NONE) &&
      !ecil_routine_is_filtered(rout)) {
    /* (The special members of a closure type have no source name.) */
    if (variant == ECIL_VARIANT_BASE) {
      name = cir_structor_name(rout, cdk_subobject);
    } else if (variant == ECIL_VARIANT_DELETING) {
      name = cir_structor_name(rout, cdk_deleting);
    } else {
      name = get_mangled_function_name_full(rout, /*force_primary_name=*/FALSE,
                                      /*externalize_if_necessary=*/FALSE);
    }  /* if */
  }  /* if */
#else /* BACK_END_TYPE != BACK_END_TYPE_CIR */
  (void)has_asm_name;
  (void)variant;
#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */
  return name;
}  /* routine_symbol_text */


static uint32_t routine_name_sid(an_ecil_frame *frame,
                                 a_routine_ptr rout,
                                 unsigned int  variant,
                                 a_boolean     *late)
/*
Return the sid of the symbol name of a routine, or 0 if it has none or it
may still change (*late is then TRUE).
*/
{
  const char  *name;

  *late = FALSE;
#if BACK_END_TYPE == BACK_END_TYPE_CIR
  if (!C_mode() && !resolving_late_names &&
      !(rout->gnu_extra_info != NULL &&
        rout->gnu_extra_info->asm_name != NULL) &&
      cir_routine_name_may_change(rout)) {
    *late = TRUE;
  }  /* if */
#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */
  name = *late ? NULL : routine_symbol_text(rout, variant);
  if (name != NULL && strncmp(name, "__builtin_", 10) == 0) {
    an_ecil_name_class  cls;

    ecil_classify_function_name(name, &cls);
    if (cls.library_name != NULL) name = cls.library_name;
  }  /* if */
  return name == NULL ? 0 : ecil_intern_cstring(frame, name);
}  /* routine_name_sid */


#define SYMBOL_BINDING_TAG 0x53594D42U


static uint32_t symbol_binding_lookup(uint32_t name_sid,
                                      an_ecil_gid_kind kind)
/*
Return the G-id already bound to the symbol name (spec 7: two G-ids never
bind one symbol name), or 0 if none or if it is of another kind.
*/
{
  uint32_t  words[2];
  uint32_t  gid;

  words[0] = SYMBOL_BINDING_TAG;
  words[1] = name_sid;
  gid = ecil_word_lookup(words, 2);
  if (gid != 0 && ecil_gid_info(gid)->kind != kind) gid = 0;
  return gid;
}  /* symbol_binding_lookup */


static void symbol_binding_unregister(uint32_t name_sid, uint32_t gid)
/*
Record that the symbol name is no longer bound to the G-id (the entity was
renamed): a later entity with that symbol name gets a G-id of its own.  Does
nothing if the name is bound to another G-id.
*/
{
  uint32_t  words[2];

  words[0] = SYMBOL_BINDING_TAG;
  words[1] = name_sid;
  if (ecil_word_lookup(words, 2) == gid) ecil_word_unregister(words, 2);
}  /* symbol_binding_unregister */


static void symbol_binding_register(uint32_t name_sid, uint32_t gid)
/*
Record that the symbol name is bound to the G-id.
*/
{
  uint32_t  words[2];

  words[0] = SYMBOL_BINDING_TAG;
  words[1] = name_sid;
  if (ecil_word_lookup(words, 2) == 0) ecil_word_register(words, 2, gid);
}  /* symbol_binding_register */


void ecil_bind_symbol(uint32_t name_sid, uint32_t gid)
/*
Record, for other modules, that the symbol name is bound to the G-id.
*/
{
  symbol_binding_register(name_sid, gid);
}  /* ecil_bind_symbol */


static a_boolean routine_is_multiversioned(a_routine_ptr rout)
/*
Return TRUE for a routine of GNU function multiversioning (the "target"
attribute): the representative of the versions or one of them.
*/
{
#if GNU_FUNCTION_MULTIVERSIONING
  return has_gnu_routine_supp(rout) &&
         (gnu_routine_supp(rout)->is_representative ||
          gnu_routine_supp(rout)->is_target_specific_version);
#else /* !GNU_FUNCTION_MULTIVERSIONING */
  (void)rout;
  return FALSE;
#endif /* GNU_FUNCTION_MULTIVERSIONING */
}  /* routine_is_multiversioned */


static void check_routine_attributes(a_routine_ptr rout)
/*
Note as unsupported the GNU routine attributes whose meaning the code
generator cannot reproduce by ignoring them (constructor, destructor, alias,
ifunc, weakref).  Attributes that only affect optimization (noipa, noclone,
used, unused, cold, hot, always_inline, pure, const, ...) are ignorable and
need no code here; noinline is sent as a supp key of DECL_FUNC (ECIL 1.3).
*/
{
#if GNU_EXTENSIONS_ALLOWED
  if (rout->is_initialization_routine || rout->is_finalization_routine) {
    ecil_note_unsupported("a constructor or destructor attribute");
  } else if (rout->is_gnu_alias || rout->is_ifunc || rout->is_weakref) {
    ecil_note_unsupported("an alias, ifunc or weakref attribute");
  } else if (routine_is_multiversioned(rout)) {
    ecil_note_unsupported("GNU function multiversioning");
  }  /* if */
#else /* !GNU_EXTENSIONS_ALLOWED */
  (void)rout;
#endif /* GNU_EXTENSIONS_ALLOWED */
}  /* check_routine_attributes */


void ecil_check_variable_attributes(a_variable_ptr var)
/*
Note as unsupported the GNU variable attributes that cannot be ignored
(cleanup, alias, weakref, init_priority).
*/
{
#if GNU_EXTENSIONS_ALLOWED
  if (var->cleanup_routine != NULL) {
    ecil_note_unsupported("a cleanup attribute");
  } else if (var->is_gnu_alias || var->is_weakref) {
    ecil_note_unsupported("an alias or weakref attribute");
#if GNU_INIT_PRIORITY_ATTRIBUTE_ALLOWED
  } else if (var->init_priority != 0) {
    ecil_note_unsupported("an init_priority attribute");
#endif /* GNU_INIT_PRIORITY_ATTRIBUTE_ALLOWED */
  }  /* if */
#else /* !GNU_EXTENSIONS_ALLOWED */
  (void)var;
#endif /* GNU_EXTENSIONS_ALLOWED */
}  /* ecil_check_variable_attributes */


static a_boolean routine_is_explicit_instantiation_definition(
                                                         a_routine_ptr rout)
/*
Return TRUE for a function template instance defined by an explicit
instantiation (not an extern template).
*/
{
  a_boolean     result = FALSE;
  a_symbol_ptr  sym = (a_symbol_ptr)rout->source_corresp.assoc_info;

  if (!C_mode() && sym != NULL &&
      (symbol_is(sym, sk_routine) || symbol_is(sym, sk_member_function)) &&
      sym->variant.routine.instance_ptr != NULL) {
    a_template_instance_ptr  tip = sym->variant.routine.instance_ptr;

    result = tip->explicit_instantiation && !tip->explicit_do_not_instantiate;
  }  /* if */
  return result;
}  /* routine_is_explicit_instantiation_definition */


static void compute_func_state(an_ecil_frame *frame,
                               a_routine_ptr rout,
                               uint32_t      gid,
                               unsigned int  variant,
                               a_boolean     defining,
                               a_boolean     unsupported,
                               a_func_state  *state)
/*
Compute the DECL_FUNC facts for a routine as they are now.  gid is 0 for a
routine that has no G-id yet (or while the late names are resolved: the name
is computed); otherwise, in C++, the name sent before is kept (the routine's
entities may be gone, and a C++ name does not change after it is sent).
*/
{
  a_type_ptr    t = skip_typerefs(rout->type);
  /* The static invoker of a lambda has the linkage of its call operator
     (spec 11.9). */
  a_routine_ptr lrout = rout;
  a_boolean     nested_unnamed = FALSE;
  a_boolean     local_vague = FALSE;

  if (!C_mode() && rout->special_kind ==
                          (a_special_function_kind)sfk_lambda_entry_point &&
      rout->variant.lambda_call_operator != NULL) {
    lrout = rout->variant.lambda_call_operator;
  }  /* if */
  check_routine_attributes(rout);
  state->type = ecil_func_type_id(frame, rout->type,
                                  defining && !rout_type_supp(t)->prototyped);
  state->linkage = ecil_linkage_for(lrout->storage_class,
                                    lrout->source_corresp.name_linkage,
                                    lrout->is_weak);
#if BACK_END_TYPE == BACK_END_TYPE_CIR
  nested_unnamed = !C_mode() && cir_routine_in_nested_unnamed_class(lrout);
  if (nested_unnamed) {
    /* Not internal: its linkage is that of the enclosing class (below). */
    state->linkage = ecil_linkage_for((a_storage_class)sc_unspecified,
                                      (unsigned int)nlk_external,
                                      lrout->is_weak);
  } else if (!C_mode() && routine_is_function_local(lrout) &&
             ecil_routine_has_vague_linkage(lrout)) {
    /* A routine that lives in a discardable function (a local class's
       member, a lambda's call operator) has no linkage of its own in the
       front end; it has that of the function (below). */
    local_vague = TRUE;
    state->linkage = ecil_linkage_for((a_storage_class)sc_unspecified,
                                      (unsigned int)nlk_external,
                                      lrout->is_weak);
  }  /* if */
#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */
  state->flags = 0;
  if (defining && !unsupported) state->flags |= ECIL_DECL_FUNC_DEFINED_HERE;
#if BACK_END_TYPE == BACK_END_TYPE_CIR
  if (!C_mode() && state->linkage == (unsigned int)elk_external &&
      cir_entity_is_tu_local(iek_routine, lrout)) {
    /* Its name would be the same in another translation unit, where it is
       another entity (spec 11.2). */
    state->linkage = (unsigned int)elk_internal;
  }  /* if */
  if (!C_mode() && defining && !unsupported &&
      state->linkage == (unsigned int)elk_external &&
      (lrout->storage_class == (a_storage_class)sc_unspecified ||
       nested_unnamed || local_vague)) {
    /* Vague linkage (spec 11.2): internal wins, and a declaration is
       external. */
    if (!local_vague && routine_is_explicit_instantiation_definition(lrout)) {
      state->linkage = (unsigned int)elk_weak_odr;
      state->flags |= ECIL_DECL_FUNC_COMDAT;
    } else if (local_vague || (nested_unnamed ? (lrout->is_inline != 0) :
               routine_might_exist_in_multiple_copies(lrout))) {
      /* (A member of a class that is declared in a class is defined in its
         class, so it is inline.) */
      state->linkage = (unsigned int)elk_linkonce_odr;
      state->flags |= ECIL_DECL_FUNC_COMDAT;
    }  /* if */
  }  /* if */
#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */
  if (rout->is_inline) state->flags |= ECIL_DECL_FUNC_INLINE;
  if (rout_type_supp(t)->does_not_return) {
    state->flags |= ECIL_DECL_FUNC_NORETURN;
  }  /* if */
  if (rout == il_header.main_routine) state->flags |= ECIL_DECL_FUNC_IS_MAIN;
  if (defining && (rout->definition_for_inlining_only ||
                   rout->suppress_inline_body)) {
    state->flags |= ECIL_DECL_FUNC_INLINE_ONLY;
  }  /* if */
  if (unsupported) state->flags |= ECIL_DECL_FUNC_UNSUPPORTED;
#if GNU_EXTENSIONS_ALLOWED || MICROSOFT_EXTENSIONS_ALLOWED
  state->noinline = rout->never_inline;
#else /* !(GNU_EXTENSIONS_ALLOWED || MICROSOFT_EXTENSIONS_ALLOWED) */
  state->noinline = FALSE;
#endif /* GNU_EXTENSIONS_ALLOWED || MICROSOFT_EXTENSIONS_ALLOWED */
  if (ecil_has_exceptions()) {
    /* NOTHROW only from the front end's own answer, and never taken back by
       a re-issue (a wrong NOTHROW skips cleanups: spec 12.6). */
    if ((gid != 0 && (ecil_gid_info(gid)->decl_flags &
                                      ECIL_DECL_FUNC_NOTHROW) != 0) ||
        is_non_throwing_routine(rout)) {
      state->flags |= ECIL_DECL_FUNC_NOTHROW;
    }  /* if */
  }  /* if */
  if (gid != 0 && !C_mode()) {
    an_ecil_gid_info  *info = ecil_gid_info(gid);

    state->name = info->decl_name;
    state->late = (info->flags & ECIL_GF_LATE_NAME) != 0 &&
                  (info->flags & ECIL_GF_NAME_BOUND) == 0;
  } else {
    state->name = routine_name_sid(frame, rout, variant, &state->late);
  }  /* if */
  if (state->late && routine_is_function_local(rout)) {
    /* The entity will not be there to name at the end. */
    ecil_note_unsupported("a local entity whose name is not final");
  }  /* if */
  if (state->name != 0 && state->linkage != (unsigned int)elk_internal) {
    ecil_mark_symbol_name(state->name);
  }  /* if */
}  /* compute_func_state */


static void emit_func_decl_if_changed(an_ecil_frame     *frame,
                                      uint32_t          gid,
                                      const a_func_state *new_state)
/*
Emit DECL_FUNC if this is the first one or any fact differs from the last
one emitted (spec 8.2: a re-issue updates the entity).
*/
{
  an_ecil_gid_info  *info = ecil_gid_info(gid);
  a_func_state      sticky = *new_state;
  const a_func_state *state = &sticky;

  if (info->decl_emitted) {
    /* A re-issue never takes NOTHROW back (spec 12.6), nor noinline (the GNU
       attributes of the declarations of a function add up, spec 6). */
    sticky.flags |= info->decl_flags & ECIL_DECL_FUNC_NOTHROW;
    sticky.noinline = sticky.noinline || info->decl_noinline;
  }  /* if */
  if (!info->decl_emitted || info->decl_type != state->type ||
      info->decl_linkage != state->linkage ||
      info->decl_flags != state->flags || info->decl_name != state->name ||
      info->decl_noinline != state->noinline) {
    an_ecil_buffer  *saved = ecil_frame_enter_decl(frame);

    ecil_rec_begin(frame, eop_decl_func);
    ecil_arg_uint(frame, gid);
    ecil_arg_uint(frame, state->type);
    ecil_arg_uint(frame, state->linkage);
    ecil_arg_uint(frame, state->flags);
    if (state->name != 0) {
      ecil_arg_uint(frame, state->name);
    } else {
      ecil_arg_nil(frame);
    }  /* if */
    {
      an_ecil_supp  supp;

      ecil_supp_begin(&supp);
      if ((state->flags & ECIL_DECL_FUNC_UNSUPPORTED) != 0) {
        add_drop_reason(&supp, gid);
      }  /* if */
      if (state->noinline) ecil_supp_true(&supp, esk_noinline);
      ecil_rec_supp_end(frame, &supp);
    }
    ecil_rec_end(frame);
    ecil_frame_leave_decl(frame, saved);
    if (info->decl_emitted && state->name != 0 &&
        state->name != info->decl_name &&
        state->linkage != (unsigned int)elk_internal) {
      /* The entity was renamed (an asm label declared after a first use):
         the new name belongs to it from now on and the old one is free
         (spec 7, effective name). */
      if (info->decl_name != 0 &&
          info->decl_linkage != (unsigned int)elk_internal) {
        symbol_binding_unregister(info->decl_name, gid);
      }  /* if */
      symbol_binding_register(state->name, gid);
    }  /* if */
    info->decl_type = state->type;
    info->decl_linkage = state->linkage;
    info->decl_flags = state->flags;
    info->decl_name = state->name;
    info->decl_noinline = state->noinline;
    info->decl_emitted = TRUE;
  }  /* if */
}  /* emit_func_decl_if_changed */


uint32_t ecil_throw_bad_array_new_length_function(an_ecil_frame *frame)
/*
The G-id of the runtime function that throws std::bad_array_new_length
(__cxa_throw_bad_array_new_length: no parameters, no return), declared on
first use: a new[] whose run-time count is less than the number of its
initializers calls it (spec 12.5).
*/
{
  if (bad_array_function_gid == 0) {
    uint32_t  ret = ecil_void_type(frame);
    uint32_t  key[3];
    uint32_t  ft;
    uint32_t  gid;

    key[0] = (uint32_t)egk_func_type;
    key[1] = ret;
    key[2] = 0;
    ft = ecil_word_lookup(key, 3);
    if (ft == 0) {
      ft = ecil_new_gid(egk_func_type);
      if (ft != 0) {
        an_ecil_buffer  *saved;

        ecil_gid_info(ft)->ref = ret;
        ecil_gid_info(ft)->count = 0;
        ecil_word_register(key, 3, ft);
        saved = ecil_frame_enter_decl(frame);
        ecil_rec_begin(frame, eop_ty_func);
        ecil_arg_uint(frame, ft);
        ecil_arg_uint(frame, ret);
        ecil_arg_uint(frame, 0);
        ecil_arg_array(frame, NULL, 0);
        ecil_rec_end(frame);
        ecil_frame_leave_decl(frame, saved);
      }  /* if */
    }  /* if */
    gid = ft != 0 ? ecil_new_gid(egk_func) : 0;
    if (gid != 0) {
      a_func_state  state;

      state.type = ft;
      state.linkage = (unsigned int)elk_external;
      state.flags = ECIL_DECL_FUNC_NORETURN;
      state.name = ecil_intern_cstring(frame,
                                       "__cxa_throw_bad_array_new_length");
      state.late = FALSE;
      state.noinline = FALSE;
      ecil_mark_symbol_name(state.name);
      emit_func_decl_if_changed(frame, gid, &state);
      symbol_binding_register(state.name, gid);
      bad_array_function_gid = gid;
    }  /* if */
  }  /* if */
  return bad_array_function_gid;
}  /* ecil_throw_bad_array_new_length_function */


static a_boolean routine_is_structor(a_routine_ptr rout)
/*
Return TRUE for a C++ constructor or destructor: it has a complete and a
base variant (spec 11.4).
*/
{
  return !C_mode() &&
         (rout->special_kind == (a_special_function_kind)sfk_constructor ||
          rout->special_kind == (a_special_function_kind)sfk_destructor);
}  /* routine_is_structor */


static void introduce_base_variant(an_ecil_frame *frame,
                                   a_routine_ptr rout,
                                   uint32_t      complete_gid)
/*
Introduce the base variant (C2/D2) of the constructor or destructor whose
complete variant has just been sent, with the same type, linkage and flags,
and pair the two with STRUCTOR (declaration section).
*/
{
  a_func_state  state;
  uint32_t      base_gid;
  a_boolean     has_vbases = FALSE;

  if (!C_mode() && rout->source_corresp.is_class_member) {
    /* STRUCTOR has_vbases is set iff the class has VIRTUAL_BASE fields, and
       the record body comes before it (spec 12.3). */
    a_type_ptr  cls = parent_class_of(rout);

    has_vbases = is_immediate_class_type(cls) &&
                 cls->variant.class_struct_union.any_virtual_base_classes;
    if (has_vbases) {
      uint32_t  cg = ecil_type_id(frame, cls);

      /* (A record that failed has no body to match.) */
      has_vbases = cg != 0 && (ecil_gid_info(cg)->flags &
                               ECIL_GF_COMPLETE) != 0;
    }  /* if */
  }  /* if */
  compute_func_state(frame, rout, 0, ECIL_VARIANT_BASE, FALSE, FALSE, &state);
  if (ecil_unsupported_reason == NULL) {
    base_gid = ecil_new_gid(egk_func);
    if (base_gid != 0) {
      an_ecil_gid_info  *info = ecil_gid_info(base_gid);
      an_ecil_buffer    *saved;

      info->entity = rout;
      info->variant = ECIL_VARIANT_BASE;
      info->partner = complete_gid;
      if (has_vbases) {
        info->flags |= ECIL_GF_STRUCTOR_VBASES;
        ecil_gid_info(complete_gid)->flags |= ECIL_GF_STRUCTOR_VBASES;
      }  /* if */
      info->flags |= ecil_gid_info(complete_gid)->flags &
                     ECIL_GF_FUNCTION_LOCAL;
      if (state.late) info->flags |= ECIL_GF_LATE_NAME;
      ecil_gid_info(complete_gid)->partner = base_gid;
      emit_func_decl_if_changed(frame, base_gid, &state);
      if (state.name != 0 && state.linkage != (unsigned int)elk_internal) {
        symbol_binding_register(state.name, base_gid);
      }  /* if */
      saved = ecil_frame_enter_decl(frame);
      ecil_rec_begin(frame, eop_structor);
      ecil_arg_uint(frame, complete_gid);
      ecil_arg_uint(frame, base_gid);
      ecil_arg_uint(frame, (rout->special_kind ==
                            (a_special_function_kind)sfk_destructor ?
                            ECIL_STRUCTOR_DTOR : 0u) |
                           (has_vbases ? ECIL_STRUCTOR_HAS_VBASES : 0u));
      ecil_rec_end(frame);
      ecil_frame_leave_decl(frame, saved);
    }  /* if */
  }  /* if */
}  /* introduce_base_variant */


a_boolean ecil_symbol_clash = FALSE;

uint32_t ecil_function_id(an_ecil_frame *frame, a_routine_ptr rout)
/*
Return the G-id of a routine, introducing it with DECL_FUNC at its first
reference (as a declaration: the definition's facts are re-issued before
FUNC_BEGIN).  The type is introduced first, so ids appear in the stream in
increasing order.  A constructor or destructor is introduced with both its
variants and the result is the complete one.
*/
{
  uint32_t  gid = ecil_gid_of(rout);

  if (gid == 0 && ecil_unsupported_reason == NULL) {
    a_func_state  state;

    compute_func_state(frame, rout, 0, routine_is_structor(rout) ?
                       ECIL_VARIANT_COMPLETE : ECIL_VARIANT_NONE, FALSE, FALSE,
                       &state);
    if (ecil_unsupported_reason == NULL &&
        state.linkage != (unsigned int)elk_internal && state.name != 0) {
      gid = symbol_binding_lookup(state.name, egk_func);
      if (gid != 0) {
        /* Another IL routine (e.g. __builtin_memcpy) has this symbol:
           share its G-id; a differing type is a re-issue. */
        an_ecil_gid_info  *info = ecil_gid_info(gid);

        if (info->entity != (void *)rout &&
            (routine_is_structor(rout) ||
             info->variant != ECIL_VARIANT_NONE)) {
          /* The variants of a constructor or destructor are paired G-ids
             that cannot be shared with another function (spec 11.4). */
          ecil_note_unsupported("a function with the symbol name of a "
                                "constructor or destructor");
          ecil_symbol_clash = TRUE;
          gid = 0;
        } else if (info->decl_type != state.type &&
                   (info->flags & ECIL_GF_DEFINED) != 0) {
          /* The body was sent with the other type, and the type of a
             function cannot change after its body (spec 8.2). */
          ecil_note_unsupported("a function with the symbol name of a "
                                "defined function of another type");
          ecil_symbol_clash = TRUE;
          gid = 0;
        } else {
          ecil_set_gid(rout, gid);
          if (info->decl_type != state.type) {
            state.flags = info->decl_flags;
            state.linkage = info->decl_linkage;
            emit_func_decl_if_changed(frame, gid, &state);
          }  /* if */
        }  /* if */
      }  /* if */
    }  /* if */
    if (gid == 0 && ecil_unsupported_reason == NULL) {
      gid = ecil_new_gid(egk_func);
      if (gid != 0) {
        ecil_gid_info(gid)->entity = rout;
        if (state.late) ecil_gid_info(gid)->flags |= ECIL_GF_LATE_NAME;
        if (routine_is_structor(rout)) {
          ecil_gid_info(gid)->variant = ECIL_VARIANT_COMPLETE;
        }  /* if */
        if (!C_mode() && routine_is_function_local(rout)) {
          ecil_gid_info(gid)->flags |= ECIL_GF_FUNCTION_LOCAL;
        }  /* if */
        if (!C_mode() && rout->special_kind ==
                          (a_special_function_kind)sfk_lambda_entry_point) {
          /* Its body is synthesized after the function being translated. */
          ecil_queue_invoker(rout);
        }  /* if */
        ecil_set_gid(rout, gid);
        emit_func_decl_if_changed(frame, gid, &state);
        if (state.name != 0 && state.linkage != (unsigned int)elk_internal) {
          symbol_binding_register(state.name, gid);
        }  /* if */
        if (routine_is_structor(rout)) {
          introduce_base_variant(frame, rout, gid);
        }  /* if */
      }  /* if */
    }  /* if */
  }  /* if */
  return gid;
}  /* ecil_function_id */


uint32_t ecil_function_base_id(an_ecil_frame *frame, a_routine_ptr rout)
/*
The G-id of the base variant of a constructor or destructor (C2/D2), which
the calls for base subobjects and the body use; any other routine's own.
*/
{
  uint32_t  gid = ecil_function_id(frame, rout);

  if (gid != 0 && ecil_gid_info(gid)->partner != 0) {
    gid = ecil_gid_info(gid)->partner;
  }  /* if */
  return gid;
}  /* ecil_function_base_id */


uint32_t ecil_function_body_id(uint32_t gid)
/*
The G-id a definition streams its body under: the base variant of a
constructor or destructor (C1/D1 never has a body of its own).
*/
{
  an_ecil_gid_info  *info = ecil_gid_info(gid);

  return (info->variant == ECIL_VARIANT_COMPLETE && info->partner != 0) ?
         info->partner : gid;
}  /* ecil_function_body_id */


static void reissue_one_function(an_ecil_frame *frame,
                                 uint32_t      gid,
                                 a_boolean     defining,
                                 a_boolean     unsupported)
/*
Re-issue DECL_FUNC of one G-id (see ecil_reissue_function).
*/
{
  a_func_state  state;
  a_routine_ptr rout = (a_routine_ptr)ecil_gid_info(gid)->entity;

  if (rout != NULL) {            /* (A runtime function has no routine.) */
    compute_func_state(frame, rout, gid, ecil_gid_info(gid)->variant,
                       defining, unsupported, &state);
    if (ecil_gid_info(gid)->flags & ECIL_GF_LATE_NAME) {
      /* Keep the (nil or bound) name; NAME_BIND supplies it. */
      state.name = ecil_gid_info(gid)->decl_name;
    }  /* if */
  }  /* if */
  if (rout != NULL && ecil_unsupported_reason == NULL) {
    if ((ecil_gid_info(gid)->flags & ECIL_GF_DEFINED) && !unsupported) {
      /* Once the body has been streamed its type must not change (a K&R
         definition later redeclared with a prototype keeps the type of
         the definition). */
      state.type = ecil_gid_info(gid)->decl_type;
    }  /* if */
    emit_func_decl_if_changed(frame, gid, &state);
    if (defining && !unsupported) {
      ecil_gid_info(gid)->flags |= ECIL_GF_DEFINED;
    } else if (unsupported) {
      ecil_gid_info(gid)->flags &= ~ECIL_GF_DEFINED;
    }  /* if */
  }  /* if */
}  /* reissue_one_function */


void ecil_reissue_function(an_ecil_frame *frame,
                           uint32_t      gid,
                           a_boolean     defining,
                           a_boolean     unsupported)
/*
Re-issue DECL_FUNC for a function whose definition is about to start (or was
dropped as unsupported), if its facts changed.  The two variants of a
constructor or destructor are re-issued together, with the same facts (the
complete variant is defined through the base one: spec 11.4).
*/
{
  uint32_t  partner = ecil_gid_info(gid)->partner;
  uint32_t  deleting = ecil_gid_info(gid)->variant == ECIL_VARIANT_COMPLETE ?
                       ecil_gid_info(gid)->aux : 0;

  reissue_one_function(frame, gid, defining, unsupported);
  if (partner != 0) {
    reissue_one_function(frame, partner, defining, unsupported);
  }  /* if */
  if (deleting != 0) {
    /* D0 is defined iff D1 is (spec 12.3). */
    reissue_one_function(frame, deleting, defining, unsupported);
  }  /* if */
}  /* ecil_reissue_function */


/* Virtual functions and the vtable facts (spec 12.2, 12.3). */

/* A growable array of 32-bit words (the elements of a record's array). */
typedef struct a_word_vec {
  uint32_t      *words;
  size_t        size;
  size_t        capacity;
} a_word_vec;

static void word_vec_push(a_word_vec *vec, uint32_t word)
/*
Append a word; out of memory is noted as unsupported.
*/
{
  if (vec->size == vec->capacity) {
    size_t    new_capacity = (vec->capacity == 0) ? 64 : vec->capacity * 2;
    uint32_t  *new_words = (uint32_t *)realloc(vec->words,
                                         new_capacity * sizeof(uint32_t));

    if (new_words == NULL) {
      ecil_note_unsupported("out of memory in the ECIL writer");
    } else {
      vec->words = new_words;
      vec->capacity = new_capacity;
    }  /* if */
  }  /* if */
  if (vec->size < vec->capacity) vec->words[vec->size++] = word;
}  /* word_vec_push */


/* One virtual function of a class, for CLASS_VFUNCS. */
typedef struct a_vfunc {
  a_routine_ptr rout;
  uint32_t      gid;
  uint32_t      slot;
  unsigned int  flags;
  uint32_t      sig;
  a_boolean     implicit;
  size_t        order;            /* Declaration order. */
} a_vfunc;

static a_boolean same_virtual_signature(a_routine_ptr a, a_routine_ptr b)
/*
Return TRUE if two virtual functions have the same signature as clang's
HasSameVirtualSignature compares them (spec 12.2): the same name, identical
parameter types as the front end compares them (the top-level cv-qualifiers
are removed, arrays and functions decayed) and the same cv-qualifiers of
this.  The ref-qualifiers are not compared.  The return type is not part of
it, except for a conversion function, whose name is its return type.
*/
{
  a_type_ptr                     ta = skip_typerefs(a->type);
  a_type_ptr                     tb = skip_typerefs(b->type);
  a_routine_type_supplement_ptr  sa = rout_type_supp(ta);
  a_routine_type_supplement_ptr  sb = rout_type_supp(tb);
  a_param_type_ptr               pa = sa->param_type_list;
  a_param_type_ptr               pb = sb->param_type_list;
  a_boolean                      same = a->special_kind == b->special_kind &&
                                        sa->qualifiers == sb->qualifiers &&
                                        sa->this_qualifiers ==
                                                         sb->this_qualifiers;

  if (same) {
    const char  *na = a->source_corresp.name;
    const char  *nb = b->source_corresp.name;

    same = (na == NULL || nb == NULL) ? na == nb : strcmp(na, nb) == 0;
  }  /* if */
  if (same && a->special_kind == (a_special_function_kind)sfk_conversion) {
    same = identical_types(ta->variant.routine.return_type,
                           tb->variant.routine.return_type);
  }  /* if */
  while (same && pa != NULL && pb != NULL) {
    same = identical_types(pa->type, pb->type);
    pa = pa->next;
    pb = pb->next;
  }  /* while */
  return same && pa == NULL && pb == NULL;
}  /* same_virtual_signature */


static a_boolean vfunc_returns_reference(a_routine_ptr rout)
/*
Return TRUE if the return type of a virtual function is a reference (a
covariant reference result is adjusted without a null check).
*/
{
  a_type_ptr  rt = skip_typerefs(rout->type)->variant.routine.return_type;

  return rt != NULL && is_reference_type(skip_typerefs(rt));
}  /* vfunc_returns_reference */


static uint32_t virtual_signature_id(a_routine_ptr rout)
/*
The sig of a virtual function (not a destructor): a non-zero id that two
functions share iff same_virtual_signature holds.  The ids are the
positions of the first function of each signature in the table of the
interned ones (1, 2, ... in order of first encounter: deterministic, no
pointers in the stream).  Out of memory is noted as unsupported (and 0 is
returned).
*/
{
  const char  *name = rout->source_corresp.name;
  uint32_t    hash = hash_name(name != NULL ? name : "");
  uint32_t    sig = 0;

  if (sig_heads == NULL || num_sig_entries >= 2 * sig_heads_size) {
    size_t    new_size = sig_heads_size == 0 ? 256 : sig_heads_size * 2;
    uint32_t  *new_heads = (uint32_t *)calloc(new_size, sizeof(uint32_t));
    size_t    i;

    if (new_heads == NULL) {
      ecil_note_unsupported("out of memory in the ECIL writer");
    } else {
      /* Rehash the entries (their chains are rebuilt). */
      for (i = 0; i < num_sig_entries; i++) {
        if (sig_entries[i].rout != NULL) {
          const char  *en = sig_entries[i].rout->source_corresp.name;
          size_t      slot = hash_name(en != NULL ? en : "") &
                                                  (uint32_t)(new_size - 1);

          sig_entries[i].next = new_heads[slot];
          new_heads[slot] = (uint32_t)(i + 1);
        }  /* if */
      }  /* for */
      free(sig_heads);
      sig_heads = new_heads;
      sig_heads_size = new_size;
    }  /* if */
  }  /* if */
  if (ecil_unsupported_reason == NULL) {
    size_t    slot = hash & (uint32_t)(sig_heads_size - 1);
    uint32_t  e;

    for (e = sig_heads[slot]; e != 0 && sig == 0;
         e = sig_entries[e - 1].next) {
      if (sig_entries[e - 1].rout != NULL &&
          same_virtual_signature(sig_entries[e - 1].rout, rout)) {
        sig = e;
      }  /* if */
    }  /* for */
    if (sig == 0) {
      if (num_sig_entries == sig_entries_capacity) {
        size_t        new_capacity = sig_entries_capacity == 0 ?
                                     256 : sig_entries_capacity * 2;
        a_sig_entry   *new_entries = (a_sig_entry *)realloc(sig_entries,
                                       new_capacity * sizeof(a_sig_entry));

        if (new_entries == NULL) {
          ecil_note_unsupported("out of memory in the ECIL writer");
        } else {
          sig_entries = new_entries;
          sig_entries_capacity = new_capacity;
        }  /* if */
      }  /* if */
      if (num_sig_entries < sig_entries_capacity) {
        sig_entries[num_sig_entries].rout = rout;
        sig_entries[num_sig_entries].local = routine_is_function_local(rout);
        sig_entries[num_sig_entries].next = sig_heads[slot];
        num_sig_entries++;
        sig = (uint32_t)num_sig_entries;
        sig_heads[slot] = sig;
      }  /* if */
    }  /* if */
  }  /* if */
  return sig;
}  /* virtual_signature_id */


void ecil_forget_local_signatures(void)
/*
A function's memory region is about to be freed: the signatures whose first
function is one of its (local classes') routines can no longer be compared, so
the entries die (they keep their ids; a later function of that signature gets
a new one, which is harmless: a local class's functions are only related to
the functions of classes of the same region).
*/
{
  size_t  i;

  for (i = 0; i < num_sig_entries; i++) {
    if (sig_entries[i].local) sig_entries[i].rout = NULL;
  }  /* for */
}  /* ecil_forget_local_signatures */


static uint32_t deleting_destructor_id(an_ecil_frame *frame,
                                       a_routine_ptr rout,
                                       uint32_t      d1)
/*
The G-id of the deleting destructor D0 of a virtual destructor whose complete
variant has G-id d1: its own DECL_FUNC (D1's type, linkage and flags, the D0
name), then STRUCTOR_DELETING with the operator delete the front end chose
for the class (spec 12.3).  0 with the reason noted if it cannot be sent.
*/
{
  an_ecil_gid_info  *info = ecil_gid_info(d1);
  uint32_t          d0 = info->aux;

  if (d0 == 0 && ecil_unsupported_reason == NULL) {
    a_type_ptr     cls = parent_class_of(rout);
    a_routine_ptr  del;
    a_boolean      sized = FALSE;
    a_boolean      aligned = FALSE;
    a_boolean      destroying = FALSE;

    if (class_type_supp(cls)->assoc_operator_delete_routine == NULL) {
      /* The lookup walks the scope stack down to the file scope: at the end
         of the translation unit the stack is empty (NO_SCOPE_DEPTH), and
         the file scope is the one the lookup needs. */
      a_scope_depth  saved_depth = depth_scope_stack;

      if (depth_scope_stack < DEPTH_OF_FILE_SCOPE) {
        depth_scope_stack = DEPTH_OF_FILE_SCOPE;
      }  /* if */
      set_class_assoc_operator_delete_routine(cls);
      depth_scope_stack = saved_depth;
    }  /* if */
    del = class_type_supp(cls)->assoc_operator_delete_routine;
    if (del == NULL) {
      ecil_note_unsupported("a virtual destructor in a class without a "
                            "usable operator delete");
    } else if (!is_default_operator_delete(del, &sized, &aligned,
                                           &destroying)) {
      ecil_note_unsupported("a virtual destructor whose operator delete is "
                            "not a usual deallocation function");
    } else {
      uint32_t  gdel = ecil_function_id(frame, del);

      if (gdel != 0 && ecil_unsupported_reason == NULL) {
        a_func_state  state;

#if CHECKING
        {
          /* The flags must agree with the number of parameters (1 + SIZED
             + ALIGNED, + 1 for the empty tag of a destroying one): ECIL
             cannot tell size_t from align_val_t. */
          size_t            n = 0;
          a_param_type_ptr  p;

          for (p = rout_type_supp(skip_typerefs(del->type))->param_type_list;
               p != NULL; p = p->next) {
            n++;
          }  /* for */
          check_assertion(n == 1 + (sized ? 1u : 0u) + (aligned ? 1u : 0u) +
                               (destroying ? 1u : 0u));
        }
#endif /* CHECKING */
        compute_func_state(frame, rout, 0, ECIL_VARIANT_DELETING, FALSE,
                           FALSE, &state);
        /* D1's type, linkage and flags as they are now (spec 12.3). */
        info = ecil_gid_info(d1);
        state.type = info->decl_type;
        state.linkage = info->decl_linkage;
        state.flags = info->decl_flags;
        if (ecil_unsupported_reason == NULL) {
          d0 = ecil_new_gid(egk_func);
          if (d0 != 0) {
            an_ecil_buffer  *saved;

            info = ecil_gid_info(d0);
            info->entity = rout;
            info->variant = ECIL_VARIANT_DELETING;
            info->partner = 0;
            info->flags |= ecil_gid_info(d1)->flags & ECIL_GF_FUNCTION_LOCAL;
            if (state.late) info->flags |= ECIL_GF_LATE_NAME;
            ecil_gid_info(d1)->aux = d0;
            emit_func_decl_if_changed(frame, d0, &state);
            if (state.name != 0 &&
                state.linkage != (unsigned int)elk_internal) {
              symbol_binding_register(state.name, d0);
            }  /* if */
            saved = ecil_frame_enter_decl(frame);
            ecil_rec_begin(frame, eop_structor_deleting);
            ecil_arg_uint(frame, d0);
            ecil_arg_uint(frame, d1);
            ecil_arg_uint(frame, gdel);
            ecil_arg_uint(frame, (sized ? ECIL_STRUCTOR_DELETING_SIZED : 0u) |
                                 (aligned ? ECIL_STRUCTOR_DELETING_ALIGNED :
                                            0u) |
                                 (destroying ?
                                    ECIL_STRUCTOR_DELETING_DESTROYING : 0u));
            ecil_rec_end(frame);
            ecil_frame_leave_decl(frame, saved);
          }  /* if */
        }  /* if */
      }  /* if */
    }  /* if */
  }  /* if */
  return d0;
}  /* deleting_destructor_id */


static int compare_vfuncs(const void *left, const void *right)
/*
Order the functions of CLASS_VFUNCS: the ones declared in the class in
declaration order, then the implicit ones in slot order.
*/
{
  const a_vfunc  *a = (const a_vfunc *)left;
  const a_vfunc  *b = (const a_vfunc *)right;
  int            result;

  if (a->implicit != b->implicit) {
    result = a->implicit ? 1 : -1;
  } else if (a->implicit && a->slot != b->slot) {
    result = a->slot < b->slot ? -1 : 1;
  } else {
    result = a->order < b->order ? -1 : (a->order > b->order ? 1 : 0);
  }  /* if */
  return result;
}  /* compare_vfuncs */


static a_type_ptr return_class_of(a_routine_ptr rout)
/*
The class that a function returns a pointer or a reference to, else NULL.
*/
{
  a_type_ptr  rt = skip_typerefs(skip_typerefs(rout->type)->variant.routine.
                                                              return_type);
  a_type_ptr  result = NULL;

  if (rt->kind == tk_pointer) {
    rt = skip_typerefs(rt->variant.pointer.type);
    if (is_immediate_class_type(rt) && !rt->incomplete) result = rt;
  }  /* if */
  return result;
}  /* return_class_of */


static a_boolean push_covariant_return_class(an_ecil_frame *frame,
                                             a_type_ptr    t)
/*
For each override of the class with a covariant return type (a class derived
from the overridden function's return class): the layout facts of the returned
class, and its vtable facts when the path from it to the overridden function's
return class goes through a virtual base (the library derives the adjustment
thunk from them; spec 12.2).  The class whose vtable facts are wanted and not
sent is pushed on the class stack (the caller handles it first, so that the
depth of the walk is not that of the C stack) and TRUE is returned.
*/
{
  a_base_class_ptr  bcp;
  a_boolean         pushed = FALSE;

  for (bcp = class_type_supp(t)->base_classes;
       bcp != NULL && !pushed && ecil_unsupported_reason == NULL;
       bcp = bcp->next) {
    an_overriding_virtual_function_ptr  ovr;

    if (bcp->is_pack_expansion) continue;
    for (ovr = bcp->variant.overriding_virtual_functions;
         ovr != NULL && !pushed && ecil_unsupported_reason == NULL;
         ovr = ovr->next) {
      a_type_ptr  c = ovr->overriding_function->is_consteval ||
                      ovr->primary_function->is_consteval ? NULL :
                      return_class_of(ovr->overriding_function);
      a_type_ptr  b = c == NULL ? NULL :
                      return_class_of(ovr->primary_function);

      if (c != NULL && b != NULL && c != b) {
        a_base_class_ptr  path;

        ecil_class_layout_facts(frame, c);
        for (path = class_type_supp(c)->base_classes; path != NULL;
             path = path->next) {
          if (skip_typerefs(path->type) == b) break;
        }  /* for */
        if (ecil_unsupported_reason == NULL && path != NULL &&
            path->derivation != NULL &&
            (path->is_virtual || any_virtual_steps_in_derivation(path)) &&
            ecil_class_is_dynamic(c) && c != t) {
          uint32_t  cg = ecil_gid_of(c);

          if (cg != 0 && (ecil_gid_info(cg)->flags & (ECIL_GF_FACTS_BUSY |
                                          ECIL_GF_VTABLE_FACTS)) == 0 &&
              push_class(c)) {
            ecil_gid_info(cg)->flags |= ECIL_GF_FACTS_BUSY;
            pushed = TRUE;
          }  /* if */
        }  /* if */
      }  /* if */
    }  /* for */
  }  /* for */
  return pushed;
}  /* push_covariant_return_class */


static void send_vtable_facts(an_ecil_frame *frame, a_type_ptr t,
                              uint32_t gid)
/*
Send the vtable facts of a dynamic class whose bases' have been sent: a
DECL_FUNC for every virtual function (and D0 with STRUCTOR_DELETING for a
virtual destructor), CLASS_VFUNCS and CLASS_OVERRIDES (spec 12.2).
*/
{
  a_class_type_supplement_ptr  ctsp = class_type_supp(t);
  a_routine_ptr                rout;
  a_vfunc                      *funcs = NULL;
  size_t                       count = 0;
  size_t                       n = 0;
  size_t                       i;
  a_word_vec                   words = { NULL, 0, 0 };
  a_base_class_ptr             bcp;
  an_ecil_buffer               *saved;

  if (ctsp->assoc_scope != NULL) {
    for (rout = ctsp->assoc_scope->routines; rout != NULL;
         rout = rout->next) {
      if (rout->is_virtual && !rout->is_consteval) count++;
    }  /* for */
  }  /* if */
  if (count != 0) {
    funcs = (a_vfunc *)malloc(count * sizeof(a_vfunc));
    if (funcs == NULL) ecil_note_unsupported("out of memory in the ECIL "
                                             "writer");
  }  /* if */
  for (rout = ctsp->assoc_scope != NULL ? ctsp->assoc_scope->routines : NULL;
       rout != NULL && funcs != NULL && ecil_unsupported_reason == NULL;
       rout = rout->next) {
    if (rout->is_virtual && !rout->is_consteval && n < count) {
      a_vfunc  *f = &funcs[n];
      uint32_t g = ecil_function_id(frame, rout);

      f->rout = rout;
      f->gid = g;
      f->slot = (uint32_t)rout->number.virtual_function;
      f->flags = (rout->pure_virtual ? ECIL_VFUNC_PURE : 0u) |
                 (rout->is_deleted ? ECIL_VFUNC_DELETED : 0u) |
                 (rout->compiler_generated ? ECIL_VFUNC_IMPLICIT : 0u) |
                 (vfunc_returns_reference(rout) ? ECIL_VFUNC_REF_RESULT : 0u);
      f->implicit = rout->compiler_generated != 0;
      f->order = n;
      if (g != 0 && ecil_unsupported_reason == NULL) {
        if (rout->special_kind == (a_special_function_kind)sfk_destructor) {
          f->sig = 0;
          (void)deleting_destructor_id(frame, rout, g);
        } else {
          f->sig = virtual_signature_id(rout);
        }  /* if */
      }  /* if */
      n++;
    }  /* if */
  }  /* for */
  if (ecil_unsupported_reason == NULL) {
    if (n > 1) qsort(funcs, n, sizeof(a_vfunc), compare_vfuncs);
    for (i = 0; i < n; i++) {
      word_vec_push(&words, funcs[i].gid);
      word_vec_push(&words, funcs[i].slot);
      word_vec_push(&words, funcs[i].flags);
      word_vec_push(&words, funcs[i].sig);
    }  /* for */
  }  /* if */
  if (ecil_unsupported_reason == NULL) {
    saved = ecil_frame_enter_decl(frame);
    ecil_rec_begin(frame, eop_class_vfuncs);
    ecil_arg_uint(frame, gid);
    ecil_arg_array(frame, words.words, words.size);
    ecil_rec_end(frame);
    ecil_frame_leave_decl(frame, saved);
  }  /* if */
  free(funcs);
  /* CLASS_OVERRIDES: for every base entry (direct and indirect, repeated
     non-virtual bases once per subobject), its own list. */
  words.size = 0;
  for (bcp = ctsp->base_classes;
       bcp != NULL && ecil_unsupported_reason == NULL; bcp = bcp->next) {
    an_overriding_virtual_function_ptr  ovr;

    if (!bcp->is_pack_expansion) {
      for (ovr = bcp->variant.overriding_virtual_functions;
           ovr != NULL && ecil_unsupported_reason == NULL; ovr = ovr->next) {
        if (!ovr->primary_function->is_consteval &&
            !ovr->overriding_function->is_consteval) {
          uint32_t  g_fn = ecil_function_id(frame, ovr->primary_function);
          uint32_t  g_ovr = ecil_function_id(frame, ovr->overriding_function);

          if (g_fn != 0 && g_ovr != 0 && ecil_unsupported_reason == NULL) {
            word_vec_push(&words, g_fn);
            word_vec_push(&words, (uint32_t)bcp->offset);
            word_vec_push(&words, g_ovr);
            word_vec_push(&words, ovr->base_class != NULL ?
                                  (uint32_t)ovr->base_class->offset : 0u);
          }  /* if */
        }  /* if */
      }  /* for */
    }  /* if */
  }  /* for */
  if (ecil_unsupported_reason == NULL) {
    saved = ecil_frame_enter_decl(frame);
    ecil_rec_begin(frame, eop_class_overrides);
    ecil_arg_uint(frame, gid);
    ecil_arg_array(frame, words.words, words.size);
    ecil_rec_end(frame);
    ecil_frame_leave_decl(frame, saved);
    ecil_gid_info(gid)->flags |= ECIL_GF_VTABLE_FACTS;
  }  /* if */
  free(words.words);
}  /* send_vtable_facts */


void ecil_class_layout_facts(an_ecil_frame *frame, a_type_ptr type)
/*
Make sure the layout facts of a class are sent (spec 12.2): a dynamic
class's follow its record body; a plain class's are sent here, at the first
need (a typeinfo, a catch parameter), after those of its bases (explicit
stack).  Unsupported if the record has no body.
*/
{
  size_t  base = class_stack_size;

  if (push_class(skip_typerefs(type))) {
    while (class_stack_size > base && ecil_unsupported_reason == NULL) {
      a_type_ptr         c = class_stack[class_stack_size - 1];
      uint32_t           gid = ecil_type_id(frame, c);
      a_base_class_ptr   bp;
      a_boolean          pushed = FALSE;

      if (gid == 0 || ecil_unsupported_reason != NULL) {
        /* (The reason is noted.) */
      } else if ((ecil_gid_info(gid)->flags & (ECIL_GF_FAILED |
                                               ECIL_GF_COMPLETE)) !=
                 ECIL_GF_COMPLETE) {
        ecil_note_unsupported_type(gid, "a class without a record body");
      } else if ((ecil_gid_info(gid)->flags & ECIL_GF_LAYOUT_FACTS) != 0) {
        class_stack_size--;
      } else {
        for (bp = record_direct_bases(c); bp != NULL && !pushed &&
                                         ecil_unsupported_reason == NULL;
             bp = bp->next_direct) {
          uint32_t  bg = ecil_type_id(frame, bp->type);

          if (bg != 0 && ecil_unsupported_reason == NULL &&
              (ecil_gid_info(bg)->flags & ECIL_GF_LAYOUT_FACTS) == 0) {
            pushed = push_class(skip_typerefs(bp->type));
          }  /* if */
        }  /* for */
        if (!pushed && ecil_unsupported_reason == NULL) {
          send_layout_facts(frame, c, gid);
          class_stack_size--;
        }  /* if */
      }  /* if */
    }  /* while */
  }  /* if */
  class_stack_size = base;
}  /* ecil_class_layout_facts */


void ecil_class_vtable_facts(an_ecil_frame *frame, a_type_ptr type)
/*
Make sure the vtable facts of a dynamic class are sent (spec 12.2): at its
first need, after those of its dynamic bases (explicit stack).
*/
{
  size_t  base = class_stack_size;

  if (!ecil_class_is_dynamic(type)) {
    ecil_note_unsupported("the vtable of a class that has no virtual "
                          "function");
  } else if (push_class(skip_typerefs(type))) {
    while (class_stack_size > base && ecil_unsupported_reason == NULL) {
      a_type_ptr         c = class_stack[class_stack_size - 1];
      uint32_t           gid = ecil_type_id(frame, c);
      a_base_class_ptr   bp;
      a_boolean          pushed = FALSE;

      if (gid == 0 || ecil_unsupported_reason != NULL) {
        /* (The reason is noted.) */
      } else if ((ecil_gid_info(gid)->flags & (ECIL_GF_FAILED |
                                               ECIL_GF_COMPLETE)) !=
                 ECIL_GF_COMPLETE) {
        ecil_note_unsupported_type(gid, "a class without a record body");
      } else if ((ecil_gid_info(gid)->flags & ECIL_GF_VTABLE_FACTS) != 0) {
        class_stack_size--;
      } else {
        for (bp = record_direct_bases(c); bp != NULL && !pushed &&
                                         ecil_unsupported_reason == NULL;
             bp = bp->next_direct) {
          uint32_t  bg = ecil_type_id(frame, bp->type);

          if (bg != 0 && ecil_unsupported_reason == NULL &&
              ecil_class_is_dynamic(bp->type) &&
              (ecil_gid_info(bg)->flags & ECIL_GF_VTABLE_FACTS) == 0) {
            pushed = push_class(skip_typerefs(bp->type));
          }  /* if */
        }  /* for */
        if (!pushed && ecil_unsupported_reason == NULL) {
          /* The covariant return classes whose facts the library needs come
             first. */
          pushed = push_covariant_return_class(frame, c);
        }  /* if */
        if (!pushed && ecil_unsupported_reason == NULL) {
          send_vtable_facts(frame, c, gid);
          ecil_gid_info(gid)->flags &= ~ECIL_GF_FACTS_BUSY;
          class_stack_size--;
        }  /* if */
      }  /* if */
    }  /* while */
  }  /* if */
  class_stack_size = base;
}  /* ecil_class_vtable_facts */


static void emit_class_abi_data(an_ecil_frame *frame, uint32_t gid);

void ecil_note_vtable_used(an_ecil_frame *frame, a_type_ptr type)
/*
A kept record uses the vtable of a dynamic class (a VPTR_INIT, a constant of
it): send its vtable facts, if they are not, and mark the vtable used, so that
the end of the translation unit defines it (spec 12.8).  The mark is logged
so that a dropped body can take it back (ecil_vtable_use_rollback).
*/
{
  a_type_ptr  t = skip_typerefs(type);

  ecil_class_vtable_facts(frame, t);
  if (ecil_unsupported_reason == NULL) {
    uint32_t  gid = ecil_gid_of(t);

    if (gid != 0 && (ecil_gid_info(gid)->flags & ECIL_GF_VTABLE_USED) == 0) {
      if (used_log_size == used_log_capacity) {
        size_t    new_capacity = used_log_capacity == 0 ?
                                 32 : used_log_capacity * 2;
        uint32_t  *new_log = (uint32_t *)realloc(used_log,
                                       new_capacity * sizeof(uint32_t));

        if (new_log == NULL) {
          ecil_note_unsupported("out of memory in the ECIL writer");
        } else {
          used_log = new_log;
          used_log_capacity = new_capacity;
        }  /* if */
      }  /* if */
      if (used_log_size < used_log_capacity) {
        used_log[used_log_size++] = gid;
        ecil_gid_info(gid)->flags |= ECIL_GF_VTABLE_USED;
        if ((ecil_gid_info(gid)->flags & ECIL_GF_ABI_DONE) != 0 &&
            ecil_gid_info(gid)->aux == 0) {
          /* A local class whose function's region ended before anything used
             its vtable (a constructor instantiated later, by make_unique<L>):
             the vtable is decided now, in this frame; the class's IL is alive
             since something of it is being translated. */
          an_ecil_saved_reason  saved;

          ecil_save_reason(&saved);
          emit_class_abi_data(frame, gid);
          ecil_unsupported_reason = NULL;
          ecil_restore_reason(&saved);
        }  /* if */
      }  /* if */
    }  /* if */
  }  /* if */
}  /* ecil_note_vtable_used */


size_t ecil_vtable_use_mark(void)
/*
The position in the log of vtable uses, taken before a body or constant.
*/
{
  return used_log_size;
}  /* ecil_vtable_use_mark */


void ecil_vtable_use_rollback(size_t mark)
/*
The body or constant after the mark was dropped: the vtables it marked used
are not (unless another kept one did, which the earlier mark records).
*/
{
  while (used_log_size > mark) {
    ecil_gid_info(used_log[--used_log_size])->flags &= ~ECIL_GF_VTABLE_USED;
  }  /* while */
}  /* ecil_vtable_use_rollback */


void ecil_vtable_use_commit(size_t mark)
/*
The body or constant after the mark was kept: its uses are final.
*/
{
  if (used_log_size > mark) used_log_size = mark;
}  /* ecil_vtable_use_commit */


/* Vtables and class typeinfo (spec 12.3, 12.8). */

/* What decides the linkage of a class's vtable and typeinfo. */
typedef enum a_vtable_key {
  vk_none,            /* No key function. */
  vk_here,            /* The key function is defined here. */
  vk_elsewhere,       /* The key function is not defined here. */
  vk_explicit,        /* An explicit instantiation definition. */
  vk_extern_template  /* extern template: defined elsewhere. */
} a_vtable_key;

/* The decision for a class: its vtable and typeinfo. */
typedef struct a_class_abi_plan {
  unsigned int  linkage;          /* An an_ecil_linkage value. */
  a_boolean     comdat;
  unsigned int  vtable_linkage;   /* The vtable's: the same, except for a
                                     vtable that cannot be defined here. */
  a_boolean     vtable_comdat;
  a_boolean     tu_local;
  a_boolean     vtable_dropped;   /* It would be defined here, but refers to
                                     a function that was dropped. */
  a_boolean     vtable_defined;   /* The vtable is defined here. */
  a_boolean     vtable_declared;  /* ... or declared (it is used). */
  a_boolean     typeinfo_defined; /* The typeinfo is defined here. */
} a_class_abi_plan;

static a_vtable_key vtable_key_of(a_type_ptr t)
/*
Classify a class by clang's key function rule (spec 12.8, decided at the end
of the translation unit): the first virtual function declared in the class, in
declaration order, that is not pure, not compiler-generated, not declared
inline or constexpr and not defined inside the class definition; there is
none if that function is later defined inline, and none for an implicit
instance of a class template (an explicit specialization is an ordinary
class).
*/
{
  a_class_type_supplement_ptr  ctsp = class_type_supp(t);
  a_vtable_key                 key = vk_none;
  a_routine_ptr                rout;

  if (t->variant.class_struct_union.is_template_class &&
      !t->variant.class_struct_union.is_specialized) {
    if (t->variant.class_struct_union.explicitly_instantiated &&
        !t->variant.class_struct_union.do_not_instantiate) {
      key = vk_explicit;
    } else if (t->variant.class_struct_union.do_not_instantiate) {
      key = vk_extern_template;
    }  /* if */
  } else if (ctsp->assoc_scope != NULL) {
    a_boolean  found = FALSE;

    for (rout = ctsp->assoc_scope->routines; rout != NULL && !found;
         rout = rout->next) {
      if (rout->is_virtual && !rout->is_consteval && !rout->pure_virtual &&
          !rout->compiler_generated && !rout->inline_in_class_definition) {
        found = TRUE;
        if (!rout->is_inline) {
          uint32_t  g = ecil_gid_of(rout);

          key = (g != 0 && (ecil_gid_info(g)->flags & ECIL_GF_DEFINED) != 0) ?
                vk_here : vk_elsewhere;
        }  /* if */
      }  /* if */
    }  /* for */
  }  /* if */
  return key;
}  /* vtable_key_of */


static a_boolean class_has_dropped_virtual_function(a_type_ptr c)
/*
Return TRUE if a virtual function of the class (not pure, not deleted) had its
definition dropped as unsupported.
*/
{
  a_class_type_supplement_ptr  ctsp = class_type_supp(c);
  a_routine_ptr                rout;
  a_boolean                    dropped = FALSE;

  if (ctsp->assoc_scope != NULL) {
    for (rout = ctsp->assoc_scope->routines; rout != NULL && !dropped;
         rout = rout->next) {
      uint32_t  g = ecil_gid_of(rout);

      if (rout->is_virtual && !rout->is_consteval &&
          !rout->pure_virtual && !rout->is_deleted && g != 0 &&
          (ecil_gid_info(g)->decl_flags & ECIL_DECL_FUNC_UNSUPPORTED) != 0) {
        dropped = TRUE;
      }  /* if */
    }  /* for */
  }  /* if */
  return dropped;
}  /* class_has_dropped_virtual_function */


static a_boolean vtable_would_refer_to_dropped_function(a_type_ptr t)
/*
Return TRUE if a function of the class or of a dynamic base (not pure, not
deleted) had its definition dropped as unsupported: a vtable with vague
linkage that referred to it cannot be defined here (spec 12.8).  The check
is a superset of the functions the vtable refers to (an overridden function of
a base is counted too).  The bases are the entries of the base class list
(every class once per entry), and a class is looked at once (ECIL_GF_VISITED,
cleared again), however many paths lead to it.
*/
{
  a_boolean         dropped = class_has_dropped_virtual_function(t);
  a_base_class_ptr  bcp;
  a_word_vec        visited = { NULL, 0, 0 };
  size_t            i;

  for (bcp = class_type_supp(t)->base_classes; bcp != NULL && !dropped;
       bcp = bcp->next) {
    uint32_t  bg = ecil_gid_of(skip_typerefs(bcp->type));

    if (bg != 0 && ecil_class_is_dynamic(bcp->type) &&
        (ecil_gid_info(bg)->flags & ECIL_GF_VISITED) == 0) {
      ecil_gid_info(bg)->flags |= ECIL_GF_VISITED;
      word_vec_push(&visited, bg);
      dropped = class_has_dropped_virtual_function(skip_typerefs(bcp->type));
    }  /* if */
  }  /* for */
  for (i = 0; i < visited.size; i++) {
    ecil_gid_info(visited.words[i])->flags &= ~ECIL_GF_VISITED;
  }  /* for */
  free(visited.words);
  return dropped;
}  /* vtable_would_refer_to_dropped_function */


static void plan_class_abi(a_type_ptr t, uint32_t gid, a_class_abi_plan *plan)
/*
Decide the linkage of a class's vtable and typeinfo and whether the vtable and
typeinfo are defined or declared here (spec 12.8).  The vtable is defined for
a key function defined here, an explicit instantiation, and (with vague
linkage) when it is used; declared when it is used and its key function is
elsewhere.  The typeinfo has the linkage the vtable would have, whether or not
the vtable is used.
*/
{
  unsigned int  flags = ecil_gid_info(gid)->flags;
  a_boolean     used = (flags & ECIL_GF_VTABLE_USED) != 0;
  a_vtable_key  key = ecil_class_is_dynamic(t) ? vtable_key_of(t) : vk_none;
  a_boolean     local = FALSE;

  memset(plan, 0, sizeof(*plan));
#if BACK_END_TYPE == BACK_END_TYPE_CIR
  plan->tu_local = cir_entity_is_tu_local(iek_type, t);
#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */
  local = type_is_function_local(t);
  plan->linkage = (unsigned int)elk_external;
  if (local) {
    /* The linkage of the statics of the enclosing function (clang). */
    a_routine_ptr  fn = enclosing_routine_for_local_type_or_null(t);

    plan->linkage = (unsigned int)elk_internal;
    if (fn != NULL && !plan->tu_local &&
        (fn->storage_class == (a_storage_class)sc_unspecified) &&
        routine_might_exist_in_multiple_copies(fn)) {
      plan->linkage = (unsigned int)elk_linkonce_odr;
      plan->comdat = TRUE;
    }  /* if */
    /* Internal: not unique across translation units, which the name string
       of the typeinfo shows with a "*" as g++ writes it. */
    if (plan->linkage == (unsigned int)elk_internal) plan->tu_local = TRUE;
    plan->vtable_defined = used;
    plan->typeinfo_defined = TRUE;
  } else if (plan->tu_local) {
    plan->linkage = (unsigned int)elk_internal;
    plan->vtable_defined = used || key == vk_here;
    plan->typeinfo_defined = TRUE;
  } else {
    switch (key) {
      case vk_here:
        plan->vtable_defined = TRUE;
        plan->typeinfo_defined = TRUE;
        break;
      case vk_elsewhere:
        plan->vtable_declared = used;
        break;
      case vk_extern_template:
        plan->vtable_declared = used;
        break;
      case vk_explicit:
        plan->linkage = (unsigned int)elk_weak_odr;
        plan->comdat = TRUE;
        plan->vtable_defined = TRUE;
        plan->typeinfo_defined = TRUE;
        break;
      default:
        plan->linkage = (unsigned int)elk_linkonce_odr;
        plan->comdat = TRUE;
        plan->vtable_defined = used;
        plan->typeinfo_defined = TRUE;
        break;
    }  /* switch */
  }  /* if */
  plan->vtable_linkage = plan->linkage;
  plan->vtable_comdat = plan->comdat;
  if (plan->vtable_defined && plan->linkage != (unsigned int)elk_external &&
      plan->linkage != (unsigned int)elk_internal &&
      vtable_would_refer_to_dropped_function(t)) {
    /* The vtable is an external declaration instead; the typeinfo, which
       refers to no function, is unchanged (spec 12.8, a dropped function). */
    plan->vtable_defined = FALSE;
    plan->vtable_dropped = TRUE;
    plan->vtable_declared = TRUE;
    plan->vtable_linkage = (unsigned int)elk_external;
    plan->vtable_comdat = FALSE;
  }  /* if */
}  /* plan_class_abi */


static uint32_t emit_abi_data(an_ecil_frame *frame, unsigned int kind,
                              uint32_t class_gid, unsigned int linkage,
                              unsigned int flags)
/*
Introduce DECL_ABI_DATA (a vtable or a class typeinfo: the name is derived
from CLASS_MANGLE) and return its G-id.
*/
{
  uint32_t  gid = ecil_new_gid(egk_abi_data);

  if (gid != 0) {
    an_ecil_buffer  *saved = ecil_frame_enter_decl(frame);

    ecil_gid_info(gid)->ref = class_gid;
    ecil_gid_info(gid)->decl_linkage = linkage;
    ecil_gid_info(gid)->decl_flags = flags;
    ecil_gid_info(gid)->decl_emitted = TRUE;
    ecil_rec_begin(frame, eop_decl_abi_data);
    ecil_arg_uint(frame, gid);
    ecil_arg_uint(frame, kind);
    ecil_arg_uint(frame, class_gid);
    ecil_arg_uint(frame, linkage);
    ecil_arg_uint(frame, flags);
    ecil_arg_nil(frame);
    ecil_rec_end(frame);
    ecil_frame_leave_decl(frame, saved);
  }  /* if */
  return gid;
}  /* emit_abi_data */


static unsigned int abi_flags_of(const a_class_abi_plan *plan,
                                 a_boolean defined, a_boolean typeinfo)
/*
The DECL_ABI_DATA flags of a class's vtable or typeinfo: TU_LOCAL (the
typeinfo name's "*" prefix, spec 12.3) is the typeinfo's only.
*/
{
  return (defined ? ECIL_ABI_DATA_DEFINED_HERE : 0u) |
         ((typeinfo ? plan->comdat : plan->vtable_comdat) ?
                                           ECIL_ABI_DATA_COMDAT : 0u) |
         (typeinfo && plan->tu_local ? ECIL_ABI_DATA_TU_LOCAL : 0u);
}  /* abi_flags_of */


static void reissue_abi_data(an_ecil_frame *frame, unsigned int kind,
                             uint32_t gid, unsigned int linkage,
                             unsigned int flags)
/*
Re-issue the DECL_ABI_DATA of gid (a vtable or class typeinfo that was
introduced earlier) if the final linkage or flags differ from the last issue;
the latest wins (spec 12.3).
*/
{
  an_ecil_gid_info  *info = ecil_gid_info(gid);

  if (info->decl_linkage != linkage || info->decl_flags != flags) {
    an_ecil_buffer  *saved = ecil_frame_enter_decl(frame);

    ecil_rec_begin(frame, eop_decl_abi_data);
    ecil_arg_uint(frame, gid);
    ecil_arg_uint(frame, kind);
    ecil_arg_uint(frame, info->ref);
    ecil_arg_uint(frame, linkage);
    ecil_arg_uint(frame, flags);
    ecil_arg_nil(frame);
    ecil_rec_end(frame);
    ecil_frame_leave_decl(frame, saved);
    info = ecil_gid_info(gid);
    info->decl_linkage = linkage;
    info->decl_flags = flags;
  }  /* if */
}  /* reissue_abi_data */


static void reissue_class_typeinfos(an_ecil_frame *frame)
/*
At the end of the translation unit: the class typeinfo that a function's
region introduced earlier may have a different final linkage (the key
function of the class was defined later) or TU-locality: re-issue it.
*/
{
  uint32_t  gid;
  uint32_t  last = ecil_num_gids();

  for (gid = 1; gid <= last; gid++) {
    an_ecil_gid_info  *info = ecil_gid_info(gid);

    if (info->kind == egk_record && info->aux2 != 0 &&
        (info->flags & (ECIL_GF_FUNCTION_LOCAL |
                        ECIL_GF_TI_INCOMPLETE)) == 0 &&
        info->entity != NULL) {
      a_class_abi_plan  plan;

      plan_class_abi((a_type_ptr)info->entity, gid, &plan);
      reissue_abi_data(frame, 2u, info->aux2, plan.linkage,
                       abi_flags_of(&plan, plan.typeinfo_defined, TRUE));
    }  /* if */
  }  /* for */
}  /* reissue_class_typeinfos */


static void issue_class_typeinfo(an_ecil_frame *frame, a_type_ptr t)
/*
Introduce the typeinfo DECL_ABI_DATA of a class and, first, those of its
direct bases (a DEFINED_HERE class typeinfo needs the typeinfo of each direct
base introduced first, spec 12.3).  A class's typeinfo is defined here unless
its key function is elsewhere (an external declaration).  Needs the layout
facts of every class it introduces (plain bases get theirs here).
*/
{
  size_t  base = class_stack_size;

  if (push_class(skip_typerefs(t))) {
    while (class_stack_size > base && ecil_unsupported_reason == NULL) {
      a_type_ptr        c = class_stack[class_stack_size - 1];
      uint32_t          gid = ecil_type_id(frame, c);
      a_base_class_ptr  bp;
      a_boolean         pushed = FALSE;

      if (gid == 0 || ecil_unsupported_reason != NULL) {
        /* (The reason is noted.) */
      } else if (ecil_gid_info(gid)->aux2 != 0) {
        class_stack_size--;
      } else {
        for (bp = record_direct_bases(c); bp != NULL && !pushed &&
                                         ecil_unsupported_reason == NULL;
             bp = bp->next_direct) {
          uint32_t  bg = ecil_type_id(frame, bp->type);

          if (bg != 0 && ecil_unsupported_reason == NULL &&
              ecil_gid_info(bg)->aux2 == 0) {
            pushed = push_class(skip_typerefs(bp->type));
          }  /* if */
        }  /* for */
        if (!pushed && ecil_unsupported_reason == NULL) {
          a_class_abi_plan  plan;

          ecil_class_layout_facts(frame, c);
          if (ecil_unsupported_reason == NULL && ecil_class_is_dynamic(c)) {
            /* A dynamic class's DECL_ABI_DATA needs its vtable facts too. */
            ecil_class_vtable_facts(frame, c);
          }  /* if */
          plan_class_abi(c, gid, &plan);
          if (ecil_unsupported_reason == NULL) {
            uint32_t  ti = emit_abi_data(frame, 2u, gid, plan.linkage,
                                         abi_flags_of(&plan,
                                                     plan.typeinfo_defined,
                                                     TRUE));

            if (ti != 0) ecil_gid_info(gid)->aux2 = ti;
          }  /* if */
          class_stack_size--;
        }  /* if */
      }  /* if */
    }  /* while */
  }  /* if */
  class_stack_size = base;
}  /* issue_class_typeinfo */


/* Typeinfo of types that are not complete classes (spec 12.3, 12.8). */

typedef enum a_typeinfo_kind {
  eti_class,
  eti_incomplete_class,
  eti_fundamental,
  eti_pointer,
  eti_member_pointer,
  eti_function,
  eti_array,
  eti_enum,
  eti_unsupported
} a_typeinfo_kind;

static a_type_ptr typeinfo_normalize(a_type_ptr type)
/*
The type whose typeinfo describes the type: a reference is its referent, and
the top-level cv-qualifiers (and typedefs) are gone.
*/
{
  type = skip_typerefs(type);
  if (type->kind == tk_pointer && is_reference_type(type)) {
    type = skip_typerefs(type_pointed_to(type));
  }  /* if */
  return type;
}  /* typeinfo_normalize */


static a_typeinfo_kind typeinfo_kind_of(a_type_ptr t)
/*
Classify a normalized type by the std::type_info class that describes it.
*/
{
  a_typeinfo_kind  kind = eti_unsupported;

  if (is_immediate_class_type(t)) {
    kind = t->incomplete ? eti_incomplete_class : eti_class;
  } else if (is_enum_type(t)) {
    kind = eti_enum;
  } else if (is_nullptr_type(t) || t->kind == tk_void) {
    kind = eti_fundamental;
  } else {
    switch (t->kind) {
      case tk_integer:
        if (t->variant.integer.int_kind != ik_bit_precise &&
            t->variant.integer.int_kind != ik_unsigned_bit_precise) {
          kind = eti_fundamental;
        }  /* if */
        break;
      case tk_float:
        kind = eti_fundamental;
        break;
      case tk_pointer:
        kind = eti_pointer;
        break;
      case tk_ptr_to_member:
        kind = eti_member_pointer;
        break;
      case tk_routine:
        kind = eti_function;
        break;
      case tk_array:
        kind = eti_array;
        break;
      default:
        break;
    }  /* switch */
  }  /* if */
  return kind;
}  /* typeinfo_kind_of */


static a_type_ptr typeinfo_pointee(a_type_ptr t, a_type_ptr *raw)
/*
The type that the typeinfo of a pointer or member pointer refers to as its
pointee, normalized (a noexcept function type loses its exception
specification, which the mask records instead); *raw is the type as written,
for its qualifiers.
*/
{
  a_type_ptr  pointee = t->kind == tk_ptr_to_member ?
                        pm_member_type(t) : t->variant.pointer.type;
  a_type_ptr  result = skip_typerefs(pointee);

  *raw = pointee;
  if (result->kind == tk_routine && is_nothrow_type(result)) {
    result = function_type_without_noexcept_exception_spec(result);
  }  /* if */
  return result;
}  /* typeinfo_pointee */


static a_boolean typeinfo_contains_incomplete_class(a_type_ptr t)
/*
Clang's ContainsIncompleteClassType: an incomplete class, or a pointer or
member pointer that has one in its chain (also as the class of a member
pointer).
*/
{
  a_boolean  found = FALSE;
  a_boolean  done = FALSE;

  t = skip_typerefs(t);
  while (!done) {
    if (is_immediate_class_type(t)) {
      found = t->incomplete;
      done = TRUE;
    } else if (t->kind == tk_pointer && !is_nullptr_type(t) &&
               !is_reference_type(t)) {
      t = skip_typerefs(t->variant.pointer.type);
    } else if (t->kind == tk_ptr_to_member) {
      if (skip_typerefs(pm_class_type(t))->incomplete) {
        found = TRUE;
        done = TRUE;
      } else {
        t = skip_typerefs(pm_member_type(t));
      }  /* if */
    } else {
      done = TRUE;
    }  /* if */
  }  /* while */
  return found;
}  /* typeinfo_contains_incomplete_class */


static a_boolean typeinfo_has_internal_local_class(an_ecil_frame *frame,
                                                   a_type_ptr    t)
/*
Return TRUE if a class of the pointer chain of the type is local to a function
whose statics are internal (its typeinfo has internal linkage: spec 12.8).
*/
{
  a_boolean  found = FALSE;
  a_boolean  done = FALSE;

  t = skip_typerefs(t);
  while (!done) {
    if (is_immediate_class_type(t)) {
      if (!t->incomplete && type_is_function_local(t)) {
        uint32_t          gid = ecil_type_id(frame, t);
        a_class_abi_plan  plan;

        if (gid != 0) {
          plan_class_abi(t, gid, &plan);
          found = plan.linkage == (unsigned int)elk_internal;
        }  /* if */
      }  /* if */
      done = TRUE;
    } else if (t->kind == tk_pointer && !is_nullptr_type(t) &&
               !is_reference_type(t)) {
      t = skip_typerefs(t->variant.pointer.type);
    } else if (t->kind == tk_ptr_to_member) {
      a_type_ptr  cls = skip_typerefs(pm_class_type(t));

      if (typeinfo_has_internal_local_class(frame, cls)) {
        found = TRUE;
        done = TRUE;
      } else {
        t = skip_typerefs(pm_member_type(t));
      }  /* if */
    } else {
      done = TRUE;
    }  /* if */
  }  /* while */
  return found;
}  /* typeinfo_has_internal_local_class */


static uint32_t typeinfo_name_sid(an_ecil_frame *frame, a_type_ptr t)
/*
The sid of the name (_ZTI...) of the typeinfo of a type that is not a class.
*/
{
  uint32_t  sid = 0;

#if BACK_END_TYPE == BACK_END_TYPE_CIR
  const char  *text = mangled_typeinfo_name(t);

  if (text != NULL) sid = ecil_intern_cstring(frame, text);
#else /* BACK_END_TYPE != BACK_END_TYPE_CIR */
  (void)frame;
  (void)t;
#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */
  return sid;
}  /* typeinfo_name_sid */


static uint32_t typeinfo_find(an_ecil_frame *frame, a_type_ptr t)
/*
The G-id of the typeinfo of a normalized type if it has been introduced, or
0.
*/
{
  uint32_t         gid = 0;
  a_typeinfo_kind  kind = typeinfo_kind_of(t);

  if (kind == eti_class || kind == eti_incomplete_class) {
    uint32_t  rec = ecil_gid_of(t);

    if (rec != 0) gid = ecil_gid_info(rec)->aux2;
  } else if (kind != eti_unsupported) {
    uint32_t  key[2];
    uint32_t  sid = typeinfo_name_sid(frame, t);

    if (sid != 0) {
      key[0] = (uint32_t)egk_abi_data;
      key[1] = sid;
      gid = ecil_word_lookup(key, 2);
    }  /* if */
  }  /* if */
  return gid;
}  /* typeinfo_find */


static void issue_incomplete_class_typeinfo(an_ecil_frame *frame,
                                            a_type_ptr    t)
/*
The typeinfo of an incomplete class: a plain __class_type_info without bases
or CLASS_INFO, internal (spec 12.3).  The decision is sticky: it stays when
the class is completed later (as in clang).
*/
{
  uint32_t  gid = ecil_type_id(frame, t);

  if (gid != 0 && ecil_unsupported_reason == NULL) {
    uint32_t  sid = intern_class_mangle(frame, t);

    if (sid == 0) {
      ecil_note_unsupported("the mangled name of an incomplete class");
    } else {
      uint32_t  ti;

      emit_class_mangle(frame, gid, sid);
      ti = emit_abi_data(frame, 2u, gid, (unsigned int)elk_internal,
                         ECIL_ABI_DATA_DEFINED_HERE);
      if (ti != 0) {
        ecil_gid_info(gid)->aux2 = ti;
        ecil_gid_info(gid)->flags |= ECIL_GF_TI_INCOMPLETE;
      }  /* if */
    }  /* if */
  }  /* if */
}  /* issue_incomplete_class_typeinfo */


static a_boolean typeinfo_is_stdlib_fundamental(a_type_ptr t)
/*
Return TRUE for a fundamental type whose typeinfo the C++ runtime library
defines (an external declaration here).
*/
{
  return typeinfo_kind_of(t) == eti_fundamental;
}  /* typeinfo_is_stdlib_fundamental */


static uint32_t emit_non_class_typeinfo(an_ecil_frame *frame, a_type_ptr t,
                                        a_typeinfo_kind kind,
                                        uint32_t ga, uint32_t gb)
/*
Introduce DECL_ABI_DATA and TYPEINFO_DEF of the typeinfo of a type that is
not a class, whose parts (the typeinfo of a pointee, of the class of a member
pointer) have been introduced (spec 12.3, 12.8).  Linkage: a fundamental
type and a pointer to a cv-unqualified or const one are external declarations
(the runtime library has them), the typeinfo of a type that is TU-local, or
whose pointer chain has an incomplete class, is internal, and any other is
linkonce_odr in a COMDAT group.  The masks come from the pointee's
qualifiers; an incomplete chain has INCOMPLETE at each level of the chain.
Return the G-id.
*/
{
  uint32_t       sid = typeinfo_name_sid(frame, t);
  uint32_t       et = ecil_type_id(frame, t);
  uint32_t       gid = 0;
  unsigned int   masks = 0;
  unsigned int   linkage = (unsigned int)elk_linkonce_odr;
  unsigned int   abi_flags = ECIL_ABI_DATA_DEFINED_HERE | ECIL_ABI_DATA_COMDAT;
  a_boolean      defined = TRUE;
  a_type_ptr     pointee = NULL;
  a_type_ptr     raw = NULL;

  if (kind == eti_pointer || kind == eti_member_pointer) {
    a_type_qualifier_set  quals;

    pointee = typeinfo_pointee(t, &raw);
    /* The qualifiers of an array are those of its element (also through
       arrays of arrays), as clang's extractPBaseFlags looks. */
    quals = f_get_type_qualifiers(raw, FALSE);
    if (quals & TQ_CONST) masks |= ECIL_TYPEINFO_CONST;
    if (quals & TQ_VOLATILE) masks |= ECIL_TYPEINFO_VOLATILE;
    if (quals & TQ_RESTRICT) masks |= ECIL_TYPEINFO_RESTRICT;
    if (typeinfo_contains_incomplete_class(pointee)) {
      masks |= ECIL_TYPEINFO_INCOMPLETE;
    }  /* if */
    if (kind == eti_member_pointer &&
        skip_typerefs(pm_class_type(t))->incomplete) {
      masks |= ECIL_TYPEINFO_INCOMPLETE_CLASS;
    }  /* if */
    if (skip_typerefs(raw)->kind == tk_routine &&
        is_nothrow_type(skip_typerefs(raw))) {
      masks |= ECIL_TYPEINFO_NOEXCEPT;
    }  /* if */
  }  /* if */
  if (kind == eti_fundamental ||
      (kind == eti_pointer && (masks & ~ECIL_TYPEINFO_CONST) == 0 &&
       typeinfo_is_stdlib_fundamental(pointee))) {
    linkage = (unsigned int)elk_external;
    abi_flags = 0;
    defined = FALSE;
  } else {
    a_boolean  tu_local = FALSE;
    a_boolean  internal;

#if BACK_END_TYPE == BACK_END_TYPE_CIR
    tu_local = cir_entity_is_tu_local(iek_type, t);
#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */
    if (!tu_local && typeinfo_has_internal_local_class(frame, t)) {
      tu_local = TRUE;
    }  /* if */
    internal = tu_local || typeinfo_contains_incomplete_class(t);
    if (internal) {
      linkage = (unsigned int)elk_internal;
      abi_flags = ECIL_ABI_DATA_DEFINED_HERE |
                  (tu_local ? ECIL_ABI_DATA_TU_LOCAL : 0u);
    }  /* if */
  }  /* if */
  if (sid == 0) {
    ecil_note_unsupported("the mangled name of a type's typeinfo");
  } else if (et != 0 && ecil_unsupported_reason == NULL) {
    uint32_t  key[2];

    gid = ecil_new_gid(egk_abi_data);
    if (gid != 0) {
      an_ecil_buffer  *saved = ecil_frame_enter_decl(frame);

      key[0] = (uint32_t)egk_abi_data;
      key[1] = sid;
      ecil_word_register(key, 2, gid);
      ecil_gid_info(gid)->ref = et;
      ecil_gid_info(gid)->decl_linkage = linkage;
      ecil_gid_info(gid)->decl_flags = abi_flags;
      ecil_gid_info(gid)->decl_emitted = TRUE;
      ecil_rec_begin(frame, eop_decl_abi_data);
      ecil_arg_uint(frame, gid);
      ecil_arg_uint(frame, 2u);
      ecil_arg_uint(frame, et);
      ecil_arg_uint(frame, linkage);
      ecil_arg_uint(frame, abi_flags);
      ecil_arg_uint(frame, sid);
      ecil_rec_end(frame);
      ecil_rec_begin(frame, eop_typeinfo_def);
      ecil_arg_uint(frame, gid);
      ecil_arg_uint(frame, kind == eti_fundamental ?
                           (unsigned int)etik_fundamental :
                           kind == eti_pointer ? (unsigned int)etik_pointer :
                           kind == eti_member_pointer ?
                                       (unsigned int)etik_member_pointer :
                           kind == eti_function ?
                                       (unsigned int)etik_function :
                           kind == eti_array ? (unsigned int)etik_array :
                                               (unsigned int)etik_enum);
      ecil_arg_uint(frame, masks);
      if (ga != 0) {
        ecil_arg_uint(frame, ga);
      } else {
        ecil_arg_nil(frame);
      }  /* if */
      if (gb != 0) {
        ecil_arg_uint(frame, gb);
      } else {
        ecil_arg_nil(frame);
      }  /* if */
      ecil_rec_end(frame);
      ecil_frame_leave_decl(frame, saved);
    }  /* if */
  }  /* if */
  (void)defined;
  return gid;
}  /* emit_non_class_typeinfo */


static a_boolean push_typeinfo_type(a_type_ptr t)
/*
Push a type on the typeinfo work stack; FALSE if memory ran out (the reason
is noted).
*/
{
  a_boolean  ok = TRUE;

  if (typeinfo_stack_size == typeinfo_stack_capacity) {
    size_t      new_capacity = (typeinfo_stack_capacity == 0) ?
                               32 : typeinfo_stack_capacity * 2;
    a_type_ptr  *new_stack = (a_type_ptr *)realloc(typeinfo_stack,
                                      new_capacity * sizeof(a_type_ptr));

    if (new_stack == NULL) {
      ecil_note_unsupported("out of memory in the ECIL writer");
      ok = FALSE;
    } else {
      typeinfo_stack = new_stack;
      typeinfo_stack_capacity = new_capacity;
    }  /* if */
  }  /* if */
  if (ok) typeinfo_stack[typeinfo_stack_size++] = t;
  return ok;
}  /* push_typeinfo_type */


uint32_t ecil_typeinfo_id(an_ecil_frame *frame, a_type_ptr type)
/*
The G-id of the typeinfo (DECL_ABI_DATA) of a type, a class or not.  Its
parts are introduced first: the typeinfo of the pointee of a pointer and of
the member type and class of a member pointer, and of a class's bases
(issue_class_typeinfo).  The work is a stack of types, not a recursion.
Return 0, having noted why, if it is not expressible.
*/
{
  size_t      base = typeinfo_stack_size;
  a_type_ptr  root = typeinfo_normalize(type);
  uint32_t    result = 0;

  if (push_typeinfo_type(root)) {
    while (typeinfo_stack_size > base && ecil_unsupported_reason == NULL) {
      a_type_ptr       t = typeinfo_stack[typeinfo_stack_size - 1];
      a_typeinfo_kind  kind = typeinfo_kind_of(t);
      uint32_t         gid = 0;

      if (kind == eti_unsupported) {
        ecil_note_unsupported("the typeinfo of a type that is not "
                              "supported");
      } else if (typeinfo_find(frame, t) != 0) {
        typeinfo_stack_size--;
      } else if (kind == eti_class) {
        issue_class_typeinfo(frame, t);
        typeinfo_stack_size--;
      } else if (kind == eti_incomplete_class) {
        issue_incomplete_class_typeinfo(frame, t);
        typeinfo_stack_size--;
      } else {
        a_type_ptr  child_a = NULL;
        a_type_ptr  child_b = NULL;
        a_type_ptr  raw;
        uint32_t    ga = 0;
        uint32_t    gb = 0;
        a_boolean   pushed = FALSE;

        if (kind == eti_pointer || kind == eti_member_pointer) {
          child_a = typeinfo_pointee(t, &raw);
        }  /* if */
        if (kind == eti_member_pointer) {
          child_b = skip_typerefs(pm_class_type(t));
        }  /* if */
        if (child_a != NULL) {
          ga = typeinfo_find(frame, child_a);
          if (ga == 0) pushed = push_typeinfo_type(child_a);
        }  /* if */
        if (child_b != NULL && !pushed) {
          gb = typeinfo_find(frame, child_b);
          if (gb == 0) pushed = push_typeinfo_type(child_b);
        }  /* if */
        if (!pushed && ecil_unsupported_reason == NULL) {
          gid = emit_non_class_typeinfo(frame, t, kind, ga, gb);
          typeinfo_stack_size--;
          if (gid == 0 && ecil_unsupported_reason == NULL) {
            ecil_note_unsupported("a typeinfo that could not be "
                                  "introduced");
          }  /* if */
        }  /* if */
      }  /* if */
    }  /* while */
    if (ecil_unsupported_reason == NULL) result = typeinfo_find(frame, root);
  }  /* if */
  typeinfo_stack_size = base;
  return result;
}  /* ecil_typeinfo_id */


#if CHECKING
static void check_virtual_function_available(a_routine_ptr rout)
/*
A virtual function that a vtable defined here refers to (not pure or
deleted) is defined here, or declared with external linkage (another
translation unit defines it), or was dropped as unsupported (the library then
decides, spec 12.8), or has internal linkage and no definition: the program
declared it in an unnamed namespace or a local class and never defined it (the
front end warns "used but never defined"), which is no error for the vtable
until the function is called.
*/
{
  if (rout->is_virtual && !rout->is_consteval && !rout->pure_virtual &&
      !rout->is_deleted) {
    uint32_t  g = ecil_gid_of(rout);

    check_assertion(g != 0 &&
                    ((ecil_gid_info(g)->flags & ECIL_GF_DEFINED) != 0 ||
                     ecil_gid_info(g)->decl_linkage ==
                                           (unsigned int)elk_external ||
                     (ecil_gid_info(g)->decl_linkage ==
                                           (unsigned int)elk_internal &&
                      !rout->defined) ||
                     (ecil_gid_info(g)->decl_flags &
                                ECIL_DECL_FUNC_UNSUPPORTED) != 0));
  }  /* if */
}  /* check_virtual_function_available */


static void check_vtable_members_available(a_type_ptr t)
/*
A vtable defined here refers to every function in it that is not pure or
deleted: the class's own virtual functions and, for each base class, the final
overrider of each of its virtual functions (a base's function that no class
overrides is its own).  Each is available (check_virtual_function_available).
(The front end's virtual_functions_marked_as_required is not an invariant of a
used vtable: a class whose vtable a derived class's constructor uses can lack
it, so it is not checked.)
*/
{
  a_class_type_supplement_ptr  ctsp = class_type_supp(t);
  a_routine_ptr                rout;
  a_base_class_ptr             bcp;

  if (ctsp->assoc_scope != NULL) {
    for (rout = ctsp->assoc_scope->routines; rout != NULL;
         rout = rout->next) {
      check_virtual_function_available(rout);
    }  /* for */
  }  /* if */
  for (bcp = ctsp->base_classes; bcp != NULL; bcp = bcp->next) {
    a_type_ptr  b = skip_typerefs(bcp->type);

    if (!bcp->is_pack_expansion && is_immediate_class_type(b) &&
        class_type_supp(b)->assoc_scope != NULL) {
      for (rout = class_type_supp(b)->assoc_scope->routines; rout != NULL;
           rout = rout->next) {
        if (rout->is_virtual) {
          an_overriding_virtual_function_ptr  ovr;
          a_routine_ptr                       final = rout;

          for (ovr = bcp->variant.overriding_virtual_functions;
               ovr != NULL; ovr = ovr->next) {
            if (ovr->primary_function == rout) {
              final = ovr->overriding_function;
              break;
            }  /* if */
          }  /* for */
          check_virtual_function_available(final);
        }  /* if */
      }  /* for */
    }  /* if */
  }  /* for */
}  /* check_vtable_members_available */
#endif /* CHECKING */


static void emit_class_abi_data(an_ecil_frame *frame, uint32_t gid)
/*
Decide and send the vtable and class typeinfo of a dynamic class (spec 12.8).
The failure of one class (a function that cannot be declared) drops that
class's vtable only: its own reason is not the caller's.
*/
{
  a_type_ptr        t = (a_type_ptr)ecil_gid_info(gid)->entity;
  a_class_abi_plan  plan;
  an_ecil_saved_reason  caller_reason;

  ecil_save_reason(&caller_reason);
  plan_class_abi(t, gid, &plan);
  if (plan.vtable_defined || plan.vtable_declared) {
    ecil_class_vtable_facts(frame, t);
  }  /* if */
  if (ecil_unsupported_reason == NULL && rtti_enabled &&
      (plan.vtable_defined || plan.vtable_declared)) {
    /* The vtable of a class, even one that is only declared, refers to the
       typeinfo of its class (declared when the key function is elsewhere).
       With rtti = 0 the RTTI slot is null and there is no typeinfo for a
       vtable (spec 12.6). */
    issue_class_typeinfo(frame, t);
    if (plan.vtable_defined && t->variant.class_struct_union.
                                              any_virtual_base_classes) {
      /* The construction vtables (_ZTC) of a class with virtual bases have
         the typeinfo of their base in the RTTI slot: every dynamic base,
         indirect ones included (spec 12.8). */
      a_base_class_ptr  bcp;

      for (bcp = class_type_supp(t)->base_classes; bcp != NULL &&
                                       ecil_unsupported_reason == NULL;
           bcp = bcp->next) {
        if (ecil_class_is_dynamic(bcp->type)) {
          issue_class_typeinfo(frame, skip_typerefs(bcp->type));
        }  /* if */
      }  /* for */
    }  /* if */
  }  /* if */
#if CHECKING
  if (ecil_unsupported_reason == NULL && plan.vtable_defined) {
    check_vtable_members_available(t);
  }  /* if */
#endif /* CHECKING */
  if (ecil_unsupported_reason == NULL &&
      (plan.vtable_defined || plan.vtable_declared)) {
    uint32_t  tv = emit_abi_data(frame, 0u, gid, plan.vtable_linkage,
                                 abi_flags_of(&plan, plan.vtable_defined,
                                              FALSE));

    if (tv != 0) ecil_gid_info(gid)->aux = tv;
    if (tv != 0 && t->variant.class_struct_union.any_virtual_base_classes) {
      /* The VTT follows the vtable, with its linkage (spec 12.3). */
      (void)emit_abi_data(frame, 1u, gid, plan.vtable_linkage,
                          abi_flags_of(&plan, plan.vtable_defined, FALSE));
    }  /* if */
  }  /* if */
  if (plan.vtable_dropped) {
    char  message[ECIL_DIAGNOSTIC_MAX];

    (void)snprintf(message, sizeof(message),
                   "the virtual table of \"%.200s\" refers to a function "
                   "whose definition was dropped; it is not defined here",
                   ecil_entity_name(&t->source_corresp) != NULL ?
                          ecil_entity_name(&t->source_corresp) : "?");
    cir_be_warning(message);
  }  /* if */
  if (ecil_unsupported_reason != NULL) {
    char  message[ECIL_DIAGNOSTIC_MAX];

    (void)snprintf(message, sizeof(message),
                   "the vtable of \"%.200s\" uses %.*s, which the CIR back "
                   "end does not support; the class's virtual table is "
                   "dropped",
                   ecil_entity_name(&t->source_corresp) != NULL ?
                          ecil_entity_name(&t->source_corresp) : "?",
                   ECIL_REASON_MAX - 1, ecil_unsupported_reason);
    cir_be_warning(message);
  }  /* if */
  ecil_restore_reason(&caller_reason);
}  /* emit_class_abi_data */


void ecil_emit_class_abi_data(an_ecil_frame *frame, a_boolean local_classes)
/*
Send the vtable and class typeinfo DECL_ABI_DATA of the dynamic classes
recorded since the last call: at the end of a function's region for the
classes local to it (their entities are alive), at the end of the translation
unit for the others (key functions and "used" are final).
*/
{
  size_t  i;

  /* At the end of the translation unit every name is final: a function
     declared now gets its name at once. */
  if (!local_classes) resolving_late_names = TRUE;
  for (i = 0; i < num_dynamic_classes; i++) {
    uint32_t  gid = dynamic_classes[i];
    unsigned int  flags = ecil_gid_info(gid)->flags;

    if ((flags & ECIL_GF_ABI_DONE) == 0 &&
        ((flags & ECIL_GF_FUNCTION_LOCAL) != 0) == local_classes) {
      ecil_gid_info(gid)->flags |= ECIL_GF_ABI_DONE;
      emit_class_abi_data(frame, gid);
    }  /* if */
  }  /* for */
  if (!local_classes) reissue_class_typeinfos(frame);
  resolving_late_names = FALSE;
  if (local_classes) {
    /* The local classes are done: forget them. */
    size_t  kept = 0;

    for (i = 0; i < num_dynamic_classes; i++) {
      if ((ecil_gid_info(dynamic_classes[i])->flags &
                                           ECIL_GF_FUNCTION_LOCAL) == 0) {
        dynamic_classes[kept++] = dynamic_classes[i];
      }  /* if */
    }  /* for */
    num_dynamic_classes = kept;
  }  /* if */
}  /* ecil_emit_class_abi_data */


static a_boolean routine_instance_is_not_needed(a_routine_ptr rout)
/*
Return TRUE for a template instance that was instantiated only to know the
deduced return type of a function (nothing used it) or that is covered by an
extern template: the front end's remove_unneeded_instantiations would clear
its body, but it never learns of it once the region is freed (spec 11.10).
*/
{
  a_boolean     result = FALSE;
  a_symbol_ptr  sym = (a_symbol_ptr)rout->source_corresp.assoc_info;

  if (!C_mode() && sym != NULL &&
      (symbol_is(sym, sk_routine) || symbol_is(sym, sk_member_function)) &&
      sym->variant.routine.instance_ptr != NULL) {
    a_template_instance_ptr  tip = sym->variant.routine.instance_ptr;
    a_master_instance_ptr    mip = master_instance_of(tip);

    result = mip->already_instantiated && !mip->automatically_instantiated &&
             !tip->explicit_instantiation &&
             (mip->instance_required_count == 0 ||
              tip->explicit_do_not_instantiate);
  }  /* if */
  return result;
}  /* routine_instance_is_not_needed */


void ecil_emit_retractions(an_ecil_frame *frame)
/*
In F_GLOBALS: RETRACT every streamed template instance that is not needed
(see routine_instance_is_not_needed), if it is internal or linkonce_odr, or
if its definition was dropped as unsupported (whatever its linkage: such an
instance must not turn an unsupported body into an error).  For a structor
pair the base variant is named.
*/
{
  uint32_t  gid;
  uint32_t  last = ecil_num_gids();

  if (!C_mode()) {
    for (gid = 1; gid <= last; gid++) {
      an_ecil_gid_info  *info = ecil_gid_info(gid);

      if (info->kind == egk_func && info->entity != NULL &&
          (info->flags & ECIL_GF_FUNCTION_LOCAL) == 0 &&
          info->variant != ECIL_VARIANT_COMPLETE &&
          info->variant != ECIL_VARIANT_DELETING &&
          ((info->flags & ECIL_GF_DEFINED) != 0 ||
           (info->decl_flags & ECIL_DECL_FUNC_UNSUPPORTED) != 0)) {
        a_routine_ptr  rout = (a_routine_ptr)info->entity;
        a_boolean      unsupported = (info->decl_flags &
                                      ECIL_DECL_FUNC_UNSUPPORTED) != 0;

        if (routine_instance_is_not_needed(rout) &&
            (unsupported || info->decl_linkage ==
                                         (unsigned int)elk_internal ||
             info->decl_linkage == (unsigned int)elk_linkonce_odr)) {
          an_ecil_buffer  *saved = ecil_frame_enter_decl(frame);

          ecil_rec1(frame, eop_retract, gid);
          ecil_frame_leave_decl(frame, saved);
          info = ecil_gid_info(gid);
          info->flags &= ~ECIL_GF_DEFINED;
          if (rout->function_def_number != NULL_function_def_number) {
            ecil_set_function_state(rout, efs_retracted);
          }  /* if */
        }  /* if */
      }  /* if */
    }  /* for */
  }  /* if */
}  /* ecil_emit_retractions */


static void add_late_binding(uint32_t gid, uint32_t sid, unsigned int linkage)
/*
Remember a binding for ecil_emit_late_names.
*/
{
  if (num_late_bindings == late_bindings_capacity) {
    size_t          new_capacity = (late_bindings_capacity == 0) ?
                                   16 : late_bindings_capacity * 2;
    a_late_binding  *new_list = (a_late_binding *)realloc(late_bindings,
                                   new_capacity * sizeof(a_late_binding));

    if (new_list != NULL) {
      late_bindings = new_list;
      late_bindings_capacity = new_capacity;
    }  /* if */
  }  /* if */
  if (num_late_bindings < late_bindings_capacity) {
    late_bindings[num_late_bindings].gid = gid;
    late_bindings[num_late_bindings].sid = sid;
    late_bindings[num_late_bindings].linkage = linkage;
    num_late_bindings++;
  } else {
    ecil_note_unsupported("out of memory in the ECIL writer");
  }  /* if */
}  /* add_late_binding */


void ecil_resolve_late_names(an_ecil_frame *frame)
/*
The file scope is complete: every name is final.  Give each function that
was sent without a name its name (re-issuing its declaration if its linkage
or COMDAT changed, with a nil name), and remember the binding for
ecil_emit_late_names.  In CHECKING builds re-mangle every function that was
named when it was sent and assert that the name did not change.
*/
{
  uint32_t  gid;
  uint32_t  last = ecil_num_gids();

  if (!C_mode()) {
#if BACK_END_TYPE == BACK_END_TYPE_CIR
    cir_tu_local_names_are_final();
#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */
    resolving_late_names = TRUE;
    for (gid = 1; gid <= last; gid++) {
      an_ecil_gid_info  *info = ecil_gid_info(gid);

      if (info->kind == egk_func && (info->flags & ECIL_GF_LATE_NAME) != 0 &&
          (info->flags & ECIL_GF_NAME_BOUND) == 0 && info->entity != NULL) {
        a_routine_ptr  rout = (a_routine_ptr)info->entity;
        a_func_state   state;
        uint32_t       final_sid;

        compute_func_state(frame, rout, 0, info->variant,
                           (info->flags & ECIL_GF_DEFINED) != 0,
                           (info->decl_flags &
                                ECIL_DECL_FUNC_UNSUPPORTED) != 0, &state);
        info = ecil_gid_info(gid);
        final_sid = state.name;
        state.name = info->decl_name;
        emit_func_decl_if_changed(frame, gid, &state);
        info = ecil_gid_info(gid);
        info->flags |= ECIL_GF_NAME_BOUND;
        if (final_sid != 0) {
          info->decl_name = final_sid;
          add_late_binding(gid, final_sid, state.linkage);
          if (state.linkage != (unsigned int)elk_internal) {
            symbol_binding_register(final_sid, gid);
          }  /* if */
        }  /* if */
      }  /* if */
    }  /* for */
    resolving_late_names = FALSE;
    /* The mangled name of a class may have changed since CLASS_MANGLE was sent
       (a typedef name for linkage came after the class was needed): the
       latest CLASS_MANGLE wins (spec 12.2).  A class local to a function is
       gone by now; its name was final when its region ended. */
    for (gid = 1; gid <= last; gid++) {
      an_ecil_gid_info  *info = ecil_gid_info(gid);

      if (info->kind == egk_record &&
          (info->flags & ECIL_GF_LAYOUT_FACTS) != 0 &&
          (info->flags & ECIL_GF_FUNCTION_LOCAL) == 0 &&
          info->entity != NULL) {
        uint32_t  sid = intern_class_mangle(frame, (a_type_ptr)info->entity);

        info = ecil_gid_info(gid);
        if (sid != 0 && sid != info->decl_name) {
          emit_class_mangle(frame, gid, sid);
        }  /* if */
      }  /* if */
    }  /* for */
    /* A #pragma redefine_extname (and the alias fixups) is applied at the end
       of the translation unit: a function whose name was sent before gets
       its asm name now.  The name of the entity is updated (spec 7). */
    for (gid = 1; !is_at_least_one_error() && gid <= last; gid++) {
      an_ecil_gid_info  *info = ecil_gid_info(gid);

      if (info->kind == egk_func && (info->flags & ECIL_GF_LATE_NAME) == 0 &&
          (info->flags & ECIL_GF_FUNCTION_LOCAL) == 0 &&
          info->entity != NULL && info->decl_name != 0) {
        a_routine_ptr  rout = (a_routine_ptr)info->entity;

        if (rout->gnu_extra_info != NULL &&
            rout->gnu_extra_info->asm_name != NULL &&
            !ecil_routine_is_filtered(rout) &&
            !routine_is_multiversioned(rout)) {
          const char  *now = routine_symbol_text(rout, info->variant);
          const char  *sent = ecil_string_text(info->decl_name);

          if (now != NULL && sent != NULL && strcmp(now, sent) != 0) {
            a_func_state  state;

            compute_func_state(frame, rout, 0, info->variant,
                               (info->flags & ECIL_GF_DEFINED) != 0,
                               (info->decl_flags &
                                    ECIL_DECL_FUNC_UNSUPPORTED) != 0, &state);
            state.name = ecil_intern_cstring(frame, now);
            ecil_mark_symbol_name(state.name);
            emit_func_decl_if_changed(frame, gid, &state);
          }  /* if */
        }  /* if */
      }  /* if */
    }  /* for */
#if CHECKING
    /* After an error the mangler gives every type a bogus encoding, so the
       names cannot be compared: the compilation fails anyway. */
    for (gid = 1; !is_at_least_one_error() && gid <= last; gid++) {
      an_ecil_gid_info  *info = ecil_gid_info(gid);

      if (info->kind == egk_func && (info->flags & ECIL_GF_LATE_NAME) == 0 &&
          (info->flags & ECIL_GF_FUNCTION_LOCAL) == 0 &&
          info->entity != NULL && info->decl_name != 0) {
        a_routine_ptr  rout = (a_routine_ptr)info->entity;

        if (!ecil_routine_is_filtered(rout) &&
            !routine_is_multiversioned(rout)) {
          const char  *now = routine_symbol_text(rout, info->variant);
          const char  *sent = ecil_string_text(info->decl_name);

          if (now != NULL && sent != NULL &&
              strncmp(now, "__builtin_", 10) != 0 &&
              strcmp(now, sent) != 0) {
            fprintf(stderr, "ECIL: the name of a function changed after it "
                            "was sent: \"%s\" is now \"%s\"\n", sent, now);
            check_assertion_str(FALSE, "an ECIL function name changed after "
                                       "it was sent");
          }  /* if */
        }  /* if */
      }  /* if */
    }  /* for */
#endif /* CHECKING */
  }  /* if */
}  /* ecil_resolve_late_names */


void ecil_emit_late_names(an_ecil_frame *frame)
/*
Write the NAME_BIND records of the names resolved by
ecil_resolve_late_names.
*/
{
  size_t  i;

  for (i = 0; i < num_late_bindings; i++) {
    ecil_rec_begin(frame, eop_name_bind);
    ecil_arg_uint(frame, late_bindings[i].gid);
    ecil_arg_uint(frame, late_bindings[i].sid);
    ecil_arg_uint(frame, late_bindings[i].linkage);
    ecil_rec_end(frame);
  }  /* for */
  num_late_bindings = 0;
}  /* ecil_emit_late_names */


static const char *var_symbol_name(a_variable_ptr var)
/*
The symbol name of an external-linkage variable, or NULL.
*/
{
  const char  *name = NULL;

  if (var->storage_class != (a_storage_class)sc_static &&
      var->source_corresp.name_linkage != (a_name_linkage_kind)nlk_internal) {
    a_boolean  has_asm_name = FALSE;

    name = ecil_entity_name(&var->source_corresp);
#if GNU_EXTENSIONS_ALLOWED || REDEFINE_EXTNAME_PRAGMA_ENABLED
    if (var->asm_name_is_valid && var->asm_name_or_reg.name != NULL) {
      name = var->asm_name_or_reg.name;
      has_asm_name = TRUE;
    }  /* if */
#endif /* GNU_EXTENSIONS_ALLOWED || REDEFINE_EXTNAME_PRAGMA_ENABLED */
    (void)has_asm_name;
#if BACK_END_TYPE == BACK_END_TYPE_CIR
    if (!C_mode() && name != NULL && !has_asm_name) {
      /* The Itanium name; a name that is not final has no symbol yet. */
      if (var->source_corresp.is_local_to_function ||
          cir_name_may_change(&var->source_corresp)) {
        name = NULL;
      } else if (variable_name_mangling_needed(var)) {
        name = get_mangled_variable_name(var);
      }  /* if */
    }  /* if */
#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */
  }  /* if */
  return name;
}  /* var_symbol_name */


a_boolean ecil_variable_may_have_multiple_copies(a_variable_ptr var)
/*
Return TRUE for a C++ variable that may be defined in several translation
units: an inline variable, a variable template, or a static data member of
a class template instance.
*/
{
  a_symbol_ptr  sym = (a_symbol_ptr)var->source_corresp.assoc_info;

  return !C_mode() && (!var->is_specialized || var->is_inline) &&
         (var->is_inline || var->is_template_variable ||
          (var->source_corresp.is_class_member && sym != NULL &&
           symbol_is(sym, sk_static_data_member) &&
           sym->variant.static_data_member.instance_ptr != NULL));
}  /* ecil_variable_may_have_multiple_copies */


#if BACK_END_TYPE == BACK_END_TYPE_CIR

static a_routine_ptr local_routine_owner(a_routine_ptr rout,
                                         a_type_ptr    *local_class)
/*
For a routine that lives in a function (a member of a class that is local to
it, nested classes included: a local class's function, a lambda's call
operator, a special member, a static invoker), return the innermost function
that encloses it and set *local_class to the class that is declared directly
in that function.  Return NULL for any other routine.
*/
{
  a_routine_ptr  owner = NULL;
  a_type_ptr     cls;

  if (rout->source_corresp.is_class_member) {
    cls = parent_class_of(rout);
    while (!cls->source_corresp.is_local_to_function &&
           cls->source_corresp.is_class_member) {
      cls = parent_class_of(cls);
    }  /* while */
    if (cls->source_corresp.is_local_to_function) {
      owner = enclosing_routine_for_local_type_or_null(cls);
      *local_class = cls;
    }  /* if */
  }  /* if */
  return owner;
}  /* local_routine_owner */


a_boolean ecil_routine_has_vague_linkage(a_routine_ptr rout)
/*
Return TRUE for a C++ routine that every translation unit that needs it
defines, which the linker keeps one of (linkonce_odr or weak_odr with COMDAT,
spec 11.2): an inline function, a template instance, a member defined in its
class, a member of an unnamed class declared in a class, and what lives in
such a function (the member functions of its local classes, a lambda's call
operator, the static invoker), at any depth.  A routine that is local to the
translation unit, or that lives in a function that is not discardable, does
not have it.  The rule is the one of Itanium and clang: the entities local to
a function share its linkage when the function is discardable, and are
internal when it is not.
*/
{
  a_boolean      result = FALSE;
  a_boolean      local_to_tu = FALSE;
  a_type_ptr     local_class = NULL;
  a_routine_ptr  owner;

  if (!C_mode()) {
    /* The routine's own template arguments and parameter types count too (a
       generic lambda's call operator of a type in an unnamed namespace). */
    local_to_tu = cir_entity_is_tu_local(iek_routine, rout);
    for (owner = local_routine_owner(rout, &local_class); owner != NULL;
         owner = local_routine_owner(rout, &local_class)) {
      /* The local class decides too: it may be made of local entities, and
         a lambda that is not subject to translation unit correspondence has
         no name that other translation units can repeat. */
      local_to_tu = local_to_tu || cir_entity_is_tu_local(iek_type,
                                                          local_class);
      if (type_is_lambda_closure(local_class)) {
        a_class_symbol_supplement_ptr  cssp =
                                      symbol_supplement_for_class(local_class);

        local_to_tu = local_to_tu || cssp == NULL ||
                      !cssp->lambda_subject_to_trans_unit_corresp;
      }  /* if */
      rout = owner;
    }  /* for */
    if (!local_to_tu) {
      a_boolean  nested_unnamed = cir_routine_in_nested_unnamed_class(rout);

      result = (nested_unnamed ||
                (ecil_linkage_for(rout->storage_class,
                                  rout->source_corresp.name_linkage,
                                  FALSE) != (unsigned int)elk_internal &&
                 rout->storage_class == (a_storage_class)sc_unspecified)) &&
               !cir_entity_is_tu_local(iek_routine, rout) &&
               (nested_unnamed ? rout->is_inline != 0 :
                                 routine_might_exist_in_multiple_copies(rout));
    }  /* if */
  }  /* if */
  return result;
}  /* ecil_routine_has_vague_linkage */

#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */


a_boolean ecil_local_static_may_have_multiple_copies(a_variable_ptr var)
/*
Return TRUE for a static local variable of a function that has vague linkage
(an inline function, a template instance, a function defined in its class,
or a function that lives in one: ecil_routine_has_vague_linkage): the
variable has vague linkage too (linkonce_odr, COMDAT; spec 11.2), and so has
its guard, and every copy must initialize it the same way.  The variable of
an anonymous union is one (it has the name of the first member).
*/
{
  a_boolean  result = FALSE;
#if BACK_END_TYPE == BACK_END_TYPE_CIR
  a_routine_ptr  fn = var->source_corresp.enclosing_routine;

  if (!C_mode() && var->source_corresp.is_local_to_function && fn != NULL &&
      cir_local_static_source_name(var) != NULL) {
    result = ecil_routine_has_vague_linkage(fn);
  }  /* if */
#else /* BACK_END_TYPE != BACK_END_TYPE_CIR */
  (void)var;
#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */
  return result;
}  /* ecil_local_static_may_have_multiple_copies */


#if BACK_END_TYPE == BACK_END_TYPE_CIR

void ecil_cxx_variable_facts(an_ecil_frame *frame,
                             a_variable_ptr var,
                             a_boolean      defining,
                             unsigned int   *linkage,
                             unsigned int   *flags,
                             uint32_t       *name_sid)
/*
The symbol name, linkage and DECL_VAR flags (STATIC_LOCAL, COMDAT) of a C++
variable at namespace, class or function scope (spec 11.2): its Itanium name
(the local-entity name for a static local), internal linkage winning over
the vague-linkage rules, and linkonce_odr with COMDAT for what may exist in
several translation units (inline variables, template static data members,
the statics of such a function) when it is defined here.
*/
{
  const char  *name = NULL;
  a_boolean   has_asm_name = FALSE;
  char        unique[1024];

  unique[0] = '\0';
  *flags = 0;
  *linkage = ecil_linkage_for(var->storage_class,
                              var->source_corresp.name_linkage, var->is_weak);
  if (var->source_corresp.is_local_to_function) {
    a_routine_ptr  fn = var->source_corresp.enclosing_routine;

    *flags |= ECIL_DECL_VAR_STATIC_LOCAL;
    *linkage = (unsigned int)elk_internal;
    if (fn == NULL || cir_local_static_source_name(var) == NULL) {
      /* Compiler-generated (__func__ and the like): an internal symbol
         that no other translation unit can name. */
      (void)snprintf(unique, sizeof(unique), "%.400s.%.400s",
                     (fn != NULL && fn->source_corresp.name != NULL) ?
                              fn->source_corresp.name : "static",
                     var->source_corresp.name != NULL ?
                              var->source_corresp.name : "var");
    } else if (cir_routine_name_may_change(fn)) {
      ecil_note_unsupported("a static local of a function whose name is not "
                            "final");
    } else {
      name = cir_local_static_name(var);
      if (defining && ecil_local_static_may_have_multiple_copies(var)) {
        *linkage = (unsigned int)elk_linkonce_odr;
        *flags |= ECIL_DECL_VAR_COMDAT;
      }  /* if */
    }  /* if */
  } else {
    name = ecil_entity_name(&var->source_corresp);
#if GNU_EXTENSIONS_ALLOWED || REDEFINE_EXTNAME_PRAGMA_ENABLED
    if (var->asm_name_is_valid && var->asm_name_or_reg.name != NULL) {
      name = var->asm_name_or_reg.name;
      has_asm_name = TRUE;
    }  /* if */
#endif /* GNU_EXTENSIONS_ALLOWED || REDEFINE_EXTNAME_PRAGMA_ENABLED */
    if (name == NULL && !has_asm_name) {
      /* The object of an anonymous union at namespace scope (it has
         internal linkage): a symbol that nothing else can name. */
      (void)snprintf(unique, sizeof(unique), "%s", "anonymous");
      *linkage = (unsigned int)elk_internal;
    }  /* if */
    if (name != NULL && !has_asm_name) {
      if (cir_name_may_change(&var->source_corresp)) {
        ecil_note_unsupported("a variable whose name is not final");
        name = NULL;
      } else if (variable_name_mangling_needed(var)) {
        name = get_mangled_variable_name(var);
      }  /* if */
    }  /* if */
    if (*linkage == (unsigned int)elk_external &&
        cir_entity_is_tu_local(iek_variable, var)) {
      /* Its name would be the same in another translation unit, where it
         is another entity (spec 11.2). */
      *linkage = (unsigned int)elk_internal;
    }  /* if */
    if (defining && *linkage == (unsigned int)elk_external &&
        ecil_variable_may_have_multiple_copies(var)) {
      a_symbol_ptr  sym = (a_symbol_ptr)var->source_corresp.assoc_info;
      a_boolean     explicit_definition = FALSE;

      if (var->explicit_instantiation && !var->explicit_do_not_instantiate) {
        explicit_definition = TRUE;
      } else if (sym != NULL && symbol_is(sym, sk_static_data_member) &&
                 sym->variant.static_data_member.instance_ptr != NULL) {
        a_template_instance_ptr  tip =
                              sym->variant.static_data_member.instance_ptr;

        explicit_definition = tip->explicit_instantiation &&
                              !tip->explicit_do_not_instantiate;
      }  /* if */
      /* An explicit instantiation definition is weak_odr (spec 11.2). */
      *linkage = (unsigned int)(explicit_definition ? elk_weak_odr :
                                                      elk_linkonce_odr);
      *flags |= ECIL_DECL_VAR_COMDAT;
    }  /* if */
  }  /* if */
  if (unique[0] != '\0' && ecil_unsupported_reason == NULL) {
    uint32_t  gid = ecil_gid_of(var);

    if (gid != 0 && ecil_gid_info(gid)->decl_emitted &&
        ecil_gid_info(gid)->decl_name != 0) {
      *name_sid = ecil_gid_info(gid)->decl_name;      /* Keep its name. */
    } else {
      *name_sid = ecil_unique_symbol_name(frame, unique);
    }  /* if */
  } else {
    *name_sid = (name != NULL && ecil_unsupported_reason == NULL) ?
                                    ecil_intern_cstring(frame, name) : 0;
  }  /* if */
}  /* ecil_cxx_variable_facts */

#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */


a_targ_alignment ecil_variable_alignment(a_variable_ptr var)
/*
The explicit alignment of a variable, in bytes (0 for the natural one): its
own alignment attribute, or that of a typedef with an alignment attribute.
*/
{
  a_targ_alignment  result = var->alignment;
  a_targ_alignment  natural = skip_typerefs(var->type)->alignment;
  a_type_ptr        t = var->type;

  /* The typedef may sit under a cv-qualified type (volatile t_align). */
  while (t->kind == tk_typeref) {
    if (t->alignment > result && t->alignment > natural) {
      result = t->alignment;
    }  /* if */
    t = t->variant.typeref.type;
  }  /* while */
  return result;
}  /* ecil_variable_alignment */


uint32_t ecil_variable_symbol_gid(an_ecil_frame *frame, a_variable_ptr var)
/*
The G-id already bound to this variable's symbol name, or 0.
*/
{
  const char  *name = var_symbol_name(var);
  uint32_t    gid = 0;

  if (name != NULL) {
    gid = symbol_binding_lookup(ecil_intern_cstring(frame, name), egk_var);
  }  /* if */
  return gid;
}  /* ecil_variable_symbol_gid */


uint32_t ecil_global_variable_id(an_ecil_frame *frame, a_variable_ptr var)
/*
Return the G-id of a file-scope variable (or a static local that has its
declaration statement), introducing it with DECL_VAR (as a declaration) at
its first reference.
*/
{
  uint32_t  gid = ecil_gid_of(var);

  if (gid == 0 && ecil_unsupported_reason == NULL) {
    uint32_t  type;

    ecil_check_variable_attributes(var);
    type = ecil_unsupported_reason == NULL ?
                                    ecil_type_id(frame, var->type) : 0;

    if (type != 0 && ecil_variable_symbol_gid(frame, var) != 0) {
      gid = ecil_variable_symbol_gid(frame, var);
      ecil_set_gid(var, gid);
    } else if (type != 0) {
      gid = ecil_new_gid(egk_var);
      if (gid != 0) {
        unsigned int      linkage = ecil_linkage_for(var->storage_class,
                                       var->source_corresp.name_linkage,
                                       var->is_weak);
        unsigned int      flags = 0;
        uint32_t          name_sid = 0;
        const char        *name = ecil_entity_name(&var->source_corresp);

        ecil_gid_info(gid)->entity = var;
        ecil_set_gid(var, gid);
        if (is_const_qualified_type(var->type)) flags |= ECIL_DECL_VAR_CONST;
        if (is_effective_thread_local(var)) flags |= ECIL_DECL_VAR_TLS;
#if BACK_END_TYPE == BACK_END_TYPE_CIR
        if (!C_mode()) {
          unsigned int  cxx_flags = 0;

          ecil_cxx_variable_facts(frame, var, FALSE, &linkage, &cxx_flags,
                                  &name_sid);
          flags |= cxx_flags;
          name = NULL;
        } else
#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */
        /* Do not insert code here. */
        {
#if GNU_EXTENSIONS_ALLOWED || REDEFINE_EXTNAME_PRAGMA_ENABLED
          if (var->asm_name_is_valid && var->asm_name_or_reg.name != NULL) {
            name = var->asm_name_or_reg.name;
          }  /* if */
#endif /* GNU_EXTENSIONS_ALLOWED || REDEFINE_EXTNAME_PRAGMA_ENABLED */
          if (name != NULL) name_sid = ecil_intern_cstring(frame, name);
        }
        if (name_sid != 0 && linkage != (unsigned int)elk_internal) {
          ecil_mark_symbol_name(name_sid);
          symbol_binding_register(name_sid, gid);
        }  /* if */
        if (name_sid == 0 && !C_mode() && ecil_unsupported_reason != NULL) {
          /* The name could not be made: the entity is introduced as an
             unsupported unnamed one, so that a use of it that survives is
             reported by the consumer instead of binding to private
             storage. */
          linkage = (unsigned int)elk_internal;
          flags |= ECIL_DECL_VAR_UNSUPPORTED;
          ecil_remember_drop_reason(gid, ecil_unsupported_reason);
        }  /* if */
        ecil_emit_decl_var(frame, gid, type, linkage, flags,
                           (uint32_t)ecil_variable_alignment(var), name_sid);
      }  /* if */
    }  /* if */
  }  /* if */
  return gid;
}  /* ecil_global_variable_id */


void ecil_check_variable_use(uint32_t gid)
/*
A use of the object of a variable whose record type was sent without a body
(a class that is not supported) cannot be expressed: note why.
*/
{
  if (gid != 0 && ecil_unsupported_reason == NULL) {
    uint32_t  t = ecil_gid_info(gid)->decl_type;

    if (t != 0 && (ecil_gid_info(t)->flags & ECIL_GF_FAILED) != 0) {
      ecil_note_unsupported_type(t, "a variable of a record type that is "
                                    "not supported");
    }  /* if */
  }  /* if */
}  /* ecil_check_variable_use */


uint32_t ecil_variable_id(an_ecil_frame *frame, a_variable_ptr var)
/*
Return the G-id of a file-scope or static variable, introducing it with
DECL_VAR (as a declaration) at its first reference.  A static local with no
declaration statement (__func__, a compound literal) that is first reached
here is introduced as a definition and queued: its initializer follows when
the queue is drained (ecil_drain_variable_definitions), so that building one
constant never builds another.
*/
{
  uint32_t  gid = 0;

  if (var->source_corresp.is_local_to_function &&
      var->storage_class != (a_storage_class)sc_static &&
      var->storage_class != (a_storage_class)sc_extern) {
    /* An automatic variable (C too) has a local id (the id stored in its IL entry is
       that of a local, not a G-id): its address cannot be a constant. */
    ecil_note_unsupported("the address of a local variable in a constant");
  } else {
    gid = ecil_gid_of(var);
    if (gid == 0 && var->source_corresp.is_local_to_function &&
        var->storage_class == (a_storage_class)sc_static &&
        ecil_unsupported_reason == NULL) {
      gid = ecil_queue_variable_definition(frame, var);
    } else {
      gid = ecil_global_variable_id(frame, var);
    }  /* if */
  }  /* if */
  return gid;
}  /* ecil_variable_id */


/* Conditionally close the "edg" namespace. */
END_EDG_NAMESPACE

#endif /* ECIL_WRITER_ENABLED */
