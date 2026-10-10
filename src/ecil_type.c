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

#if ECIL_WRITER_ENABLED

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
  a_param_type_ptr
                param_cursor;     /* Function: next parameter to visit. */
  a_field_ptr   field_cursor;     /* Record: next field to visit. */
  a_boolean     promote_params;   /* Function: K&R promoted form. */
} a_type_work_item;

/* The type work stack and the list of records still awaiting a body. */
STATIC_THREAD a_type_work_item
                *type_stack;
STATIC_THREAD size_t
                type_stack_size;
STATIC_THREAD size_t
                type_stack_capacity;
STATIC_THREAD uint32_t
                *pending_records;
STATIC_THREAD size_t
                num_pending_records;
STATIC_THREAD size_t
                pending_records_capacity;

void ecil_type_reset(void)
/*
Forget the writer's type state at the end of a compilation.
*/
{
  free(type_stack);
  free(pending_records);
  type_stack = NULL;
  pending_records = NULL;
  type_stack_size = type_stack_capacity = 0;
  num_pending_records = pending_records_capacity = 0;
}  /* ecil_type_reset */


const char *ecil_entity_name(a_source_correspondence *scp)
/*
Return the plain (C) name of an entity, or NULL if it has none.
*/
{
  return scp->name;
}  /* ecil_entity_name */


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
    info->size = (uint32_t)type->size;
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
    item->param_cursor = NULL;
    item->field_cursor = NULL;
    item->promote_params = FALSE;
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


static a_boolean need_type(a_type_ptr child)
/*
Return TRUE if the child type has an id; otherwise push it (and return FALSE)
so the caller re-visits its own item afterwards.
*/
{
  a_boolean known = known_type_id(child) != 0;

  if (!known) (void)push_type(child, tph_start);
  return known;
}  /* need_type */


static uint32_t finish_pointer(an_ecil_frame *frame, a_type_ptr t)
/*
Make the ptr type of a pointer whose pointee has an id.
*/
{
  return ecil_ptr_type(frame, known_type_id(t->variant.pointer.type));
}  /* finish_pointer */


uint32_t ecil_array_of(an_ecil_frame *frame, uint32_t elem, uint64_t count)
/*
Return the G-id of the complete array type of count elements of type elem
(for a type the front end does not have, such as the one-element array of a
tentative definition with an incomplete array type, C11 6.9.2p5).
*/
{
  uint32_t  key[4];
  uint32_t  gid;

  key[0] = (uint32_t)egk_array;
  key[1] = elem;
  key[2] = (uint32_t)(count & 0xFFFFFFFFu);
  key[3] = (uint32_t)(count >> 32);
  gid = ecil_word_lookup(key, 4);
  if (gid == 0) {
    gid = ecil_new_gid(egk_array);
    if (gid != 0) {
      an_ecil_gid_info  *info = ecil_gid_info(gid);
      an_ecil_gid_info  *einfo = ecil_gid_info(elem);
      an_ecil_buffer    *saved;

      info->ref = elem;
      info->count = count;
      info->size = (uint32_t)(einfo->size * count);
      info->align = einfo->align;
      info->flags = einfo->flags & (ECIL_GF_OPAQUE | ECIL_GF_ABI_OPAQUE |
                                    ECIL_GF_FAILED);
      if (info->flags != 0) {
        info->flags = ECIL_GF_ABI_OPAQUE | (info->flags & ECIL_GF_FAILED);
      }  /* if */
      ecil_word_register(key, 4, gid);
      saved = ecil_frame_enter_decl(frame);
      ecil_rec_begin(frame, eop_ty_array);
      ecil_arg_uint(frame, gid);
      ecil_arg_uint(frame, elem);
      ecil_arg_uint(frame, count);
      ecil_arg_uint(frame, 0);
      ecil_rec_end(frame);
      ecil_frame_leave_decl(frame, saved);
    }  /* if */
  }  /* if */
  return gid;
}  /* ecil_array_of */


static uint32_t finish_array(an_ecil_frame *frame, a_type_ptr t)
/*
Make the array type of an array whose element type has an id.
*/
{
  uint32_t      elem = known_type_id(t->variant.array.element_type);
  uint32_t      key[4];
  uint32_t      gid;
  a_boolean     incomplete = t->incomplete &&
                             !t->variant.array.bound_is_zero;
  uint64_t      count = incomplete ? 0 :
                        (uint64_t)t->variant.array.variant.number_of_elements;
  unsigned int  flags = 0;

  if (incomplete) flags |= ECIL_ARRAY_INCOMPLETE;
  if (t->variant.array.bound_is_zero) flags |= ECIL_ARRAY_ZERO_LENGTH;
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
      info->size = (uint32_t)t->size;
      info->align = einfo->align;
      info->flags = einfo->flags & (ECIL_GF_OPAQUE | ECIL_GF_ABI_OPAQUE |
                                    ECIL_GF_FAILED);
      if (info->flags != 0) {
        info->flags = ECIL_GF_ABI_OPAQUE | (info->flags & ECIL_GF_FAILED);
      }  /* if */
      ecil_word_register(key, 4, gid);
      saved = ecil_frame_enter_decl(frame);
      ecil_rec_begin(frame, eop_ty_array);
      ecil_arg_uint(frame, gid);
      ecil_arg_uint(frame, elem);
      if (incomplete) {
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


static a_type_ptr promoted_param_type(a_type_ptr type,
                                      uint32_t   *promoted_gid,
                                      an_ecil_frame *frame)
/*
For a K&R definition: the default-promoted ECIL type of a parameter type.
Sets *promoted_gid to the G-id (the parameter's own G-id if unchanged).
Returns the IL type (unused by callers other than for symmetry).
*/
{
  uint32_t          gid = known_type_id(type);
  an_ecil_gid_info  *info = ecil_gid_info(gid);

  if ((info->kind == egk_int && info->bits < 32) || info->kind == egk_bool) {
    gid = ecil_intern_int_type(frame, 32, TRUE, 4, 4);
  } else if (info->kind == egk_float &&
             ecil_is_plain_float_type(type)) {
    gid = ecil_intern_float_type(frame, (unsigned int)eff_ieee_double, 8, 8);
  }  /* if */
  *promoted_gid = gid;
  return type;
}  /* promoted_param_type */


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

  if (list_params) {
    for (p = supp->param_type_list; p != NULL; p = p->next) n++;
  }  /* if */
  key = (uint32_t *)malloc((n + 3) * sizeof(uint32_t));
  params = (uint32_t *)malloc((n + 1) * sizeof(uint32_t));
  if (key == NULL || params == NULL) {
    ecil_note_unsupported("out of memory in the ECIL writer");
  } else {
    size_t  i = 0;

    if (supp->has_ellipsis) flags |= ECIL_FUNC_TYPE_VARIADIC;
    if (no_proto) flags |= ECIL_FUNC_TYPE_NO_PROTO;
    if (list_params) {
      for (p = supp->param_type_list; p != NULL; p = p->next) {
        uint32_t  pg = known_type_id(p->type);

        if (no_proto) (void)promoted_param_type(p->type, &pg, frame);
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
        if (supp->has_ellipsis) info->flags |= ECIL_GF_VARARGS;
        if (no_proto) info->flags |= ECIL_GF_NO_PROTO;
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

static void note_pending_record(uint32_t gid)
/*
Remember a record that was declared while still incomplete.
*/
{
  if (num_pending_records == pending_records_capacity) {
    size_t    new_capacity = (pending_records_capacity == 0) ?
                             32 : pending_records_capacity * 2;
    uint32_t  *new_list = (uint32_t *)realloc(pending_records,
                                       new_capacity * sizeof(uint32_t));

    if (new_list != NULL) {
      pending_records = new_list;
      pending_records_capacity = new_capacity;
    }  /* if */
  }  /* if */
  if (num_pending_records < pending_records_capacity) {
    pending_records[num_pending_records++] = gid;
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


static void emit_record_fields(an_ecil_frame *frame, a_type_ptr t,
                               uint32_t gid)
/*
Emit TY_RECORD_BODY and the REC_FIELD records of a complete record whose
field types all have ids; assign each field its member index.
*/
{
  an_ecil_gid_info  *info = ecil_gid_info(gid);
  a_field_ptr       field;
  uint64_t          n = 0;
  uint64_t          idx = 0;
  unsigned int      body_flags = 0;
  an_ecil_buffer    *saved;

  for (field = t->variant.class_struct_union.field_list; field != NULL;
       field = field->next) {
    n++;
  }  /* for */
  if (t->variant.class_struct_union.is_packed) {
    body_flags |= ECIL_RECORD_PACKED;
  }  /* if */
  if (t->variant.class_struct_union.contains_flexible_array_member) {
    body_flags |= ECIL_RECORD_FLEXIBLE_ARRAY;
  }  /* if */
  info->count = n;
  /* Intern the names first: a STR must not fall between the body record and
     its REC_FIELD records. */
  for (field = t->variant.class_struct_union.field_list; field != NULL;
       field = field->next) {
    if (!field_is_unnamed(field)) {
      (void)ecil_intern_cstring(frame, field->source_corresp.name);
    }  /* if */
  }  /* for */
  saved = ecil_frame_enter_decl(frame);
  ecil_rec_begin(frame, eop_ty_record_body);
  ecil_arg_uint(frame, gid);
  ecil_arg_uint(frame, (uint64_t)t->size);
  ecil_arg_uint(frame, (uint64_t)t->size);
  ecil_arg_uint(frame, (uint64_t)t->alignment);
  ecil_arg_uint(frame, body_flags);
  ecil_arg_uint(frame, n);
  ecil_rec_end(frame);
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

      if (finfo->flags & (ECIL_GF_OPAQUE | ECIL_GF_ABI_OPAQUE)) {
        info->flags |= ECIL_GF_ABI_OPAQUE;
      }  /* if */
      if (finfo->kind == egk_record && finfo->size == 0 &&
          (finfo->flags & ECIL_GF_COMPLETE)) {
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
  ecil_frame_leave_decl(frame, saved);
  info->flags |= ECIL_GF_COMPLETE;
}  /* emit_record_fields */


static void emit_record_body(an_ecil_frame *frame, a_type_ptr t, uint32_t gid)
/*
Emit the body of a complete record whose field types all have ids, unless
a member is a record that failed: then this record fails too (it keeps its
declaration only, and uses that need it complete are unsupported).
*/
{
  a_field_ptr  field;
  a_boolean    failed = FALSE;

  for (field = t->variant.class_struct_union.field_list;
       field != NULL && !failed; field = field->next) {
    uint32_t  fg = known_type_id(field->type);

    failed = fg != 0 && (ecil_gid_info(fg)->flags & ECIL_GF_FAILED) != 0;
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
          if (t->variant.pointer.is_reference) {
            ecil_set_gid(t, ecil_opaque_type(frame, t));
            type_stack_size--;
          } else {
            type_stack[idx].phase = tph_children;
          }  /* if */
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
            info->size = (uint32_t)t->size;
            info->align = (uint32_t)t->alignment;
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
            if (t->incomplete) {
              note_pending_record(gid);
              type_stack_size--;
            } else {
              type_stack[idx].phase = tph_fields;
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
        if (need_type(t->variant.pointer.type)) {
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
      } else {
        /* A function type: the return type, then each parameter. */
        a_boolean complete = TRUE;

        if (!type_stack[idx].ret_done) {
          if (need_type(t->variant.routine.return_type)) {
            idx = type_stack_size - 1;
            type_stack[idx].ret_done = TRUE;
          } else {
            complete = FALSE;
          }  /* if */
        }  /* if */
        while (complete && type_stack[idx].param_cursor != NULL) {
          if (need_type(type_stack[idx].param_cursor->type)) {
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


void ecil_complete_pending_records(an_ecil_frame *frame)
/*
Emit the body of every record that was declared incomplete and has been
completed since.  Those still incomplete stay pending.  A record the stream
cannot describe (an _Atomic member, a _BitInt bit-field, an unsupported
member) is marked failed instead; that never makes the caller's own
construct unsupported, so the reason of the caller is kept aside.
*/
{
  size_t      i;
  size_t      kept = 0;
  const char  *caller_reason = ecil_unsupported_reason;

  ecil_unsupported_reason = NULL;
  for (i = 0; i < num_pending_records; i++) {
    uint32_t          gid = pending_records[i];
    an_ecil_gid_info  *info = ecil_gid_info(gid);
    a_type_ptr        t = (a_type_ptr)info->entity;

    if (t->incomplete) {
      pending_records[kept++] = gid;
    } else if (has_bit_precise_bit_field(t) || record_has_atomic_member(t)) {
      info->flags |= ECIL_GF_FAILED | ECIL_GF_ABI_OPAQUE;
    } else {
      size_t            base = type_stack_size;
      a_type_work_item  *item = push_type(t, tph_fields);

      if (item != NULL) {
        item->field_cursor = t->variant.class_struct_union.field_list;
        run_type_stack(frame, base);
      }  /* if */
      if (ecil_unsupported_reason != NULL) {
        /* run_type_stack marked the record failed. */
        info->flags |= ECIL_GF_FAILED | ECIL_GF_ABI_OPAQUE;
        ecil_unsupported_reason = NULL;
      }  /* if */
    }  /* if */
  }  /* for */
  num_pending_records = kept;
  ecil_unsupported_reason = caller_reason;
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


static const char *const library_names[] = {
  "abort", "abs", "acos", "acosf", "acosh", "acoshf", "acoshl", "acosl",
  "aligned_alloc", "asin", "asinf", "asinh", "asinhf", "asinhl", "asinl",
  "atan", "atan2", "atan2f", "atan2l", "atanf", "atanh", "atanhf",
  "atanhl", "atanl", "atoi", "atol", "atoll", "bcmp", "bcopy", "bzero",
  "calloc", "cbrt", "cbrtf", "cbrtl", "ceil", "ceilf", "ceill",
  "copysign", "copysignf", "copysignl", "cos", "cosf", "cosh", "coshf",
  "coshl", "cosl", "erf", "erfc", "erfcf", "erfcl", "erff", "erfl",
  "exit", "_exit", "_Exit", "exp", "exp10", "exp10f", "exp10l", "exp2",
  "exp2f", "exp2l", "expf", "expl", "expm1", "expm1f", "expm1l", "fabs",
  "fabsf", "fabsl", "fdim", "fdimf", "fdiml", "ffs", "ffsl", "ffsll",
  "floor", "floorf", "floorl", "fma", "fmaf", "fmal", "fmax", "fmaxf",
  "fmaxl", "fmin", "fminf", "fminl", "fmod", "fmodf", "fmodl", "fprintf",
  "fputc", "fputs", "free", "frexp", "frexpf", "frexpl", "fscanf",
  "fwrite", "hypot", "hypotf", "hypotl", "ilogb", "ilogbf", "ilogbl",
  "imaxabs", "index", "isalnum", "isalpha", "isascii", "isblank",
  "iscntrl", "isdigit", "isgraph", "islower", "isprint", "ispunct",
  "isspace", "isupper", "isxdigit", "labs", "ldexp", "ldexpf", "ldexpl",
  "lgamma", "lgammaf", "lgammal", "llabs", "llrint", "llrintf",
  "llrintl", "llround", "llroundf", "llroundl", "log", "log10", "log10f",
  "log10l", "log1p", "log1pf", "log1pl", "log2", "log2f", "log2l",
  "logb", "logbf", "logbl", "logf", "logl", "lrint", "lrintf", "lrintl",
  "lround", "lroundf", "lroundl", "malloc", "memchr", "memcmp", "memcpy",
  "memmove", "mempcpy", "memset", "modf", "modff", "modfl", "nan",
  "nanf", "nanl", "nearbyint", "nearbyintf", "nearbyintl", "nextafter",
  "nextafterf", "nextafterl", "nexttoward", "nexttowardf", "nexttowardl",
  "pow", "pow10", "pow10f", "pow10l", "powf", "powl", "printf",
  "putchar", "puts", "realloc", "remainder", "remainderf", "remainderl",
  "remquo", "remquof", "remquol", "rindex", "rint", "rintf", "rintl",
  "round", "roundf", "roundl", "scalbln", "scalblnf", "scalblnl",
  "scalbn", "scalbnf", "scalbnl", "scanf", "sin", "sinf", "sinh",
  "sinhf", "sinhl", "sinl", "snprintf", "sprintf", "sqrt", "sqrtf",
  "sqrtl", "sscanf", "stpcpy", "stpncpy", "strcasecmp", "strcat",
  "strchr", "strcmp", "strcpy", "strcspn", "strdup", "strlen",
  "strncasecmp", "strncat", "strncmp", "strncpy", "strndup", "strnlen",
  "strpbrk", "strrchr", "strspn", "strstr", "tan", "tanf", "tanh",
  "tanhf", "tanhl", "tanl", "tgamma", "tgammaf", "tgammal", "tolower",
  "toupper", "trunc", "truncf", "truncl", "vfprintf", "vfscanf",
  "vprintf", "vscanf", "vsnprintf", "vsprintf", "vsscanf", NULL
};


static a_boolean in_library_list(const char *base)
/*
Return TRUE if base is the name of a listed libc/libm function.
*/
{
  size_t    i;
  a_boolean found = FALSE;

  for (i = 0; library_names[i] != NULL && !found; i++) {
    found = strcmp(library_names[i], base) == 0;
  }  /* for */
  return found;
}  /* in_library_list */


a_boolean ecil_is_library_function(const char *name)
/*
Return TRUE if name is, as written, a listed libc/libm function (the front
end declares some of them as builtin routines under their plain names).
*/
{
  return in_library_list(name);
}  /* ecil_is_library_function */


const char *ecil_library_builtin_name(const char *name)
/*
Return the library function behind a __builtin_X name (X, a pointer into
name) when X is an ordinary C library function that the code generator can
simply call; NULL for other builtins.  (Design.md P2: a call to such a
builtin is an ordinary CALL of the library function.)  The rule is by name:
the front end's builtin tables do not say which builtins have a libc
counterpart, so the libc/libm names (C99 plus the common POSIX/GNU string and
ctype functions) are listed here.  Builtins that ECIL has BUILTIN records for
(ffs, alloca, bswap, ...) are tested before this list, in ecil_code.c.
*/
{

  const char  *result = NULL;
  const char  *base = name + 10;

  if (strncmp(name, "__builtin_", 10) == 0 && in_library_list(base)) {
    result = base;
  }  /* if */
  return result;
}  /* ecil_library_builtin_name */


/* The facts a DECL_FUNC carries. */
typedef struct a_func_state {
  uint32_t      type;
  unsigned int  linkage;
  unsigned int  flags;
  uint32_t      name;
} a_func_state;


static uint32_t routine_name_sid(an_ecil_frame *frame, a_routine_ptr rout)
/*
Return the sid of the symbol name of a routine (its asm name if it has one).
*/
{
  const char  *name = ecil_entity_name(&rout->source_corresp);

#if GNU_EXTENSIONS_ALLOWED || REDEFINE_EXTNAME_PRAGMA_ENABLED
  if (rout->gnu_extra_info != NULL && rout->gnu_extra_info->asm_name != NULL) {
    name = rout->gnu_extra_info->asm_name;
  }  /* if */
#endif /* GNU_EXTENSIONS_ALLOWED || REDEFINE_EXTNAME_PRAGMA_ENABLED */
  if (name != NULL && ecil_library_builtin_name(name) != NULL) {
    name = ecil_library_builtin_name(name);
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


static void check_routine_attributes(a_routine_ptr rout)
/*
Note as unsupported the GNU routine attributes whose meaning the code
generator cannot reproduce by ignoring them (constructor, destructor, alias,
ifunc, weakref).  Attributes that only affect optimization (noinline, noipa,
noclone, used, unused, cold, hot, always_inline, pure, const, ...) are
ignorable and need no code here.
*/
{
#if GNU_EXTENSIONS_ALLOWED
  if (rout->is_initialization_routine || rout->is_finalization_routine) {
    ecil_note_unsupported("a constructor or destructor attribute");
  } else if (rout->is_gnu_alias || rout->is_ifunc || rout->is_weakref) {
    ecil_note_unsupported("an alias, ifunc or weakref attribute");
  }  /* if */
#else /* !GNU_EXTENSIONS_ALLOWED */
  (void)rout;
#endif /* GNU_EXTENSIONS_ALLOWED */
}  /* check_routine_attributes */


void ecil_check_variable_attributes(a_variable_ptr var)
/*
Note as unsupported the GNU variable attributes that cannot be ignored
(cleanup, alias, weakref).
*/
{
#if GNU_EXTENSIONS_ALLOWED
  if (var->cleanup_routine != NULL) {
    ecil_note_unsupported("a cleanup attribute");
  } else if (var->is_gnu_alias || var->is_weakref) {
    ecil_note_unsupported("an alias or weakref attribute");
  }  /* if */
#else /* !GNU_EXTENSIONS_ALLOWED */
  (void)var;
#endif /* GNU_EXTENSIONS_ALLOWED */
}  /* ecil_check_variable_attributes */


static void compute_func_state(an_ecil_frame *frame,
                               a_routine_ptr rout,
                               a_boolean     defining,
                               a_boolean     unsupported,
                               a_func_state  *state)
/*
Compute the DECL_FUNC facts for a routine as they are now.
*/
{
  a_type_ptr  t = skip_typerefs(rout->type);

  check_routine_attributes(rout);
  state->type = ecil_func_type_id(frame, rout->type,
                                  defining && !rout_type_supp(t)->prototyped);
  state->linkage = (unsigned int)elk_external;
  if (rout->storage_class == (a_storage_class)sc_static ||
      rout->source_corresp.name_linkage == (a_name_linkage_kind)nlk_internal) {
    state->linkage = (unsigned int)elk_internal;
  } else if (rout->is_weak) {
    state->linkage = (unsigned int)elk_weak;
  }  /* if */
  state->flags = 0;
  if (defining && !unsupported) state->flags |= ECIL_DECL_FUNC_DEFINED_HERE;
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
  state->name = routine_name_sid(frame, rout);
  if (state->name != 0 && state->linkage != (unsigned int)elk_internal) {
    ecil_mark_symbol_name(state->name);
  }  /* if */
}  /* compute_func_state */


static void emit_func_decl_if_changed(an_ecil_frame     *frame,
                                      uint32_t          gid,
                                      const a_func_state *state)
/*
Emit DECL_FUNC if this is the first one or any fact differs from the last
one emitted (spec 8.2: a re-issue updates the entity).
*/
{
  an_ecil_gid_info  *info = ecil_gid_info(gid);

  if (!info->decl_emitted || info->decl_type != state->type ||
      info->decl_linkage != state->linkage ||
      info->decl_flags != state->flags || info->decl_name != state->name) {
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
    ecil_rec_end(frame);
    ecil_frame_leave_decl(frame, saved);
    info->decl_type = state->type;
    info->decl_linkage = state->linkage;
    info->decl_flags = state->flags;
    info->decl_name = state->name;
    info->decl_emitted = TRUE;
  }  /* if */
}  /* emit_func_decl_if_changed */


uint32_t ecil_function_id(an_ecil_frame *frame, a_routine_ptr rout)
/*
Return the G-id of a routine, introducing it with DECL_FUNC at its first
reference (as a declaration: the definition's facts are re-issued before
FUNC_BEGIN).  The type is introduced first, so ids appear in the stream in
increasing order.
*/
{
  uint32_t  gid = ecil_gid_of(rout);

  if (gid == 0 && ecil_unsupported_reason == NULL) {
    a_func_state  state;

    compute_func_state(frame, rout, FALSE, FALSE, &state);
    if (ecil_unsupported_reason == NULL &&
        state.linkage != (unsigned int)elk_internal && state.name != 0) {
      gid = symbol_binding_lookup(state.name, egk_func);
      if (gid != 0) {
        /* Another IL routine (e.g. __builtin_memcpy) has this symbol:
           share its G-id; a differing type is a re-issue. */
        an_ecil_gid_info  *info = ecil_gid_info(gid);

        ecil_set_gid(rout, gid);
        if (info->decl_type != state.type) {
          state.flags = info->decl_flags;
          state.linkage = info->decl_linkage;
          emit_func_decl_if_changed(frame, gid, &state);
        }  /* if */
      }  /* if */
    }  /* if */
    if (gid == 0 && ecil_unsupported_reason == NULL) {
      gid = ecil_new_gid(egk_func);
      if (gid != 0) {
        ecil_gid_info(gid)->entity = rout;
        ecil_set_gid(rout, gid);
        emit_func_decl_if_changed(frame, gid, &state);
        if (state.name != 0 && state.linkage != (unsigned int)elk_internal) {
          symbol_binding_register(state.name, gid);
        }  /* if */
      }  /* if */
    }  /* if */
  }  /* if */
  return gid;
}  /* ecil_function_id */


void ecil_reissue_function(an_ecil_frame *frame,
                           uint32_t      gid,
                           a_boolean     defining,
                           a_boolean     unsupported)
/*
Re-issue DECL_FUNC for a function whose definition is about to start (or was
dropped as unsupported), if its facts changed.
*/
{
  a_func_state  state;
  a_routine_ptr rout = (a_routine_ptr)ecil_gid_info(gid)->entity;

  compute_func_state(frame, rout, defining, unsupported, &state);
  if (ecil_unsupported_reason == NULL) {
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
}  /* ecil_reissue_function */


static const char *var_symbol_name(a_variable_ptr var)
/*
The symbol name of an external-linkage variable, or NULL.
*/
{
  const char  *name = NULL;

  if (var->storage_class != (a_storage_class)sc_static &&
      var->source_corresp.name_linkage != (a_name_linkage_kind)nlk_internal) {
    name = ecil_entity_name(&var->source_corresp);
#if GNU_EXTENSIONS_ALLOWED || REDEFINE_EXTNAME_PRAGMA_ENABLED
    if (var->asm_name_is_valid && var->asm_name_or_reg.name != NULL) {
      name = var->asm_name_or_reg.name;
    }  /* if */
#endif /* GNU_EXTENSIONS_ALLOWED || REDEFINE_EXTNAME_PRAGMA_ENABLED */
  }  /* if */
  return name;
}  /* var_symbol_name */


a_targ_alignment ecil_variable_alignment(a_variable_ptr var)
/*
The explicit alignment of a variable, in bytes (0 for the natural one): its
own alignment attribute, or that of a typedef with an alignment attribute.
*/
{
  a_targ_alignment  result = var->alignment;
  a_targ_alignment  declared = var->type->alignment;

  if (declared > result && declared > skip_typerefs(var->type)->alignment) {
    result = declared;
  }  /* if */
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


uint32_t ecil_variable_id(an_ecil_frame *frame, a_variable_ptr var)
/*
Return the G-id of a file-scope or static variable, introducing it with
DECL_VAR (as a declaration) at its first reference.
*/
{
  uint32_t  gid = ecil_gid_of(var);

  if (gid == 0 && var->source_corresp.is_local_to_function &&
      var->storage_class == (a_storage_class)sc_static &&
      ecil_unsupported_reason == NULL) {
    /* A static local with no declaration statement (__func__) that is
       first reached through an address constant: define it now. */
    ecil_define_variable(frame, var, NULL);
    gid = ecil_gid_of(var);
  } else if (gid == 0 && ecil_unsupported_reason == NULL) {
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
        unsigned int      linkage = (unsigned int)elk_external;
        unsigned int      flags = 0;
        uint32_t          name_sid = 0;
        const char        *name = ecil_entity_name(&var->source_corresp);
        an_ecil_gid_info  *info = ecil_gid_info(gid);
        an_ecil_buffer    *saved;

        info->entity = var;
        ecil_set_gid(var, gid);
        if (var->storage_class == (a_storage_class)sc_static ||
            var->source_corresp.name_linkage ==
                                         (a_name_linkage_kind)nlk_internal) {
          linkage = (unsigned int)elk_internal;
        } else if (var->is_weak) {
          linkage = (unsigned int)elk_weak;
        }  /* if */
        if (is_const_qualified_type(var->type)) flags |= ECIL_DECL_VAR_CONST;
        if (is_effective_thread_local(var)) flags |= ECIL_DECL_VAR_TLS;
#if GNU_EXTENSIONS_ALLOWED || REDEFINE_EXTNAME_PRAGMA_ENABLED
        if (var->asm_name_is_valid && var->asm_name_or_reg.name != NULL) {
          name = var->asm_name_or_reg.name;
        }  /* if */
#endif /* GNU_EXTENSIONS_ALLOWED || REDEFINE_EXTNAME_PRAGMA_ENABLED */
        if (name != NULL) name_sid = ecil_intern_cstring(frame, name);
        if (name_sid != 0 && linkage != (unsigned int)elk_internal) {
          ecil_mark_symbol_name(name_sid);
          symbol_binding_register(name_sid, gid);
        }  /* if */
        saved = ecil_frame_enter_decl(frame);
        ecil_rec_begin(frame, eop_decl_var);
        ecil_arg_uint(frame, gid);
        ecil_arg_uint(frame, type);
        ecil_arg_uint(frame, linkage);
        ecil_arg_uint(frame, flags);
        ecil_arg_uint(frame, (uint64_t)ecil_variable_alignment(var));
        if (name_sid != 0) {
          ecil_arg_uint(frame, name_sid);
        } else {
          ecil_arg_nil(frame);
        }  /* if */
        ecil_rec_end(frame);
        ecil_frame_leave_decl(frame, saved);
        info->decl_type = type;
        info->decl_linkage = linkage;
        info->decl_flags = flags;
        info->decl_name = name_sid;
        info->decl_align = (uint32_t)ecil_variable_alignment(var);
        info->decl_emitted = TRUE;
      }  /* if */
    }  /* if */
  }  /* if */
  return gid;
}  /* ecil_variable_id */


/* Conditionally close the "edg" namespace. */
END_EDG_NAMESPACE

#endif /* ECIL_WRITER_ENABLED */
