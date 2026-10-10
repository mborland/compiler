/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*

ecil_const.c -- ECIL constants and initializers, string literals, and the
                definitions of global and static variables (F_GLOBALS).

Constants are built with C_* records on the consumer's constant stack.  An
aggregate initializer is normalized here (C11 6.7.9 "current object": the
IL keeps designators and implicit zero tails): each entry is given an
explicit member or element index, and anything unlisted is zero.  Nested
aggregates are walked with an explicit stack of cursors, never recursion.

Constant records are first built in a scratch buffer and appended to the
destination section afterwards, so the declarations that building them
introduces (types, globals) never fall inside a C_AGG_BEGIN...C_AGG_END
sequence.

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

#include <float.h>
#include <math.h>

/* Conditionally open the "edg" namespace. */
BEGIN_EDG_NAMESPACE

STATIC_THREAD an_ecil_buffer
                const_scratch;
STATIC_THREAD int
                const_scratch_in_use;   /* Nesting depth of the emitter. */
STATIC_THREAD an_init_level
                *level_stack;
STATIC_THREAD size_t
                level_stack_size;
STATIC_THREAD size_t
                level_stack_capacity;

void ecil_const_reset(void)
/*
Release the writer's constant-building storage at the end of a compilation.
*/
{
  size_t  i;

  for (i = 0; i < level_stack_size; i++) ecil_init_level_end(&level_stack[i]);
  ecil_buf_free(&const_scratch);
  free(level_stack);
  level_stack = NULL;
  level_stack_size = level_stack_capacity = 0;
  const_scratch_in_use = 0;
}  /* ecil_const_reset */


/* The aggregate cursor. */

static void level_reset_cursor(an_init_level *level, a_constant_ptr aggregate)
/*
Position the streaming cursor at the start of one aggregate constant.
*/
{
  a_type_ptr  t = skip_typerefs(aggregate->type);

  level->type = t;
  level->is_array = (t->kind == tk_array);
  level->is_union = (t->kind == tk_union);
  level->done = FALSE;
  level->cur = 0;
  level->cur_field = level->is_array ? NULL :
                                 t->variant.class_struct_union.field_list;
  level->next_const = aggregate->variant.aggregate.first_constant;
}  /* level_reset_cursor */


static void skip_unnamed_bit_fields(an_init_level *level)
/*
Unnamed bit-fields do not take an initializer (C11 6.7.9p9).
*/
{
  while (level->cur_field != NULL && level->cur_field->is_bit_field &&
         level->cur_field->source_corresp.name == NULL) {
    level->cur_field = level->cur_field->next;
    level->cur++;
  }  /* while */
}  /* skip_unnamed_bit_fields */


static a_boolean stream_next(an_init_level  *level,
                             uint64_t       *index,
                             uint64_t       *count,
                             a_constant_ptr *value)
/*
The streaming cursor: the next entry of the current aggregate constant.
Designators are consumed.  Return FALSE when the list is exhausted.
*/
{
  a_boolean found = FALSE;

  while (!found && !level->done && level->next_const != NULL) {
    a_constant_ptr  c = level->next_const;

    level->next_const = c->next;
    if (c->kind == (a_constant_repr_kind)ck_designator) {
      if (c->variant.designator.is_field_designator) {
        a_field_ptr field = c->variant.designator.variant.field;

        level->cur_field = field;
        level->cur = (uint64_t)ecil_gid_of(field) - 1;
      } else {
        level->cur = (uint64_t)c->variant.designator.variant.array_element;
      }  /* if */
    } else {
      if (!level->is_array) skip_unnamed_bit_fields(level);
      *index = level->cur;
      *value = c;
      found = TRUE;
      if (c->kind == (a_constant_repr_kind)ck_init_repeat &&
          c->variant.init_repeat.multidimensional_aggr_tail_not_repeated) {
        /* GCC's layout of "[0 ... 5].a[1 ... 2].b = 4, 5": the tail goes to
           the last iteration only.  Not translated. */
        ecil_note_unsupported("a GNU range designator followed by more "
                              "initializers");
      }  /* if */
      if (c->kind == (a_constant_repr_kind)ck_init_repeat) {
        *count = (uint64_t)c->variant.init_repeat.count;
      } else {
        *count = 1;
      }  /* if */
      level->cur += *count;
      if (!level->is_array && level->cur_field != NULL) {
        level->cur_field = level->cur_field->next;
      }  /* if */
      if (level->is_union) level->done = TRUE;
    }  /* if */
  }  /* while */
  return found;
}  /* stream_next */


static a_boolean union_has_more(an_init_level *scan)
/*
Return TRUE if the union initializer has another entry after the one just
read (the cursor is copied, not advanced).
*/
{
  an_init_level  look = *scan;
  uint64_t       index;
  uint64_t       count;
  a_constant_ptr value;

  look.done = FALSE;
  return stream_next(&look, &index, &count, &value);
}  /* union_has_more */


static a_boolean needs_gathering(an_init_level *level,
                                 a_constant_ptr aggregate)
/*
Return TRUE if the entries of the aggregate constant do not come in strictly
increasing member order (a designator revisits or precedes an earlier
member), so that they have to be merged.
*/
{
  an_init_level scan = *level;
  uint64_t      index;
  uint64_t      count;
  uint64_t      expected = 0;
  a_constant_ptr value;
  a_boolean     gather = FALSE;

  level_reset_cursor(&scan, aggregate);
  while (!gather && stream_next(&scan, &index, &count, &value)) {
    if (index < expected) gather = TRUE;
    expected = index + count;
    if (scan.is_union) {
      /* A union initialized twice ("{ .i = 1, .f = 2.0f }") takes the last
         initializer: look at the entries after the first one. */
      scan.done = FALSE;
      expected = 0;
      if (scan.next_const != NULL && union_has_more(&scan)) gather = TRUE;
    }  /* if */
  }  /* while */
  return gather;
}  /* needs_gathering */


static uint64_t union_first_member(a_constant_ptr agg, a_boolean *has)
/*
The member index of the first entry of a union initializer (*has FALSE if
it has none).
*/
{
  an_init_level  scan;
  uint64_t       index = 0;
  uint64_t       count;
  a_constant_ptr value;

  level_reset_cursor(&scan, agg);
  *has = stream_next(&scan, &index, &count, &value);
  return index;
}  /* union_first_member */


static a_boolean union_same_member(a_constant_ptr a, a_constant_ptr b)
/*
Return TRUE if two initializers of one union initialize the same member (so
the later one continues the earlier, as in ".f.q.a = 1, .f.q.c = 5").
*/
{
  a_boolean  has_a;
  a_boolean  has_b;
  uint64_t   ia = union_first_member(a, &has_a);
  uint64_t   ib = union_first_member(b, &has_b);

  return has_a && has_b && ia == ib;
}  /* union_same_member */


static a_boolean same_aggregate_type(a_constant_ptr a, a_constant_ptr b)
/*
Return TRUE if both are aggregate constants of one struct or array type.
*/
{
  a_type_ptr  ta = skip_typerefs(a->type);

  return a->kind == (a_constant_repr_kind)ck_aggregate &&
         b->kind == (a_constant_repr_kind)ck_aggregate &&
         ta == skip_typerefs(b->type) &&
         (ta->kind != tk_union || union_same_member(a, b));
}  /* same_aggregate_type */


static a_boolean string_and_array(a_constant_ptr a, a_constant_ptr b)
/*
Return TRUE if one is a string constant and the other an array aggregate of
bytes: element initializers that modify a string-initialized array.
*/
{
  a_constant_ptr  s = (a->kind == (a_constant_repr_kind)ck_string) ? a : b;
  a_constant_ptr  g = (s == a) ? b : a;
  a_boolean       ok = FALSE;

  if (s->kind == (a_constant_repr_kind)ck_string &&
      g->kind == (a_constant_repr_kind)ck_aggregate &&
      skip_typerefs(g->type)->kind == tk_array) {
    a_type_ptr  elem = skip_typerefs(
                           skip_typerefs(g->type)->variant.array.element_type);

    ok = elem->kind == tk_integer && elem->size == 1;
    if (!ok) {
      ecil_note_unsupported("element initializers after a wide string "
                            "initializer of the same array");
    }  /* if */
  }  /* if */
  return ok;
}  /* string_and_array */


static a_boolean insert_entries(an_init_level *level, size_t pos, size_t n)
/*
Open n slots at pos in the entry array (the array grows on demand).
*/
{
  an_init_entry *grown = (an_init_entry *)realloc(level->entries,
                  (level->num_entries + n) * sizeof(an_init_entry));
  a_boolean     ok = (grown != NULL);

  if (ok) {
    level->entries = grown;
    memmove(&level->entries[pos + n], &level->entries[pos],
            (level->num_entries - pos) * sizeof(an_init_entry));
    level->num_entries += n;
  } else {
    ecil_note_unsupported("out of memory in the ECIL writer");
  }  /* if */
  return ok;
}  /* insert_entries */


static void split_repeat(an_init_level *level, size_t r, uint64_t index)
/*
The repeat entry r covers index and the entry at index is an aggregate that
revisits it: split r into the part before, the single element (the repeated
aggregate) and the part after.
*/
{
  an_init_entry  rep = level->entries[r];
  uint64_t       before = index - rep.index;
  uint64_t       after = rep.count - before - 1;
  size_t         extra = (before != 0 ? 1u : 0u) + (after != 0 ? 1u : 0u) + 1u;
  size_t         at = r;

  if (insert_entries(level, r + 1, extra - 1)) {
    if (before != 0) {
      level->entries[at] = rep;
      level->entries[at].count = before;
      at++;
    }  /* if */
    level->entries[at] = rep;
    level->entries[at].index = index;
    level->entries[at].count = 1;
    level->entries[at].value = rep.value->variant.init_repeat.constant;
    at++;
    if (after != 0) {
      level->entries[at] = rep;
      level->entries[at].index = index + 1;
      level->entries[at].count = after;
    }  /* if */
  }  /* if */
}  /* split_repeat */


static void merge_entries(an_init_level *level)
/*
Merge the entries that revisit one member: an aggregate that comes after an
aggregate (or a repeat of aggregates) of the same type for the same member
adds its entries to it; a union replaces an earlier union.  Order is kept.
*/
{
  size_t  j;
  size_t  k;

  /* Split repeats that a later aggregate revisits. */
  for (j = 0; j < level->num_entries; j++) {
    an_init_entry  *e = &level->entries[j];

    if (e->count == 1 &&
        e->value->kind == (a_constant_repr_kind)ck_aggregate) {
      for (k = 0; k < j; k++) {
        an_init_entry  *r = &level->entries[k];

        if (r->count > 1 && !r->removed &&
            r->index <= e->index && e->index < r->index + r->count &&
            r->value->variant.init_repeat.constant->kind ==
                                        (a_constant_repr_kind)ck_aggregate &&
            same_aggregate_type(r->value->variant.init_repeat.constant,
                                e->value)) {
          split_repeat(level, k, e->index);
          j = (size_t)0 - 1;         /* The array changed: rescan. */
          break;
        }  /* if */
      }  /* for */
    }  /* if */
  }  /* for */
  /* Merge duplicates of one member (quadratic only when members repeat). */
  for (j = 1; j < level->num_entries; j++) {
    an_init_entry  *e = &level->entries[j];

    if (e->count != 1 || e->removed ||
        (e->value->kind != (a_constant_repr_kind)ck_aggregate &&
         e->value->kind != (a_constant_repr_kind)ck_string)) {
      continue;
    }  /* if */
    for (k = 0; k < j; k++) {
      an_init_entry  *p = &level->entries[k];

      if (p->count == 1 && !p->removed && p->index == e->index &&
          (p->value->kind == (a_constant_repr_kind)ck_aggregate ||
           p->value->kind == (a_constant_repr_kind)ck_string)) {
        if (e->value->explicit_braces_on_aggregate &&
            (p->value->kind == (a_constant_repr_kind)ck_string ||
             !p->value->variant.aggregate.has_dynamic_init_component)) {
          /* A later brace-enclosed initializer of the member replaces the
             whole earlier one ("{ .s.b = 1, .s = { 2 } }"); only a
             designator path ("{ .s.b = 1, .s.c = 2 }") continues it. */
          p->removed = TRUE;
        } else if (same_aggregate_type(p->value, e->value) ||
                   string_and_array(p->value, e->value)) {
          size_t         n1 = p->sources != NULL ? p->num_sources : 1;
          size_t         n2 = e->sources != NULL ? e->num_sources : 1;
          a_constant_ptr *joined = (a_constant_ptr *)malloc(
                                        (n1 + n2) * sizeof(a_constant_ptr));

          if (joined == NULL) {
            ecil_note_unsupported("out of memory in the ECIL writer");
          } else {
            if (p->sources != NULL) {
              memcpy(joined, p->sources, n1 * sizeof(a_constant_ptr));
            } else {
              joined[0] = p->value;
            }  /* if */
            if (e->sources != NULL) {
              memcpy(joined + n1, e->sources, n2 * sizeof(a_constant_ptr));
            } else {
              joined[n1] = e->value;
            }  /* if */
            free(p->sources);
            free(e->sources);
            if (p->value->kind == (a_constant_repr_kind)ck_string) {
              p->value = e->value;       /* The array supplies the type. */
            }  /* if */
            p->sources = joined;
            p->num_sources = n1 + n2;
            e->sources = NULL;
            e->removed = TRUE;
          }  /* if */
        } else if (skip_typerefs(p->value->type) ==
                       skip_typerefs(e->value->type) &&
                   skip_typerefs(p->value->type)->kind == tk_union &&
                   !p->value->variant.aggregate.has_dynamic_init_component) {
          p->removed = TRUE;         /* A later union initializer wins. */
        }  /* if */
        break;
      }  /* if */
    }  /* for */
  }  /* for */
}  /* merge_entries */


void ecil_init_level_start(an_init_level *level,
                           a_constant_ptr *sources,
                           size_t         num_sources)
/*
Start walking the entries of an aggregate (see ecil_write.h).
*/
{
  size_t  s;

  level->entries = NULL;
  level->num_entries = 0;
  level->entry_pos = 0;
  level->entry_sources = NULL;
  level->entry_num_sources = 0;
  level->entry_count = 1;
  level->entry_is_char = FALSE;
  for (s = 0; s < num_sources - 1 &&
              sources[s]->kind != (a_constant_repr_kind)ck_aggregate; s++) {
  }  /* for */
  level_reset_cursor(level, sources[s]);   /* The first aggregate: its type. */
  if (num_sources > 1 || needs_gathering(level, sources[0])) {
    /* Gather every source's entries, in order, then merge. */
    for (s = 0; s < num_sources; s++) {
      uint64_t        index;
      uint64_t        count;
      a_constant_ptr  value;

      if (sources[s]->kind == (a_constant_repr_kind)ck_string) {
        /* The bytes of a string, as element entries (cut to the array). */
        uint64_t  length = (uint64_t)sources[s]->variant.string.length;
        uint64_t  bound = (uint64_t)
                  level->type->variant.array.variant.number_of_elements;
        uint64_t  i;

        if (bound != 0 && length > bound) length = bound;
        for (i = 0; i < length; i++) {
          if (insert_entries(level, level->num_entries, 1)) {
            an_init_entry  *e = &level->entries[level->num_entries - 1];

            e->index = i;
            e->count = 1;
            e->value = sources[s];
            e->sources = NULL;
            e->num_sources = 0;
            e->removed = FALSE;
            e->is_char = TRUE;
          }  /* if */
        }  /* for */
        continue;
      }  /* if */
      level_reset_cursor(level, sources[s]);
      while (stream_next(level, &index, &count, &value)) {
        level->done = FALSE;         /* A union's later entries are kept. */
        if (insert_entries(level, level->num_entries, 1)) {
          an_init_entry  *e = &level->entries[level->num_entries - 1];

          e->index = index;
          e->count = count;
          e->value = value;
          e->sources = NULL;
          e->num_sources = 0;
          e->removed = FALSE;
          e->is_char = FALSE;
        } else {
          break;
        }  /* if */
      }  /* while */
    }  /* for */
    merge_entries(level);
    if (level->is_union) {
      /* Only the last initializer of a union counts. */
      size_t  last = level->num_entries;
      size_t  e;

      for (e = 0; e < level->num_entries; e++) {
        if (!level->entries[e].removed) last = e;
      }  /* for */
      for (e = 0; e < level->num_entries; e++) {
        if (e != last) level->entries[e].removed = TRUE;
      }  /* for */
    }  /* if */
    level->done = TRUE;               /* The streaming cursor is not used. */
    level->next_const = NULL;
  }  /* if */
}  /* ecil_init_level_start */


a_boolean ecil_init_level_next(an_init_level  *level,
                               uint64_t       *index,
                               a_constant_ptr *value)
/*
Return the next entry: its member or element index and its constant (which
may be a ck_aggregate, a ck_init_repeat or a leaf).  Designators are
consumed.  Return FALSE when the list is exhausted.
*/
{
  a_boolean found = FALSE;

  level->entry_sources = NULL;
  level->entry_num_sources = 0;
  level->entry_is_char = FALSE;
  if (level->entries == NULL && level->num_entries == 0 &&
      level->next_const != NULL) {
    found = stream_next(level, index, &level->entry_count, value);
  } else if (level->entries != NULL) {
    while (!found && level->entry_pos < level->num_entries) {
      an_init_entry  *e = &level->entries[level->entry_pos++];

      if (!e->removed) {
        *index = e->index;
        *value = e->value;
        level->entry_count = e->count;
        level->entry_sources = e->sources;
        level->entry_num_sources = e->num_sources;
        level->entry_is_char = e->is_char;
        found = TRUE;
      }  /* if */
    }  /* while */
  }  /* if */
  return found;
}  /* ecil_init_level_next */


void ecil_init_level_end(an_init_level *level)
/*
Release the storage of a level.
*/
{
  size_t  i;

  for (i = 0; i < level->num_entries; i++) free(level->entries[i].sources);
  free(level->entries);
  level->entries = NULL;
  level->num_entries = 0;
}  /* ecil_init_level_end */


/* Leaf values. */

static void arg_integer_value(an_ecil_frame *frame,
                              a_constant_ptr c,
                              an_ecil_gid_info *info)
/*
Append the value of an integer constant as an operand, using the width and
signedness of its ECIL type (pointers are unsigned).
*/
{
  a_boolean ovflo = FALSE;
  a_boolean is_signed = (info->kind == egk_int) && info->is_signed;
  unsigned  bits = (info->kind == egk_int) ? info->bits : 64;

  if (bits <= 64) {
    if (is_signed) {
      ecil_arg_int(frame, (int64_t)value_of_integer_constant(c, &ovflo));
    } else {
      ecil_arg_uint(frame, (uint64_t)unsigned_value_of_integer_constant(
                                                              c, &ovflo));
    }  /* if */
  } else {
    a_host_large_integer value = value_of_integer_constant(c, &ovflo);
    uint64_t             low = (uint64_t)value;
    uint64_t             high;

    if (sizeof(value) > 8) {
      high = (uint64_t)((a_host_large_unsigned)value >>
                        (sizeof(value) > 8 ? 64 : 0));
    } else {
      high = (is_signed && value < 0) ? ~(uint64_t)0 : 0;
    }  /* if */
    ecil_arg_int128(frame, low, high, is_signed);
  }  /* if */
}  /* arg_integer_value */


static a_boolean inexact_float_constant(void)
/*
Note that a floating constant cannot be converted to its target format
exactly on this host (spec P3); return FALSE.
*/
{
  ecil_note_unsupported("a floating constant that cannot be converted "
                        "exactly on this host");
  return FALSE;
}  /* inexact_float_constant */


static a_boolean arg_float_value(an_ecil_frame *frame, a_constant_ptr c)
/*
Append a floating constant as an f32, f64 or FBITS operand.  Return FALSE
(after noting why) if this host cannot produce the target bits exactly.
*/
{
  a_boolean         ok = TRUE;
  a_type_ptr        t = skip_typerefs(c->type);
  a_host_fp_value   hv = fetch_host_fp_value(t->variant.float_kind,
                                             &c->variant.float_value);
  an_ecil_gid_info  *info = ecil_gid_info(ecil_type_id(frame, t));

  if (info->float_format == (unsigned int)eff_ieee_single) {
    float     f = (float)hv;
    uint32_t  bits;

    memcpy(&bits, &f, sizeof(bits));
    if (hv == hv && (a_host_fp_value)f != hv) {
      ok = inexact_float_constant();
    }  /* if */
    ecil_arg_f32_bits(frame, bits);
  } else if (info->float_format == (unsigned int)eff_ieee_double) {
    double    d = (double)hv;
    uint64_t  bits;

    memcpy(&bits, &d, sizeof(bits));
    if (hv == hv && (a_host_fp_value)d != hv) {
      ok = inexact_float_constant();
    }  /* if */
    ecil_arg_f64_bits(frame, bits);
  } else if (info->float_format == (unsigned int)eff_x87_80) {
    unsigned char bytes[10];

#if LDBL_MANT_DIG == 64
    long double ld = (long double)hv;

    memcpy(bytes, &ld, 10);
    if (hv == hv && (a_host_fp_value)ld != hv) {
      ok = inexact_float_constant();
    }  /* if */
    ecil_arg_fbits(frame, bytes, 10);
#else /* LDBL_MANT_DIG != 64 */
    double    d = (double)hv;
    uint64_t  dbits;
    uint64_t  mant;
    unsigned  exp15;
    unsigned  sign;
    unsigned  exp11;
    uint64_t  frac;
    unsigned  i;

    if (!isnan(d) && (a_host_fp_value)d != hv) {
      ok = FALSE;
      ecil_note_unsupported("a long double constant that cannot be "
                            "converted exactly on this host");
    } else {
      memcpy(&dbits, &d, sizeof(dbits));
      sign = (unsigned)(dbits >> 63);
      exp11 = (unsigned)((dbits >> 52) & 0x7FF);
      frac = dbits & 0xFFFFFFFFFFFFFull;
      if (exp11 == 0x7FF) {
        exp15 = 0x7FFF;
        mant = ((uint64_t)1 << 63) | (frac << 11);
      } else if (exp11 == 0) {
        if (frac == 0) {
          exp15 = 0;
          mant = 0;
        } else {
          int shift = 0;

          while (((frac << shift) & (1ull << 52)) == 0) shift++;
          exp15 = (unsigned)(16383 - 1022 - shift);
          mant = (frac << shift) << 11;
        }  /* if */
      } else {
        exp15 = exp11 - 1023 + 16383;
        mant = ((uint64_t)1 << 63) | (frac << 11);
      }  /* if */
      for (i = 0; i < 8; i++) bytes[i] = (unsigned char)(mant >> (8 * i));
      bytes[8] = (unsigned char)(exp15 & 0xFF);
      bytes[9] = (unsigned char)(((exp15 >> 8) & 0x7F) | (sign << 7));
      ecil_arg_fbits(frame, bytes, 10);
    }  /* if */
#endif /* LDBL_MANT_DIG == 64 */
  } else {
    ok = FALSE;
    ecil_note_unsupported("a floating constant of this format");
  }  /* if */
  return ok;
}  /* arg_float_value */


static void emit_string_bytes(an_ecil_frame *frame,
                              a_constant_ptr c,
                              uint32_t       type_gid)
/*
C_STRING for an array initializer: the bytes of the string, cut to the
array's size (the consumer zero-pads any tail).
*/
{
  an_ecil_gid_info  *info = ecil_gid_info(type_gid);
  uint64_t          length = (uint64_t)c->variant.string.length;

  if (info->kind == egk_array && !(info->count == 0 && info->size == 0) &&
      length > info->size) {
    length = info->size;
  }  /* if */
  ecil_rec_begin(frame, eop_c_string);
  ecil_arg_uint(frame, type_gid);
  ecil_arg_bin(frame, c->variant.string.value, (size_t)length);
  ecil_rec_end(frame);
}  /* emit_string_bytes */


static a_boolean emit_leaf(an_ecil_frame *frame, a_constant_ptr c,
                           uint32_t target)
/*
Emit the C_* record of a non-aggregate constant.  Return FALSE (after
noting why) if it is not expressible.  target is the G-id of the type of the
object the constant initializes, or 0: a string constant takes it (the
literal's own type, e.g. char[5], may differ from the target's, unsigned
char[5]).
*/
{
  a_boolean ok = TRUE;
  uint32_t  t = ecil_type_id(frame, c->type);

  if (t == 0) {
    ok = FALSE;
  } else {
    an_ecil_gid_info  *info = ecil_gid_info(t);

    switch (c->kind) {
      case ck_integer:
        if (info->kind == egk_ptr) {
          a_boolean ovflo = FALSE;
          uint64_t  value = (uint64_t)unsigned_value_of_integer_constant(
                                                              c, &ovflo);

          if (value == 0) {
            ecil_rec1(frame, eop_c_null, t);
          } else {
            ecil_rec_begin(frame, eop_c_ptr);
            ecil_arg_uint(frame, t);
            ecil_arg_uint(frame, value);
            ecil_rec_end(frame);
          }  /* if */
        } else {
          ecil_rec_begin(frame, eop_c_int);
          ecil_arg_uint(frame, t);
          arg_integer_value(frame, c, info);
          ecil_rec_end(frame);
        }  /* if */
        break;
      case ck_float:
        ecil_rec_begin(frame, eop_c_float);
        ecil_arg_uint(frame, t);
        ok = arg_float_value(frame, c);
        ecil_rec_end(frame);
        break;
      case ck_string:
        emit_string_bytes(frame, c, (target != 0 &&
                                     ecil_gid_info(target)->kind ==
                                                      egk_array) ? target : t);
        break;
      case ck_address:
        { uint32_t  g = 0;

          switch (c->variant.address.kind) {
            case abk_variable:
              g = ecil_variable_id(frame, c->variant.address.variant.variable);
              break;
            case abk_routine:
              g = ecil_function_id(frame, c->variant.address.variant.routine);
              break;
            case abk_constant:
              if (c->variant.address.variant.constant->kind ==
                      (a_constant_repr_kind)ck_string) {
                g = ecil_string_literal_id(frame,
                                    c->variant.address.variant.constant);
              } else {
                ecil_note_unsupported("the address of a non-string constant");
              }  /* if */
              break;
            default:
              ecil_note_unsupported("this kind of address constant");
              break;
          }  /* switch */
          if (g == 0) {
            ok = FALSE;
          } else if (info->kind != egk_ptr) {
            ok = FALSE;
            ecil_note_unsupported("an address constant of integer type");
          } else {
            ecil_rec_begin(frame, eop_c_addr);
            ecil_arg_uint(frame, t);
            ecil_arg_uint(frame, g);
            ecil_arg_int(frame, (int64_t)c->variant.address.offset);
            ecil_rec_end(frame);
          }  /* if */
        }
        break;
      case ck_dynamic_init:
        ok = FALSE;
        ecil_note_unsupported("a non-constant initializer element");
        break;
      default:
        ok = FALSE;
        { static char reason[80];

          (void)snprintf(reason, sizeof(reason), "the constant kind %d",
                         (int)c->kind);
          ecil_note_unsupported(reason);
        }
        break;
    }  /* switch */
  }  /* if */
  return ok;
}  /* emit_leaf */


void ecil_emit_string_char(an_ecil_frame *frame, an_init_level *level,
                           a_constant_ptr str, uint64_t pos)
/*
Emit C_INT for byte pos of a string constant, as an element of the array of
the level.
*/
{
  uint32_t  t = ecil_type_id(frame, level->type->variant.array.element_type);
  unsigned char  b = ((const unsigned char *)str->variant.string.value)[pos];

  if (t != 0) {
    ecil_rec_begin(frame, eop_c_int);
    ecil_arg_uint(frame, t);
    if (ecil_gid_info(t)->is_signed) {
      ecil_arg_int(frame, (int64_t)(signed char)b);
    } else {
      ecil_arg_uint(frame, (uint64_t)b);
    }  /* if */
    ecil_rec_end(frame);
  }  /* if */
}  /* ecil_emit_string_char */


static an_init_level *push_level(a_constant_ptr *sources, size_t n)
/*
Push a cursor for an aggregate initialized by the given constants (or return
NULL if memory ran out).
*/
{
  an_init_level *level = NULL;

  if (level_stack_size == level_stack_capacity) {
    size_t        new_capacity = (level_stack_capacity == 0) ?
                                 32 : level_stack_capacity * 2;
    an_init_level *new_stack = (an_init_level *)realloc(level_stack,
                                       new_capacity * sizeof(an_init_level));

    if (new_stack != NULL) {
      level_stack = new_stack;
      level_stack_capacity = new_capacity;
    }  /* if */
  }  /* if */
  if (level_stack_size < level_stack_capacity) {
    level = &level_stack[level_stack_size++];
    ecil_init_level_start(level, sources, n);
  } else {
    ecil_note_unsupported("out of memory in the ECIL writer");
  }  /* if */
  return level;
}  /* push_level */


uint32_t ecil_member_type_gid(an_ecil_frame *frame, an_init_level *level,
                              uint64_t index)
/*
The G-id of the type of member or element index of the aggregate of level.
*/
{
  uint32_t  gid = 0;

  if (level->is_array) {
    gid = ecil_type_id(frame, level->type->variant.array.element_type);
  } else {
    a_field_ptr  field = level->type->variant.class_struct_union.field_list;

    while (field != NULL && index > 0) {
      field = field->next;
      index--;
    }  /* while */
    if (field != NULL) gid = ecil_type_id(frame, field->type);
  }  /* if */
  return gid;
}  /* ecil_member_type_gid */


static a_boolean begin_aggregate(an_ecil_frame *frame, a_constant_ptr agg,
                                 a_constant_ptr *sources, size_t n)
/*
Introduce the aggregate's type, push its cursor and emit C_AGG_BEGIN.  The
aggregate is initialized by agg, or by the n merged constants in sources.
*/
{
  uint32_t  t = ecil_type_id(frame, agg->type);
  a_boolean ok = (t != 0);

  if (ok && ecil_gid_info(t)->kind != egk_record &&
      ecil_gid_info(t)->kind != egk_array) {
    /* E.g. a vector or complex initializer: the type is opaque. */
    ecil_note_unsupported_type(t, "an aggregate initializer of an opaque "
                                  "type");
    ok = FALSE;
  }  /* if */
  if (ok) {
    if (sources == NULL) {
      sources = &agg;
      n = 1;
    }  /* if */
    ok = push_level(sources, n) != NULL;
  }  /* if */

  if (ok) ecil_rec1(frame, eop_c_agg_begin, t);
  return ok;
}  /* begin_aggregate */


a_boolean ecil_emit_float_constant(an_ecil_frame *frame, a_constant_ptr c)
/*
Emit the CONST_FLOAT expression record for a floating constant into the
current section.  Return FALSE, having noted why, if it cannot be expressed.
*/
{
  uint32_t  t = ecil_type_id(frame, c->type);
  a_boolean ok = FALSE;

  if (t != 0) {
    ecil_rec_begin(frame, eop_const_float);
    ecil_arg_uint(frame, t);
    ok = arg_float_value(frame, c);
    ecil_rec_end(frame);
  }  /* if */
  return ok;
}  /* ecil_emit_float_constant */


a_boolean ecil_emit_constant_sources(an_ecil_frame *frame,
                                     a_constant_ptr constant,
                                     a_constant_ptr *sources,
                                     size_t         num_sources,
                                     uint32_t       target_type)
/*
Emit the C_* records that push one constant (possibly a nested aggregate)
onto the constant stack, into the current section.  target_type is the G-id
of the type of the object it initializes (0 if unknown).  If sources is not
NULL the object is initialized by those merged aggregate constants (see
an_init_level) and constant is the first of them.  Return FALSE, having
emitted nothing and noted why, if it cannot be expressed.
*/
{
  a_boolean       ok;
  an_ecil_buffer  *target = frame->current;
  size_t          base = level_stack_size;
  an_ecil_buffer  nested;
  an_ecil_buffer  *scratch = &const_scratch;

  /* Build in a scratch buffer so introductions made on the way do not
     fall inside the C_* sequence.  Introducing an object can define a
     variable (a static local such as __func__) and so re-enter here: the
     nested call needs a buffer of its own. */
  if (const_scratch_in_use) {
    ecil_buf_init(&nested);
    scratch = &nested;
  }  /* if */
  const_scratch_in_use++;
  scratch->size = 0;
  scratch->failed = FALSE;
  frame->current = scratch;
  if (constant->kind == (a_constant_repr_kind)ck_aggregate) {
    ok = begin_aggregate(frame, constant, sources, num_sources);
    while (ok && level_stack_size > base) {
      an_init_level   *level = &level_stack[level_stack_size - 1];
      uint64_t        index = 0;
      a_constant_ptr  value = NULL;

      if (!ecil_init_level_next(level, &index, &value)) {
        ecil_rec0(frame, eop_c_agg_end);
        ecil_init_level_end(level);
        level_stack_size--;
      } else if (level->entry_is_char) {
        ecil_rec1(frame, eop_c_sel, index);
        ecil_emit_string_char(frame, level, value, index);
      } else if (value->kind == (a_constant_repr_kind)ck_init_repeat) {
        a_constant_ptr  repeated = value->variant.init_repeat.constant;

        ecil_rec2(frame, eop_c_repeat, index, level->entry_count);
        if (repeated->kind == (a_constant_repr_kind)ck_aggregate) {
          ok = begin_aggregate(frame, repeated, NULL, 0);
        } else {
          ok = emit_leaf(frame, repeated,
                         ecil_member_type_gid(frame, level, index));
        }  /* if */
      } else {
        a_constant_ptr  *srcs = level->entry_sources;
        size_t          num_srcs = level->entry_num_sources;

        ecil_rec1(frame, eop_c_sel, index);
        if (value->kind == (a_constant_repr_kind)ck_aggregate) {
          ok = begin_aggregate(frame, value, srcs, num_srcs);
        } else {
          ok = emit_leaf(frame, value,
                         ecil_member_type_gid(frame, level, index));
        }  /* if */
      }  /* if */
    }  /* while */
    if (!ok) {
      while (level_stack_size > base) {
        ecil_init_level_end(&level_stack[--level_stack_size]);
      }  /* while */
    }  /* if */
  } else {
    ok = emit_leaf(frame, constant, target_type);
  }  /* if */
  frame->current = target;
  if (ok && !scratch->failed) {
    ecil_buf_put_bytes(target, scratch->data, scratch->size);
  } else if (ok) {
    ok = FALSE;
    ecil_note_unsupported("out of memory in the ECIL writer");
  }  /* if */
  if (scratch == &nested) ecil_buf_free(&nested);
  const_scratch_in_use--;
  return ok;
}  /* ecil_emit_constant_sources */


a_boolean ecil_emit_constant_typed(an_ecil_frame *frame,
                                   a_constant_ptr constant,
                                   uint32_t       target_type)
/*
ecil_emit_constant_sources for a single constant.
*/
{
  return ecil_emit_constant_sources(frame, constant, NULL, 0, target_type);
}  /* ecil_emit_constant_typed */


a_boolean ecil_emit_constant(an_ecil_frame *frame, a_constant_ptr constant)
/*
ecil_emit_constant_typed when the target type is not known.
*/
{
  return ecil_emit_constant_typed(frame, constant, 0);
}  /* ecil_emit_constant */


/* String literals. */

uint32_t ecil_string_literal_id(an_ecil_frame *frame, a_constant_ptr constant)
/*
Return the G-id of the private constant global that holds a string literal,
defining it (DECL_VAR + C_STRING + VAR_INIT in the declaration section) the
first time.  Equal literals (same array type, same bytes) share one global,
in order of first use.
*/
{
  uint32_t  t = ecil_type_id(frame, constant->type);
  uint32_t  gid = 0;

  if (t != 0) {
    size_t    length = (size_t)constant->variant.string.length;
    size_t    n_words = 3 + (length + 3) / 4;
    uint32_t  *key = (uint32_t *)calloc(n_words, sizeof(uint32_t));

    if (key == NULL) {
      ecil_note_unsupported("out of memory in the ECIL writer");
    } else {
      size_t  i;

      key[0] = 0x53545231u;
      key[1] = t;
      key[2] = (uint32_t)length;
      for (i = 0; i < length; i++) {
        key[3 + i / 4] |= (uint32_t)(unsigned char)
                          constant->variant.string.value[i] << (8 * (i % 4));
      }  /* for */
      gid = ecil_word_lookup(key, n_words);
      if (gid == 0) {
        uint32_t  name_sid = ecil_unique_symbol_name(frame, ".str");

        gid = ecil_new_gid(egk_var);
        if (gid != 0) {
          an_ecil_buffer  *saved;
          an_ecil_gid_info
                          *info = ecil_gid_info(gid);

          ecil_word_register(key, n_words, gid);
          info->decl_type = t;
          info->decl_emitted = TRUE;
          saved = ecil_frame_enter_decl(frame);
          ecil_rec_begin(frame, eop_decl_var);
          ecil_arg_uint(frame, gid);
          ecil_arg_uint(frame, t);
          ecil_arg_uint(frame, (uint64_t)elk_private);
          ecil_arg_uint(frame, ECIL_DECL_VAR_DEFINITION |
                               ECIL_DECL_VAR_CONST |
                               ECIL_DECL_VAR_STRING_LITERAL);
          ecil_arg_uint(frame, 0);
          ecil_arg_uint(frame, name_sid);
          ecil_rec_end(frame);
          emit_string_bytes(frame, constant, t);
          ecil_rec1(frame, eop_var_init, gid);
          ecil_frame_leave_decl(frame, saved);
        }  /* if */
      }  /* if */
      free(key);
    }  /* if */
  }  /* if */
  return gid;
}  /* ecil_string_literal_id */


/* Variable definitions. */

static a_boolean variable_is_filtered(a_variable_ptr var)
/*
Return TRUE for variables that are not sent (the writer's equivalent of
ignore_variable_in_back_end).
*/
{
  return var->is_prototype_instantiation || var->is_nonreal ||
         var->init_kind == (an_init_kind)initk_binding;
}  /* variable_is_filtered */


static a_boolean initializes_flexible_array(a_constant_ptr c)
/*
Return TRUE if the aggregate initializer gives elements to the flexible array
member of its struct (a GNU extension: the object is larger than its type).
*/
{
  a_boolean  found = FALSE;
  a_type_ptr t = skip_typerefs(c->type);

  if (c->kind == (a_constant_repr_kind)ck_aggregate &&
      (t->kind == tk_struct || t->kind == tk_class) &&
      t->variant.class_struct_union.contains_flexible_array_member) {
    an_init_level  level;
    uint64_t       index;
    a_constant_ptr value;
    uint64_t       last = 0;
    a_field_ptr    field;

    for (field = t->variant.class_struct_union.field_list; field != NULL;
         field = field->next) {
      if (field->next == NULL) last = (uint64_t)ecil_gid_of(field) - 1;
    }  /* for */
    ecil_init_level_start(&level, &c, 1);
    while (!found && ecil_init_level_next(&level, &index, &value)) {
      found = (index == last);
    }  /* while */
    ecil_init_level_end(&level);
  }  /* if */
  return found;
}  /* initializes_flexible_array */


static void emit_variable_definition(an_ecil_frame     *frame,
                                     a_variable_ptr    var,
                                     uint32_t          gid,
                                     uint32_t          type,
                                     unsigned int      flags,
                                     an_initializer_ptr init,
                                     a_boolean         has_init)
/*
Emit DECL_VAR(definition) for a variable that has its G-id and type, then
its initializer and VAR_INIT.  If the initializer cannot be expressed
(ecil_unsupported_reason becomes set) a global is re-issued as an
unsupported declaration (spec 8.11, F1); a static local is left for the
caller to handle by dropping the function.
*/
{
  a_boolean         is_static_local = var->source_corresp.is_local_to_function;
  unsigned int      linkage = (unsigned int)elk_external;
  uint32_t          name_sid = 0;
  const char        *name = ecil_entity_name(&var->source_corresp);
  an_ecil_gid_info  *info = ecil_gid_info(gid);
  an_ecil_buffer    *saved;

  if (var->storage_class == (a_storage_class)sc_static ||
      var->source_corresp.name_linkage == (a_name_linkage_kind)nlk_internal ||
      is_static_local) {
    linkage = (unsigned int)elk_internal;
  } else if (var->is_weak) {
    linkage = (unsigned int)elk_weak;
  }  /* if */
  if (is_const_qualified_type(var->type)) flags |= ECIL_DECL_VAR_CONST;
  if (is_effective_thread_local(var)) flags |= ECIL_DECL_VAR_TLS;
  if (is_static_local) {
    char        full[1024];
    const char  *function_name = "static";

    if (var->source_corresp.enclosing_routine != NULL &&
        var->source_corresp.enclosing_routine->source_corresp.name != NULL) {
      function_name =
            var->source_corresp.enclosing_routine->source_corresp.name;
    }  /* if */
    flags |= ECIL_DECL_VAR_STATIC_LOCAL;
    (void)snprintf(full, sizeof(full), "%.400s.%.400s", function_name,
                   name != NULL ? name : "var");
    name_sid = ecil_unique_symbol_name(frame, full);
  } else {
#if GNU_EXTENSIONS_ALLOWED || REDEFINE_EXTNAME_PRAGMA_ENABLED
    if (var->asm_name_is_valid && var->asm_name_or_reg.name != NULL) {
      name = var->asm_name_or_reg.name;
    }  /* if */
#endif /* GNU_EXTENSIONS_ALLOWED || REDEFINE_EXTNAME_PRAGMA_ENABLED */
    if (name != NULL) {
      name_sid = ecil_intern_cstring(frame, name);
      if (linkage != (unsigned int)elk_internal) {
        ecil_mark_symbol_name(name_sid);
        ecil_bind_symbol(name_sid, gid);
      }  /* if */
    } else {
      /* A compound literal with static storage. */
      name_sid = ecil_unique_symbol_name(frame, ".compoundliteral");
    }  /* if */
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
  info->decl_type = type;
  info->decl_linkage = linkage;
  info->decl_flags = flags;
  info->decl_name = name_sid;
  info->decl_align = (uint32_t)ecil_variable_alignment(var);
  info->decl_emitted = TRUE;
  info->flags |= ECIL_GF_DEFINED;
  if (has_init && initializes_flexible_array(init->constant)) {
    ecil_note_unsupported("an initializer of a flexible array member");
  } else if (has_init && ecil_emit_constant_typed(frame, init->constant,
                                                  type)) {
    ecil_rec1(frame, eop_var_init, gid);
  }  /* if */
  ecil_frame_leave_decl(frame, saved);
  if (ecil_unsupported_reason != NULL && !is_static_local) {
    unsigned int  new_flags = (flags & ~(unsigned int)
                               ECIL_DECL_VAR_DEFINITION) |
                              ECIL_DECL_VAR_UNSUPPORTED;

    saved = ecil_frame_enter_decl(frame);
    ecil_rec_begin(frame, eop_decl_var);
    ecil_arg_uint(frame, gid);
    ecil_arg_uint(frame, type);
    ecil_arg_uint(frame, linkage);
    ecil_arg_uint(frame, new_flags);
    ecil_arg_uint(frame, (uint64_t)ecil_variable_alignment(var));
    if (name_sid != 0) {
      ecil_arg_uint(frame, name_sid);
    } else {
      ecil_arg_nil(frame);
    }  /* if */
    ecil_rec_end(frame);
    ecil_frame_leave_decl(frame, saved);
    info->decl_flags = new_flags;
    info->flags &= ~ECIL_GF_DEFINED;
  }  /* if */
}  /* emit_variable_definition */


void ecil_define_variable(an_ecil_frame *frame,
                          a_variable_ptr var,
                          a_scope_ptr    scope)
/*
Define a global or static variable: DECL_VAR with the definition flag,
then its initializer (C_* records and VAR_INIT) if it has one; a definition
without VAR_INIT is zero-initialized.  A static local is named
<function>.<variable>, made unique.  A failure sets
ecil_unsupported_reason (see emit_variable_definition).
*/
{
  an_init_kind        init_kind;
  an_initializer_ptr  init = NULL;
  uint32_t            type;
  unsigned int        flags = ECIL_DECL_VAR_DEFINITION;
  a_boolean           has_init = FALSE;

  ecil_check_variable_attributes(var);
  type = ecil_unsupported_reason == NULL ? ecil_type_id(frame, var->type) : 0;
  if (type != 0) {
    get_variable_initializer(var, scope, &init_kind, &init);
    if (init_kind == (an_init_kind)initk_static) {
      has_init = TRUE;
    } else if (init_kind == (an_init_kind)initk_dynamic ||
               init_kind == (an_init_kind)initk_binding) {
      ecil_note_unsupported("a non-constant initializer of a static object");
    } else if (init_kind == (an_init_kind)initk_none &&
               var->storage_class != (a_storage_class)sc_extern &&
               !var->source_corresp.is_local_to_function) {
      flags |= ECIL_DECL_VAR_TENTATIVE;
    }  /* if */
  }  /* if */
  if (type != 0 && !has_init &&
      var->storage_class != (a_storage_class)sc_extern &&
      ecil_gid_info(type)->kind == egk_array) {
    a_type_ptr  vt = skip_typerefs(var->type);

    if (vt->kind == tk_array && vt->incomplete &&
        !vt->variant.array.bound_is_zero) {
      /* A tentative definition of an incomplete array type is one element
         (C11 6.9.2p5). */
      type = ecil_array_of(frame, ecil_gid_info(type)->ref, 1);
    }  /* if */
  }  /* if */
  if (type != 0 && (ecil_gid_info(type)->flags & ECIL_GF_FAILED)) {
    ecil_note_unsupported_type(type, "a record type that is not supported");
  }  /* if */
  if (type != 0 && ecil_unsupported_reason == NULL) {
    uint32_t  gid = ecil_gid_of(var);

    if (gid == 0) {
      /* Another IL entry may already be bound to this symbol. */
      gid = ecil_variable_symbol_gid(frame, var);
      if (gid == 0) {
        gid = ecil_new_gid(egk_var);
        if (gid != 0) ecil_gid_info(gid)->entity = var;
      }  /* if */
      if (gid != 0) ecil_set_gid(var, gid);
    }  /* if */
    if (gid != 0) {
      emit_variable_definition(frame, var, gid, type, flags, init, has_init);
    }  /* if */
  }  /* if */
}  /* ecil_define_variable */


void ecil_emit_globals(an_ecil_frame *frame)
/*
Fill the F_GLOBALS frame: records completed since they were declared,
definitions of the file-scope variables (including tentative definitions,
without VAR_INIT), and re-issued function declarations whose final status
differs from the last one sent (C99 inline-only, spec 8.2).  Static locals
are not here: they were defined in the frame of their function.
*/
{
  a_variable_ptr  var;
  uint32_t        gid;
  uint32_t        last = ecil_num_gids();

  ecil_complete_pending_records(frame);
  for (var = il_header.primary_scope->variables; var != NULL;
       var = var->next) {
    a_boolean skip = variable_is_filtered(var) ||
                     var->source_corresp.is_local_to_function ||
                     var->storage_class == (a_storage_class)sc_typedef;

    if (!skip && var->storage_class == (a_storage_class)sc_extern &&
        var->init_kind == (an_init_kind)initk_none) {
      skip = TRUE;                   /* A declaration only. */
    }  /* if */
    if (!skip && var->storage_class == (a_storage_class)sc_static &&
        !var->source_corresp.referenced && ecil_gid_of(var) == 0) {
      skip = TRUE;                   /* An unreferenced static. */
    }  /* if */
    if (!skip) {
      ecil_unsupported_reason = NULL;
      ecil_define_variable(frame, var, il_header.primary_scope);
      if (ecil_unsupported_reason != NULL) {
        char  message[400];

        (void)snprintf(message, sizeof(message),
                       "the initializer of \"%.200s\" uses %s, which the "
                       "CIR back end does not support; the definition is "
                       "dropped",
                       var->source_corresp.name != NULL ?
                                   var->source_corresp.name : "?",
                       ecil_unsupported_reason);
        cir_be_warning(message);
        ecil_unsupported_reason = NULL;
      }  /* if */
    }  /* if */
  }  /* for */
  ecil_complete_pending_records(frame);
  for (gid = 1; gid <= last; gid++) {
    an_ecil_gid_info  *info = ecil_gid_info(gid);

    if (info->kind == egk_func) {
      ecil_reissue_function(frame, gid,
                            (info->flags & ECIL_GF_DEFINED) != 0,
                            (info->decl_flags & ECIL_DECL_FUNC_UNSUPPORTED)
                                                                    != 0);
    }  /* if */
  }  /* for */
}  /* ecil_emit_globals */


/* Conditionally close the "edg" namespace. */
END_EDG_NAMESPACE

#endif /* ECIL_WRITER_ENABLED */
