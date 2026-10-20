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

/* A static local whose DECL_VAR was sent and whose initializer is still to
   be built (see ecil_queue_variable_definition). */
typedef struct an_ecil_pending_definition {
  a_variable_ptr
                var;
  uint32_t      gid;
  uint32_t      type;
  an_initializer_ptr
                init;
  a_boolean     has_init;
} an_ecil_pending_definition;

STATIC_THREAD an_ecil_buffer
                const_scratch;          /* The constant being built. */
STATIC_THREAD uint32_t
                *string_key;            /* The key of a string literal. */
STATIC_THREAD size_t
                string_key_capacity;
STATIC_THREAD an_ecil_pending_definition
                *pending_defs;          /* Queued static local definitions. */
STATIC_THREAD size_t
                num_pending_defs;
STATIC_THREAD size_t
                next_pending_def;       /* The first one not yet defined. */
STATIC_THREAD size_t
                pending_defs_capacity;
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
  free(string_key);
  free(pending_defs);
  level_stack = NULL;
  level_stack_size = level_stack_capacity = 0;
  string_key = NULL;
  string_key_capacity = 0;
  pending_defs = NULL;
  num_pending_defs = next_pending_def = pending_defs_capacity = 0;
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
  level->lead = 0;
  level->vptr_shift = 0;
  if (!level->is_array) {
    a_field_ptr  first = t->variant.class_struct_union.field_list;

    /* The vptr (field 0 of a class with one of its own: the front end's
       constants list none) and the base classes come first in the record: the
       data members start at the index of the first one.  The entries are
       numbered without the vptr, the records with it (spec 12.7). */
    if (!C_mode() && ecil_class_has_own_vptr(t)) level->vptr_shift = 1;
    if (first != NULL && !C_mode() && ecil_gid_of(first) != 0) {
      level->lead = (uint64_t)ecil_gid_of(first) - 1 - level->vptr_shift;
    }  /* if */
  }  /* if */
  level->cur_field = (level->is_array || level->lead != 0) ? NULL :
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
      if (!level->is_array && !level->is_union && level->lead != 0 &&
          level->cur >= level->lead && level->cur_field == NULL &&
          c->kind == (a_constant_repr_kind)ck_aggregate &&
          !C_mode() && class_type_supp(level->type) != NULL) {
        /* A base class initializer after the data members (the front end
           lists the base last in C++17 for "{ .x = 1 }"): it is the entry
           of that base. */
        a_base_class_ptr  base;
        uint64_t          n = 0;

        for (base = class_type_supp(level->type)->direct_base_classes;
             base != NULL; base = base->next) {
          if (base->direct) {
            if (n < level->lead &&
                skip_typerefs(base->type) == skip_typerefs(c->type)) {
              *index = n;
              level->cur = n;
              level->cur_field = NULL;
              break;
            }  /* if */
            n++;
          }  /* if */
        }  /* for */
      }  /* if */
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
      } else if (!level->is_array && level->cur == level->lead) {
        /* The last base class entry: the data members follow. */
        level->cur_field = level->type->variant.class_struct_union.field_list;
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
  an_init_entry *grown = level->entries;
  a_boolean     ok = TRUE;

  if (level->num_entries + n > level->entries_capacity) {
    size_t  new_capacity = level->entries_capacity == 0 ?
                           16 : level->entries_capacity * 2;

    if (new_capacity < level->num_entries + n) {
      new_capacity = level->num_entries + n;
    }  /* if */
    grown = (an_init_entry *)realloc(level->entries,
                                     new_capacity * sizeof(an_init_entry));
    ok = (grown != NULL);
    if (ok) {
      level->entries = grown;
      level->entries_capacity = new_capacity;
    }  /* if */
  }  /* if */
  if (ok) {
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
          /* The array changed from k on (the entries before k and what they
             were compared with are as they were): rescan from k. */
          j = k - 1;
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
  level->entries_capacity = 0;
  level->members = NULL;
  level->num_members = 0;
  level->entry_pos = 0;
  level->entry_sources = NULL;
  level->entry_num_sources = 0;
  level->skip_constants = FALSE;
  level->addr_lid = 0;
  level->num_partial = 0;
  level->partial_pending = FALSE;
  level->partial_index = 0;
  level->partial_count = 0;
  level->partial_type = NULL;
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
  free(level->members);
  level->entries = NULL;
  level->num_entries = 0;
  level->entries_capacity = 0;
  level->members = NULL;
  level->num_members = 0;
}  /* ecil_init_level_end */


/* Leaf values. */

#if !INTEGER_VALUE_REPR_IS_A_HOST_INTEGER
static void integer_value_low_128(an_integer_value *value,
                                  a_boolean        is_signed,
                                  uint64_t         *low,
                                  uint64_t         *high)
/*
The low 128 bits of an integer value (two's complement; the value is
extended by its sign if it has fewer bits).  The parts of an integer value
are in big-endian order.
*/
{
  unsigned int  i;
  unsigned int  count = (unsigned int)INT_VALUE_PARTS_PER_INTEGER_VALUE;
  uint64_t      fill = (is_signed && (value->part[0] & SIGN_BIT_INT_VALUE_PART)
                        != 0) ? ~(uint64_t)0 : 0;

  *low = 0;
  *high = 0;
  for (i = 0; i < 8; i++) {
    uint64_t  part = fill & 0xFFFFu;
    unsigned  pos = i * 16u;             /* The bit position in 128 bits. */

    if (i < count) part = (uint64_t)value->part[count - 1 - i];
    if (pos < 64) {
      *low |= part << pos;
    } else {
      *high |= part << (pos - 64);
    }  /* if */
  }  /* for */
}  /* integer_value_low_128 */
#endif /* !INTEGER_VALUE_REPR_IS_A_HOST_INTEGER */


void ecil_arg_integer_constant(an_ecil_frame *frame,
                               a_constant_ptr c,
                               unsigned int  bits,
                               a_boolean     is_signed)
/*
Append the value of an integer constant of the given width and signedness as
an operand: a 64-bit operand, or all of the 128 bits of a 128-bit integer
(the front end keeps wide values in an array of parts).
*/
{
  a_boolean ovflo = FALSE;

  if (bits <= 64) {
    if (is_signed) {
      ecil_arg_int(frame, (int64_t)value_of_integer_constant(c, &ovflo));
    } else {
      ecil_arg_uint(frame, (uint64_t)unsigned_value_of_integer_constant(
                                                              c, &ovflo));
    }  /* if */
  } else {
    uint64_t  low;
    uint64_t  high;

#if INTEGER_VALUE_REPR_IS_A_HOST_INTEGER
    a_host_large_integer value = value_of_integer_constant(c, &ovflo);

    low = (uint64_t)value;
    if (sizeof(value) > 8) {
      high = (uint64_t)((a_host_large_unsigned)value >> 64);
    } else {
      high = (is_signed && value < 0) ? ~(uint64_t)0 : 0;
    }  /* if */
#else /* !INTEGER_VALUE_REPR_IS_A_HOST_INTEGER */
    integer_value_low_128(&c->variant.integer_value, int_constant_is_signed(c),
                          &low, &high);
#endif /* INTEGER_VALUE_REPR_IS_A_HOST_INTEGER */
    ecil_arg_int128(frame, low, high, is_signed);
  }  /* if */
}  /* ecil_arg_integer_constant */


static void arg_integer_value(an_ecil_frame *frame,
                              a_constant_ptr c,
                              an_ecil_gid_info *info)
/*
Append the value of an integer constant as an operand, using the width and
signedness of its ECIL type (pointers are unsigned).
*/
{
  ecil_arg_integer_constant(frame, c,
                            (info->kind == egk_int) ? info->bits : 64,
                            (info->kind == egk_int) && info->is_signed);
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


a_boolean ecil_emit_scalar_constant(an_ecil_frame *frame,
                                    a_constant_ptr c,
                                    uint32_t       t,
                                    a_boolean      is_initializer)
/*
Emit the record of an integer or floating constant c of ECIL type t: the
constant-stack record C_NULL, C_PTR, C_INT or C_FLOAT of an initializer, or
the expression record CONST_NULL, CONST_PTR, CONST_INT or CONST_FLOAT.
Return FALSE (after noting why) if it is not expressible.  A floating
constant is encoded in the format of its own type, so that type (c->type)
is the one the record names, whatever t is.
*/
{
  an_ecil_gid_kind  kind = ecil_gid_info(t)->kind;
  a_boolean         ok = TRUE;

  if (c->kind == (a_constant_repr_kind)ck_float) {
    t = ecil_type_id(frame, c->type);
    if (t == 0) {
      ok = FALSE;
    } else {
      ecil_rec_begin(frame, is_initializer ? eop_c_float : eop_const_float);
      ecil_arg_uint(frame, t);
      ok = arg_float_value(frame, c);
      ecil_rec_end(frame);
    }  /* if */
  } else if (kind == egk_ptr) {
    a_boolean ovflo = FALSE;
    uint64_t  value = (uint64_t)unsigned_value_of_integer_constant(c, &ovflo);

    if (value == 0) {
      ecil_rec1(frame, is_initializer ? eop_c_null : eop_const_null, t);
    } else {
      ecil_rec2(frame, is_initializer ? eop_c_ptr : eop_const_ptr, t, value);
    }  /* if */
  } else {
    ecil_rec_begin(frame, is_initializer ? eop_c_int : eop_const_int);
    ecil_arg_uint(frame, t);
    arg_integer_value(frame, c, ecil_gid_info(t));
    ecil_rec_end(frame);
  }  /* if */
  return ok;
}  /* ecil_emit_scalar_constant */


uint32_t ecil_address_constant_gid(an_ecil_frame *frame,
                                   a_constant_ptr c,
                                   a_boolean      *is_function)
/*
The G-id of the entity an address constant (ck_address) points into, which
is introduced if need be (a variable may be queued for definition, see
ecil_variable_id); *is_function says whether it is a function.  Return 0,
having noted why, if the address is not expressible.
*/
{
  uint32_t  g = 0;

  *is_function = FALSE;
  switch (c->variant.address.kind) {
    case abk_variable:
      g = ecil_variable_id(frame, c->variant.address.variant.variable);
      ecil_check_variable_use(g);
      break;
    case abk_routine:
      g = ecil_function_id(frame, c->variant.address.variant.routine);
      *is_function = TRUE;
      break;
    case abk_typeid:
      /* &typeid(T): the address of the typeinfo object (constexpr). */
      g = ecil_typeinfo_id(frame, c->variant.address.variant.type);
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
  return g;
}  /* ecil_address_constant_gid */


static a_boolean in_var_init = FALSE;
/* TRUE while the constant of a VAR_INIT is built: a C_METHOD with a non-zero
   adjustment is valid only there (spec 12.4). */

static a_boolean pm_data_member_offset(a_constant_ptr c, uint64_t *offset)
/*
Set *offset to the byte offset, in the complete class of the type of the
pointer to a data member constant c, of the member it designates: the
member's own offset (plus those of the anonymous unions that contain it) and
the offset of the base subobject that the pointer was converted through
(spec 12.4).  Return FALSE, having noted why, if it is not expressible.
*/
{
  a_field_ptr       field = c->variant.ptr_to_member.variant.field;
  a_base_class_ptr  bcp = c->variant.ptr_to_member.casting_base_class;
  a_targ_ptrdiff_t  total = 0;
  a_boolean         ok = TRUE;

  if (field->is_bit_field) {
    ok = FALSE;
    ecil_note_unsupported("a pointer to a bit-field");
  } else if (bcp != NULL && bcp->is_virtual) {
    ok = FALSE;
    ecil_note_unsupported("a pointer to a member converted through a "
                          "virtual base class");
  }  /* if */
  while (ok && field != NULL) {
    a_type_ptr                   field_class = parent_class_of(field);
    a_class_type_supplement_ptr  ctsp = class_type_supp(field_class);

    total += (a_targ_ptrdiff_t)field->offset;
    if (field_class->variant.class_struct_union.
                                       is_nonstd_anonymous_union_type) {
      /* A non-standard anonymous union or struct: go to its parent. */
      a_symbol_ptr  field_sym = symbol_for(field);

      field_sym = field_sym == NULL ? NULL :
                  field_sym->variant.field.anonymous_parent_object;
      if (field_sym == NULL || field_sym->kind != (a_symbol_kind)sk_field) {
        ok = FALSE;
        ecil_note_unsupported("a pointer to a member of an anonymous "
                              "union");
      } else {
        field = field_sym->variant.field.ptr;
      }  /* if */
    } else if (ctsp->anonymous_union_kind == (an_anonymous_union_kind)
                                                                auk_field) {
      field = ctsp->anonymous_union_field;
    } else {
      field = NULL;
    }  /* if */
  }  /* while */
  if (ok && bcp != NULL) {
    if (c->variant.ptr_to_member.cast_to_base) {
      total -= (a_targ_ptrdiff_t)bcp->offset;
    } else {
      total += (a_targ_ptrdiff_t)bcp->offset;
    }  /* if */
  }  /* if */
  if (ok && total < 0) {
    ok = FALSE;
    ecil_note_unsupported("a pointer to a data member with a negative "
                          "offset");
  }  /* if */
  *offset = (uint64_t)total;
  return ok;
}  /* pm_data_member_offset */


a_boolean ecil_emit_ptr_to_member_constant(an_ecil_frame *frame,
                                           a_constant_ptr c,
                                           a_boolean      as_constant)
/*
CONST_DATA_MEMBER or CONST_METHOD (C_DATA_MEMBER or C_METHOD with
as_constant) of a pointer-to-member constant (spec 12.4).  A C_METHOD with a
non-zero adjustment is valid only in a VAR_INIT: in a body it is not
expressible (an expression constant uses CONST_METHOD and a STORE).  Return
FALSE, having noted why, if it is not expressible.
*/
{
  uint32_t  t = ecil_type_id(frame, c->type);
  a_boolean ok = (t != 0);

  if (ok && c->variant.ptr_to_member.is_function_ptr) {
    a_routine_ptr  routine = c->variant.ptr_to_member.variant.routine;
    a_targ_ptrdiff_t  adj = 0;
    uint32_t       g = 0;
    unsigned int   flags = 0;

    if (routine != NULL) {
      a_base_class_ptr  bcp = c->variant.ptr_to_member.casting_base_class;

      if (bcp != NULL) {
        adj = (a_targ_ptrdiff_t)bcp->offset;
        if (c->variant.ptr_to_member.cast_to_base) adj = -adj;
        if (bcp->is_virtual) {
          ok = FALSE;
          ecil_note_unsupported("a pointer to a member function converted "
                                "through a virtual base class");
        }  /* if */
      }  /* if */
      if (ok && adj < 0) {
        ok = FALSE;
        ecil_note_unsupported("a pointer to a member function with a "
                              "negative this adjustment");
      }  /* if */
      if (ok && as_constant && adj != 0 && !in_var_init) {
        ok = FALSE;
        ecil_note_unsupported("a constant pointer to a member function "
                              "with a this adjustment in a function body");
      }  /* if */
      if (ok) {
        g = ecil_function_id(frame, routine);
        ok = (g != 0);
      }  /* if */
      if (ok && routine->is_virtual) {
        flags |= ECIL_METHOD_VIRTUAL;
        ecil_class_vtable_facts(frame, parent_class_of(routine));
      }  /* if */
    }  /* if */
    if (ok) {
      ecil_rec_begin(frame, as_constant ? eop_c_method : eop_const_method);
      ecil_arg_uint(frame, t);
      if (g != 0) {
        ecil_arg_uint(frame, g);
      } else {
        ecil_arg_nil(frame);
      }  /* if */
      ecil_arg_uint(frame, (uint64_t)adj);
      ecil_arg_uint(frame, flags);
      ecil_rec_end(frame);
    }  /* if */
  } else if (ok) {
    uint64_t  offset = 0;

    if (c->variant.ptr_to_member.variant.field != NULL) {
      ok = pm_data_member_offset(c, &offset);
    }  /* if */
    if (ok) {
      ecil_rec_begin(frame, as_constant ? eop_c_data_member :
                                          eop_const_data_member);
      ecil_arg_uint(frame, t);
      if (c->variant.ptr_to_member.variant.field != NULL) {
        ecil_arg_uint(frame, offset);
      } else {
        ecil_arg_nil(frame);
      }  /* if */
      ecil_rec_end(frame);
    }  /* if */
  }  /* if */
  return ok;
}  /* ecil_emit_ptr_to_member_constant */


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
    switch (c->kind) {
      case ck_integer:
      case ck_float:
        if (c->kind == ck_integer && target != 0 && target != t &&
            (ecil_gid_info(target)->kind == egk_bool ||
             ecil_gid_info(target)->kind == egk_int) &&
            (ecil_gid_info(t)->kind == egk_bool ||
             ecil_gid_info(t)->kind == egk_int)) {
          /* The constant of an object of another integer type (a bool
             variable initialized by a constant of a narrow type): it has the
             type of the object. */
          ok = ecil_emit_scalar_constant(frame, c, target, TRUE);
        } else {
          ok = ecil_emit_scalar_constant(frame, c, t, TRUE);
        }  /* if */
        break;
      case ck_string:
        emit_string_bytes(frame, c, (target != 0 &&
                                     ecil_gid_info(target)->kind ==
                                                      egk_array) ? target : t);
        break;
      case ck_address:
        { a_boolean is_function;
          uint32_t  g = ecil_address_constant_gid(frame, c, &is_function);

          if (g == 0) {
            ok = FALSE;
          } else if (ecil_gid_info(t)->kind != egk_ptr) {
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
      case ck_ptr_to_member:
        ok = ecil_emit_ptr_to_member_constant(frame, c, TRUE);
        break;
      case ck_dynamic_init:
        if (ecil_skip_dynamic_elements) {
          /* Static storage is zero before its dynamic initialization. */
          if (!C_mode() && ecil_class_is_dynamic(c->type)) {
            ok = FALSE;
            ecil_note_unsupported("a zero constant of a class with virtual "
                                  "functions");
          } else {
            ecil_rec1(frame, eop_c_zero, t);
          }  /* if */
        } else {
          ok = FALSE;
          ecil_note_unsupported("a non-constant initializer element");
        }  /* if */
        break;
      default:
        { char        reason[120];
          const char  *name = ecil_constant_kind_name((int)c->kind);

          ok = FALSE;
          if (name != NULL) {
            (void)snprintf(reason, sizeof(reason), "the constant \"%.60s\"",
                           name);
          } else {
            (void)snprintf(reason, sizeof(reason), "the constant kind %d",
                           (int)c->kind);
          }  /* if */
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
The members of a record are put in an array on first use, so that entries in
any order (designators) cost one step each.
*/
{
  uint32_t  gid = 0;

  if (level->is_array) {
    gid = ecil_type_id(frame, level->type->variant.array.element_type);
  } else if (index < level->lead) {
    /* An entry of a base class. */
    a_base_class_ptr  base = NULL;
    uint64_t          n = 0;

    if (!C_mode() && class_type_supp(level->type) != NULL) {
      for (base = class_type_supp(level->type)->direct_base_classes;
           base != NULL; base = base->next) {
        if (base->direct) {
          if (n == index) break;
          n++;
        }  /* if */
      }  /* for */
    }  /* if */
    if (base != NULL) gid = ecil_type_id(frame, base->type);
  } else {
    a_field_ptr  field = NULL;

    index -= level->lead;
    if (level->members == NULL) {
      uint64_t     count = 0;
      a_field_ptr  f;

      for (f = level->type->variant.class_struct_union.field_list; f != NULL;
           f = f->next) {
        count++;
      }  /* for */
      level->members = (a_field_ptr *)malloc((count + 1) *
                                             sizeof(a_field_ptr));
      if (level->members != NULL) {
        level->num_members = 0;
        for (f = level->type->variant.class_struct_union.field_list;
             f != NULL; f = f->next) {
          level->members[level->num_members++] = f;
        }  /* for */
      }  /* if */
    }  /* if */
    if (level->members != NULL) {
      if (index < level->num_members) field = level->members[index];
    } else {
      /* Out of memory: walk the list. */
      field = level->type->variant.class_struct_union.field_list;
      while (field != NULL && index > 0) {
        field = field->next;
        index--;
      }  /* while */
    }  /* if */
    if (field != NULL) gid = ecil_type_id(frame, field->type);
  }  /* if */
  return gid;
}  /* ecil_member_type_gid */


static a_boolean begin_aggregate(an_ecil_frame *frame, a_constant_ptr agg,
                                 a_constant_ptr *sources, size_t n,
                                 a_boolean is_base)
/*
Introduce the aggregate's type, push its cursor and emit C_AGG_BEGIN.  The
aggregate is initialized by agg, or by the n merged constants in sources;
is_base says that it is a base subobject of the aggregate that holds it.  A
dynamic class needs its vtable facts, and its vtable is used unless it is a
base subobject (the vptrs are the library's: spec 12.7, 12.8).
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
  if (ok && !C_mode() && ecil_class_is_dynamic(agg->type)) {
    if (is_base) {
      ecil_class_vtable_facts(frame, agg->type);
    } else {
      ecil_note_vtable_used(frame, agg->type);
    }  /* if */
    ok = ecil_unsupported_reason == NULL;
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
  an_ecil_buffer  *scratch = &const_scratch;

  /* Build in a scratch buffer so introductions made on the way do not
     fall inside the C_* sequence.  Building a constant never builds another
     (a static local it refers to is only queued, see
     ecil_queue_variable_definition), so one buffer serves. */
  scratch->size = 0;
  scratch->failed = FALSE;
  frame->current = scratch;
  if (constant->kind == (a_constant_repr_kind)ck_aggregate) {
    ok = begin_aggregate(frame, constant, sources, num_sources, FALSE);
    while (ok && level_stack_size > base) {
      an_init_level   *level = &level_stack[level_stack_size - 1];
      uint64_t        index = 0;
      a_constant_ptr  value = NULL;

      if (!ecil_init_level_next(level, &index, &value)) {
        ecil_rec0(frame, eop_c_agg_end);
        ecil_init_level_end(level);
        level_stack_size--;
      } else if (level->entry_is_char) {
        ecil_rec1(frame, eop_c_sel, index + level->vptr_shift);
        ecil_emit_string_char(frame, level, value, index);
      } else if (value->kind == (a_constant_repr_kind)ck_init_repeat) {
        a_constant_ptr  repeated = value->variant.init_repeat.constant;

        uint32_t        want = ecil_member_type_gid(frame, level, index);
        uint32_t        have = repeated->type != NULL ?
                               ecil_type_id(frame, repeated->type) : 0;

        if (want != 0 && have != 0 && want != have) {
          /* The front end repeats the innermost element at the level of an
             enclosing array (a multi-dimensional array of class objects). */
          ecil_note_unsupported("a repeated initializer of a "
                                "multi-dimensional array");
          ok = FALSE;
        } else {
          ecil_rec2(frame, eop_c_repeat, index + level->vptr_shift,
                    level->entry_count);
        }  /* if */
        if (!ok) {
          /* Noted. */
        } else if (repeated->kind == (a_constant_repr_kind)ck_aggregate) {
          ok = begin_aggregate(frame, repeated, NULL, 0, FALSE);
        } else {
          ok = emit_leaf(frame, repeated,
                         ecil_member_type_gid(frame, level, index));
        }  /* if */
      } else {
        a_constant_ptr  *srcs = level->entry_sources;
        size_t          num_srcs = level->entry_num_sources;

        ecil_rec1(frame, eop_c_sel, index + level->vptr_shift);
        if (value->kind == (a_constant_repr_kind)ck_aggregate) {
          ok = begin_aggregate(frame, value, srcs, num_srcs,
                               !level->is_array && index < level->lead);
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


/* String literals. */

uint32_t ecil_string_literal_id(an_ecil_frame *frame, a_constant_ptr constant)
/*
Return the G-id of the private constant global that holds a string literal,
defining it (DECL_VAR + C_STRING + VAR_INIT in the declaration section) the
first time.  Equal literals (same array type, same bytes) share one global,
in order of first use.  The lookup key is built in a buffer that is kept from
one literal to the next.
*/
{
  uint32_t  t = ecil_type_id(frame, constant->type);
  uint32_t  gid = 0;

  if (t != 0) {
    size_t    length = (size_t)constant->variant.string.length;
    size_t    n_words = 3 + (length + 3) / 4;

    if (n_words > string_key_capacity) {
      size_t    new_capacity = n_words * 2;
      uint32_t  *new_key = (uint32_t *)realloc(string_key,
                                        new_capacity * sizeof(uint32_t));

      if (new_key != NULL) {
        string_key = new_key;
        string_key_capacity = new_capacity;
      }  /* if */
    }  /* if */
    if (n_words > string_key_capacity) {
      ecil_note_unsupported("out of memory in the ECIL writer");
    } else {
      uint32_t  *key = string_key;
      size_t    i;

      memset(key, 0, n_words * sizeof(uint32_t));
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

          ecil_word_register(key, n_words, gid);
          ecil_emit_decl_var(frame, gid, t, (unsigned int)elk_private,
                             ECIL_DECL_VAR_DEFINITION | ECIL_DECL_VAR_CONST |
                             ECIL_DECL_VAR_STRING_LITERAL, 0, name_sid,
                             FALSE);
          saved = ecil_frame_enter_decl(frame);
          emit_string_bytes(frame, constant, t);
          ecil_rec1(frame, eop_var_init, gid);
          ecil_frame_leave_decl(frame, saved);
        }  /* if */
      }  /* if */
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


static a_boolean has_mutable_member(a_type_ptr type)
/*
Return TRUE if the type (or its element type, for an array) is a class with
a mutable member: an object of it is written to although it is const.
*/
{
  type = skip_typerefs(type);
  while (type->kind == tk_array) {
    type = skip_typerefs(type->variant.array.element_type);
  }  /* while */
  return !C_mode() && is_immediate_class_type(type) &&
         type->variant.class_struct_union.any_mutable_member;
}  /* has_mutable_member */


static void begin_variable_definition(an_ecil_frame *frame,
                                      a_variable_ptr var,
                                      uint32_t       gid,
                                      uint32_t       type,
                                      unsigned int   flags,
                                      a_boolean      needs_static_init)
/*
Send DECL_VAR(definition) for a variable that has its G-id and type: the
introduction of the G-id (a static local is named <function>.<variable>,
made unique).  The initializer follows in finish_variable_definition.  A
variable that needs a STATIC_INIT (a dynamic initializer, a destructor) is
GUARDED if it is a static local or has vague linkage (spec 11.7).
*/
{
  a_boolean         is_static_local = var->source_corresp.is_local_to_function;
  unsigned int      linkage = ecil_linkage_for(var->storage_class,
                                    var->source_corresp.name_linkage,
                                    var->is_weak);
  uint32_t          name_sid = 0;
  const char        *name = ecil_entity_name(&var->source_corresp);

  if (is_static_local) linkage = (unsigned int)elk_internal;
  /* A const object that is initialized or destroyed at run time is written
     to: it is not constant storage. */
  if (is_const_qualified_type(var->type) &&
      !(needs_static_init && !C_mode()) && !has_mutable_member(var->type)) {
    flags |= ECIL_DECL_VAR_CONST;
  }  /* if */
  if (is_effective_thread_local(var)) flags |= ECIL_DECL_VAR_TLS;
#if BACK_END_TYPE == BACK_END_TYPE_CIR
  if (!C_mode()) {
    unsigned int  cxx_flags = 0;

    ecil_cxx_variable_facts(frame, var, TRUE, &linkage, &cxx_flags,
                            &name_sid);
    flags |= cxx_flags;
    if (needs_static_init && (is_static_local ||
                              (cxx_flags & ECIL_DECL_VAR_COMDAT) != 0)) {
      flags |= ECIL_DECL_VAR_GUARDED;
    }  /* if */
    if (name_sid != 0 && linkage != (unsigned int)elk_internal) {
      ecil_mark_symbol_name(name_sid);
      ecil_bind_symbol(name_sid, gid);
    }  /* if */
  } else
#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */
  /* Do not insert code here. */
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
  if (ecil_unsupported_reason == NULL) {
    ecil_emit_decl_var(frame, gid, type, linkage, flags,
                       (uint32_t)ecil_variable_alignment(var), name_sid,
                       ecil_variable_is_used(var));
    ecil_gid_info(gid)->flags |= ECIL_GF_DEFINED;
  }  /* if */
}  /* begin_variable_definition */


static void finish_variable_definition(an_ecil_frame      *frame,
                                       a_variable_ptr     var,
                                       uint32_t           gid,
                                       uint32_t           type,
                                       an_initializer_ptr init,
                                       a_boolean          has_init)
/*
Send the initializer and VAR_INIT of a variable whose DECL_VAR(definition)
has been sent.  If the initializer cannot be expressed
(ecil_unsupported_reason becomes set) a global is re-issued as an
unsupported declaration (spec 8.11, F1); a static local is left for the
caller to handle by dropping the function.
*/
{
  an_ecil_buffer  *saved = ecil_frame_enter_decl(frame);

  if (has_init && initializes_flexible_array(init->constant)) {
    ecil_note_unsupported("an initializer of a flexible array member");
  } else if (has_init) {
    a_boolean  built;

    ecil_skip_dynamic_elements = (ecil_gid_info(gid)->flags &
                                             ECIL_GF_PARTIAL_INIT) != 0;
    in_var_init = TRUE;
    built = ecil_emit_constant_typed(frame, init->constant, type);
    in_var_init = FALSE;
    ecil_skip_dynamic_elements = FALSE;
    if (built) ecil_rec1(frame, eop_var_init, gid);
  }  /* if */
  ecil_frame_leave_decl(frame, saved);
  if (ecil_unsupported_reason != NULL &&
      !var->source_corresp.is_local_to_function) {
    /* The G-id table may have moved while the initializer was built. */
    const an_ecil_gid_info  *info = ecil_gid_info(gid);

    ecil_remember_drop_reason(gid, ecil_unsupported_reason);
    ecil_emit_dropped_decl_var(frame, gid, type, info->decl_linkage,
                               info->decl_flags,
                               (uint32_t)ecil_variable_alignment(var),
                               info->decl_name);
    ecil_gid_info(gid)->flags &= ~ECIL_GF_DEFINED;
  }  /* if */
}  /* finish_variable_definition */


a_boolean ecil_skip_dynamic_elements = FALSE;

static a_boolean aggregate_is_partly_constant(a_constant_ptr agg)
/*
Return TRUE if an aggregate initializer that has dynamic elements also has an
element that is a plain constant: the constant elements are the static
initialization (VAR_INIT) and the others a STATIC_INIT with the PARTIAL flag
(C++11 [basic.start.init]p2, as g++ does; spec 11.7).  A function-local
static is split too (its STATIC_INIT is guarded), unless its function may
exist in several translation units (inline, a template instance: the
variable is COMDAT and every copy must initialize it the same way; clang
sends zero data and the full guarded initialization, and so does the
writer); thread_local variables and variables with vague linkage (inline
variables, template statics: COMDAT and guarded) are not split.
*/
{
  a_constant_ptr  e;
  a_boolean       found = FALSE;

  for (e = agg->variant.aggregate.first_constant; e != NULL && !found;
       e = e->next) {
    found = e->kind == (a_constant_repr_kind)ck_integer ||
            e->kind == (a_constant_repr_kind)ck_float ||
            e->kind == (a_constant_repr_kind)ck_address;
  }  /* for */
  return found;
}  /* aggregate_is_partly_constant */


STATIC_THREAD an_initializer
                constant_scratch;   /* A constant initializer found in a
                                       dynamic initialization. */

static uint32_t prepare_variable_definition(an_ecil_frame      *frame,
                                            a_variable_ptr     var,
                                            a_scope_ptr        scope,
                                            uint32_t           *type_out,
                                            an_initializer_ptr *init_out,
                                            a_boolean          *has_init_out)
/*
Decide what a definition of the variable is (its type, its initializer if
any), assign its G-id and send DECL_VAR(definition).  Return the G-id, or 0
(with ecil_unsupported_reason set if the variable cannot be defined).  A
failure of the initializer itself comes later, in
finish_variable_definition.
*/
{
  an_init_kind        init_kind;
  an_initializer_ptr  init = NULL;
  uint32_t            type;
  uint32_t            gid = 0;
  unsigned int        flags = ECIL_DECL_VAR_DEFINITION;
  a_boolean           has_init = FALSE;
  a_boolean           dynamic_init = FALSE;

  a_boolean           needs_static_init = FALSE;
  a_boolean           partial_init = FALSE;

  ecil_check_variable_attributes(var);
  if (!C_mode() && ecil_class_has_nontrivial_dtor(
                                          skip_array_types(var->type))) {
    /* The destructor is registered for program exit by a STATIC_INIT. */
    needs_static_init = TRUE;
  }  /* if */
  type = ecil_unsupported_reason == NULL ? ecil_type_id(frame, var->type) : 0;
  if (type != 0) {
    get_variable_initializer(var, scope, &init_kind, &init);
    if (init_kind == (an_init_kind)initk_static) {
      has_init = TRUE;
    } else if (init_kind == (an_init_kind)initk_dynamic && !C_mode() &&
               init->dynamic != NULL &&
               (init->dynamic->kind == (a_dynamic_init_kind)dik_zero ||
                init->dynamic->kind == (a_dynamic_init_kind)dik_none)) {
      /* Zero-initialized: only a destructor to register, if any. */
    } else if (init_kind == (an_init_kind)initk_dynamic && !C_mode() &&
               init->dynamic != NULL &&
               (init->dynamic->kind == (a_dynamic_init_kind)dik_constant ||
                (init->dynamic->kind ==
                              (a_dynamic_init_kind)dik_nonconstant_aggregate &&
                 !init->dynamic->variant.constant.non_constant &&
                 !init->dynamic->variant.constant.ptr->variant.aggregate.
                                               has_dynamic_init_component &&
                 init->dynamic->variant.constant.ptr->kind ==
                              (a_constant_repr_kind)ck_aggregate))) {
      /* A constant initializer that an object with a destructor carries as
         a dynamic initialization: it is the VAR_INIT. */
      constant_scratch.constant = init->dynamic->variant.constant.ptr;
      init = &constant_scratch;
      has_init = TRUE;
    } else if (init_kind == (an_init_kind)initk_dynamic && !C_mode() &&
               init->dynamic != NULL &&
               init->dynamic->kind ==
                              (a_dynamic_init_kind)dik_nonconstant_aggregate &&
               !is_effective_thread_local(var) &&
               (var->source_corresp.is_local_to_function ?
                  !ecil_local_static_may_have_multiple_copies(var) :
                  (!var->is_inline &&
                   !ecil_variable_may_have_multiple_copies(var))) &&
               init->dynamic->variant.constant.ptr != NULL &&
               init->dynamic->variant.constant.ptr->kind ==
                              (a_constant_repr_kind)ck_aggregate &&
               aggregate_is_partly_constant(
                                 init->dynamic->variant.constant.ptr)) {
      /* A partly constant aggregate: the constant elements are the static
         initialization (VAR_INIT), the others are built by a STATIC_INIT. */
      constant_scratch.constant = init->dynamic->variant.constant.ptr;
      init = &constant_scratch;
      has_init = TRUE;
      needs_static_init = TRUE;
      dynamic_init = TRUE;
      partial_init = TRUE;
    } else if (init_kind == (an_init_kind)initk_dynamic && !C_mode()) {
      /* Built by a STATIC_INIT (spec 11.7). */
      needs_static_init = TRUE;
      dynamic_init = TRUE;
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
  if (needs_static_init && is_effective_thread_local(var) &&
      !var->source_corresp.is_local_to_function) {
    ecil_note_unsupported("a thread_local variable that is initialized or "
                          "destroyed dynamically");
  }  /* if */
  if (type != 0 && ecil_unsupported_reason == NULL) {
    gid = ecil_gid_of(var);
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
      begin_variable_definition(frame, var, gid, type, flags,
                                needs_static_init);
      if (needs_static_init && ecil_unsupported_reason == NULL) {
        ecil_gid_info(gid)->flags |= ECIL_GF_NEEDS_STATIC_INIT;
        if (partial_init) ecil_gid_info(gid)->flags |= ECIL_GF_PARTIAL_INIT;
        if (!dynamic_init) ecil_gid_info(gid)->flags |= ECIL_GF_CONST_INIT;
      }  /* if */
      if (ecil_unsupported_reason != NULL) {
        /* No DECL_VAR could be sent (the name is not final): introduce the
           G-id as an unsupported declaration so that it is not left
           unintroduced, and define nothing. */
        ecil_remember_drop_reason(gid, ecil_unsupported_reason);
        ecil_emit_dropped_decl_var(frame, gid, type,
                                   (unsigned int)elk_internal, flags,
                                   (uint32_t)ecil_variable_alignment(var), 0);
        ecil_gid_info(gid)->flags &= ~ECIL_GF_DEFINED;
        gid = 0;
      }  /* if */
    }  /* if */
  } else if (ecil_unsupported_reason != NULL &&
             !var->source_corresp.is_local_to_function && !C_mode()) {
    /* The definition is not supported (a dynamic initializer, a destructor):
       a function that refers to the variable must be reported by the
       consumer, not left with an unresolved symbol. */
    an_ecil_saved_reason  saved;

    ecil_save_reason(&saved);
    gid = ecil_global_variable_id(frame, var);
    if (gid != 0) {
      const an_ecil_gid_info  *info;

      ecil_remember_drop_reason(gid, saved.text);
      info = ecil_gid_info(gid);

      ecil_emit_dropped_decl_var(frame, gid, info->decl_type,
                                 info->decl_linkage, info->decl_flags,
                                 (uint32_t)ecil_variable_alignment(var),
                                 info->decl_name);
      gid = 0;
    }  /* if */
    ecil_restore_reason(&saved);
  }  /* if */
  *type_out = type;
  *init_out = init;
  *has_init_out = has_init;
  return gid;
}  /* prepare_variable_definition */


void ecil_define_variable(an_ecil_frame *frame,
                          a_variable_ptr var,
                          a_scope_ptr    scope)
/*
Define a global or static variable: DECL_VAR with the definition flag,
then its initializer (C_* records and VAR_INIT) if it has one; a definition
without VAR_INIT is zero-initialized.  A static local is named
<function>.<variable>, made unique.  A failure sets
ecil_unsupported_reason (see finish_variable_definition).  Static locals the
initializer refers to are queued, not defined here.
*/
{
  uint32_t            type;
  an_initializer_ptr  init;
  a_boolean           has_init;
  uint32_t            gid = prepare_variable_definition(frame, var, scope,
                                                        &type, &init,
                                                        &has_init);

  if (gid != 0) {
    finish_variable_definition(frame, var, gid, type, init, has_init);
  }  /* if */
}  /* ecil_define_variable */


uint32_t ecil_queue_variable_definition(an_ecil_frame *frame,
                                        a_variable_ptr var)
/*
Introduce a static local that has no declaration statement (__func__, a
compound literal) and was reached while a constant or an expression was
being built: assign its G-id and send DECL_VAR(definition) now, so that the
id is introduced before it is used, and queue the initializer (and VAR_INIT)
for ecil_drain_variable_definitions.  Return the G-id, or 0 (with the reason
noted).  The variable counts as defined from here on.
*/
{
  uint32_t            type;
  an_initializer_ptr  init;
  a_boolean           has_init;
  uint32_t            gid = prepare_variable_definition(frame, var, NULL,
                                                        &type, &init,
                                                        &has_init);

  if (gid != 0) {
    if (num_pending_defs == pending_defs_capacity) {
      size_t                      new_capacity = pending_defs_capacity == 0 ?
                                        16 : pending_defs_capacity * 2;
      an_ecil_pending_definition  *grown =
                (an_ecil_pending_definition *)realloc(pending_defs,
                    new_capacity * sizeof(an_ecil_pending_definition));

      if (grown != NULL) {
        pending_defs = grown;
        pending_defs_capacity = new_capacity;
      }  /* if */
    }  /* if */
    if (num_pending_defs == pending_defs_capacity) {
      ecil_note_unsupported("out of memory in the ECIL writer");
    } else {
      an_ecil_pending_definition  *d = &pending_defs[num_pending_defs++];

      d->var = var;
      d->gid = gid;
      d->type = type;
      d->init = init;
      d->has_init = has_init;
    }  /* if */
  }  /* if */
  return gid;
}  /* ecil_queue_variable_definition */


void ecil_drain_variable_definitions(an_ecil_frame *frame)
/*
Build the initializers of the queued static locals, in the order queued
(building one can queue more; they follow).  Every queued definition is
completed whatever the state of the caller, because its DECL_VAR has been
sent; the caller's own reason, if any, is kept and otherwise the first
failure among the definitions becomes the reason.
*/
{
  if (next_pending_def < num_pending_defs) {
    an_ecil_saved_reason  caller_reason;
    an_ecil_saved_reason  failure;

    ecil_save_reason(&caller_reason);
    failure.is_set = FALSE;
    while (next_pending_def < num_pending_defs) {
      an_ecil_pending_definition  d = pending_defs[next_pending_def++];

      finish_variable_definition(frame, d.var, d.gid, d.type, d.init,
                                 d.has_init);
      if (ecil_unsupported_reason != NULL) {
        if (failure.is_set) {
          ecil_unsupported_reason = NULL;
        } else {
          ecil_save_reason(&failure);
        }  /* if */
      }  /* if */
    }  /* while */
    num_pending_defs = next_pending_def = 0;
    if (failure.is_set) ecil_restore_reason(&failure);
    ecil_restore_reason(&caller_reason);
  }  /* if */
}  /* ecil_drain_variable_definitions */


static void emit_scope_variables(an_ecil_frame *frame, a_scope_ptr scope)
/*
Define the variables of a file, namespace or class scope that are defined in
this translation unit (tentative definitions included, without VAR_INIT).
*/
{
  a_variable_ptr  var;

  for (var = scope->variables; var != NULL; var = var->next) {
    a_boolean skip = variable_is_filtered(var) ||
                     var->source_corresp.is_local_to_function ||
                     var->storage_class == (a_storage_class)sc_typedef;

    if (!skip && var->source_corresp.is_class_member &&
        var->storage_class == (a_storage_class)sc_extern) {
      skip = TRUE;                   /* A static data member declaration. */
    } else if (!skip && var->storage_class == (a_storage_class)sc_extern &&
               var->init_kind == (an_init_kind)initk_none) {
      skip = TRUE;                   /* A declaration only. */
    }  /* if */
    if (!skip && var->storage_class == (a_storage_class)sc_static &&
        !var->source_corresp.referenced && ecil_gid_of(var) == 0 &&
        !ecil_variable_is_used(var)) {
      skip = TRUE;                   /* An unreferenced static (that is not
                                        marked used). */
    }  /* if */
    if (!skip && !C_mode() && ecil_gid_of(var) == 0 &&
        !var->source_corresp.referenced && !ecil_variable_is_used(var) &&
        !(var->explicit_instantiation &&
          !var->explicit_do_not_instantiate) &&
        ecil_variable_may_have_multiple_copies(var)) {
      skip = TRUE;                   /* An unused inline variable or
                                        template static member. */
    }  /* if */
    if (!skip) {
      ecil_unsupported_reason = NULL;
      ecil_define_variable(frame, var, scope);
      ecil_drain_variable_definitions(frame);
      if (ecil_unsupported_reason != NULL) {
        char  message[ECIL_DIAGNOSTIC_MAX];

        (void)snprintf(message, sizeof(message),
                       "the initializer of \"%.200s\" uses %.*s, which the "
                       "CIR back end does not support; the definition is "
                       "dropped",
                       var->source_corresp.name != NULL ?
                                   var->source_corresp.name : "?",
                       ECIL_REASON_MAX - 1, ecil_unsupported_reason);
        if (ecil_variable_is_root(var)) {
          cir_be_error(message);
        } else {
          cir_be_warning(message);
        }  /* if */
        ecil_unsupported_reason = NULL;
      }  /* if */
    }  /* if */
  }  /* for */
}  /* emit_scope_variables */


static a_boolean push_walk_scope(a_scope_ptr **stack,
                                 size_t       *size,
                                 size_t       *capacity,
                                 a_scope_ptr  scope)
/*
Push a scope on the scope walk's heap stack; FALSE if memory ran out.
*/
{
  a_boolean  ok = TRUE;

  if (scope != NULL) {
    if (*size == *capacity) {
      size_t       new_capacity = (*capacity == 0) ? 64 : *capacity * 2;
      a_scope_ptr  *new_stack = (a_scope_ptr *)realloc(*stack,
                                          new_capacity * sizeof(a_scope_ptr));

      if (new_stack == NULL) {
        ok = FALSE;
      } else {
        *stack = new_stack;
        *capacity = new_capacity;
      }  /* if */
    }  /* if */
    if (ok) (*stack)[(*size)++] = scope;
  }  /* if */
  return ok;
}  /* push_walk_scope */


static void emit_namespace_and_class_variables(an_ecil_frame *frame)
/*
C++: define the variables of every namespace and class scope under the file
scope (the file scope itself is done by the caller), walking the scopes with
a heap stack.
*/
{
  a_scope_ptr  *stack = NULL;
  size_t       size = 0;
  size_t       capacity = 0;
  a_boolean    ok = TRUE;

  ok = push_walk_scope(&stack, &size, &capacity, il_header.primary_scope);
  while (ok && size > 0) {
    a_scope_ptr  scope = stack[--size];
    a_namespace_ptr
                 ns;
    a_type_ptr   type;

    if (scope != il_header.primary_scope) emit_scope_variables(frame, scope);
    for (ns = scope->namespaces; ns != NULL && ok; ns = ns->next) {
      if (!ns->is_namespace_alias) {
        ok = push_walk_scope(&stack, &size, &capacity,
                             ns->variant.assoc_scope);
      }  /* if */
    }  /* for */
    for (type = scope->types; type != NULL && ok; type = type->next) {
      if ((type->kind == (a_type_kind)tk_class ||
           type->kind == (a_type_kind)tk_struct ||
           type->kind == (a_type_kind)tk_union) &&
          !type->incomplete &&
          !type->variant.class_struct_union.is_nonreal_class) {
        a_class_type_supplement_ptr  ctsp = class_type_supp(type);

        if (ctsp != NULL && ctsp->assoc_scope != NULL) {
          ok = push_walk_scope(&stack, &size, &capacity, ctsp->assoc_scope);
        }  /* if */
      }  /* if */
    }  /* for */
  }  /* while */
  free(stack);
}  /* emit_namespace_and_class_variables */


void ecil_emit_globals(an_ecil_frame *frame)
/*
Fill the F_GLOBALS frame: records completed since they were declared,
definitions of the file-scope variables (including tentative definitions,
without VAR_INIT), in C++ those of the namespace and class scopes too, and
re-issued function declarations whose final status differs from the last one
sent (C99 inline-only, spec 8.2).  Static locals are not here: they were
defined in the frame of their function.
*/
{
  uint32_t        gid;
  uint32_t        last;

  ecil_complete_pending_records(frame);
  emit_scope_variables(frame, il_header.primary_scope);
  if (!C_mode()) emit_namespace_and_class_variables(frame);
  if (!C_mode()) {
    /* The body section: the dynamic initializations (and the registrations
       of destructors) of the variables of static storage duration, in the
       order the front end lists them (spec 11.7). */
    a_dynamic_init_ptr  di;

    ecil_frame_begin_body(frame);
    for (di = il_header.primary_scope->dynamic_inits; di != NULL;
         di = di->next) {
      ecil_stream_static_init(frame, di);
    }  /* for */
  }  /* if */
  last = ecil_num_gids();
  ecil_complete_pending_records(frame);
  ecil_resolve_late_names(frame);
  for (gid = 1; gid <= last; gid++) {
    an_ecil_gid_info  *info = ecil_gid_info(gid);

    if (info->kind == egk_func &&
        (info->flags & ECIL_GF_FUNCTION_LOCAL) == 0) {
      /* (A routine of a function's memory region is gone by now.) */
      ecil_reissue_function(frame, gid,
                            (info->flags & ECIL_GF_DEFINED) != 0,
                            (info->decl_flags & ECIL_DECL_FUNC_UNSUPPORTED)
                                                                    != 0);
    }  /* if */
  }  /* for */
  if (!C_mode()) {
    /* The vtables and class typeinfo of the dynamic classes: the key
       functions, the "used" marks and the linkage are final (spec 12.8). */
    ecil_emit_class_abi_data(frame, FALSE);
  }  /* if */
  ecil_emit_retractions(frame);
}  /* ecil_emit_globals */


/* Conditionally close the "edg" namespace. */
END_EDG_NAMESPACE

#endif /* ECIL_WRITER_ENABLED */
