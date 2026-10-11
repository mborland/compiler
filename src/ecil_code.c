/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*

ecil_code.c -- ECIL function bodies: statements and expressions.

A function memory region becomes one F_REGION frame: its declaration section
(types, declarations, string literals and static locals introduced on the
way) and a body section FUNC_BEGIN ... FUNC_END.  Expressions are post-order
(a value stack in the consumer); control flow is bracketed.

The traversal is an explicit heap work stack in the style of the constexpr
interpreter (interpret.c an_interpreter_work_item): an item has a kind, a
phase and the IL entry it translates; a handler runs once per phase and
pushes the children it must wait for.  Nothing recurses on the depth of the
IL, so a 50,000-term sum or a 20,000-link else-if ladder uses heap, not
stack.

Value categories: the IL has no load node.  A node that is not a glvalue but
whose kind designates storage (a variable, an indirection, a subscript, a
member selection, ...) means "address, then load"; the writer emits the
address (LREF, GREF, DEREF, FIELD, ...) and then LOAD.  Casts are classified
here (an_ecil_cast_kind) from the canonical ECIL types.

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

/* The kinds of work item. */
enum a_code_item_kind {
  cik_expr,         /* Translate an expression node. */
  cik_stmt,         /* Translate a statement. */
  cik_stmt_list,    /* A cursor over a chain of sibling statements. */
  cik_init,         /* Initialize a local object from a dynamic init. */
  cik_agg,          /* Initialize a local aggregate entry by entry. */
  cik_store         /* Emit a STORE of a value just evaluated. */
};

/* Item flags. */
#define CIF_COND 0x01u      /* The result is wanted as a bool (TO_BOOL). */

/* An item of the work stack. */
typedef struct a_code_item {
  enum a_code_item_kind
                kind;
  unsigned int  phase;
  unsigned int  flags;
  void          *entry;           /* The IL entry being translated. */
  void          *cursor;          /* An operand or statement cursor. */
  uint32_t      a;                /* Scratch (a type id, a lid, ...). */
  uint32_t      b;
} a_code_item;

STATIC_THREAD a_code_item
                *code_stack;
STATIC_THREAD size_t
                code_stack_size;
STATIC_THREAD size_t
                code_stack_capacity;
STATIC_THREAD uint32_t
                next_lid;         /* The last frame-local id assigned. */
STATIC_THREAD uint8_t
                *function_states; /* An an_ecil_function_state, indexed by
                                     function_def_number. */
STATIC_THREAD size_t
                function_states_capacity;
STATIC_THREAD an_init_level
                *agg_levels;      /* Dynamic aggregate cursors. */
STATIC_THREAD size_t
                num_agg_levels;
STATIC_THREAD size_t
                agg_levels_capacity;

void ecil_code_reset(void)
/*
Release the writer's function-body storage at the end of a compilation.
*/
{
  while (num_agg_levels > 0) {
    ecil_init_level_end(&agg_levels[--num_agg_levels]);
  }  /* while */
  free(code_stack);
  free(function_states);
  free(agg_levels);
  code_stack = NULL;
  function_states = NULL;
  agg_levels = NULL;
  code_stack_size = code_stack_capacity = 0;
  function_states_capacity = 0;
  num_agg_levels = agg_levels_capacity = 0;
  next_lid = 0;
}  /* ecil_code_reset */


an_ecil_function_state ecil_function_state(a_routine_ptr rout)
/*
Return what the writer has done with the routine's body (efs_none if nothing
yet).  A routine that has a body has a function definition number, which
indexes the map (number 0 is the "no definition" number and is never
tracked: it reads as efs_none).
*/
{
  size_t                  n = (size_t)rout->function_def_number;
  an_ecil_function_state  state = efs_none;

  if (n != (size_t)NULL_function_def_number && n < function_states_capacity) {
    state = (an_ecil_function_state)function_states[n];
  }  /* if */
  return state;
}  /* ecil_function_state */


void ecil_set_function_state(a_routine_ptr rout, an_ecil_function_state state)
/*
Record what the writer has done with the routine's body.
*/
{
  size_t  n = (size_t)rout->function_def_number;

  check_assertion(n != (size_t)NULL_function_def_number);
  if (n == (size_t)NULL_function_def_number) {
    /* Not tracked (see ecil_function_state). */
  } else if (n >= function_states_capacity) {
    size_t  new_capacity = (n + 1024) * 2;
    uint8_t *new_map = (uint8_t *)realloc(function_states, new_capacity);

    if (new_map != NULL) {
      memset(new_map + function_states_capacity, 0,
             new_capacity - function_states_capacity);
      function_states = new_map;
      function_states_capacity = new_capacity;
    }  /* if */
  }  /* if */
  if (n != (size_t)NULL_function_def_number &&
      n < function_states_capacity) {
    function_states[n] = (uint8_t)state;
  }  /* if */
}  /* ecil_set_function_state */


a_boolean ecil_function_is_handled(a_routine_ptr rout)
/*
Return TRUE if the writer is done with the routine's body: streamed, dropped
as unsupported or retracted.  (A delayed one is not done.)
*/
{
  an_ecil_function_state  state = ecil_function_state(rout);

  return state == efs_streamed || state == efs_dropped ||
         state == efs_retracted;
}  /* ecil_function_is_handled */


/* The work stack. */

#if CHECKING
/* High-water mark of the work stack (tests of non-recursion). */
size_t ecil_code_stack_high;
#endif /* CHECKING */


static void rec_bool(an_ecil_frame *frame, unsigned int opcode, a_boolean v)
/*
A record whose single operand is a bool (written as false/true, spec 5).
*/
{
  ecil_rec_begin(frame, opcode);
  ecil_arg_bool(frame, v);
  ecil_rec_end(frame);
}  /* rec_bool */


static a_code_item *push_item(enum a_code_item_kind kind,
                              void                  *entry,
                              unsigned int          flags)
/*
Push an item and return it (valid until the next push), or NULL if the
stack could not grow.
*/
{
  a_code_item *item = NULL;

  if (code_stack_size == code_stack_capacity) {
    size_t      new_capacity = (code_stack_capacity == 0) ?
                               256 : code_stack_capacity * 2;
    a_code_item *new_stack = (a_code_item *)realloc(code_stack,
                                       new_capacity * sizeof(a_code_item));

    if (new_stack != NULL) {
      code_stack = new_stack;
      code_stack_capacity = new_capacity;
    }  /* if */
  }  /* if */
  if (code_stack_size < code_stack_capacity) {
    item = &code_stack[code_stack_size++];
#if CHECKING
    if (code_stack_size > ecil_code_stack_high) {
      ecil_code_stack_high = code_stack_size;
    }  /* if */
#endif /* CHECKING */
    item->kind = kind;
    item->phase = 0;
    item->flags = flags;
    item->entry = entry;
    item->cursor = NULL;
    item->a = 0;
    item->b = 0;
  } else {
    ecil_note_unsupported("out of memory in the ECIL writer");
  }  /* if */
  return item;
}  /* push_item */


#define push_expr(node, flags) \
  ((void)push_item(cik_expr, (void *)(node), (flags)))
#define push_stmt(stmt) ((void)push_item(cik_stmt, (void *)(stmt), 0))


/* Small helpers on types and nodes. */

static an_ecil_gid_info *info_of(uint32_t gid)
/*
Return the writer's record for a type id.
*/
{
  return ecil_gid_info(gid);
}  /* info_of */


static a_boolean is_void_node_type(an_expr_node_ptr node)
/*
Return TRUE if the expression has type void.
*/
{
  return skip_typerefs(node->type)->kind == tk_void;
}  /* is_void_node_type */


static a_boolean is_assignment_operator(an_expr_operator_kind op)
/*
Return TRUE for simple and compound assignment.
*/
{
  return op == eok_assign || op == eok_add_assign ||
         op == eok_subtract_assign || op == eok_multiply_assign ||
         op == eok_divide_assign || op == eok_remainder_assign ||
         op == eok_shiftl_assign || op == eok_shiftr_assign ||
         op == eok_and_assign || op == eok_or_assign ||
         op == eok_xor_assign || op == eok_padd_assign ||
         op == eok_psubtract_assign;
}  /* is_assignment_operator */


static a_boolean node_pushes_value(an_expr_node_ptr node)
/*
Return TRUE if translating the expression leaves a value on the consumer's
stack.  Void expressions and assignments whose result is unused leave none.
*/
{
  a_boolean pushes = TRUE;

  node = skip_parens(node);
  if (is_void_node_type(node)) {
    pushes = FALSE;
  } else if (node->kind == (an_expr_node_kind)enk_operation &&
             is_assignment_operator(node->variant.operation.kind) &&
             node->result_is_not_used) {
    pushes = FALSE;
  }  /* if */
  return pushes;
}  /* node_pushes_value */


static a_boolean access_is_volatile(an_expr_node_ptr node)
/*
Return TRUE if loading or storing through this node's lvalue is volatile.
*/
{
  a_type_ptr  t = node->orig_lvalue_type != NULL ? node->orig_lvalue_type :
                                                   node->type;

  return node->volatile_fetch ||
         (get_type_qualifiers(t) & TQ_VOLATILE) != 0;
}  /* access_is_volatile */


static uint32_t new_lid(void)
/*
Assign the next frame-local id.
*/
{
  return ++next_lid;
}  /* new_lid */


static void emit_local(an_ecil_frame *frame,
                       uint32_t      lid,
                       uint32_t      t,
                       uint32_t      name_sid,
                       uint32_t      alignment)
/*
LOCAL: declare local (or temporary) lid of ECIL type t, named by name_sid
(0: unnamed), with an explicit alignment (0: the natural one).
*/
{
  ecil_rec_begin(frame, eop_local);
  ecil_arg_uint(frame, lid);
  ecil_arg_uint(frame, t);
  if (name_sid != 0) {
    ecil_arg_uint(frame, name_sid);
  } else {
    ecil_arg_nil(frame);
  }  /* if */
  ecil_arg_uint(frame, alignment);
  ecil_arg_uint(frame, 0);
  ecil_rec_end(frame);
}  /* emit_local */


/* Conversions. */

static unsigned int float_rank(const an_ecil_gid_info *info)
/*
Order floating types by precision.
*/
{
  unsigned int  rank = 0;

  switch ((an_ecil_float_format)info->float_format) {
    case eff_ieee_half:
    case eff_bfloat16:   rank = 1; break;
    case eff_ieee_single: rank = 2; break;
    case eff_ieee_double: rank = 3; break;
    case eff_x87_80:     rank = 4; break;
    default:             rank = 5; break;
  }  /* switch */
  return rank;
}  /* float_rank */


static uint32_t common_gid(an_ecil_frame *frame, uint32_t a, uint32_t b)
/*
The usual arithmetic conversions on two ECIL arithmetic types.
*/
{
  an_ecil_gid_info  *ia = info_of(a);
  an_ecil_gid_info  *ib = info_of(b);
  uint32_t          result = a;

  if (ia->kind == egk_float || ib->kind == egk_float) {
    if (ia->kind != egk_float) {
      result = b;
    } else if (ib->kind == egk_float && float_rank(ib) > float_rank(ia)) {
      result = b;
    }  /* if */
  } else {
    uint32_t  pa = ecil_promote_gid(frame, a, NULL);
    uint32_t  pb = ecil_promote_gid(frame, b, NULL);

    ia = info_of(pa);
    ib = info_of(pb);
    if (ia->is_signed == ib->is_signed) {
      result = (ia->bits >= ib->bits) ? pa : pb;
    } else {
      uint32_t  u = ia->is_signed ? pb : pa;
      uint32_t  s = ia->is_signed ? pa : pb;

      if (info_of(u)->bits >= info_of(s)->bits) {
        result = u;
      } else {
        result = s;
      }  /* if */
    }  /* if */
  }  /* if */
  return result;
}  /* common_gid */


static void emit_cast(an_ecil_frame *frame,
                      unsigned int  kind,
                      uint32_t      dst)
/*
Emit CAST(kind, dst).
*/
{
  ecil_rec_begin(frame, eop_cast);
  ecil_arg_uint(frame, kind);
  ecil_arg_uint(frame, dst);
  ecil_rec_end(frame);
}  /* emit_cast */


static void emit_convert(an_ecil_frame *frame, uint32_t src, uint32_t dst)
/*
Convert the prvalue on top of the stack from ECIL type src to dst,
classifying the cast (spec section 8.8).  Nothing is emitted if the types
are the same.
*/
{
  if (src != dst) {
    an_ecil_gid_info  *s = info_of(src);
    an_ecil_gid_info  *d = info_of(dst);

    if ((s->flags | d->flags) & (ECIL_GF_OPAQUE | ECIL_GF_ABI_OPAQUE)) {
      ecil_note_unsupported_type(
              (s->flags & (ECIL_GF_VECTOR | ECIL_GF_COMPLEX)) ? src : dst,
              "a conversion involving an unsupported type");
    } else if (d->kind == egk_bool) {
      if (s->kind == egk_float) {
        emit_cast(frame, (unsigned int)eck_float_to_bool, dst);
      } else if (s->kind == egk_ptr) {
        emit_cast(frame, (unsigned int)eck_ptr_to_bool, dst);
      } else {
        emit_cast(frame, (unsigned int)eck_int_to_bool, dst);
      }  /* if */
    } else if (s->kind == egk_bool) {
      if (d->kind == egk_int) {
        emit_cast(frame, (unsigned int)eck_bool_to_int, dst);
      } else if (d->kind == egk_float) {
        emit_cast(frame, (unsigned int)eck_bool_to_float, dst);
      } else if (d->kind == egk_ptr) {
        uint32_t  wide = ecil_target_uintptr_type(frame);

        emit_cast(frame, (unsigned int)eck_bool_to_int, wide);
        emit_cast(frame, (unsigned int)eck_int_to_ptr, dst);
      } else {
        ecil_note_unsupported("a conversion from _Bool");
      }  /* if */
    } else if (s->kind == egk_int && d->kind == egk_int) {
      emit_cast(frame, (unsigned int)eck_integral, dst);
    } else if (s->kind == egk_int && d->kind == egk_float) {
      emit_cast(frame, (unsigned int)eck_int_to_float, dst);
    } else if (s->kind == egk_float && d->kind == egk_int) {
      emit_cast(frame, (unsigned int)eck_float_to_int, dst);
    } else if (s->kind == egk_float && d->kind == egk_float) {
      emit_cast(frame, (unsigned int)eck_floating, dst);
    } else if (s->kind == egk_int && d->kind == egk_ptr) {
      emit_cast(frame, (unsigned int)eck_int_to_ptr, dst);
    } else if (s->kind == egk_ptr && d->kind == egk_int) {
      emit_cast(frame, (unsigned int)eck_ptr_to_int, dst);
    } else if (s->kind == egk_ptr && d->kind == egk_ptr) {
      emit_cast(frame, (unsigned int)eck_bitcast, dst);
    } else {
      ecil_note_unsupported("this conversion");
    }  /* if */
  }  /* if */
}  /* emit_convert */


static void note_unsupported_kind(const char *what, int kind)
/*
Note an unsupported operator or node kind, giving its enumerator value (see
an_expr_operator_kind, an_expr_node_kind, a_constant_repr_kind in il_def.h).
*/
{
  char  reason[80];

  (void)snprintf(reason, sizeof(reason), "the %s %d", what, kind);
  ecil_note_unsupported(reason);
}  /* note_unsupported_kind */


static void note_unsupported_operator(an_ecil_frame *frame,
                                      an_expr_operator_kind op,
                                      an_expr_node_ptr node)
/*
Note an unsupported operator.  Operators on vector or complex values are
reported as such, the others by enumerator number.
*/
{
  an_expr_node_ptr  operand;
  uint32_t          t = ecil_type_id(frame, node->type);
  a_boolean         named = FALSE;

  for (operand = node->variant.operation.operands;
       operand != NULL && !named; operand = operand->next) {
    uint32_t  ot = ecil_type_id(frame, operand->type);

    if (ot != 0 && (info_of(ot)->flags & (ECIL_GF_VECTOR | ECIL_GF_COMPLEX))) {
      ecil_note_unsupported_type(ot, "this operator");
      named = TRUE;
    }  /* if */
  }  /* for */
  if (!named && t != 0 &&
      (info_of(t)->flags & (ECIL_GF_VECTOR | ECIL_GF_COMPLEX))) {
    ecil_note_unsupported_type(t, "this operator");
    named = TRUE;
  }  /* if */
  if (!named && ecil_unsupported_reason == NULL) {
    note_unsupported_kind("operator", (int)op);
  }  /* if */
}  /* note_unsupported_operator */



/* Leaf expressions. */

static void emit_constant_node(an_ecil_frame *frame, an_expr_node_ptr node)
/*
An enk_constant node: a value, or (string literals) a glvalue.  The scalar
and address-resolution parts are shared with the initializer emitter
(ecil_const.c).
*/
{
  a_constant_ptr  c = node_constant(node);
  uint32_t        t = ecil_type_id(frame, node->type);

  if (t != 0) {
    switch (c->kind) {
      case ck_integer:
      case ck_float:
        (void)ecil_emit_scalar_constant(frame, c, t, FALSE);
        break;
      case ck_string:
        { uint32_t  g = ecil_string_literal_id(frame, c);

          if (g != 0) ecil_rec1(frame, eop_gref, g);
        }
        break;
      case ck_address:
        { a_boolean is_function;
          uint32_t  g = ecil_address_constant_gid(frame, c, &is_function);

          if (g != 0) {
            ecil_rec1(frame, eop_gref, g);
            if (!is_function) ecil_rec0(frame, eop_addr);
            if (!is_function || c->variant.address.offset != 0) {
              /* ADDR_OFFSET's type is a pointer type: for an address that
                 was cast to an integer, use void * and convert after. */
              uint32_t  pt = (info_of(t)->kind == egk_ptr) ? t :
                     ecil_ptr_type(frame, ecil_void_type(frame));

              ecil_rec_begin(frame, eop_addr_offset);
              ecil_arg_uint(frame, pt);
              ecil_arg_int(frame, (int64_t)c->variant.address.offset);
              ecil_rec_end(frame);
              if (pt != t) emit_convert(frame, pt, t);
            } else if (info_of(t)->kind != egk_ptr) {
              emit_convert(frame, ecil_ptr_type(frame,
                                                ecil_void_type(frame)), t);
            }  /* if */
          }  /* if */
        }
        break;
      default:
        note_unsupported_kind("constant kind", (int)c->kind);
        break;
    }  /* switch */
  }  /* if */
}  /* emit_constant_node */


static a_boolean variable_is_automatic(a_variable_ptr var)
/*
Return TRUE for locals with automatic storage (including parameters).
*/
{
  return var->source_corresp.is_local_to_function &&
         var->storage_class != (a_storage_class)sc_static &&
         var->storage_class != (a_storage_class)sc_extern;
}  /* variable_is_automatic */


static void emit_variable_node(an_ecil_frame *frame, an_expr_node_ptr node)
/*
An enk_variable node: LREF or GREF (a glvalue).  A static local with no
declaration statement (__func__) is defined at its first use: its G-id is
introduced here and its initializer queued (ecil_variable_id), which counts
as defined from then on.
*/
{
  a_variable_ptr  var = node_variable(node);

  if (variable_is_automatic(var)) {
    uint32_t  lid = ecil_lid_of(var);

    if (lid == 0) {
      ecil_note_unsupported("a reference to an undeclared local variable");
    } else {
      ecil_rec1(frame, eop_lref, lid);
    }  /* if */
  } else {
    uint32_t  gid = ecil_gid_of(var);

    if (gid == 0) gid = ecil_variable_id(frame, var);
    if (gid != 0 && ecil_unsupported_reason == NULL) {
      ecil_rec1(frame, eop_gref, gid);
    }  /* if */
  }  /* if */
}  /* emit_variable_node */


static a_boolean value_is_opaque_scalar(an_ecil_frame *frame,
                                        an_expr_node_ptr node)
/*
Return TRUE if the node's type is an opaque scalar (_Float16, _BitInt, ...):
ECIL has no operations on such values, so every value-level use of one is
unsupported (spec 8.11).  Records with opaque members are copied whole and
are not scalars.
*/
{
  uint32_t  t = ecil_type_id(frame, node->type);

  return t != 0 && info_of(t)->kind == egk_opaque;
}  /* value_is_opaque_scalar */


static void emit_load_if_value(an_ecil_frame *frame, an_expr_node_ptr node)
/*
After the address of a storage-designating node: add the implicit
lvalue-to-rvalue conversion if the node is used as a value.  A value of an
opaque scalar type or of a record whose body could not be written is
unsupported.
*/
{
  if (!is_glvalue_node(node)) {
    uint32_t    gid = ecil_type_id(frame, node->type);
    a_type_ptr  t = skip_typerefs(node->type);

    if (gid != 0 && (info_of(gid)->kind == egk_opaque ||
                     (info_of(gid)->flags & ECIL_GF_FAILED) != 0)) {
      ecil_note_unsupported_type(gid, "a value of an unsupported type");
    } else if (t->kind != tk_array && t->kind != tk_routine) {
      ecil_rec_begin(frame, eop_load);
      ecil_arg_uint(frame, access_is_volatile(node) ?
                           ECIL_ACCESS_VOLATILE : 0u);
      ecil_rec_end(frame);
    }  /* if */
  }  /* if */
}  /* emit_load_if_value */


/* Unsupported builtins and the library builtin rule. */

static void note_unsupported_builtin(const char *name)
/*
Note an unsupported builtin function, naming it in the reason.
*/
{
  char  reason[160];

  (void)snprintf(reason, sizeof(reason), "the builtin function %.100s",
                 name != NULL ? name : "?");
  ecil_note_unsupported(reason);
}  /* note_unsupported_builtin */


static a_boolean classify_routine(a_routine_ptr       rout,
                                  an_ecil_name_class  *cls)
/*
Classify a routine by name (one hashed lookup) and return TRUE if it is a
front-end builtin routine (a __builtin_X name or a routine the front end
declares itself, such as __sync_* and __atomic_*) that ECIL has no mapping
for.  Library functions, under either spelling, are ordinary calls.
*/
{
  const char  *name = rout->source_corresp.name;
  a_boolean   is_builtin = FALSE;
  a_boolean   unsupported = FALSE;

  cls->special_id = 0;
  cls->library_name = NULL;
  cls->is_library_function = FALSE;
  if (name != NULL) {
    is_builtin = strncmp(name, "__builtin_", 10) == 0;
#if BUILTIN_FUNCTIONS_ENABLED
    if (!is_builtin &&
        rout->special_kind == (a_special_function_kind)sfk_none &&
        rout->variant.builtin_function_kind !=
                                           (a_builtin_function_kind)bfk_none) {
      is_builtin = TRUE;
    }  /* if */
#endif /* BUILTIN_FUNCTIONS_ENABLED */
    if (is_builtin) {
      /* Only builtins need the lookup: the classification of an ordinary
         routine is never used. */
      ecil_classify_function_name(name, cls);
      if (cls->special_id == 0 && cls->library_name == NULL &&
          !cls->is_library_function) {
        unsupported = TRUE;
      }  /* if */
    }  /* if */
  }  /* if */
  return unsupported;
}  /* classify_routine */


/* Operators. */

static unsigned int arith_flags(uint32_t gid)
/*
ECIL_ARITH_NSW for signed integer operations of rank int or wider (C: signed
overflow is undefined), unless -fwrapv semantics are in effect.
*/
{
  an_ecil_gid_info  *info = info_of(gid);

  return (!ecil_wrapv && info->kind == egk_int && info->is_signed &&
          info->bits >= ecil_int_bits()) ? ECIL_ARITH_NSW : 0u;
}  /* arith_flags */


static a_boolean operator_opcode(an_expr_operator_kind op,
                                 unsigned int          *opcode)
/*
Map a plain arithmetic or bitwise operator (and its compound-assignment
form) to its ECIL opcode.  Return FALSE for other operators.
*/
{
  a_boolean found = TRUE;

  switch (op) {
    case eok_add: case eok_add_assign:
      *opcode = eop_add; break;
    case eok_subtract: case eok_subtract_assign:
      *opcode = eop_sub; break;
    case eok_multiply: case eok_multiply_assign:
      *opcode = eop_mul; break;
    case eok_divide: case eok_divide_assign:
      *opcode = eop_div; break;
    case eok_remainder: case eok_remainder_assign:
      *opcode = eop_rem; break;
    case eok_shiftl: case eok_shiftl_assign:
      *opcode = eop_shl; break;
    case eok_shiftr: case eok_shiftr_assign:
      *opcode = eop_shr; break;
    case eok_and: case eok_and_assign:
      *opcode = eop_bit_and; break;
    case eok_or: case eok_or_assign:
      *opcode = eop_bit_or; break;
    case eok_xor: case eok_xor_assign:
      *opcode = eop_bit_xor; break;
    default:
      found = FALSE; break;
  }  /* switch */
  return found;
}  /* operator_opcode */


static a_boolean comparison_kind(an_expr_operator_kind op, unsigned int *kind)
/*
Map a comparison operator to an_ecil_cmp_kind.
*/
{
  a_boolean found = TRUE;

  switch (op) {
    case eok_lt: *kind = (unsigned int)ecmp_lt; break;
    case eok_le: *kind = (unsigned int)ecmp_le; break;
    case eok_gt: *kind = (unsigned int)ecmp_gt; break;
    case eok_ge: *kind = (unsigned int)ecmp_ge; break;
    case eok_eq: *kind = (unsigned int)ecmp_eq; break;
    case eok_ne: *kind = (unsigned int)ecmp_ne; break;
    default: found = FALSE; break;
  }  /* switch */
  return found;
}  /* comparison_kind */


static void finish_bool_result(an_ecil_frame *frame,
                               an_expr_node_ptr node,
                               unsigned int   flags)
/*
After a record that leaves a bool (CMP, LOG_NOT, LAND_END, LOR_END): in a
condition context nothing more is needed (the peephole of spec 8.7);
otherwise convert to the C type of the node (int) with bool_to_int.
*/
{
  if (!(flags & CIF_COND)) {
    uint32_t  t = ecil_type_id(frame, node->type);

    if (t != 0 && info_of(t)->kind != egk_bool) {
      emit_cast(frame, (unsigned int)eck_bool_to_int, t);
    }  /* if */
  }  /* if */
}  /* finish_bool_result */


static void finish_value(an_ecil_frame *frame, unsigned int flags)
/*
After a non-bool value in a condition context: TO_BOOL.
*/
{
  if (flags & CIF_COND) ecil_rec0(frame, eop_to_bool);
}  /* finish_value */


static a_boolean pointer_operand_first(an_expr_node_ptr op1,
                                       an_expr_node_ptr op2,
                                       an_expr_node_ptr *ptr_op,
                                       an_expr_node_ptr *int_op,
                                       an_ecil_frame    *frame)
/*
Pick the pointer and the integer operand of pointer arithmetic or a
subscript (either order); return FALSE if neither is a pointer.
*/
{
  uint32_t  t1 = ecil_type_id(frame, op1->type);
  a_boolean ok = TRUE;

  if (t1 != 0 && info_of(t1)->kind == egk_ptr) {
    *ptr_op = op1;
    *int_op = op2;
  } else {
    *ptr_op = op2;
    *int_op = op1;
  }  /* if */
  if (ecil_unsupported_reason != NULL) ok = FALSE;
  return ok;
}  /* pointer_operand_first */


static void emit_ptr_stride(an_ecil_frame *frame,
                            an_expr_node_ptr index_node,
                            a_boolean      negate)
/*
PTR_STRIDE with the signedness of the index.
*/
{
  uint32_t      t = ecil_type_id(frame, index_node->type);
  unsigned int  flags = negate ? ECIL_PTR_STRIDE_NEGATE : 0u;

  if (t != 0 && !info_of(t)->is_signed) {
    flags |= ECIL_PTR_STRIDE_INDEX_UNSIGNED;
  }  /* if */
  ecil_rec1(frame, eop_ptr_stride, flags);
}  /* emit_ptr_stride */


/* The address of a member: FIELD / EXTRACT. */

static a_boolean field_index(an_ecil_frame *frame,
                             a_type_ptr    record_type,
                             a_field_ptr   field,
                             uint32_t      *rec_gid,
                             uint32_t      *index)
/*
Resolve a member selection to the record's G-id and the member's position.
*/
{
  a_boolean ok = FALSE;

  *rec_gid = ecil_type_id(frame, record_type);
  if (*rec_gid != 0 && ecil_gid_of(field) != 0) {
    *index = ecil_gid_of(field) - 1;
    ok = TRUE;
  } else if (*rec_gid != 0) {
    if (info_of(*rec_gid)->flags & ECIL_GF_OPAQUE) {
      ecil_note_unsupported("a member of a record with a _BitInt bit-field "
                            "(opaque storage)");
    } else {
      ecil_note_unsupported("a member of an incomplete type");
    }  /* if */
  }  /* if */
  return ok;
}  /* field_index */


/* Expression handlers. */

/* Floating-point classification and comparison builtins. */

static a_boolean fp_is_comparison(unsigned int id)
/*
Return TRUE for the two-operand comparison builtins.
*/
{
  return id >= ECIL_SPECIAL_FP_ISLESS && id <= ECIL_SPECIAL_FP_LAST;
}  /* fp_is_comparison */


static unsigned int fp_mask(unsigned int id)
/*
The ECIL_FP_CLASS_* mask of a single-argument classification builtin.
*/
{
  unsigned int  mask = 0;

  switch (id) {
    case ECIL_SPECIAL_FP_ISNAN:
      mask = ECIL_FP_CLASS_SNAN | ECIL_FP_CLASS_QNAN;
      break;
    case ECIL_SPECIAL_FP_ISINF:
      mask = ECIL_FP_CLASS_NEG_INF | ECIL_FP_CLASS_POS_INF;
      break;
    case ECIL_SPECIAL_FP_ISFINITE:
      mask = ECIL_FP_CLASS_NEG_NORMAL | ECIL_FP_CLASS_NEG_SUBNORMAL |
             ECIL_FP_CLASS_NEG_ZERO | ECIL_FP_CLASS_POS_ZERO |
             ECIL_FP_CLASS_POS_SUBNORMAL | ECIL_FP_CLASS_POS_NORMAL;
      break;
    case ECIL_SPECIAL_FP_ISNORMAL:
      mask = ECIL_FP_CLASS_NEG_NORMAL | ECIL_FP_CLASS_POS_NORMAL;
      break;
    case ECIL_SPECIAL_FP_ISSIGNALING:
      mask = ECIL_FP_CLASS_SNAN;
      break;
    default:
      break;
  }  /* switch */
  return mask;
}  /* fp_mask */


static an_expr_node_ptr fp_operand(a_code_item *it, an_expr_node_ptr callee)
/*
The operand that is classified: the last of fpclassify's six, else the first.
*/
{
  an_expr_node_ptr  x = callee->next;

  if (it->b == ECIL_SPECIAL_FP_FPCLASSIFY) {
    while (x != NULL && x->next != NULL) x = x->next;
  }  /* if */
  return x;
}  /* fp_operand */


static uint32_t fp_operand_type(an_ecil_frame *frame, an_expr_node_ptr x)
/*
The floating type the operand is classified as: its own type, or double for
an integer operand (GCC converts it); 0 if neither.
*/
{
  uint32_t  xt = ecil_type_id(frame, x->type);
  uint32_t  result = 0;

  if (xt != 0 && info_of(xt)->kind == egk_float) {
    result = xt;
  } else if (xt != 0 && info_of(xt)->kind == egk_int) {
    result = ecil_target_double_type(frame);
  }  /* if */
  return result;
}  /* fp_operand_type */


static void emit_fp_operand_conversion(an_ecil_frame *frame, a_code_item *it,
                                       an_expr_node_ptr callee)
/*
After the classified operand is on the stack: convert an integer operand to
double.
*/
{
  an_expr_node_ptr  x = fp_operand(it, callee);
  uint32_t          xt = ecil_type_id(frame, x->type);
  uint32_t          ft = fp_operand_type(frame, x);

  if (xt != 0 && ft != 0 && xt != ft) emit_convert(frame, xt, ft);
}  /* emit_fp_operand_conversion */


static void setup_fp_call(an_ecil_frame *frame, a_code_item *it,
                          an_expr_node_ptr callee)
/*
Check the operands of a floating-point builtin call and prepare it: choose
the operand pushed (the last for fpclassify), and for the builtins that use
the operand more than once start a temporary (LOCAL, LREF; the value is
stored into it when it has been computed).
*/
{
  an_expr_node_ptr  arg;
  size_t            n = 0;

  for (arg = callee->next; arg != NULL; arg = arg->next) n++;
  if (it->b == ECIL_SPECIAL_FP_FPCLASSIFY) {
    if (n != 6) {
      ecil_note_unsupported("a malformed fpclassify call");
    } else {
      size_t  i;

      arg = callee->next;
      for (i = 0; i < 5; i++, arg = arg->next) {
        if (skip_parens(arg)->kind != (an_expr_node_kind)enk_constant) {
          ecil_note_unsupported("fpclassify with a non-constant result");
        }  /* if */
      }  /* for */
      it->cursor = arg;                  /* Only x is evaluated. */
    }  /* if */
  } else if (fp_is_comparison(it->b)) {
    if (n != 2) ecil_note_unsupported("a malformed comparison builtin call");
  } else if (n != 1) {
    ecil_note_unsupported("a malformed classification builtin call");
  }  /* if */
  if (ecil_unsupported_reason == NULL && !fp_is_comparison(it->b)) {
    uint32_t  xt = fp_operand_type(frame, fp_operand(it, callee));

    if (xt == 0) {
      ecil_note_unsupported("a classification of a non-arithmetic value");
    } else if (it->b == ECIL_SPECIAL_FP_ISINF_SIGN ||
               it->b == ECIL_SPECIAL_FP_FPCLASSIFY) {
      uint32_t  lid = new_lid();

      it->a = lid;
      emit_local(frame, lid, xt, 0, 0);
      ecil_rec1(frame, eop_lref, lid);
    }  /* if */
  }  /* if */
}  /* setup_fp_call */


static void emit_fp_test(an_ecil_frame *frame, uint32_t lid,
                         unsigned int mask, a_boolean signbit_test)
/*
Push the bool "the saved value is in the classes of mask" (or its sign bit).
*/
{
  uint32_t  bool_t = ecil_bool_type(frame);

  ecil_rec1(frame, eop_lref, lid);
  ecil_rec1(frame, eop_load, 0);
  if (signbit_test) {
    ecil_rec_begin(frame, eop_builtin);
    ecil_arg_uint(frame, (unsigned int)ebi_signbit);
    ecil_arg_uint(frame, 1);
    ecil_arg_uint(frame, bool_t);
    ecil_rec_end(frame);
  } else {
    ecil_rec2(frame, eop_const_int,
              ecil_target_int_type(frame), mask);
    ecil_rec_begin(frame, eop_builtin);
    ecil_arg_uint(frame, (unsigned int)ebi_is_fp_class);
    ecil_arg_uint(frame, 2);
    ecil_arg_uint(frame, bool_t);
    ecil_rec_end(frame);
  }  /* if */
}  /* emit_fp_test */


static void emit_tern_begin(an_ecil_frame *frame, uint32_t t)
/*
TERN_BEGIN with result type t.
*/
{
  ecil_rec_begin(frame, eop_tern_begin);
  ecil_arg_uint(frame, t);
  ecil_rec_end(frame);
}  /* emit_tern_begin */


static void emit_fp_call(an_ecil_frame *frame, a_code_item *it,
                         an_expr_node_ptr node, an_expr_node_ptr callee)
/*
All operands are on the stack: emit the floating-point builtin.  Bool
results are converted by finish_bool_result; isinf_sign and fpclassify
return an int built from a TERN chain over the saved operand.
*/
{
  uint32_t  bool_t = ecil_bool_type(frame);
  uint32_t  rt = ecil_type_id(frame, node->type);

  if (fp_is_comparison(it->b)) {
    uint32_t      tb = ecil_type_id(frame, callee->next->next->type);
    uint32_t      ta = ecil_type_id(frame, callee->next->type);
    uint32_t      tc = (ta != 0 && tb != 0) ? common_gid(frame, ta, tb) : 0;
    unsigned int  kind = (unsigned int)ecmp_lt;

    if (tb != 0 && tc != 0) emit_convert(frame, tb, tc);
    switch (it->b) {
      case ECIL_SPECIAL_FP_ISGREATER: kind = (unsigned int)ecmp_gt; break;
      case ECIL_SPECIAL_FP_ISLESSEQUAL: kind = (unsigned int)ecmp_le; break;
      case ECIL_SPECIAL_FP_ISGREATEREQUAL: kind = (unsigned int)ecmp_ge; break;
      case ECIL_SPECIAL_FP_ISLESSGREATER: kind = (unsigned int)ecmp_one; break;
      case ECIL_SPECIAL_FP_ISUNORDERED: kind = (unsigned int)ecmp_uno; break;
      default: break;
    }  /* switch */
    ecil_rec1(frame, eop_cmp, kind);
    finish_bool_result(frame, node, it->flags);
  } else if (it->b == ECIL_SPECIAL_FP_SIGNBIT) {
    emit_fp_operand_conversion(frame, it, callee);
    ecil_rec_begin(frame, eop_builtin);
    ecil_arg_uint(frame, (unsigned int)ebi_signbit);
    ecil_arg_uint(frame, 1);
    ecil_arg_uint(frame, bool_t);
    ecil_rec_end(frame);
    finish_bool_result(frame, node, it->flags);
  } else if (it->b == ECIL_SPECIAL_FP_ISINF_SIGN ||
             it->b == ECIL_SPECIAL_FP_FPCLASSIFY) {
    uint32_t          lid = (uint32_t)it->a;
    an_expr_node_ptr  arg = callee->next;
    unsigned int      i;

    emit_fp_operand_conversion(frame, it, callee);
    ecil_rec2(frame, eop_store, 0, 0);       /* The LOCAL <- operand. */
    if (it->b == ECIL_SPECIAL_FP_ISINF_SIGN) {
      emit_fp_test(frame, lid, fp_mask(ECIL_SPECIAL_FP_ISINF), FALSE);
      emit_tern_begin(frame, rt);
      emit_fp_test(frame, lid, 0, TRUE);
      emit_tern_begin(frame, rt);
      ecil_rec_begin(frame, eop_const_int);
      ecil_arg_uint(frame, rt);
      ecil_arg_int(frame, -1);
      ecil_rec_end(frame);
      ecil_rec0(frame, eop_tern_else);
      ecil_rec2(frame, eop_const_int, rt, 1);
      ecil_rec0(frame, eop_tern_end);
      ecil_rec0(frame, eop_tern_else);
      ecil_rec2(frame, eop_const_int, rt, 0);
      ecil_rec0(frame, eop_tern_end);
    } else {
      static const unsigned int masks[4] = {
        ECIL_FP_CLASS_SNAN | ECIL_FP_CLASS_QNAN,
        ECIL_FP_CLASS_NEG_INF | ECIL_FP_CLASS_POS_INF,
        ECIL_FP_CLASS_NEG_NORMAL | ECIL_FP_CLASS_POS_NORMAL,
        ECIL_FP_CLASS_NEG_SUBNORMAL | ECIL_FP_CLASS_POS_SUBNORMAL
      };

      for (i = 0; i < 4; i++, arg = arg->next) {
        emit_fp_test(frame, lid, masks[i], FALSE);
        emit_tern_begin(frame, rt);
        emit_constant_node(frame, skip_parens(arg));
        ecil_rec0(frame, eop_tern_else);
      }  /* for */
      emit_constant_node(frame, skip_parens(arg));
      for (i = 0; i < 4; i++) ecil_rec0(frame, eop_tern_end);
    }  /* if */
  } else {
    emit_fp_operand_conversion(frame, it, callee);
    ecil_rec2(frame, eop_const_int,
              ecil_target_int_type(frame), fp_mask(it->b));
    ecil_rec_begin(frame, eop_builtin);
    ecil_arg_uint(frame, (unsigned int)ebi_is_fp_class);
    ecil_arg_uint(frame, 2);
    ecil_arg_uint(frame, bool_t);
    ecil_rec_end(frame);
    finish_bool_result(frame, node, it->flags);
  }  /* if */
}  /* emit_fp_call */


/* __builtin_clzg and __builtin_ctzg. */

static void setup_clzg_call(an_ecil_frame *frame, a_code_item *it,
                            an_expr_node_ptr callee)
/*
Check the operands: an integer and an optional constant result for zero.
With the second operand the first is saved in a temporary (LOCAL, LREF; the
value is stored when it has been computed) because it is used twice.
*/
{
  an_expr_node_ptr  x = callee->next;
  size_t            n = 0;
  an_expr_node_ptr  arg;
  uint32_t          xt;

  for (arg = x; arg != NULL; arg = arg->next) n++;
  xt = (x != NULL) ? ecil_type_id(frame, x->type) : 0;
  if ((n != 1 && n != 2) || xt == 0 || info_of(xt)->kind != egk_int) {
    ecil_note_unsupported("a malformed or non-integer clzg/ctzg call");
  } else if (n == 2) {
    if (skip_parens(x->next)->kind != (an_expr_node_kind)enk_constant) {
      ecil_note_unsupported("clzg/ctzg with a non-constant result for zero");
    } else {
      uint32_t  lid = new_lid();

      it->a = lid;
      emit_local(frame, lid, xt, 0, 0);
      ecil_rec1(frame, eop_lref, lid);
    }  /* if */
  }  /* if */
}  /* setup_clzg_call */


static void emit_count_builtin(an_ecil_frame *frame, unsigned int id,
                               uint32_t rt)
/*
BUILTIN clz or ctz of the value on top of the stack, with result type rt.
*/
{
  ecil_rec_begin(frame, eop_builtin);
  ecil_arg_uint(frame, id);
  ecil_arg_uint(frame, 1);
  ecil_arg_uint(frame, rt);
  ecil_rec_end(frame);
}  /* emit_count_builtin */


static void emit_clzg_call(an_ecil_frame *frame, a_code_item *it,
                           an_expr_node_ptr node, an_expr_node_ptr callee)
/*
The operand is on the stack.  Without a result for zero this is the plain
count builtin; with one it is x == 0 ? result : count(x) over the temporary.
*/
{
  uint32_t      rt = ecil_type_id(frame, node->type);
  unsigned int  id = (it->b == ECIL_SPECIAL_CLZG) ? (unsigned int)ebi_clz :
                                               (unsigned int)ebi_ctz;

  if (it->a == 0) {
    emit_count_builtin(frame, id, rt);
  } else {
    uint32_t  xt = ecil_type_id(frame, callee->next->type);

    ecil_rec2(frame, eop_store, 0, 0);       /* The LOCAL <- operand. */
    ecil_rec1(frame, eop_lref, it->a);
    ecil_rec1(frame, eop_load, 0);
    ecil_rec2(frame, eop_const_int, xt, 0);
    ecil_rec1(frame, eop_cmp, (unsigned int)ecmp_eq);
    emit_tern_begin(frame, rt);
    emit_constant_node(frame, skip_parens(callee->next->next));
    ecil_rec0(frame, eop_tern_else);
    ecil_rec1(frame, eop_lref, it->a);
    ecil_rec1(frame, eop_load, 0);
    emit_count_builtin(frame, id, rt);
    ecil_rec0(frame, eop_tern_end);
  }  /* if */
}  /* emit_clzg_call */


static void do_call(an_ecil_frame *frame, size_t idx)
/*
eok_call: callee (unless direct), arguments in order, then CALL.
*/
{
  a_code_item       *it = &code_stack[idx];
  an_expr_node_ptr  node = (an_expr_node_ptr)it->entry;
  an_expr_node_ptr  callee = skip_parens(node->variant.operation.operands);
  a_boolean         done = FALSE;

  if (it->phase == 0) {
    a_routine_ptr direct = NULL;
    an_expr_node_ptr c = callee;

    /* Look through (*f), (&f) and parentheses to a named routine. */
    while (c != NULL &&
           c->kind == (an_expr_node_kind)enk_operation &&
           (c->variant.operation.kind == eok_indirect ||
            c->variant.operation.kind == eok_address_of ||
            c->variant.operation.kind == eok_parens)) {
      c = skip_parens(c->variant.operation.operands);
    }  /* while */
    if (c != NULL && c->kind == (an_expr_node_kind)enk_routine &&
        node_routine(c) != NULL) {
      direct = node_routine(c);
    }  /* if */
    if (direct != NULL && direct->source_corresp.name != NULL &&
        (strcmp(direct->source_corresp.name, "__builtin_object_size") == 0 ||
         strcmp(direct->source_corresp.name,
                "__builtin_dynamic_object_size") == 0) &&
        callee->next != NULL && callee->next->next != NULL &&
        callee->next->next->kind == (an_expr_node_kind)enk_constant) {
      /* The object size is unknown to the code generator: GCC's answer is
         (size_t)-1 (0 for the minimum-size kinds 2 and 3). */
      a_boolean   ovflo = FALSE;
      long        kind = (long)value_of_integer_constant(
                              node_constant(callee->next->next), &ovflo);
      uint32_t    t = ecil_type_id(frame, node->type);

      if (t != 0) {
        ecil_rec2(frame, eop_const_int, t,
                  (kind & 2) != 0 ? (uint64_t)0 : ~(uint64_t)0);
        finish_value(frame, it->flags);
      }  /* if */
      code_stack_size--;
      done = TRUE;
    } else if (direct != NULL) {
      const char          *name = direct->source_corresp.name;
      an_ecil_name_class  cls;

      if (classify_routine(direct, &cls)) {
        note_unsupported_builtin(name);
      } else if (cls.special_id != 0) {
        it->b = cls.special_id;
      }  /* if */
      if (ecil_unsupported_reason == NULL && it->b == 0) {
        it->a = ecil_function_id(frame, direct);
      }  /* if */
      it->cursor = callee->next;
      if (ecil_unsupported_reason == NULL && it->b >= ECIL_SPECIAL_FP_FIRST &&
          it->b <= ECIL_SPECIAL_FP_LAST) {
        setup_fp_call(frame, it, callee);
      } else if (ecil_unsupported_reason == NULL &&
                 (it->b == ECIL_SPECIAL_CLZG || it->b == ECIL_SPECIAL_CTZG)) {
        setup_clzg_call(frame, it, callee);
      }  /* if */
    } else {
      it->cursor = callee->next;
    }  /* if */
    if (!done) it->phase = 1;
    if (!done && ecil_unsupported_reason == NULL && direct == NULL) {
      push_expr(callee, 0);
      done = TRUE;
    }  /* if */
  }  /* if */
  if (done) {
    /* The item was completed, or the callee operand was pushed. */
  } else if (ecil_unsupported_reason == NULL && it->cursor != NULL) {
    an_expr_node_ptr arg = (an_expr_node_ptr)it->cursor;

    /* __builtin_c23_va_start(ap, ...): only ap is used. */
    it->cursor = arg->next;
    if (it->b == ECIL_SPECIAL_C23_VA_START ||
        it->b == ECIL_SPECIAL_ALLOCA_ALIGN ||
        it->b == ECIL_SPECIAL_CLZG || it->b == ECIL_SPECIAL_CTZG) {
      it->cursor = NULL;               /* Only the first operand is used. */
    } else if (it->b >= ECIL_SPECIAL_ADD_OVERFLOW_P &&
               it->b <= ECIL_SPECIAL_MUL_OVERFLOW_P && arg->next != NULL &&
               arg->next->next == NULL) {
      it->cursor = NULL;               /* The last (type-only) operand. */
    }  /* if */
    if (fp_is_comparison(it->b) && arg == callee->next->next) {
      /* The first operand is complete: convert it to the common type. */
      uint32_t  ta = ecil_type_id(frame, callee->next->type);
      uint32_t  tb = ecil_type_id(frame, arg->type);
      uint32_t  tc = (ta != 0 && tb != 0) ? common_gid(frame, ta, tb) : 0;

      if (tc != 0) emit_convert(frame, ta, tc);
    }  /* if */
    push_expr(arg, 0);
  } else if (ecil_unsupported_reason == NULL) {
    /* All operands are on the stack: emit the call. */
    an_expr_node_ptr  arg;
    uint64_t          nargs = 0;
    a_type_ptr        fn_type;
    uint32_t          t_call;
    uint32_t          result_gid;
    unsigned int      flags = 0;

    for (arg = callee->next; arg != NULL; arg = arg->next) {
      uint32_t  at = ecil_type_id(frame, arg->type);

      nargs++;
      if (at != 0 && (info_of(at)->kind == egk_opaque ||
                      (info_of(at)->kind == egk_record &&
                       (info_of(at)->flags & ECIL_GF_ABI_OPAQUE)))) {
        ecil_note_unsupported("a call with an argument of unsupported type");
      }  /* if */
    }  /* for */
    result_gid = ecil_type_id(frame, node->type);
    if (result_gid != 0 && (info_of(result_gid)->kind == egk_opaque ||
                            (info_of(result_gid)->kind == egk_record &&
                             (info_of(result_gid)->flags &
                                                    ECIL_GF_ABI_OPAQUE)))) {
      ecil_note_unsupported("a call returning an unsupported type");
    }  /* if */
    fn_type = skip_typerefs(callee->type);
    if (fn_type->kind == tk_pointer) {
      fn_type = skip_typerefs(fn_type->variant.pointer.type);
    }  /* if */
    if (fn_type->kind != tk_routine) {
      ecil_note_unsupported("a call through a non-function expression");
    } else {
      t_call = ecil_func_type_id(frame, fn_type, FALSE);
      if (rout_type_supp(fn_type)->has_ellipsis) {
        flags |= ECIL_CALL_VARIADIC_CALLEE;
      }  /* if */
      if (rout_type_supp(fn_type)->does_not_return) {
        flags |= ECIL_CALL_NORETURN;
      }  /* if */
      if (node->result_is_not_used) flags |= ECIL_CALL_RESULT_UNUSED;
      if (ecil_unsupported_reason == NULL && t_call != 0) {
        it = &code_stack[idx];
        if (it->b >= ECIL_SPECIAL_FP_FIRST && it->b <= ECIL_SPECIAL_FP_LAST) {
          emit_fp_call(frame, it, node, callee);
        } else if (it->b == ECIL_SPECIAL_CLZG || it->b == ECIL_SPECIAL_CTZG) {
          emit_clzg_call(frame, it, node, callee);
        } else if (it->b == ECIL_SPECIAL_C23_VA_START) {
          uint32_t  ap_t = ecil_type_id(frame, callee->next->type);

          if (ap_t != 0 && info_of(ap_t)->kind == egk_array) {
            ecil_rec0(frame, eop_addr);
            emit_cast(frame, (unsigned int)eck_array_to_ptrdecay,
                      ecil_ptr_type(frame, info_of(ap_t)->ref));
          }  /* if */
          ecil_rec0(frame, eop_va_start);
          if (!is_void_node_type(node) && result_gid != 0) {
            /* The call expression has a value (an implicitly declared
               int function): VA_START produced none, so supply one. */
            ecil_rec2(frame, eop_const_int, result_gid, 0);
          }  /* if */
        } else if (it->b >= ECIL_SPECIAL_ADD_OVERFLOW_P &&
                   it->b <= ECIL_SPECIAL_MUL_OVERFLOW_P) {
          uint32_t  tt = ecil_type_id(frame,
                                  callee->next->next->next->type);
          uint32_t  lid = new_lid();

          if (tt != 0) {
            emit_local(frame, lid, tt, 0, 0);
            ecil_rec1(frame, eop_lref, lid);
            ecil_rec0(frame, eop_addr);
            ecil_rec_begin(frame, eop_builtin);
            ecil_arg_uint(frame, (uint64_t)ebi_add_overflow +
                                 (it->b - ECIL_SPECIAL_ADD_OVERFLOW_P));
            ecil_arg_uint(frame, 3);
            ecil_arg_uint(frame, result_gid);
            ecil_rec_end(frame);
          }  /* if */
        } else if (it->b != 0) {
          ecil_rec_begin(frame, eop_builtin);
          ecil_arg_uint(frame, it->b == ECIL_SPECIAL_ALLOCA_ALIGN ?
                               (unsigned int)ebi_alloca : it->b);
          ecil_arg_uint(frame, it->b == ECIL_SPECIAL_ALLOCA_ALIGN ? 1u :
                               nargs);
          if (is_void_node_type(node)) {
            ecil_arg_nil(frame);
          } else {
            ecil_arg_uint(frame, result_gid);
          }  /* if */
          ecil_rec_end(frame);
        } else if (it->a != 0) {
          ecil_rec_begin(frame, eop_call);
          ecil_arg_uint(frame, it->a);
          ecil_arg_uint(frame, t_call);
          ecil_arg_uint(frame, nargs);
          ecil_arg_uint(frame, flags);
          ecil_rec_end(frame);
        } else {
          ecil_rec_begin(frame, eop_call_indirect);
          ecil_arg_uint(frame, t_call);
          ecil_arg_uint(frame, nargs);
          ecil_arg_uint(frame, flags);
          ecil_rec_end(frame);
        }  /* if */
        /* The bool-valued floating-point builtins finished their result. */
        if (it->b < ECIL_SPECIAL_FP_FIRST || it->b > ECIL_SPECIAL_FP_LAST ||
            it->b == ECIL_SPECIAL_FP_ISINF_SIGN ||
            it->b == ECIL_SPECIAL_FP_FPCLASSIFY) {
          finish_value(frame, it->flags);
        }  /* if */
      }  /* if */
    }  /* if */
    code_stack_size--;
  }  /* if */
}  /* do_call */


static uint32_t bit_field_promoted_gid(an_ecil_frame *frame,
                                       an_expr_node_ptr lhs,
                                       uint32_t         tl)
/*
The type of a compound assignment's left operand after the integer
promotions: a bit-field narrower than int promotes to int whatever its
declared type (C11 6.3.1.1p2), so "unsigned b:3; b /= -2" divides signed.
*/
{
  uint32_t  result = tl;

  if (lhs->kind == (an_expr_node_kind)enk_operation &&
      (lhs->variant.operation.kind == eok_dot_field ||
       lhs->variant.operation.kind == eok_points_to_field) &&
      lhs->variant.operation.operands != NULL &&
      lhs->variant.operation.operands->next != NULL &&
      lhs->variant.operation.operands->next->kind ==
                                        (an_expr_node_kind)enk_field) {
    a_field_ptr  field =
        node_field(lhs->variant.operation.operands->next);

    if (field->is_bit_field && field->bit_size < ecil_int_bits()) {
      result = ecil_target_int_type(frame);
    }  /* if */
  }  /* if */
  return result;
}  /* bit_field_promoted_gid */


static void do_compound_assignment(an_ecil_frame *frame, size_t idx)
/*
Compound assignment is expanded here so that cast classification stays in
one place:  <lhs glv> DUP LOAD cast <rhs> cast <op> cast STORE.
*/
{
  a_code_item       *it = &code_stack[idx];
  an_expr_node_ptr  node = (an_expr_node_ptr)it->entry;
  an_expr_node_ptr  lhs = node->variant.operation.operands;
  an_expr_node_ptr  rhs = lhs->next;
  an_expr_operator_kind
                    op = node->variant.operation.kind;
  uint32_t          tl = ecil_type_id(frame, lhs->type);
  uint32_t          tr = ecil_type_id(frame, rhs->type);

  if (ecil_unsupported_reason != NULL || tl == 0 || tr == 0) {
    code_stack_size--;
  } else if (it->phase == 0) {
    it->phase = 1;
    push_expr(lhs, 0);
  } else if (it->phase == 1) {
    uint32_t  comp = 0;

    ecil_rec0(frame, eop_dup);
    ecil_rec_begin(frame, eop_load);
    ecil_arg_uint(frame, access_is_volatile(lhs) ? ECIL_ACCESS_VOLATILE : 0u);
    ecil_rec_end(frame);
    if (op == eok_padd_assign || op == eok_psubtract_assign) {
      comp = tl;
    } else if (op == eok_shiftl_assign || op == eok_shiftr_assign) {
      comp = ecil_promote_gid(frame, bit_field_promoted_gid(frame, lhs, tl),
                              NULL);
    } else {
      comp = common_gid(frame, bit_field_promoted_gid(frame, lhs, tl), tr);
    }  /* if */
    if (op != eok_padd_assign && op != eok_psubtract_assign) {
      emit_convert(frame, tl, comp);
    }  /* if */
    it = &code_stack[idx];
    it->a = comp;
    it->phase = 2;
    push_expr(rhs, 0);
  } else {
    uint32_t      comp = it->a;
    unsigned int  opcode = 0;

    if (op == eok_padd_assign || op == eok_psubtract_assign) {
      emit_ptr_stride(frame, rhs, op == eok_psubtract_assign);
    } else if (operator_opcode(op, &opcode)) {
      emit_convert(frame, tr, comp);
      ecil_rec1(frame, opcode, opcode == eop_shl || opcode == eop_shr ||
                               opcode == eop_bit_and || opcode == eop_bit_or ||
                               opcode == eop_bit_xor ? 0u : arith_flags(comp));
      emit_convert(frame, comp, tl);
    } else {
      ecil_note_unsupported("this compound assignment");
    }  /* if */
    if (ecil_unsupported_reason == NULL) {
      ecil_rec_begin(frame, eop_store);
      ecil_arg_uint(frame, access_is_volatile(lhs) ? ECIL_ACCESS_VOLATILE
                                                   : 0u);
      ecil_arg_uint(frame, node->result_is_not_used ? (unsigned int)esr_none :
                                                     (unsigned int)esr_prv);
      ecil_rec_end(frame);
      finish_value(frame, code_stack[idx].flags);
    }  /* if */
    code_stack_size--;
  }  /* if */
}  /* do_compound_assignment */


static void do_operation(an_ecil_frame *frame, size_t idx)
/*
An enk_operation node.  Most operators are post-order: push the operands
(last first, so the first runs first), then emit the operator on the
revisit.
*/
{
  a_code_item       *it = &code_stack[idx];
  an_expr_node_ptr  node = (an_expr_node_ptr)it->entry;
  an_expr_operator_kind
                    op = node->variant.operation.kind;
  an_expr_node_ptr  op1 = node->variant.operation.operands;
  an_expr_node_ptr  op2 = op1 != NULL ? op1->next : NULL;
  unsigned int      flags = it->flags;
  unsigned int      opcode = 0;
  unsigned int      cmp = 0;
  uint32_t          t = 0;

  if (op == eok_call) {
    do_call(frame, idx);
  } else if (is_assignment_operator(op) && op != eok_assign) {
    do_compound_assignment(frame, idx);
  } else if (it->phase == 0) {
    /* Phase 0: push the continuation and the operands. */
    it->phase = 1;
    if (op == eok_question &&
        node->variant.operation.is_gnu_two_operand_question_mark) {
      ecil_note_unsupported("the GNU ?: with an omitted operand");
    }  /* if */
    switch (op) {
      case eok_land:
      case eok_lor:
        /* lhs cond, BEGIN, rhs cond, END. */
        push_expr(op1, CIF_COND);
        break;
      case eok_question:
        push_expr(op1, CIF_COND);
        break;
      case eok_comma:
        push_expr(op1, 0);
        break;
      case eok_not:
        push_expr(op1, CIF_COND);
        break;
      case eok_dot_field:
      case eok_points_to_field:
      case eok_array_to_pointer:
      case eok_address_of:
      case eok_indirect:
      case eok_pre_incr:
      case eok_pre_decr:
      case eok_post_incr:
      case eok_post_decr:
      case eok_unary_plus:
      case eok_negate:
      case eok_complement:
      case eok_cast:
      case eok_bool_cast:
      case eok_lvalue_adjust:
        push_expr(op1, 0);
        break;
      case eok_padd:
      case eok_psubtract:
      case eok_subscript:
        { an_expr_node_ptr ptr_op;
          an_expr_node_ptr int_op;

          if (pointer_operand_first(op1, op2, &ptr_op, &int_op, frame)) {
            it = &code_stack[idx];
            it->cursor = int_op;
            push_expr(int_op, 0);
            push_expr(ptr_op, 0);
          }  /* if */
        }
        break;
      case eok_assign:
        push_expr(op2, 0);
        push_expr(op1, 0);
        break;
      case eok_va_start:
      case eok_va_end:
      case eok_va_arg:
      case eok_va_copy:
        push_expr(op1, 0);
        break;
      default:
        if (operator_opcode(op, &opcode) || comparison_kind(op, &cmp) ||
            op == eok_pdiff) {
          push_expr(op2, 0);
          push_expr(op1, 0);
        } else {
          note_unsupported_operator(frame, op, node);
        }  /* if */
        break;
    }  /* switch */
  } else {
    /* Phase 1 or more: the operands are on the stack. */
    a_boolean done = TRUE;

    switch (op) {
      case eok_land:
      case eok_lor:
        if (it->phase == 1) {
          ecil_rec0(frame, op == eok_land ? eop_land_begin : eop_lor_begin);
          it->phase = 2;
          push_expr(op2, CIF_COND);
          done = FALSE;
        } else {
          ecil_rec0(frame, op == eok_land ? eop_land_end : eop_lor_end);
          finish_bool_result(frame, node, flags);
        }  /* if */
        break;
      case eok_question:
        if (it->phase == 1) {
          t = is_void_node_type(node) ? 0 : ecil_type_id(frame, node->type);
          if (ecil_unsupported_reason == NULL) {
            ecil_rec_begin(frame, eop_tern_begin);
            if (t == 0) {
              ecil_arg_nil(frame);
            } else {
              ecil_arg_uint(frame, t);
            }  /* if */
            ecil_rec_end(frame);
            it = &code_stack[idx];
            it->phase = 2;
            push_expr(op2, 0);
          }  /* if */
          done = FALSE;
        } else if (it->phase == 2) {
          ecil_rec0(frame, eop_tern_else);
          it->phase = 3;
          push_expr(op2->next, 0);
          done = FALSE;
        } else {
          ecil_rec0(frame, eop_tern_end);
          finish_value(frame, flags);
        }  /* if */
        break;
      case eok_comma:
        if (it->phase == 1) {
          if (node_pushes_value(op1)) ecil_rec0(frame, eop_drop);
          it->phase = 2;
          push_expr(op2, 0);
          done = FALSE;
        } else {
          finish_value(frame, flags);
        }  /* if */
        break;
      case eok_not:
        ecil_rec0(frame, eop_log_not);
        finish_bool_result(frame, node, flags);
        break;
      case eok_unary_plus:
        finish_value(frame, flags);
        break;
      case eok_negate:
        t = ecil_type_id(frame, node->type);
        if (t != 0) ecil_rec1(frame, eop_neg, arith_flags(t));
        finish_value(frame, flags);
        break;
      case eok_complement:
        ecil_rec0(frame, eop_bit_not);
        finish_value(frame, flags);
        break;
      case eok_address_of:
        { an_expr_node_ptr target = skip_parens(op1);
          a_boolean        is_function =
              target->kind == (an_expr_node_kind)enk_routine;

          if (!is_function) ecil_rec0(frame, eop_addr);
        }
        finish_value(frame, flags);
        break;
      case eok_indirect:
        { uint32_t pt = ecil_type_id(frame, op1->type);
          a_boolean to_function = FALSE;

          if (pt != 0 && info_of(pt)->kind == egk_ptr) {
            to_function = info_of(info_of(pt)->ref)->kind == egk_func_type;
          }  /* if */
          if (!to_function) {
            ecil_rec0(frame, eop_deref);
            emit_load_if_value(frame, node);
          }  /* if */
        }
        finish_value(frame, flags);
        break;
      case eok_subscript:
        emit_ptr_stride(frame, (an_expr_node_ptr)it->cursor, FALSE);
        ecil_rec0(frame, eop_deref);
        emit_load_if_value(frame, node);
        finish_value(frame, flags);
        break;
      case eok_padd:
      case eok_psubtract:
        emit_ptr_stride(frame, (an_expr_node_ptr)it->cursor,
                        op == eok_psubtract);
        finish_value(frame, flags);
        break;
      case eok_pdiff:
        t = ecil_type_id(frame, node->type);
        if (t != 0) ecil_rec1(frame, eop_ptr_diff, t);
        finish_value(frame, flags);
        break;
      case eok_array_to_pointer:
        { a_boolean         operand_is_glvalue =
                                      is_glvalue_node(skip_parens(op1));
          uint32_t          array_gid = ecil_type_id(frame, op1->type);
          uint32_t          ptr_gid = ecil_type_id(frame, node->type);

          if (ecil_unsupported_reason == NULL) {
            if (!operand_is_glvalue) {
              uint32_t  temp = new_lid();

              ecil_rec_begin(frame, eop_materialize_temp);
              ecil_arg_uint(frame, temp);
              ecil_arg_uint(frame, array_gid);
              ecil_rec_end(frame);
            }  /* if */
            ecil_rec0(frame, eop_addr);
            emit_cast(frame, (unsigned int)eck_array_to_ptrdecay, ptr_gid);
          }  /* if */
        }
        finish_value(frame, flags);
        break;
      case eok_dot_field:
      case eok_points_to_field:
        { an_expr_node_ptr  base = skip_parens(op1);
          an_expr_node_ptr  field_node = op2;
          a_type_ptr        rt = skip_typerefs(base->type);
          uint32_t          rec = 0;
          uint32_t          index = 0;

          if (op == eok_points_to_field) {
            rt = skip_typerefs(rt->variant.pointer.type);
          }  /* if */
          if (field_index(frame, rt, node_field(field_node), &rec, &index)) {
            if (op == eok_points_to_field) {
              ecil_rec0(frame, eop_deref);
              ecil_rec2(frame, eop_field, rec, index);
              emit_load_if_value(frame, node);
            } else if (is_glvalue_node(base)) {
              ecil_rec2(frame, eop_field, rec, index);
              emit_load_if_value(frame, node);
            } else {
              ecil_rec2(frame, eop_extract, rec, index);
            }  /* if */
          }  /* if */
        }
        finish_value(frame, flags);
        break;
      case eok_cast:
      case eok_bool_cast:
      case eok_lvalue_adjust:
        { uint32_t  dst = ecil_type_id(frame, node->type);
          uint32_t  src = ecil_type_id(frame, op1->type);

          if (is_void_node_type(node)) {
            if (node_pushes_value(op1)) ecil_rec0(frame, eop_drop);
          } else if (dst != 0 && src != 0) {
            if (op == eok_bool_cast) {
              if (info_of(src)->kind != egk_bool) {
                ecil_rec0(frame, eop_to_bool);
              }  /* if */
            } else {
              emit_convert(frame, src, dst);
            }  /* if */
            finish_value(frame, flags);
          }  /* if */
        }
        break;
      case eok_assign:
        ecil_rec_begin(frame, eop_store);
        ecil_arg_uint(frame, access_is_volatile(op1) ? ECIL_ACCESS_VOLATILE
                                                     : 0u);
        ecil_arg_uint(frame, node->result_is_not_used ?
                             (unsigned int)esr_none : (unsigned int)esr_prv);
        ecil_rec_end(frame);
        finish_value(frame, flags);
        break;
      case eok_pre_incr:
      case eok_pre_decr:
      case eok_post_incr:
      case eok_post_decr:
        { uint32_t        ot = ecil_type_id(frame, op1->type);
          unsigned int    kind;
          unsigned int    iflags = 0;

          kind = (op == eok_pre_incr) ? (unsigned int)eidk_pre_inc :
                 (op == eok_pre_decr) ? (unsigned int)eidk_pre_dec :
                 (op == eok_post_incr) ? (unsigned int)eidk_post_inc :
                                         (unsigned int)eidk_post_dec;
          if (ot != 0 && info_of(ot)->kind == egk_bool) {
            ecil_note_unsupported("++/-- on _Bool");
          } else if (ot != 0) {
            if (arith_flags(ot) != 0) iflags |= ECIL_INC_DEC_NSW;
            if (access_is_volatile(op1)) iflags |= ECIL_INC_DEC_VOLATILE;
            ecil_rec_begin(frame, eop_inc_dec);
            ecil_arg_uint(frame, kind);
            ecil_arg_uint(frame, iflags);
            ecil_rec_end(frame);
            finish_value(frame, flags);
          }  /* if */
        }
        break;
      case eok_va_start:
      case eok_va_end:
      case eok_va_arg:
      case eok_va_copy:
        /* The va_list operand arrives as an array glvalue (decay it) or as
           a pointer rvalue (a va_list parameter). */
        if (it->phase == 1 || (it->phase == 2 && op == eok_va_copy)) {
          an_expr_node_ptr  ap = (it->phase == 1) ? op1 : op2;
          uint32_t          ap_t = ecil_type_id(frame, ap->type);

          if (ecil_unsupported_reason == NULL && ap_t != 0 &&
              info_of(ap_t)->kind == egk_array) {
            ecil_rec0(frame, eop_addr);
            emit_cast(frame, (unsigned int)eck_array_to_ptrdecay,
                      ecil_ptr_type(frame, info_of(ap_t)->ref));
          } else if (ecil_unsupported_reason == NULL && ap_t != 0 &&
                     info_of(ap_t)->kind == egk_record) {
            ecil_rec0(frame, eop_addr);     /* A va_list that is a struct. */
          }  /* if */
        }  /* if */
        if (op == eok_va_copy && it->phase == 1) {
          it->phase = 2;
          push_expr(op2, 0);
          done = FALSE;
        } else if (op == eok_va_copy) {
          ecil_rec0(frame, eop_va_copy);
        } else if (op == eok_va_arg) {
          uint32_t  rt = ecil_type_id(frame, node->type);

          if (rt != 0 && info_of(rt)->kind == egk_record &&
              (info_of(rt)->flags & ECIL_GF_ABI_OPAQUE)) {
            ecil_note_unsupported("va_arg of an unsupported type");
          } else if (rt != 0) {
            ecil_rec1(frame, eop_va_arg, rt);
            finish_value(frame, flags);
          }  /* if */
        } else {
          ecil_rec0(frame, op == eok_va_start ? eop_va_start : eop_va_end);
        }  /* if */
        break;
      default:
        if (comparison_kind(op, &cmp)) {
          ecil_rec1(frame, eop_cmp, cmp);
          finish_bool_result(frame, node, flags);
        } else if (operator_opcode(op, &opcode)) {
          t = ecil_type_id(frame, node->type);
          if (opcode == eop_shl || opcode == eop_shr) {
            uint32_t  tr = ecil_type_id(frame, op2->type);

            if (t != 0 && tr != 0) emit_convert(frame, tr, t);
            ecil_rec1(frame, opcode, 0);
          } else if (opcode == eop_bit_and || opcode == eop_bit_or ||
                     opcode == eop_bit_xor) {
            ecil_rec1(frame, opcode, 0);
          } else {
            ecil_rec1(frame, opcode, t != 0 ? arith_flags(t) : 0u);
          }  /* if */
          finish_value(frame, flags);
        } else {
          note_unsupported_operator(frame, op, node);
        }  /* if */
        break;
    }  /* switch */
    if (done) code_stack_size--;
  }  /* if */
}  /* do_operation */


static void do_expr(an_ecil_frame *frame, size_t idx)
/*
Visit an expression item.
*/
{
  a_code_item       *it = &code_stack[idx];
  an_expr_node_ptr  node = skip_parens((an_expr_node_ptr)it->entry);

  it->entry = node;
  switch (node->kind) {
    case enk_variable:
      emit_variable_node(frame, node);
      emit_load_if_value(frame, node);
      finish_value(frame, it->flags);
      code_stack_size--;
      break;
    case enk_routine:
      if (node_routine(node) == NULL) {
        ecil_note_unsupported("a routine reference without a routine");
      } else {
        const char          *name = node_routine(node)->source_corresp.name;
        uint32_t            gid;
        an_ecil_name_class  cls;

        if (classify_routine(node_routine(node), &cls)) {
          note_unsupported_builtin(name);
        } else {
          gid = ecil_function_id(frame, node_routine(node));
          if (gid != 0) ecil_rec1(frame, eop_gref, gid);
        }  /* if */
      }  /* if */
      code_stack_size--;
      break;
    case enk_constant:
      emit_constant_node(frame, node);
      if (ecil_unsupported_reason == NULL && !is_glvalue_node(node)) {
        finish_value(frame, it->flags);
      }  /* if */
      code_stack_size--;
      break;
    case enk_operation:
      if (!is_glvalue_node(node) && value_is_opaque_scalar(frame, node) &&
          node->variant.operation.kind != eok_parens) {
        ecil_note_unsupported_type(ecil_type_id(frame, node->type),
                                   "an operation on an unsupported scalar "
                                   "type");
        code_stack_size--;
      } else {
        do_operation(frame, idx);
      }  /* if */
      break;
    case enk_c11_generic:
      it->entry = node->variant.c11_generic.result;
      break;
    case enk_temp_init:
      if (it->phase == 0) {
        uint32_t  t = ecil_type_id(frame, node->type);
        uint32_t  lid;

        if (t != 0) {
          lid = new_lid();
          emit_local(frame, lid, t, 0, 0);
          it = &code_stack[idx];
          it->a = lid;
          it->b = t;
          it->phase = 1;
          { a_code_item *init = push_item(cik_init,
                                   node->variant.init.dynamic_init, 0);

            if (init != NULL) {
              init->a = lid;
              init->b = t;
            }  /* if */
          }
        }  /* if */
      } else {
        ecil_rec1(frame, eop_lref, it->a);
        emit_load_if_value(frame, node);
        finish_value(frame, it->flags);
        code_stack_size--;
      }  /* if */
      break;
    case enk_builtin_operation:
      /* __builtin_offsetof with a run-time subscript: the address of the
         designated member of a null object, as an integer.  (Constant
         offsets are folded by the front end.) */
      if (node->variant.builtin_operation.kind == (a_builtin_operation_kind)
                                                              bok_offsetof &&
          node->variant.builtin_operation.operands != NULL &&
          node->variant.builtin_operation.operands->next != NULL) {
        an_expr_node_ptr  member =
                    node->variant.builtin_operation.operands->next;

        if (it->phase == 0) {
          it->phase = 1;
          push_expr(member, 0);
        } else {
          uint32_t  mt = ecil_type_id(frame, member->type);
          uint32_t  rt = ecil_type_id(frame, node->type);

          if (mt != 0 && rt != 0) {
            uint32_t  pt = ecil_ptr_type(frame, mt);

            ecil_rec0(frame, eop_addr);
            if (pt != 0) emit_convert(frame, pt, rt);
            finish_value(frame, it->flags);
          }  /* if */
          code_stack_size--;
        }  /* if */
      } else {
        note_unsupported_kind("builtin operation",
                              (int)node->variant.builtin_operation.kind);
        code_stack_size--;
      }  /* if */
      break;
    case enk_builtin_choose_expr:
      { an_expr_node_ptr ops = node->variant.builtin_choose_expr.operands;

        it->entry = node->variant.builtin_choose_expr.choose_first ?
                    ops->next : ops->next->next;
      }
      break;
    case enk_statement:
      ecil_note_unsupported("a GNU statement expression");
      break;
    default:
      note_unsupported_kind("expression kind", (int)node->kind);
      break;
  }  /* switch */
}  /* do_expr */


/* Initialization of locals. */

static a_boolean constant_has_dynamic_part(a_constant_ptr c)
/*
Return TRUE if the aggregate constant contains a ck_dynamic_init entry.
*/
{
  return c->kind == (a_constant_repr_kind)ck_aggregate &&
         c->variant.aggregate.has_dynamic_init_component;
}  /* constant_has_dynamic_part */


static a_boolean merged_has_dynamic_part(a_constant_ptr *sources, size_t n)
/*
Return TRUE if any of the merged aggregate constants has a dynamic part.
*/
{
  size_t    i;
  a_boolean found = FALSE;

  for (i = 0; i < n && !found; i++) {
    found = constant_has_dynamic_part(sources[i]);
  }  /* for */
  return found;
}  /* merged_has_dynamic_part */


static void reset_agg_levels(void)
/*
Discard the dynamic aggregate cursors.
*/
{
  while (num_agg_levels > 0) {
    ecil_init_level_end(&agg_levels[--num_agg_levels]);
  }  /* while */
}  /* reset_agg_levels */


static a_boolean push_agg_level(a_constant_ptr *sources, size_t n)
/*
Push a cursor for a dynamic aggregate initialized by the given constants;
FALSE if memory ran out.
*/
{
  a_boolean ok = TRUE;

  if (num_agg_levels == agg_levels_capacity) {
    size_t        new_capacity = (agg_levels_capacity == 0) ?
                                 32 : agg_levels_capacity * 2;
    an_init_level *new_levels = (an_init_level *)realloc(agg_levels,
                                    new_capacity * sizeof(an_init_level));

    if (new_levels == NULL) {
      ok = FALSE;
      ecil_note_unsupported("out of memory in the ECIL writer");
    } else {
      agg_levels = new_levels;
      agg_levels_capacity = new_capacity;
    }  /* if */
  }  /* if */
  if (ok) ecil_init_level_start(&agg_levels[num_agg_levels++], sources, n);
  return ok;
}  /* push_agg_level */


static void begin_dynamic_aggregate(an_ecil_frame *frame, a_constant_ptr c,
                                    a_constant_ptr *sources, size_t n)
/*
AGG_INIT_BEGIN for a nested dynamic aggregate (the target glvalue is on the
stack) and push its cursor.  It is initialized by c, or by the n merged
constants in sources.
*/
{
  uint32_t      t = ecil_type_id(frame, c->type);
  unsigned int  flags = (c->partial_aggr_value || c->is_partially_initialized)
                        ? ECIL_AGG_INIT_ZERO_FILL : 0u;

  if (t != 0 && ecil_gid_info(t)->kind != egk_record &&
      ecil_gid_info(t)->kind != egk_array) {
    ecil_note_unsupported_type(t, "an aggregate initializer of an opaque "
                                  "type");
  } else if (t != 0 && push_agg_level(sources != NULL ? sources : &c,
                                      sources != NULL ? n : 1)) {
    ecil_rec_begin(frame, eop_agg_init_begin);
    ecil_arg_uint(frame, t);
    ecil_arg_uint(frame, flags);
    ecil_rec_end(frame);
  }  /* if */
}  /* begin_dynamic_aggregate */


static void do_init(an_ecil_frame *frame, size_t idx)
/*
cik_init: initialize local (or temporary) number it->a, of ECIL type it->b,
from the dynamic init in it->entry.
*/
{
  a_code_item     *it = &code_stack[idx];
  a_dynamic_init_ptr
                  di = (a_dynamic_init_ptr)it->entry;
  uint32_t        lid = it->a;

  if (it->phase == 1) {
    /* dik_expression: the value is on the stack. */
    ecil_rec_begin(frame, eop_store);
    ecil_arg_uint(frame, 0);
    ecil_arg_uint(frame, (unsigned int)esr_none);
    ecil_rec_end(frame);
    code_stack_size--;
  } else {
    switch (di->kind) {
      case dik_none:
        code_stack_size--;
        break;
      case dik_zero:
        ecil_rec1(frame, eop_lref, lid);
        ecil_rec0(frame, eop_init_zero);
        code_stack_size--;
        break;
      case dik_constant:
        ecil_rec1(frame, eop_lref, lid);
        if (ecil_emit_constant_typed(frame, di->variant.constant.ptr,
                                     it->b)) {
          ecil_rec0(frame, eop_init_const);
        }  /* if */
        code_stack_size--;
        break;
      case dik_expression:
        ecil_rec1(frame, eop_lref, lid);
        it->phase = 1;
        push_expr(di->variant.expression, 0);
        break;
      case dik_nonconstant_aggregate:
        ecil_rec1(frame, eop_lref, lid);
        it->kind = cik_agg;
        it->phase = 0;
        it->cursor = (void *)(uintptr_t)num_agg_levels;
        begin_dynamic_aggregate(frame, di->variant.constant.ptr, NULL, 0);
        break;
      default:
        ecil_note_unsupported("this kind of initialization");
        break;
    }  /* switch */
  }  /* if */
}  /* do_init */


static void do_agg(an_ecil_frame *frame, size_t idx)
/*
cik_agg: walk the entries of a dynamic aggregate.  it->cursor holds the
cursor-stack depth at which this aggregate started.
*/
{
  size_t  base = (size_t)(uintptr_t)code_stack[idx].cursor;

  if (num_agg_levels <= base) {
    code_stack_size--;              /* All levels finished. */
  } else {
    an_init_level   *level = &agg_levels[num_agg_levels - 1];
    uint64_t        index = 0;
    a_constant_ptr  value = NULL;

    if (!ecil_init_level_next(level, &index, &value)) {
      ecil_rec0(frame, eop_agg_init_end);
      ecil_init_level_end(level);
      num_agg_levels--;
    } else if (level->entry_is_char) {
      ecil_rec1(frame, eop_agg_sel, index);
      ecil_emit_string_char(frame, level, value, index);
      ecil_rec0(frame, eop_init_const);
    } else if (value->kind == (a_constant_repr_kind)ck_init_repeat) {
      uint64_t  count = level->entry_count;
      uint64_t  i;

      if (value->variant.init_repeat.constant->kind ==
                      (a_constant_repr_kind)ck_aggregate ||
          count > 4096) {
        ecil_note_unsupported("a large or aggregate repeated initializer");
      } else {
        for (i = 0; i < count && ecil_unsupported_reason == NULL; i++) {
          ecil_rec1(frame, eop_agg_sel, index + i);
          if (ecil_emit_constant_typed(frame,
                          value->variant.init_repeat.constant,
                          ecil_member_type_gid(frame, level, index))) {
            ecil_rec0(frame, eop_init_const);
          }  /* if */
        }  /* for */
      }  /* if */
    } else if (value->kind == (a_constant_repr_kind)ck_dynamic_init) {
      a_dynamic_init_ptr  di = value->variant.dynamic_init.ptr;

      ecil_rec1(frame, eop_agg_sel, index);
      if (di->kind == dik_expression) {
        (void)push_item(cik_store, NULL, 0);
        push_expr(di->variant.expression, 0);
      } else if (di->kind == dik_constant) {
        if (ecil_emit_constant_typed(frame, di->variant.constant.ptr,
                              ecil_member_type_gid(frame, level, index))) {
          ecil_rec0(frame, eop_init_const);
        }  /* if */
      } else {
        ecil_note_unsupported("this kind of aggregate member initializer");
      }  /* if */
    } else if (constant_has_dynamic_part(value) ||
               (level->entry_sources != NULL &&
                merged_has_dynamic_part(level->entry_sources,
                                        level->entry_num_sources))) {
      a_constant_ptr  *srcs = level->entry_sources;
      size_t          num_srcs = level->entry_num_sources;

      ecil_rec1(frame, eop_agg_sel, index);
      begin_dynamic_aggregate(frame, value, srcs, num_srcs);
    } else {
      ecil_rec1(frame, eop_agg_sel, index);
      if (ecil_emit_constant_sources(frame, value, level->entry_sources,
                           level->entry_num_sources,
                           ecil_member_type_gid(frame, level, index))) {
        ecil_rec0(frame, eop_init_const);
      }  /* if */
    }  /* if */
  }  /* if */
}  /* do_agg */


/* Statements. */

static void do_decl(an_ecil_frame *frame, a_statement_ptr s)
/*
stmk_decl: declare the locals (LOCAL) and define the static variables.
*/
{
  an_il_entity_list_entry_ptr e;

  for (e = s->variant.decl.entities; e != NULL; e = e->next) {
    if (e->entity.kind == (an_il_entry_kind)iek_variable) {
      a_variable_ptr  var = (a_variable_ptr)e->entity.ptr;

      if (var->storage_class == (a_storage_class)sc_static) {
        ecil_define_variable(frame, var, NULL);
      } else if (var->storage_class == (a_storage_class)sc_extern) {
        /* A block-scope declaration: introduced when referenced. */
      } else if (var->is_vla || var->has_variably_modified_type) {
        ecil_note_unsupported("a variable length array");
      } else {
        uint32_t  t = ecil_type_id(frame, var->type);

        if (t != 0 && (info_of(t)->flags & ECIL_GF_FAILED)) {
          ecil_note_unsupported_type(t, "a variable of a record type that "
                                        "is not supported");
        } else if (t != 0) {
          uint32_t  lid = new_lid();
          uint32_t  name_sid = var->source_corresp.name != NULL ?
                     ecil_intern_cstring(frame, var->source_corresp.name) : 0;

          ecil_set_lid(var, lid);
          ecil_check_variable_attributes(var);
          emit_local(frame, lid, t, name_sid,
                     (uint32_t)ecil_variable_alignment(var));
        }  /* if */
      }  /* if */
    }  /* if */
  }  /* for */
}  /* do_decl */


static a_boolean case_is_directly_in_switch(a_statement_ptr s)
/*
Return TRUE if a case or default label sits in its switch's body directly or
inside plain blocks only (no Duff's-device placement inside a loop or if).
*/
{
  a_statement_ptr sw = s->variant.switch_case.switch_statement;
  a_statement_ptr p = s->parent;
  a_boolean       ok = TRUE;

  while (ok && p != NULL && p != sw) {
    if (p->kind != (a_statement_kind)stmk_block) ok = FALSE;
    p = p->parent;
  }  /* while */
  return ok;
}  /* case_is_directly_in_switch */


static void emit_case_label(an_ecil_frame *frame, a_statement_ptr s)
/*
CASE_VALUE, CASE_RANGE or CASE_DEFAULT for a stmk_switch_case.
*/
{
  a_switch_case_entry_ptr entry = s->variant.switch_case.extra_info;
  a_boolean               ovflo = FALSE;

  if (!case_is_directly_in_switch(s)) {
    ecil_note_unsupported("a case label nested in other control flow");
  } else if (entry->case_value == NULL) {
    ecil_rec0(frame, eop_case_default);
  } else {
    uint32_t  t = ecil_type_id(frame, entry->case_value->type);
    a_boolean is_signed = t != 0 && info_of(t)->is_signed;

    if (entry->range_end != NULL) {
      ecil_rec_begin(frame, eop_case_range);
      if (is_signed) {
        ecil_arg_int(frame, (int64_t)value_of_integer_constant(
                                              entry->case_value, &ovflo));
        ecil_arg_int(frame, (int64_t)value_of_integer_constant(
                                              entry->range_end, &ovflo));
      } else {
        ecil_arg_uint(frame, (uint64_t)unsigned_value_of_integer_constant(
                                              entry->case_value, &ovflo));
        ecil_arg_uint(frame, (uint64_t)unsigned_value_of_integer_constant(
                                              entry->range_end, &ovflo));
      }  /* if */
      ecil_rec_end(frame);
    } else {
      ecil_rec_begin(frame, eop_case_value);
      if (is_signed) {
        ecil_arg_int(frame, (int64_t)value_of_integer_constant(
                                              entry->case_value, &ovflo));
      } else {
        ecil_arg_uint(frame, (uint64_t)unsigned_value_of_integer_constant(
                                              entry->case_value, &ovflo));
      }  /* if */
      ecil_rec_end(frame);
    }  /* if */
  }  /* if */
}  /* emit_case_label */


static void do_stmt(an_ecil_frame *frame, size_t idx)
/*
Visit a statement item.
*/
{
  a_code_item     *it = &code_stack[idx];
  a_statement_ptr s = (a_statement_ptr)it->entry;
  a_boolean       done = TRUE;

  switch (s->kind) {
    case stmk_empty:
      break;
    case stmk_expr:
      if (it->phase == 0) {
        it->phase = 1;
        push_expr(s->expr, 0);
        done = FALSE;
      } else if (node_pushes_value(s->expr)) {
        ecil_rec0(frame, eop_drop);
      }  /* if */
      break;
    case stmk_block:
      if (it->phase == 0) {
        ecil_rec0(frame, eop_block_begin);
        it->phase = 1;
        (void)push_item(cik_stmt_list, s->variant.block.statements, 0);
        done = FALSE;
      } else {
        ecil_rec0(frame, eop_block_end);
      }  /* if */
      break;
    case stmk_decl:
      do_decl(frame, s);
      break;
    case stmk_init:
      if (it->phase == 1) {
        /* The initialization ran. */
      } else {
        a_dynamic_init_ptr di = s->variant.dynamic_init;
        a_variable_ptr     var = di->variable;

        if (var == NULL) {
          ecil_note_unsupported("an initialization without a variable");
        } else if (var->storage_class == (a_storage_class)sc_static) {
          /* Initialized statically (defined at the declaration). */
        } else {
          uint32_t  lid = ecil_lid_of(var);

          if (lid == 0) {
            ecil_note_unsupported("the initialization of an undeclared "
                                  "local variable");
          } else {
            it->phase = 1;
            { a_code_item *init = push_item(cik_init, di, 0);

              if (init != NULL) init->a = lid;
            }
            done = FALSE;
          }  /* if */
        }  /* if */
      }  /* if */
      break;
    case stmk_if:
      if (it->phase == 0) {
        it->phase = 1;
        push_expr(s->expr, CIF_COND);
        done = FALSE;
      } else if (it->phase == 1) {
        rec_bool(frame, eop_if_begin,
                 s->variant.if_stmt.else_statement != NULL);
        it->phase = 2;
        push_stmt(s->variant.if_stmt.then_statement);
        done = FALSE;
      } else if (it->phase == 2 && s->variant.if_stmt.else_statement != NULL) {
        ecil_rec0(frame, eop_if_else);
        it->phase = 3;
        push_stmt(s->variant.if_stmt.else_statement);
        done = FALSE;
      } else {
        ecil_rec0(frame, eop_if_end);
      }  /* if */
      break;
    case stmk_while:
      if (it->phase == 0) {
        ecil_rec0(frame, eop_while_begin);
        it->phase = 1;
        push_expr(s->expr, CIF_COND);
        done = FALSE;
      } else if (it->phase == 1) {
        ecil_rec0(frame, eop_while_body);
        it->phase = 2;
        push_stmt(s->variant.loop_statement);
        done = FALSE;
      } else {
        ecil_rec0(frame, eop_while_end);
      }  /* if */
      break;
    case stmk_end_test_while:
      if (it->phase == 0) {
        ecil_rec0(frame, eop_do_begin);
        it->phase = 1;
        push_stmt(s->variant.loop_statement);
        done = FALSE;
      } else if (it->phase == 1) {
        ecil_rec0(frame, eop_do_cond);
        it->phase = 2;
        push_expr(s->expr, CIF_COND);
        done = FALSE;
      } else {
        ecil_rec0(frame, eop_do_end);
      }  /* if */
      break;
    case stmk_for:
      { a_for_loop_ptr loop = s->variant.for_loop.extra_info;

        if (it->phase == 0) {
          /* FOR_BEGIN opens the scope of the init declarations (ECIL 0.7):
             they follow it directly and stay in scope to FOR_END.  The
             front end wraps them in a block (in C there is no
             for_init_scope); the block's statements are sent without a
             BLOCK around them. */
          ecil_rec0(frame, eop_for_begin);
          it->phase = 1;
          if (loop->initialization != NULL &&
              loop->initialization->kind == (a_statement_kind)stmk_block) {
            (void)push_item(cik_stmt_list,
                            loop->initialization->variant.block.statements,
                            0);
          } else if (loop->initialization != NULL) {
            push_stmt(loop->initialization);
          }  /* if */
          done = FALSE;
        } else if (it->phase == 1) {
          rec_bool(frame, eop_for_cond, s->expr != NULL);
          it->phase = 2;
          if (s->expr != NULL) push_expr(s->expr, CIF_COND);
          done = FALSE;
        } else if (it->phase == 2) {
          rec_bool(frame, eop_for_step, loop->increment != NULL);
          it->phase = 3;
          if (loop->increment != NULL) push_expr(loop->increment, 0);
          done = FALSE;
        } else if (it->phase == 3) {
          if (loop->increment != NULL && node_pushes_value(loop->increment)) {
            ecil_rec0(frame, eop_drop);
          }  /* if */
          ecil_rec0(frame, eop_for_body);
          it->phase = 4;
          if (s->variant.for_loop.statement != NULL) {
            push_stmt(s->variant.for_loop.statement);
          }  /* if */
          done = FALSE;
        } else {
          ecil_rec0(frame, eop_for_end);
        }  /* if */
      }
      break;
    case stmk_switch:
      if (it->phase == 0) {
        it->phase = 1;
        push_expr(s->expr, 0);
        done = FALSE;
      } else if (it->phase == 1) {
        ecil_rec0(frame, eop_switch_begin);
        it->phase = 2;
        push_stmt(s->variant.switch_stmt.body_statement);
        done = FALSE;
      } else {
        ecil_rec0(frame, eop_switch_end);
      }  /* if */
      break;
    case stmk_switch_case:
      emit_case_label(frame, s);
      break;
    case stmk_goto:
    case stmk_label:
      { a_label_ptr label = s->variant.label.ptr;

        if (label->break_label || label->switch_break_label) {
          if (s->kind == (a_statement_kind)stmk_goto) {
            ecil_rec0(frame, eop_break_stmt);
          }  /* if */
        } else if (label->continue_label) {
          if (s->kind == (a_statement_kind)stmk_goto) {
            ecil_rec0(frame, eop_continue_stmt);
          }  /* if */
        } else {
          uint32_t  lid = ecil_lid_of(label);
          uint32_t  name_sid = 0;

          if (lid == 0) {
            lid = new_lid();
            ecil_set_lid(label, lid);
          }  /* if */
          if (s->kind == (a_statement_kind)stmk_label) {
            if (label->source_corresp.name != NULL) {
              name_sid = ecil_intern_cstring(frame,
                                             label->source_corresp.name);
            }  /* if */
            ecil_rec_begin(frame, eop_label);
            ecil_arg_uint(frame, lid);
            if (name_sid != 0) {
              ecil_arg_uint(frame, name_sid);
            } else {
              ecil_arg_nil(frame);
            }  /* if */
            ecil_rec_end(frame);
          } else {
            ecil_rec2(frame, eop_goto_stmt, lid, 0);
          }  /* if */
        }  /* if */
      }
      break;
    case stmk_return:
      if (it->phase == 0 && s->expr != NULL) {
        it->phase = 1;
        push_expr(s->expr, 0);
        done = FALSE;
      } else {
        rec_bool(frame, eop_return_stmt, s->expr != NULL);
      }  /* if */
      break;
    case stmk_asm:
    case stmk_asm_func_body:
      ecil_note_unsupported("inline assembly");
      break;
    case stmk_assigned_goto:
      ecil_note_unsupported("a computed goto");
      break;
    case stmk_stmt_expr_result:
      ecil_note_unsupported("a GNU statement expression");
      break;
    case stmk_set_vla_size:
    case stmk_vla_decl:
      ecil_note_unsupported("a variable length array");
      break;
    default:
      ecil_note_unsupported("this kind of statement");
      break;
  }  /* switch */
  if (done) code_stack_size--;
}  /* do_stmt */


static void do_stmt_list(an_ecil_frame *frame, size_t idx)
/*
A cursor over sibling statements: one statement per visit.
*/
{
  a_code_item     *it = &code_stack[idx];
  a_statement_ptr s = (a_statement_ptr)it->entry;

  (void)frame;
  if (s == NULL) {
    code_stack_size--;
  } else {
    it->entry = s->next;
    push_stmt(s);
  }  /* if */
}  /* do_stmt_list */


static void run_code_stack(an_ecil_frame *frame)
/*
Process items until the stack is empty or something is unsupported.
*/
{
  while (code_stack_size > 0 && ecil_unsupported_reason == NULL) {
    size_t  idx = code_stack_size - 1;

    switch (code_stack[idx].kind) {
      case cik_expr:      do_expr(frame, idx); break;
      case cik_stmt:      do_stmt(frame, idx); break;
      case cik_stmt_list: do_stmt_list(frame, idx); break;
      case cik_init:      do_init(frame, idx); break;
      case cik_agg:       do_agg(frame, idx); break;
      case cik_store:
        ecil_rec_begin(frame, eop_store);
        ecil_arg_uint(frame, 0);
        ecil_arg_uint(frame, (unsigned int)esr_none);
        ecil_rec_end(frame);
        code_stack_size--;
        break;
    }  /* switch */
  }  /* while */
  code_stack_size = 0;
  reset_agg_levels();
}  /* run_code_stack */


/* Functions. */

static a_boolean type_is_opaque_value(uint32_t gid)
/*
Return TRUE for an opaque scalar and for a record with opaque storage by
value: a value of the type cannot be passed or returned.
*/
{
  return info_of(gid)->kind == egk_opaque ||
         (info_of(gid)->kind == egk_record &&
          (info_of(gid)->flags & ECIL_GF_ABI_OPAQUE) != 0);
}  /* type_is_opaque_value */


static void check_definition_return_type(an_ecil_frame *frame,
                                         a_routine_ptr  rout)
/*
Diagnose a definition whose return type is opaque (spec 8.11).
*/
{
  a_type_ptr  ft = skip_typerefs(rout->type);
  uint32_t    rt = ecil_type_id(frame, ft->variant.routine.return_type);

  if (rt != 0 && type_is_opaque_value(rt)) {
    ecil_note_unsupported_type(rt, "a return type that is unsupported");
  }  /* if */
}  /* check_definition_return_type */


static void emit_function(an_ecil_frame *frame, a_scope_ptr scope)
/*
Translate one function scope: re-issue its DECL_FUNC if the definition's
facts differ, then FUNC_BEGIN, PARAMs, the body and FUNC_END.  If the body
uses a construct v0 cannot express, discard the body records (spec 8.11:
the declaration section is kept), re-issue the declaration as unsupported
and warn.
*/
{
  a_routine_ptr rout = scope->variant.routine.ptr;
  uint32_t      gid;
  an_ecil_mark  mark;
  a_boolean     dropped = FALSE;

  /* A reason noted while introducing the declaration (an unsupported
     parameter type) is reported with the dropped body below. */
  ecil_unsupported_reason = NULL;
  /* Records declared incomplete in an earlier frame and completed since get
     their bodies before anything here can need them complete. */
  ecil_complete_pending_records(frame);
  gid = ecil_function_id(frame, rout);
  if (gid != 0 && ecil_unsupported_reason == NULL) {
    ecil_gid_info(gid)->entity = rout;
    ecil_reissue_function(frame, gid, TRUE, FALSE);
  }  /* if */
  ecil_frame_begin_body(frame);
  mark = ecil_frame_mark(frame);
  next_lid = 0;
  if (gid != 0 && ecil_unsupported_reason == NULL) {
    a_variable_ptr  param;
    a_boolean       prototyped = rout_type_supp(
                                   skip_typerefs(rout->type))->prototyped;
    a_boolean       reachable = TRUE;

    check_definition_return_type(frame, rout);
    ecil_rec2(frame, eop_func_begin, gid, 0);
    for (param = scope->variant.routine.parameters; param != NULL;
         param = param->next) {
      uint32_t  decl = ecil_type_id(frame, param->type);
      uint32_t  abi = decl;
      uint32_t  lid;
      uint32_t  name_sid = 0;

      if (decl != 0 && type_is_opaque_value(decl)) {
        /* The ABI of an opaque parameter is unknown (spec 8.11). */
        ecil_note_unsupported_type(decl, "a parameter of an unsupported "
                                         "type");
      }  /* if */

      if (decl != 0 && !prototyped) {
        abi = ecil_promote_gid(frame, decl, param->type);
      }  /* if */
      if (param->source_corresp.name != NULL) {
        name_sid = ecil_intern_cstring(frame, param->source_corresp.name);
      }  /* if */
      if (decl != 0 && ecil_unsupported_reason == NULL) {
        lid = new_lid();
        ecil_set_lid(param, lid);
        ecil_rec_begin(frame, eop_param);
        ecil_arg_uint(frame, lid);
        ecil_arg_uint(frame, abi);
        ecil_arg_uint(frame, decl);
        if (name_sid != 0) {
          ecil_arg_uint(frame, name_sid);
        } else {
          ecil_arg_nil(frame);
        }  /* if */
        ecil_arg_uint(frame, 0);
        ecil_rec_end(frame);
      }  /* if */
    }  /* for */
    if (scope->assoc_block != NULL &&
        scope->assoc_block->kind == (a_statement_kind)stmk_block &&
        scope->assoc_block->variant.block.extra_info != NULL) {
      reachable = scope->assoc_block->variant.block.extra_info->
                                                    end_of_block_reachable;
    }  /* if */
    if (ecil_unsupported_reason == NULL && scope->assoc_block != NULL) {
      push_stmt(scope->assoc_block);
      run_code_stack(frame);
    }  /* if */
    if (ecil_unsupported_reason == NULL) {
#if CHECKING
      ecil_check_function_lids(next_lid);
#endif /* CHECKING */
      ecil_rec1(frame, eop_func_end, reachable ? ECIL_FUNC_END_REACHABLE : 0u);
    }  /* if */
  }  /* if */
  /* The static locals met on the way were introduced (DECL_VAR) where they
     were first used; their initializers are built now, whether or not the
     body survives.  A failure among them makes the function unsupported. */
  ecil_drain_variable_definitions(frame);
  if (ecil_unsupported_reason != NULL) {
    char  message[500];

    dropped = TRUE;
    ecil_frame_truncate(frame, mark);
    (void)snprintf(message, sizeof(message),
                   "the body of \"%.200s\" uses %.100s, which the CIR back "
                   "end does not support; the function is dropped",
                   rout->source_corresp.name != NULL ?
                               rout->source_corresp.name : "?",
                   ecil_unsupported_reason);
    ecil_unsupported_reason = NULL;
    if (gid != 0) ecil_reissue_function(frame, gid, FALSE, TRUE);
    ecil_unsupported_reason = NULL;
    cir_be_warning(message);
  }  /* if */
  code_stack_size = 0;
  reset_agg_levels();
  ecil_end_lid_epoch(next_lid);
  ecil_set_function_state(rout, dropped ? efs_dropped : efs_streamed);
}  /* emit_function */


void ecil_stream_function_region(an_ecil_frame         *frame,
                                 a_memory_region_number region_number)
/*
Translate every function scope of a memory region into the frame (a function
and its lambdas share the region).
*/
{
  a_scope_ptr             scope = il_header.region_scope_entry[region_number];
  a_scope_ptr             saved_function_scope = innermost_function_scope;
  a_memory_region_number  saved_region = curr_il_region_number;

  curr_il_region_number = region_number;
  for (; scope != NULL; scope = scope->next) {
    if (scope->kind == (a_scope_kind)sck_function &&
        !ecil_function_is_handled(scope->variant.routine.ptr) &&
        scope->variant.routine.ptr->defined &&
        !ecil_routine_is_filtered(scope->variant.routine.ptr)) {
      innermost_function_scope = scope;
      emit_function(frame, scope);
    }  /* if */
  }  /* for */
  innermost_function_scope = saved_function_scope;
  curr_il_region_number = saved_region;
}  /* ecil_stream_function_region */


/* Conditionally close the "edg" namespace. */
END_EDG_NAMESPACE

#endif /* ECIL_WRITER_ENABLED */
