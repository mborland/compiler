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
#include "overload.h"
#if BACK_END_TYPE == BACK_END_TYPE_CIR
#include "lower_name.h"
#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */

#if ECIL_WRITER_ENABLED

/* Conditionally open the "edg" namespace. */
BEGIN_EDG_NAMESPACE

/* The names of IL kinds for diagnostics (generated). */
#include "ecil_kind_names.h"

/* The kinds of work item. */
enum a_code_item_kind {
  cik_expr,         /* Translate an expression node. */
  cik_stmt,         /* Translate a statement. */
  cik_stmt_list,    /* A cursor over a chain of sibling statements. */
  cik_init,         /* Initialize a local object from a dynamic init. */
  cik_agg,          /* Initialize a local aggregate entry by entry. */
  cik_store,        /* Emit a STORE of a value just evaluated. */
  cik_ctor_call,    /* The operands and CALL of a constructor (the object's
                       address is already pushed). */
  cik_var_init,     /* The initialization of a variable and its cleanup. */
  cik_new,          /* A new expression (phases: count, size, operands,
                       call, initialization) or a delete expression. */
  cik_meminit       /* The member initializers of a constructor body. */
};

/* Item flags. */
#define CIF_COND 0x01u      /* The result is wanted as a bool (TO_BOOL). */
#define CIF_DEST 0x02u      /* A class value that is not trivially copyable
                               is consumed by a STORE(INIT) or a DROP the
                               parent arranges (spec 11.6). */
#define CIF_GLV 0x04u       /* The storage of a temporary is wanted, not its
                               value. */
#define CIF_UNUSED 0x10u    /* The context discards the value. */
#define CIF_DROP 0x08u      /* A full expression statement: its value, if
                               any, is dropped inside the FULLEXPR. */

/* Phases of an assignment whose value is evaluated first (C++17). */
#define ASSIGN_RHS_FIRST 5u     /* An assignment: value in a temporary. */
#define ASSIGN_LHS_NEXT  6u     /* ... and its target is evaluated next. */

/* cik_new state (it->c): the cookie size is in the bits above 8. */
#define NEW_ARRAY       0x01u
#define NEW_DYNAMIC     0x02u     /* The count is in the local it->a. */
#define NEW_NARGS_SHIFT 2u
#define NEW_NARGS_ONE   (1u << NEW_NARGS_SHIFT)
#define NEW_NARGS_MASK  (0x3Fu << NEW_NARGS_SHIFT)

/* The destination of cik_init when the object's glvalue is already on the
   value stack. */
#define DEST_ON_STACK 0xFFFFFFFFu

/* A destination that is a static-duration object: DEST_GLOBAL_FLAG | G-id. */
#define DEST_GLOBAL_FLAG 0x80000000u

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
  uint32_t      c;
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
STATIC_THREAD an_expr_node_ptr
                *pure_stack;      /* The stack of expression_is_pure. */
STATIC_THREAD size_t
                pure_stack_capacity;
STATIC_THREAD a_variable_ptr
                nrvo_variable;    /* The named return value of the function
                                     being translated, or NULL. */
STATIC_THREAD a_type_ptr
                return_type;      /* The return type of that function. */
STATIC_THREAD uint32_t
                this_lid;         /* The L-id of its "this", or 0. */
STATIC_THREAD a_scope_ptr
                function_scope;   /* The scope of that function. */
STATIC_THREAD a_type_ptr
                function_class;   /* The class of a member function. */

typedef struct a_pending_cleanup {
  uint32_t      lid;
  a_type_ptr    type;
  a_routine_ptr dtor;
} a_pending_cleanup;

STATIC_THREAD a_pending_cleanup
                *pending_cleanups;  /* Extended temporaries (6.6.6). */
STATIC_THREAD size_t
                num_pending_cleanups;
STATIC_THREAD size_t
                pending_cleanups_capacity;
STATIC_THREAD a_boolean
                return_is_nontrivial_abi;
STATIC_THREAD a_lambda_ptr
                agg_lambda;       /* The lambda whose closure is being
                                     initialized (for implied sources). */
STATIC_THREAD a_boolean
                in_field_initializer;  /* Translating a default member
                                          initializer: parameter 0 is
                                          "this". */
                                  /* The function being translated returns a
                                     class that is not trivially copyable. */
STATIC_THREAD a_boolean
                return_is_record; /* It returns a class or an array (through
                                     the return slot). */
STATIC_THREAD a_variable_ptr
                static_init_var;  /* The variable whose STATIC_INIT is being
                                     built, or NULL. */
STATIC_THREAD unsigned int
                static_temp_count;/* The static temporaries it has made. */

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
  free(pending_cleanups);
  free(pure_stack);
  pure_stack = NULL;
  pure_stack_capacity = 0;
  pending_cleanups = NULL;
  pending_cleanups_capacity = num_pending_cleanups = 0;
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
    item->c = 0;
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


static a_boolean value_is_pushed(an_expr_node_ptr node, a_boolean unused)
/*
Return TRUE if translating the expression leaves a value on the consumer's
stack.  Void expressions and assignments whose result is unused leave none,
nor does a class temporary that is built in place when its value is
discarded ("unused": the context discards it: a comma's left operand, a void
cast, a statement).
*/
{
  a_boolean pushes = TRUE;
  a_boolean walking = TRUE;

  while (walking) {
    walking = FALSE;
    node = skip_parens(node);
    if (node->kind == (an_expr_node_kind)enk_object_lifetime) {
      node = node->variant.object_lifetime.expr;
      walking = TRUE;
    } else if (node->kind == (an_expr_node_kind)enk_operation &&
               node->variant.operation.kind == eok_comma &&
               node->result_is_not_used &&
               node->variant.operation.operands != NULL &&
               node->variant.operation.operands->next != NULL) {
      /* The value of a discarded comma is its right operand's, discarded. */
      node = node->variant.operation.operands->next;
      unused = TRUE;
      walking = TRUE;
    }  /* if */
  }  /* while */
  if (is_void_node_type(node)) {
    pushes = FALSE;
  } else if (node->kind == (an_expr_node_kind)enk_operation &&
             is_assignment_operator(node->variant.operation.kind) &&
             node->result_is_not_used) {
    pushes = FALSE;
  } else if ((node->kind == (an_expr_node_kind)enk_temp_init ||
              node->kind == (an_expr_node_kind)enk_lambda) &&
             node->variant.init.dynamic_init != NULL &&
             node->variant.init.dynamic_init->master_entry != NULL) {
    /* Built directly in the variable of its master entry. */
    pushes = FALSE;
  } else if ((node->kind == (an_expr_node_kind)enk_temp_init ||
              node->kind == (an_expr_node_kind)enk_lambda) &&
             (node->result_is_not_used || unused) &&
             ecil_class_is_nontrivial_abi(node->type)) {
    /* The temporary is built in place and is not loaded (spec 11.6). */
    pushes = FALSE;
  }  /* if */
  return pushes;
}  /* value_is_pushed */


static a_boolean node_pushes_value(an_expr_node_ptr node)
/*
value_is_pushed for an expression whose value is used (or has its own
result_is_not_used).
*/
{
  return value_is_pushed(node, FALSE);
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


static void emit_materialize_temp(an_ecil_frame *frame,
                                  uint32_t      lid,
                                  uint32_t      t)
/*
MATERIALIZE_TEMP(lid, t): a temporary of a record that could not be written
is unsupported.
*/
{
  if (t != 0 && (info_of(t)->flags & ECIL_GF_FAILED) != 0) {
    ecil_note_unsupported_type(t, "a temporary of a record type that is not "
                                  "supported");
  } else {
    ecil_rec_begin(frame, eop_materialize_temp);
    ecil_arg_uint(frame, lid);
    ecil_arg_uint(frame, t);
    ecil_rec_end(frame);
  }  /* if */
}  /* emit_materialize_temp */


static void emit_local_flags(an_ecil_frame *frame,
                             uint32_t      lid,
                             uint32_t      t,
                             uint32_t      name_sid,
                             uint32_t      alignment,
                             unsigned int  flags)
/*
LOCAL: declare local (or temporary) lid of ECIL type t, named by name_sid
(0: unnamed), with an explicit alignment (0: the natural one) and
ECIL_LOCAL_* flags.
*/
{
  if (t != 0 && (info_of(t)->flags & ECIL_GF_FAILED) != 0) {
    ecil_note_unsupported_type(t, "a variable or temporary of a record type "
                                  "that is not supported");
  } else {
    ecil_rec_begin(frame, eop_local);
    ecil_arg_uint(frame, lid);
    ecil_arg_uint(frame, t);
    if (name_sid != 0) {
      ecil_arg_uint(frame, name_sid);
    } else {
      ecil_arg_nil(frame);
    }  /* if */
    ecil_arg_uint(frame, alignment);
    ecil_arg_uint(frame, flags);
    ecil_rec_end(frame);
  }  /* if */
}  /* emit_local_flags */


static void emit_local(an_ecil_frame *frame,
                       uint32_t      lid,
                       uint32_t      t,
                       uint32_t      name_sid,
                       uint32_t      alignment)
/*
LOCAL without flags.
*/
{
  emit_local_flags(frame, lid, t, name_sid, alignment, 0u);
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


static void note_unsupported_kind(const char *what,
                                  int        kind,
                                  const char *name)
/*
Note an unsupported operator or node kind, naming it (the enumerator's value
too if it has no name: see an_expr_operator_kind, an_expr_node_kind,
a_constant_repr_kind in il_def.h).
*/
{
  char  reason[120];

  if (name != NULL) {
    (void)snprintf(reason, sizeof(reason), "the %s \"%.60s\"", what, name);
  } else {
    (void)snprintf(reason, sizeof(reason), "the %s %d", what, kind);
  }  /* if */
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
    note_unsupported_kind("operator", (int)op, ecil_operator_name((int)op));
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
      case ck_aggregate:
        if (!C_mode() && info_of(t)->kind == egk_record &&
            !ecil_class_is_nontrivial_abi(node->type)) {
          /* A class prvalue that is a constant: build it in a temporary
             (INIT_CONST) and load it. */
          uint32_t  lid = new_lid();

          emit_local_flags(frame, lid, t, 0, 0,
                           C_mode() ? 0u : (unsigned int)ECIL_LOCAL_TEMPORARY);
          ecil_rec1(frame, eop_lref, lid);
          if (ecil_emit_constant_typed(frame, c, t)) {
            ecil_rec0(frame, eop_init_const);
          }  /* if */
          ecil_rec1(frame, eop_lref, lid);
          ecil_rec1(frame, eop_load, 0);
        } else {
          note_unsupported_kind("constant kind", (int)c->kind,
                                ecil_constant_kind_name((int)c->kind));
        }  /* if */
        break;
      default:
        note_unsupported_kind("constant kind", (int)c->kind,
                                ecil_constant_kind_name((int)c->kind));
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
    ecil_check_variable_use(gid);
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

#define MAX_ANON_DEPTH 16

static a_boolean anonymous_member_path(a_type_ptr   record_type,
                                       a_field_ptr  field,
                                       a_field_ptr  *path,
                                       a_type_ptr   *owners,
                                       unsigned int *depth)
/*
For a member selected from record_type that is a member of an anonymous union
or struct nested in it (the front end names the outer object only), the
unnamed members that lead to it, innermost first (path[0] holds the field's
class; owners[i] is the class that has path[i]).  FALSE if the chain cannot
be found.
*/
{
  a_type_ptr  cur = skip_typerefs(parent_class_of(field));
  a_boolean   ok = TRUE;

  *depth = 0;
  while (ok && cur != record_type) {
    a_type_ptr   outer = NULL;
    a_field_ptr  f;

    if (cur->source_corresp.is_class_member) {
      outer = skip_typerefs(parent_class_of(cur));
    }  /* if */
    if (*depth >= MAX_ANON_DEPTH || outer == NULL ||
        !is_immediate_class_type(outer)) {
      ok = FALSE;
    } else {
      for (f = outer->variant.class_struct_union.field_list;
           f != NULL && skip_typerefs(f->type) != cur; f = f->next) {
      }  /* for */
      if (f == NULL) {
        ok = FALSE;
      } else {
        path[*depth] = f;
        owners[*depth] = outer;
        (*depth)++;
        cur = outer;
      }  /* if */
    }  /* if */
  }  /* while */
  if (!ok) ecil_note_unsupported("a member of an anonymous union or struct");
  return ok;
}  /* anonymous_member_path */


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
    ecil_rec_begin(frame, eop_const_int);
    ecil_arg_uint(frame, xt);
    if (xt != 0 && info_of(xt)->kind == egk_int && info_of(xt)->bits > 64) {
      ecil_arg_int128(frame, 0, 0, info_of(xt)->is_signed);
    } else {
      ecil_arg_uint(frame, 0);
    }  /* if */
    ecil_rec_end(frame);
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


static a_boolean operand_is_known_non_null(an_expr_node_ptr operand)
/*
Return TRUE if the pointer operand of a class cast cannot be null: the
object pointer of a member function, the address of an object, a reference.
*/
{
  an_expr_node_ptr  node = skip_parens(operand);
  a_boolean         result = FALSE;

  if (node->kind == (an_expr_node_kind)enk_variable) {
    a_variable_ptr  var = node_variable(node);

    result = var->is_this_parameter ||
             (skip_typerefs(var->type)->kind == tk_pointer &&
              skip_typerefs(var->type)->variant.pointer.is_reference);
  } else if (node->kind == (an_expr_node_kind)enk_operation) {
    an_expr_operator_kind  k = node->variant.operation.kind;

    result = k == eok_address_of || k == eok_reference_to ||
             k == eok_array_to_pointer;
  }  /* if */
  return result;
}  /* operand_is_known_non_null */


static void emit_class_cast(an_ecil_frame *frame, an_expr_node_ptr node)
/*
eok_base_class_cast and eok_derived_class_cast between a class and one of its
direct bases: [ADDR] BASE_CAST|DERIVED_CAST [DEREF].  The operand (already
translated) is a pointer, or a class glvalue (then the address is taken and
the result is a glvalue again).  The byte offset of the base subobject comes
from the derived class's base list.
*/
{
  an_expr_node_ptr  op = node->variant.operation.operands;
  a_boolean         is_base_cast = node->variant.operation.kind ==
                                   (an_expr_operator_kind)eok_base_class_cast;
  a_type_ptr        to = skip_typerefs(node->type);
  a_type_ptr        from = skip_typerefs(op->type);
  a_boolean         pointers = to->kind == tk_pointer;
  a_type_ptr        to_class = pointers ?
                           skip_typerefs(to->variant.pointer.type) : to;
  a_type_ptr        from_class = pointers && from->kind == tk_pointer ?
                           skip_typerefs(from->variant.pointer.type) : from;
  a_type_ptr        derived = is_base_cast ? from_class : to_class;
  a_type_ptr        base_class = is_base_cast ? to_class : from_class;
  a_base_class_ptr  bcp = NULL;
  a_boolean         glvalue_operand = !pointers && is_glvalue_node(op);

  if (!is_immediate_class_type(derived) ||
      !is_immediate_class_type(base_class)) {
    ecil_note_unsupported("a class cast of unsupported types");
  } else if (!pointers && !glvalue_operand) {
    ecil_note_unsupported("a class cast of a class prvalue");
  } else {
    for (bcp = class_type_supp(derived)->direct_base_classes; bcp != NULL;
         bcp = bcp->next) {
      if (bcp->direct && skip_typerefs(bcp->type) == base_class) break;
    }  /* for */
    if (bcp == NULL) {
      ecil_note_unsupported("a cast through an indirect base class");
    } else if (bcp->is_virtual) {
      ecil_note_unsupported("a virtual base class");
    }  /* if */
  }  /* if */
  if (ecil_unsupported_reason == NULL) {
    uint32_t      tder = ecil_type_id(frame, derived);
    uint32_t      tbase = ecil_type_id(frame, base_class);
    unsigned int  flags = 0;

    if (tder != 0 && tbase != 0 && ecil_unsupported_reason == NULL) {
      if (glvalue_operand) {
        ecil_rec0(frame, eop_addr);
        flags |= ECIL_CAST_NONNULL;
      } else if (node->variant.operation.implicit_in_member_naming ||
                 operand_is_known_non_null(op)) {
        flags |= ECIL_CAST_NONNULL;
      }  /* if */
      ecil_rec_begin(frame, is_base_cast ? eop_base_cast : eop_derived_cast);
      ecil_arg_uint(frame, is_base_cast ? tder : tbase);
      ecil_arg_uint(frame, is_base_cast ? tbase : tder);
      ecil_arg_uint(frame, (uint64_t)bcp->offset);
      ecil_arg_uint(frame, flags);
      ecil_rec_end(frame);
      if (glvalue_operand) {
        ecil_rec0(frame, eop_deref);
        emit_load_if_value(frame, node);
      }  /* if */
    }  /* if */
  }  /* if */
}  /* emit_class_cast */


/* Per-call item bits (a_code_item.c). */
#define CALL_MEMBER_DOT      0x01u  /* x.f(...): the object is a class glvalue
                                       (or prvalue) whose address is passed. */
#define CALL_MEMBER_PTR      0x02u  /* p->f(...): the object is a pointer. */
#define CALL_OBJECT_PENDING  0x10u  /* The object operand was just pushed. */
#define CALL_ARG_LOAD_PENDING \
                             0x20u  /* The operand just pushed is a class
                                       that is not trivially copyable: its
                                       storage is passed (a LOAD). */
#define CALL_REVERSE         0x40u  /* The second operand is evaluated first
                                       (C++17: an overloaded assignment) into
                                       a temporary, whose lid is in the bits
                                       of c above 8, and read back. */


static a_boolean expression_is_pure(an_expr_node_ptr node)
/*
Return TRUE if evaluating the expression has no side effects: it only reads
variables and memory, through conversions, member selections, indirections,
subscripts and arithmetic.  Anything else (a call, an assignment, an
increment, a temporary) is treated as having side effects.  The walk uses
an explicit stack that is kept between the calls.
*/
{
  size_t            size = 0;
  a_boolean         pure = TRUE;

  while (pure && (node != NULL || size > 0)) {
    if (node == NULL) {
      node = pure_stack[--size];
    } else {
      an_expr_node_ptr  current = skip_parens(node);
      an_expr_node_ptr  operand = NULL;

      node = NULL;
      if (current->kind == (an_expr_node_kind)enk_variable ||
          current->kind == (an_expr_node_kind)enk_constant) {
        /* A leaf. */
      } else if (current->kind == (an_expr_node_kind)enk_operation) {
        an_expr_operator_kind  k = current->variant.operation.kind;
        unsigned int           opcode = 0;
        unsigned int           cmp = 0;

        if (!is_assignment_operator(k) &&
            (k == eok_cast || k == eok_lvalue_adjust ||
             k == eok_class_rvalue_adjust || k == eok_base_class_cast ||
             k == eok_derived_class_cast || k == eok_subscript ||
             k == eok_indirect || k == eok_ref_indirect ||
             k == eok_dot_field || k == eok_points_to_field ||
             k == eok_address_of || k == eok_array_to_pointer ||
             k == eok_not || k == eok_negate || k == eok_complement ||
             k == eok_unary_plus || k == eok_land || k == eok_lor ||
             k == eok_comma || k == eok_question || k == eok_pdiff ||
             operator_opcode(k, &opcode) || comparison_kind(k, &cmp))) {
          operand = current->variant.operation.operands;
        } else {
          pure = FALSE;
        }  /* if */
      } else {
        pure = FALSE;
      }  /* if */
      while (pure && operand != NULL) {
        if (size == pure_stack_capacity) {
          size_t            new_capacity = pure_stack_capacity == 0 ? 64 :
                                           pure_stack_capacity * 2;
          an_expr_node_ptr  *new_stack = (an_expr_node_ptr *)realloc(
                          pure_stack, new_capacity * sizeof(an_expr_node_ptr));

          if (new_stack == NULL) {
            pure = FALSE;
          } else {
            pure_stack = new_stack;
            pure_stack_capacity = new_capacity;
          }  /* if */
        }  /* if */
        if (pure) {
          pure_stack[size++] = operand;
          operand = operand->next;
        }  /* if */
      }  /* while */
    }  /* if */
  }  /* while */
  return pure;
}  /* expression_is_pure */


static a_boolean designation_reads_no_memory(an_expr_node_ptr node)
/*
Return TRUE if the address that the lvalue designates cannot depend on memory
that another expression changes: a variable, or members and bases of one (a
reference variable is bound once).
*/
{
  a_boolean walking = TRUE;
  a_boolean ok = TRUE;

  node = skip_parens(node);
  if (!is_glvalue_node(node)) {
    /* The value of a variable (a pointer, say) is a read of memory. */
    ok = FALSE;
  }  /* if */
  while (walking && ok) {
    walking = FALSE;
    if (node->kind == (an_expr_node_kind)enk_operation) {
      an_expr_operator_kind k = node->variant.operation.kind;

      if (k == eok_dot_field || k == eok_lvalue_adjust ||
          k == eok_base_class_cast || k == eok_derived_class_cast ||
          k == eok_cast || k == eok_ref_indirect) {
        node = skip_parens(node->variant.operation.operands);
        walking = TRUE;
      } else {
        ok = FALSE;
      }  /* if */
    }  /* if */
  }  /* while */
  return ok && node->kind == (an_expr_node_kind)enk_variable;
}  /* designation_reads_no_memory */


static a_boolean target_may_be_evaluated_first(an_expr_node_ptr target,
                                               an_expr_node_ptr value)
/*
Return TRUE if designating the target of an assignment before evaluating the
value (C++17: the value is evaluated first) cannot be observed: the target is
a variable or a member of one, or neither side has side effects.
*/
{
  return designation_reads_no_memory(target) ||
         (expression_is_pure(target) && expression_is_pure(value));
}  /* target_may_be_evaluated_first */


static a_boolean object_has_no_side_effects(an_expr_node_ptr node)
/*
Return TRUE if evaluating the object expression of a static member selection
(x.s, p->s) only designates storage, so that it may be skipped: a chain of
member selections, indirections and class casts down to a variable.
*/
{
  a_boolean walking = TRUE;

  node = skip_parens(node);
  while (walking) {
    walking = FALSE;
    if (node->kind == (an_expr_node_kind)enk_operation) {
      an_expr_operator_kind k = node->variant.operation.kind;

      if (k == eok_ref_indirect || k == eok_indirect ||
          k == eok_dot_field || k == eok_points_to_field ||
          k == eok_lvalue_adjust || k == eok_base_class_cast ||
          k == eok_derived_class_cast) {
        node = skip_parens(node->variant.operation.operands);
        walking = TRUE;
      }  /* if */
    }  /* if */
  }  /* while */
  return node->kind == (an_expr_node_kind)enk_variable;
}  /* object_has_no_side_effects */


static unsigned int class_dsize_flag(an_ecil_frame *frame, a_type_ptr type)
/*
ECIL_STORE_DSIZE for a class with reusable tail padding (data size below its
size), else 0: such a bitwise copy only copies the data size, since the
destination may be a base subobject (spec 11.6).
*/
{
  unsigned int  flags = 0;

  if (!C_mode() && is_immediate_class_type(skip_typerefs(type))) {
    uint32_t  gid = ecil_type_id(frame, type);

    if (gid != 0 && info_of(gid)->kind == egk_record &&
        (info_of(gid)->flags & ECIL_GF_COMPLETE) != 0 &&
        ecil_data_size_of_type(skip_typerefs(type)) < info_of(gid)->size) {
      flags = ECIL_STORE_DSIZE;
    }  /* if */
  }  /* if */
  return flags;
}  /* class_dsize_flag */


static a_boolean node_is_bit_field(an_expr_node_ptr node)
/*
Return TRUE if the node designates a bit-field (possibly under cv adjustments
or parentheses): it has no address.
*/
{
  a_boolean  result = FALSE;
  a_boolean  walking = TRUE;

  node = skip_parens(node);
  while (walking) {
    walking = FALSE;
    if (node->kind == (an_expr_node_kind)enk_operation) {
      an_expr_operator_kind  k = node->variant.operation.kind;

      if (k == eok_lvalue_adjust) {
        node = skip_parens(node->variant.operation.operands);
        walking = TRUE;
      } else if ((k == eok_dot_field || k == eok_points_to_field) &&
                 node->variant.operation.operands != NULL &&
                 node->variant.operation.operands->next != NULL &&
                 node->variant.operation.operands->next->kind ==
                                        (an_expr_node_kind)enk_field) {
        result = node_field(node->variant.operation.operands->next)->
                                                                  is_bit_field;
      }  /* if */
    }  /* if */
  }  /* while */
  return result;
}  /* node_is_bit_field */


static a_boolean source_is_load(an_expr_node_ptr source)
/*
Return TRUE if the value of the expression is the load of an object (a
variable, an indirection, a member or an element) rather than a computed
value, so that the load can be consumed by a bitwise copy (spec 11.6).
*/
{
  a_boolean  loaded = FALSE;

  source = skip_parens(source);
  if (!is_glvalue_node(source)) {
    if (source->kind == (an_expr_node_kind)enk_variable) {
      loaded = TRUE;
    } else if (source->kind == (an_expr_node_kind)enk_operation) {
      an_expr_operator_kind  k = source->variant.operation.kind;

      loaded = k == eok_indirect || k == eok_ref_indirect ||
               k == eok_dot_field || k == eok_points_to_field ||
               k == eok_subscript;
    }  /* if */
  }  /* if */
  return loaded;
}  /* source_is_load */


static unsigned int class_copy_flags(an_ecil_frame  *frame,
                                     a_type_ptr     type,
                                     an_expr_node_ptr source)
/*
The STORE flags of a bitwise copy of a class value that was just loaded from
the source glvalue: a class with reusable tail padding copies only its data
size, since the destination may be a base subobject (spec 11.6).
*/
{
  unsigned int  flags = 0;

  if (source_is_load(source)) flags = class_dsize_flag(frame, type);
  return flags;
}  /* class_copy_flags */


/* Cleanups and the objects they destroy. */

static void add_pending_cleanup(uint32_t      lid,
                                a_type_ptr    type,
                                a_routine_ptr dtor)
/*
Remember a lifetime-extended temporary: its cleanup is begun at the end of
the statement that initializes the reference.
*/
{
  if (num_pending_cleanups == pending_cleanups_capacity) {
    size_t            new_capacity = (pending_cleanups_capacity == 0) ?
                                     8 : pending_cleanups_capacity * 2;
    a_pending_cleanup *new_list = (a_pending_cleanup *)realloc(
                          pending_cleanups,
                          new_capacity * sizeof(a_pending_cleanup));

    if (new_list != NULL) {
      pending_cleanups = new_list;
      pending_cleanups_capacity = new_capacity;
    }  /* if */
  }  /* if */
  if (num_pending_cleanups < pending_cleanups_capacity) {
    pending_cleanups[num_pending_cleanups].lid = lid;
    pending_cleanups[num_pending_cleanups].type = type;
    pending_cleanups[num_pending_cleanups].dtor = dtor;
    num_pending_cleanups++;
  } else {
    ecil_note_unsupported("out of memory in the ECIL writer");
  }  /* if */
}  /* add_pending_cleanup */


/* What a destructor call is applied to. */
#define DT_LOCAL  0u    /* A local or temporary: lid. */
#define DT_MEMBER 1u    /* A member of *lid: record rec, member index. */
#define DT_BASE   2u    /* A direct base of *lid: record rec, base base_gid at
                           byte offset. */
#define DT_THIS   3u    /* The object *lid points to. */
#define DT_GLOBAL 4u    /* A static-duration object: G-id lid. */

typedef struct a_destroy_target {
  unsigned int  kind;
  uint32_t      lid;
  uint32_t      rec;
  uint32_t      index;
  uint32_t      base_gid;
  uint64_t      offset;
  uint32_t      array_elem;     /* An array: the G-id of its element type. */
} a_destroy_target;


static uint32_t array_element_gid(an_ecil_frame *frame, a_type_ptr type)
/*
For an array type, the G-id of its innermost element type (arrays of arrays
are one array of that many elements: spec 11.8); 0 for any other type.
*/
{
  uint32_t  gid = 0;

  type = skip_typerefs(type);
  if (type->kind == tk_array) {
    while (type->kind == tk_array) {
      type = skip_typerefs(type->variant.array.element_type);
    }  /* while */
    gid = ecil_type_id(frame, type);
  }  /* if */
  return gid;
}  /* array_element_gid */


static void emit_this_member(an_ecil_frame *frame,
                             uint32_t      lid,
                             uint32_t      rec,
                             uint32_t      index)
/*
Push the glvalue of a member of the object pointer variable lid (this or a
copy constructor's parameter) points to.
*/
{
  ecil_rec1(frame, eop_lref, lid);
  ecil_rec1(frame, eop_load, 0);
  ecil_rec0(frame, eop_deref);
  ecil_rec2(frame, eop_field, rec, index);
}  /* emit_this_member */


static void emit_this_base(an_ecil_frame *frame,
                           uint32_t      lid,
                           uint32_t      rec,
                           uint32_t      base_gid,
                           uint64_t      offset)
/*
Push the address of a direct base subobject of the object that the pointer
variable lid points to.
*/
{
  ecil_rec1(frame, eop_lref, lid);
  ecil_rec1(frame, eop_load, 0);
  ecil_rec_begin(frame, eop_base_cast);
  ecil_arg_uint(frame, rec);
  ecil_arg_uint(frame, base_gid);
  ecil_arg_uint(frame, offset);
  ecil_arg_uint(frame, ECIL_CAST_NONNULL);
  ecil_rec_end(frame);
}  /* emit_this_base */


static void emit_target_address(an_ecil_frame         *frame,
                                const a_destroy_target *tgt)
/*
Push the address of the object a destructor is applied to.
*/
{
  if (tgt->kind == DT_LOCAL) {
    ecil_rec1(frame, eop_lref, tgt->lid);
    ecil_rec0(frame, eop_addr);
  } else if (tgt->kind == DT_MEMBER) {
    emit_this_member(frame, tgt->lid, tgt->rec, tgt->index);
    ecil_rec0(frame, eop_addr);
  } else if (tgt->kind == DT_THIS) {
    ecil_rec1(frame, eop_lref, tgt->lid);
    ecil_rec1(frame, eop_load, 0);
  } else if (tgt->kind == DT_GLOBAL) {
    ecil_rec1(frame, eop_gref, tgt->lid);
    ecil_rec0(frame, eop_addr);
  } else {
    emit_this_base(frame, tgt->lid, tgt->rec, tgt->base_gid, tgt->offset);
  }  /* if */
}  /* emit_target_address */


static void emit_call_record(an_ecil_frame *frame,
                             uint32_t      gid,
                             a_routine_ptr rout,
                             uint64_t      nargs)
/*
CALL of a constructor or destructor G-id with nargs arguments.
*/
{
  uint32_t  t_call = ecil_func_type_id(frame, rout->type, FALSE);

  if (gid != 0 && t_call != 0 && ecil_unsupported_reason == NULL) {
    ecil_rec_begin(frame, eop_call);
    ecil_arg_uint(frame, gid);
    ecil_arg_uint(frame, t_call);
    ecil_arg_uint(frame, nargs);
    ecil_arg_uint(frame, 0);
    ecil_rec_end(frame);
  }  /* if */
}  /* emit_call_record */


static void emit_cleanup(an_ecil_frame         *frame,
                         unsigned int          kind,
                         const a_destroy_target *tgt,
                         a_routine_ptr         dtor,
                         unsigned int          variant)
/*
CLEANUP_BEGIN(kind) <destructor call> CLEANUP_BODY: the destructor (complete
or base variant) runs when the enclosing scope region or full expression ends
(spec 11.5).
*/
{
  /* The elements of an array are complete objects (spec 11.4). */
  uint32_t  gid = variant == ECIL_VARIANT_BASE ?
                  ecil_function_base_id(frame, dtor) :
                  ecil_function_id(frame, dtor);

  if (gid != 0 && ecil_unsupported_reason == NULL) {
    ecil_rec_begin(frame, eop_cleanup_begin);
    ecil_arg_uint(frame, kind);
    if (kind == (unsigned int)ecl_exit) {
      ecil_arg_uint(frame, tgt->lid);       /* The object that is destroyed. */
    } else {
      ecil_arg_nil(frame);
    }  /* if */
    ecil_arg_uint(frame, 0);
    ecil_rec_end(frame);
    if (tgt->array_elem != 0) {
      if (tgt->kind == DT_LOCAL) {
        ecil_rec1(frame, eop_lref, tgt->lid);
      } else if (tgt->kind == DT_GLOBAL) {
        ecil_rec1(frame, eop_gref, tgt->lid);
      } else {
        emit_this_member(frame, tgt->lid, tgt->rec, tgt->index);
      }  /* if */
      ecil_rec2(frame, eop_array_dtor_begin, tgt->array_elem,
                ECIL_ARRAY_LOOP_FIXED);
      emit_call_record(frame, gid, dtor, 1);
      ecil_rec0(frame, eop_array_dtor_end);
    } else {
      emit_target_address(frame, tgt);
      emit_call_record(frame, gid, dtor, 1);
    }  /* if */
    ecil_rec0(frame, eop_cleanup_body);
  }  /* if */
}  /* emit_cleanup */


static void emit_local_cleanup(an_ecil_frame *frame,
                               uint32_t      lid,
                               a_type_ptr    type,
                               a_routine_ptr dtor)
/*
The cleanup of a local or temporary of the given type (kind all): an array
of objects is destroyed element by element.
*/
{
  a_destroy_target  tgt;

  memset(&tgt, 0, sizeof(tgt));
  tgt.kind = DT_LOCAL;
  tgt.lid = lid;
  tgt.array_elem = array_element_gid(frame, type);
  emit_cleanup(frame, (unsigned int)ecl_all, &tgt, dtor,
               ECIL_VARIANT_COMPLETE);
}  /* emit_local_cleanup */


static void flush_pending_cleanups(an_ecil_frame *frame, size_t from)
/*
Begin the cleanups of the lifetime-extended temporaries recorded since
"from" (the end of the statement that bound them), in construction order.
*/
{
  size_t  i;

  for (i = from; i < num_pending_cleanups; i++) {
    emit_local_cleanup(frame, pending_cleanups[i].lid,
                       pending_cleanups[i].type, pending_cleanups[i].dtor);
  }  /* for */
  num_pending_cleanups = from;
}  /* flush_pending_cleanups */


static uint32_t make_static_temp(an_ecil_frame *frame,
                                 uint32_t      t,
                                 a_variable_ptr owner)
/*
Introduce the static-duration object that holds a temporary which the
reference variable "owner" extends: an internal definition named _ZGR<the
name of the owner>[<seq-id>]_ (Itanium 5.2.4), constructed by the owner's
STATIC_INIT and registered for destruction at exit there.
*/
{
  char          text[1500];
  const char    *base;
  unsigned int  seq = static_temp_count++;
  uint32_t      gid = 0;

#if BACK_END_TYPE == BACK_END_TYPE_CIR
  if (owner->source_corresp.is_local_to_function) {
    base = cir_local_static_name(owner);
  } else if (variable_name_mangling_needed(owner)) {
    base = get_mangled_variable_name(owner);
  } else {
    base = owner->source_corresp.name;
  }  /* if */
#else /* BACK_END_TYPE != BACK_END_TYPE_CIR */
  base = owner->source_corresp.name;
#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */
  if (base == NULL) base = "";
  if (strncmp(base, "_Z", 2) == 0) {
    (void)snprintf(text, sizeof(text), "_ZGR%.1000s", base + 2);
  } else {
    (void)snprintf(text, sizeof(text), "_ZGR%lu%.1000s",
                   (unsigned long)strlen(base), base);
  }  /* if */
  if (seq != 0) {
    /* <seq-id> of the second temporary is 0, of the third 1, ... in base 36
       with digits and capital letters. */
    char          digits[16];
    size_t        n = 0;
    unsigned int  v = seq - 1;
    size_t        len = strlen(text);

    do {
      unsigned int  d = v % 36;

      digits[n++] = (char)(d < 10 ? '0' + d : 'A' + (d - 10));
      v /= 36;
    } while (v != 0 && n < sizeof(digits));
    while (n > 0 && len + 2 < sizeof(text)) text[len++] = digits[--n];
    text[len] = '\0';
  }  /* if */
  (void)strncat(text, "_", sizeof(text) - strlen(text) - 1);
  gid = ecil_new_gid(egk_var);
  if (gid != 0) {
    ecil_emit_decl_var(frame, gid, t, (unsigned int)elk_internal,
                       ECIL_DECL_VAR_DEFINITION,
                       (uint32_t)ecil_gid_info(t)->align,
                       ecil_intern_cstring(frame, text));
  }  /* if */
  return gid;
}  /* make_static_temp */


static a_boolean lifetime_needs_cleanups(an_object_lifetime_ptr lifetime)
/*
Return TRUE if the full-expression lifetime has a destruction that runs in
the stream: the destructions only for exceptions (freeing the storage of a
new, a partly built array) are not sent in v1 (spec 11.1).
*/
{
  a_boolean           needed = FALSE;
  a_dynamic_init_ptr  di;

  if (lifetime != NULL) {
    for (di = lifetime->destructions; di != NULL && !needed;
         di = di->next_in_destruction_list) {
      needed = di->destructor != NULL &&
               !di->is_freeing_of_storage_on_exception &&
               !di->destruction_is_for_partially_constructed_aggregate;
    }  /* for */
  }  /* if */
  return needed;
}  /* lifetime_needs_cleanups */


static a_boolean lifetime_has_cleanups(an_expr_node_ptr node)
/*
Return TRUE for an enk_object_lifetime whose full expression has temporaries
to destroy (it becomes a FULLEXPR).
*/
{
  node = skip_parens(node);
  return node->kind == (an_expr_node_kind)enk_object_lifetime &&
         lifetime_needs_cleanups(node->variant.object_lifetime.ptr);
}  /* lifetime_has_cleanups */


static a_boolean is_class_temp_object(an_expr_node_ptr node)
/*
Return TRUE if the node is a class temporary (enk_temp_init) or a variable,
possibly under a cv adjustment: its storage is the object a member function is
called on.
*/
{
  node = skip_parens(node);
  if (node->kind == (an_expr_node_kind)enk_operation &&
      node->variant.operation.kind == eok_class_rvalue_adjust) {
    node = skip_parens(node->variant.operation.operands);
  }  /* if */
  return !C_mode() && ((node->kind == (an_expr_node_kind)enk_temp_init ||
              node->kind == (an_expr_node_kind)enk_lambda) ||
                       node->kind == (an_expr_node_kind)enk_variable);
}  /* is_class_temp_object */


static a_boolean call_arg_is_variadic(an_expr_node_ptr callee,
                                      an_expr_node_ptr arg)
/*
Return TRUE if the argument is one of the variable arguments of a call
of a function with an ellipsis (its position is beyond the parameters).
*/
{
  a_type_ptr        ft = skip_typerefs(callee->type);
  a_param_type_ptr  ptp;
  an_expr_node_ptr  a;
  a_boolean         result = FALSE;

  if (ft->kind == tk_pointer) ft = skip_typerefs(ft->variant.pointer.type);
  if (ft->kind == tk_routine && rout_type_supp(ft)->has_ellipsis) {
    ptp = rout_type_supp(ft)->param_type_list;
    for (a = callee->next; a != NULL && a != arg; a = a->next) {
      if (ptp != NULL) ptp = ptp->next;
    }  /* for */
    result = a == arg && ptp == NULL;
  }  /* if */
  return result;
}  /* call_arg_is_variadic */


static void do_call(an_ecil_frame *frame, size_t idx)
/*
eok_call: callee (unless direct), arguments in order, then CALL.  The member
call operators (x.f(...) and p->f(...)) have the routine first and the object
as their first argument: its address (for x.f) is the object pointer.
*/
{
  a_code_item       *it = &code_stack[idx];
  an_expr_node_ptr  node = (an_expr_node_ptr)it->entry;
  an_expr_node_ptr  callee = skip_parens(node->variant.operation.operands);
  a_boolean         done = FALSE;

  if (it->phase == 20) {
    /* The second operand is on the stack above its temporary: store it. */
    ecil_rec_begin(frame, eop_store);
    ecil_arg_uint(frame, ECIL_STORE_INIT);
    ecil_arg_uint(frame, (unsigned int)esr_none);
    ecil_rec_end(frame);
    it->phase = 1;
  }  /* if */
  if (it->phase == 0) {
    a_routine_ptr direct = NULL;
    an_expr_node_ptr c = callee;
    an_expr_operator_kind
                  call_op = node->variant.operation.kind;
    a_boolean     is_member_call = call_op == eok_dot_member_call ||
                                   call_op == eok_points_to_member_call;

    /* Look through (*f), (&f) and parentheses to a named routine. */
    while (c != NULL &&
           c->kind == (an_expr_node_kind)enk_operation &&
           (c->variant.operation.kind == eok_indirect ||
            c->variant.operation.kind == eok_address_of ||
            c->variant.operation.kind == eok_parens ||
            c->variant.operation.kind == eok_dot_static ||
            c->variant.operation.kind == eok_points_to_static)) {
      if (c->variant.operation.kind == eok_dot_static ||
          c->variant.operation.kind == eok_points_to_static) {
        /* The object is evaluated and discarded: only when it has no side
           effects can the member be called directly. */
        if (!object_has_no_side_effects(c->variant.operation.operands)) break;
        c = skip_parens(c->variant.operation.operands->next);
      } else {
        c = skip_parens(c->variant.operation.operands);
      }  /* if */
    }  /* while */
    if (c != NULL && c->kind == (an_expr_node_kind)enk_routine &&
        node_routine(c) != NULL) {
      direct = node_routine(c);
    }  /* if */
    if (!C_mode() && node->variant.operation.eval_right_to_left &&
        callee->next != NULL) {
      /* C++17 evaluates the operands of an overloaded assignment (and of a
         reversed comparison) right to left.  With two operands the right
         one is evaluated into a temporary first when the order could be
         observed; with more, an operand with side effects is diagnosed. */
      an_expr_node_ptr  operand;
      unsigned int      impure = 0;

      for (operand = callee->next; operand != NULL; operand = operand->next) {
        if (!expression_is_pure(operand)) impure++;
      }  /* for */
      if (callee->next->next != NULL && callee->next->next->next == NULL) {
        if (!target_may_be_evaluated_first(callee->next,
                                           callee->next->next)) {
          if (ecil_class_is_nontrivial_abi(callee->next->next->type)) {
            ecil_note_unsupported("an overloaded assignment or reversed "
                                  "comparison whose right operand is a "
                                  "class that is not trivially copyable and "
                                  "must be evaluated first");
          } else {
            it->c |= CALL_REVERSE;
          }  /* if */
        }  /* if */
      } else if (impure > 0) {
        ecil_note_unsupported("an overloaded assignment or reversed "
                              "comparison whose operands have side effects");
      }  /* if */
    }  /* if */
    if (is_member_call) {
      if (node->variant.operation.is_virtual_call) {
        ecil_note_unsupported("a virtual call");
      } else if (direct == NULL || callee->next == NULL ||
                 !routine_type_is_nonstatic_member_function(direct->type)) {
        ecil_note_unsupported("a member call of a static or unknown "
                              "member function");
      } else {
        it->c |= (call_op == eok_dot_member_call) ? CALL_MEMBER_DOT :
                                                    CALL_MEMBER_PTR;
      }  /* if */
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
    } else if (direct != NULL && direct->source_corresp.name != NULL &&
               strcmp(direct->source_corresp.name,
                      "__builtin_is_constant_evaluated") == 0) {
      /* Code that runs is not being evaluated as a constant expression. */
      uint32_t  t = ecil_type_id(frame, node->type);

      if (t != 0) {
        ecil_rec2(frame, eop_const_int, t, 0);
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
    if (!done && ecil_unsupported_reason == NULL &&
        (it->c & CALL_REVERSE) != 0) {
      /* <temporary> <second operand> STORE, before the first operand. */
      an_expr_node_ptr  second = callee->next->next;
      uint32_t          vt = ecil_type_id(frame, second->type);

      if (vt == 0 || it->b != 0 || direct == NULL ||
          next_lid >= (1u << 24)) {
        ecil_note_unsupported("an overloaded assignment or reversed "
                              "comparison whose operands have side effects");
      } else {
        uint32_t  tmp = new_lid();

        emit_local(frame, tmp, vt, 0, 0);
        ecil_rec1(frame, eop_lref, tmp);
        it = &code_stack[idx];
        it->c |= tmp << 8;
        it->phase = 20;
        push_expr(second, 0);
        done = TRUE;
      }  /* if */
    } else if (!done && ecil_unsupported_reason == NULL && direct == NULL) {
      push_expr(callee, 0);
      done = TRUE;
    }  /* if */
  }  /* if */
  if (!done && ecil_unsupported_reason == NULL &&
      (code_stack[idx].c & CALL_ARG_LOAD_PENDING) != 0) {
    /* A by-value argument of a NONTRIVIAL_ABI class is the load of the
       storage that holds it (spec 11.6). */
    code_stack[idx].c &= ~CALL_ARG_LOAD_PENDING;
    ecil_rec_begin(frame, eop_load);
    ecil_arg_uint(frame, 0);
    ecil_rec_end(frame);
  }  /* if */
  if (!done && ecil_unsupported_reason == NULL &&
      (code_stack[idx].c & CALL_OBJECT_PENDING) != 0) {
    /* The object operand is complete: pass its address. */
    an_expr_node_ptr  object = skip_parens(callee->next);

    it = &code_stack[idx];
    it->c &= ~CALL_OBJECT_PENDING;
    if ((it->c & CALL_MEMBER_DOT) != 0) {
      if (!is_glvalue_node(object) && !is_class_temp_object(object)) {
        uint32_t  ot = ecil_type_id(frame, object->type);

        if (ecil_class_has_nontrivial_dtor(object->type)) {
          ecil_note_unsupported("a temporary with a destructor");
        } else if (ot != 0) {
          uint32_t  temp = new_lid();

          emit_materialize_temp(frame, temp, ot);
        }  /* if */
      }  /* if */
      if (ecil_unsupported_reason == NULL) ecil_rec0(frame, eop_addr);
    }  /* if */
  }  /* if */
  if (done) {
    /* The item was completed, or the callee operand was pushed. */
  } else if (ecil_unsupported_reason == NULL && it->cursor != NULL) {
    an_expr_node_ptr arg = (an_expr_node_ptr)it->cursor;

    if ((it->c & CALL_REVERSE) != 0 && arg == callee->next->next) {
      /* The second operand was evaluated first: read it back. */
      it->cursor = arg->next;
      ecil_rec1(frame, eop_lref, it->c >> 8);
      ecil_rec1(frame, eop_load, 0);
      done = TRUE;
    } else {
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
      if (arg == callee->next && (code_stack[idx].c &
                                  (CALL_MEMBER_DOT | CALL_MEMBER_PTR)) != 0) {
        code_stack[idx].c |= CALL_OBJECT_PENDING;
      } else if (!C_mode() && ecil_class_is_nontrivial_abi(arg->type) &&
                 call_arg_is_variadic(callee, arg)) {
        /* Passing such a class through "..." is conditionally supported. */
        ecil_note_unsupported("a class that is not trivially copyable passed "
                              "through an ellipsis");
      } else if (is_glvalue_node(skip_parens(arg)) &&
                 ecil_class_is_nontrivial_abi(arg->type)) {
        code_stack[idx].c |= CALL_ARG_LOAD_PENDING;
      }  /* if */
      push_expr(arg, (code_stack[idx].c & CALL_OBJECT_PENDING) != 0 &&
                     (code_stack[idx].c & CALL_MEMBER_DOT) != 0 &&
                     is_class_temp_object(arg) ? CIF_GLV : 0u);
    }  /* if */
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
      if (arg == callee->next && (it->c & (CALL_MEMBER_DOT |
                                           CALL_MEMBER_PTR)) != 0) {
        /* The object: its address is passed, whatever its class. */
      } else if (at != 0 && (info_of(at)->kind == egk_opaque ||
                      (info_of(at)->kind == egk_record &&
                       (info_of(at)->flags & ECIL_GF_ABI_OPAQUE)))) {
        ecil_note_unsupported("a call with an argument of unsupported type");
      }  /* if */
    }  /* for */
    result_gid = ecil_type_id(frame, node->type);
    if (ecil_class_is_nontrivial_abi(node->type) &&
        (it->flags & CIF_DEST) == 0) {
      /* Such a value is built in a destination the parent provides (spec
         11.6); an unused one is dropped, but only without a destructor. */
      if (!node->result_is_not_used && (it->flags & CIF_UNUSED) == 0) {
        ecil_note_unsupported("a class value that is not trivially "
                              "copyable used other than to initialize an "
                              "object");
      } else if (ecil_class_has_nontrivial_dtor(node->type)) {
        ecil_note_unsupported("a temporary with a destructor");
      }  /* if */
    }  /* if */
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


static unsigned int store_result_kind(an_expr_node_ptr node)
/*
What an assignment leaves on the stack when its value is used: the stored
value (a prvalue), or in C++, where the assignment is an lvalue, its target
if the node is used as an lvalue.
*/
{
  return (!C_mode() && is_glvalue_node(node)) ? (unsigned int)esr_glv :
                                                (unsigned int)esr_prv;
}  /* store_result_kind */


static void do_compound_assignment(an_ecil_frame *frame, size_t idx)
/*
Compound assignment is expanded here so that cast classification stays in
one place:  <lhs glv> DUP LOAD cast <rhs> cast <op> cast STORE.  In C++17 the
right operand is evaluated first: if it has side effects its value is kept in
a temporary, which is then read after the target's value (phases 10 and 11).
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
  a_boolean         finish = FALSE;

  if (ecil_unsupported_reason != NULL || tl == 0 || tr == 0) {
    code_stack_size--;
  } else if (it->phase == 0) {
    if (!C_mode() && node->variant.operation.eval_right_to_left &&
        !(expression_is_pure(lhs) && expression_is_pure(rhs))) {
      /* The target's value is read before the right operand here, and the
         right operand may change it (b |= f(b)). */
      uint32_t  tmp = new_lid();

      emit_local(frame, tmp, tr, 0, 0);
      it->b = tmp;
      it->phase = 10;
      ecil_rec1(frame, eop_lref, tmp);
      push_expr(rhs, 0);
    } else {
      it->phase = 1;
      push_expr(lhs, 0);
    }  /* if */
  } else if (it->phase == 10) {
    ecil_rec_begin(frame, eop_store);
    ecil_arg_uint(frame, ECIL_STORE_INIT);
    ecil_arg_uint(frame, (unsigned int)esr_none);
    ecil_rec_end(frame);
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
    if (it->b != 0) {
      /* The right operand's value was computed first. */
      ecil_rec1(frame, eop_lref, it->b);
      ecil_rec1(frame, eop_load, 0);
      finish = TRUE;
    } else {
      it->phase = 2;
      push_expr(rhs, 0);
    }  /* if */
  } else {
    finish = TRUE;
  }  /* if */
  if (finish) {
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
                           store_result_kind(node));
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

  if (op == eok_call || op == eok_dot_member_call ||
      op == eok_points_to_member_call) {
    do_call(frame, idx);
  } else if (is_assignment_operator(op) && op != eok_assign) {
    do_compound_assignment(frame, idx);
  } else if (op == eok_dot_static || op == eok_points_to_static) {
    if (op2 == NULL) {
      ecil_note_unsupported("a static member selected without a member");
      code_stack_size--;
    } else if (!object_has_no_side_effects(op1) && it->phase == 0) {
      /* The object expression is evaluated first, and its value dropped. */
      it->phase = 1;
      push_expr(op1, CIF_UNUSED);
    } else {
      if (it->phase == 1 && value_is_pushed(op1, TRUE)) {
        ecil_rec0(frame, eop_drop);
      }  /* if */
      it->phase = 0;
      it->entry = op2;               /* The member itself. */
    }  /* if */
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
        push_expr(op1, CIF_UNUSED);
        break;
      case eok_not:
        push_expr(op1, CIF_COND);
        break;
      case eok_dot_field:
        /* A member of a class temporary is read from the temporary's
           storage (a record prvalue is never taken apart: spec 11.6). */
        push_expr(op1, is_class_temp_object(op1) ? CIF_GLV : 0u);
        break;
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
      case eok_lvalue_cast:
      case eok_ref_cast:
      case eok_ref_indirect:
      case eok_reference_to:
      case eok_base_class_cast:
      case eok_derived_class_cast:
      case eok_dot_vacuous_destructor_call:
      case eok_points_to_vacuous_destructor_call:
        push_expr(op1, ((op == eok_cast ||
                         op == eok_dot_vacuous_destructor_call ||
                         op == eok_points_to_vacuous_destructor_call) &&
                        is_void_node_type(node)) ? CIF_UNUSED : 0u);
        break;
      case eok_class_rvalue_adjust:
        push_expr(op1, it->flags & CIF_GLV);
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
        if (!C_mode() && node->variant.operation.eval_right_to_left &&
            !target_may_be_evaluated_first(op1, op2) &&
            ecil_class_is_nontrivial_abi(op1->type)) {
          /* The value (a temporary with a destructor, say) cannot be kept in
             a temporary of the assignment. */
          ecil_note_unsupported("an assignment of a class that is not "
                                "trivially copyable whose operands must be "
                                "evaluated in the other order");
        } else if (!C_mode() && node->variant.operation.eval_right_to_left &&
            !target_may_be_evaluated_first(op1, op2) &&
            skip_typerefs(op1->type)->kind != tk_array &&
            ecil_type_id(frame, op2->type) != 0) {
          /* C++17: the right operand is evaluated before the left one, which
             has side effects: keep the value in a temporary. */
          uint32_t  tmp = new_lid();

          emit_local(frame, tmp, ecil_type_id(frame, op2->type), 0, 0);
          it->a = tmp;
          it->phase = ASSIGN_RHS_FIRST;
          ecil_rec1(frame, eop_lref, tmp);
          push_expr(op2, 0);
        } else {
          push_expr(op2, 0);
          push_expr(op1, 0);
        }  /* if */
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
        { a_boolean  lvalue = !is_void_node_type(node) &&
                              is_glvalue_node(node);
          /* A class conditional that builds its value in a variable (its
             arms are temporaries with a master entry) is a void TERN. */
          a_boolean  master_arms = !lvalue && op2 != NULL &&
                  skip_parens(op2)->kind == (an_expr_node_kind)enk_temp_init &&
                  skip_parens(op2)->variant.init.dynamic_init != NULL &&
                  skip_parens(op2)->variant.init.dynamic_init->master_entry !=
                                                                      NULL;
          a_boolean  unused_class = !lvalue &&
                                    ((node->result_is_not_used &&
                                      ecil_class_is_nontrivial_abi(
                                                              node->type)) ||
                                     master_arms);

          if (it->phase == 1) {
            t = (is_void_node_type(node) || unused_class) ? 0 :
                ecil_type_id(frame, node->type);
            if (lvalue && t != 0) {
              /* The arms are glvalues: the conditional yields an address
                 (spec 8.9 allows one value per arm). */
              if (node_is_bit_field(op2) || node_is_bit_field(op2->next)) {
                ecil_note_unsupported("a conditional lvalue with a "
                                      "bit-field operand");
              } else if (!is_glvalue_node(skip_parens(op2)) ||
                         !is_glvalue_node(skip_parens(op2->next))) {
                ecil_note_unsupported("a conditional lvalue with an operand "
                                      "that is not an lvalue");
              } else {
                t = ecil_ptr_type(frame, t);
              }  /* if */
            } else if (!is_void_node_type(node) && !unused_class &&
                       ecil_class_is_nontrivial_abi(node->type)) {
              ecil_note_unsupported("a conditional expression of a class "
                                    "that is not trivially copyable");
            }  /* if */
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
            if (lvalue) ecil_rec0(frame, eop_addr);
            ecil_rec0(frame, eop_tern_else);
            it->phase = 3;
            push_expr(op2->next, 0);
            done = FALSE;
          } else {
            if (lvalue) ecil_rec0(frame, eop_addr);
            ecil_rec0(frame, eop_tern_end);
            if (lvalue) {
              ecil_rec0(frame, eop_deref);
            } else {
              finish_value(frame, flags);
            }  /* if */
          }  /* if */
        }
        break;
      case eok_comma:
        if (it->phase == 1) {
          if (value_is_pushed(op1, TRUE)) ecil_rec0(frame, eop_drop);
          it->phase = 2;
          push_expr(op2, (node->result_is_not_used ||
                          (it->flags & CIF_UNUSED) != 0) ? CIF_UNUSED : 0u);
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

              emit_materialize_temp(frame, temp, array_gid);
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
          a_field_ptr       field = node_field(field_node);
          unsigned int      depth = 0;
          a_field_ptr       path[MAX_ANON_DEPTH];
          a_type_ptr        owners[MAX_ANON_DEPTH];
          a_boolean         use_extract = FALSE;
          a_boolean         ok;

          if (op == eok_points_to_field) {
            ecil_rec0(frame, eop_deref);
          } else if (!(is_glvalue_node(base) || is_class_temp_object(base))) {
            use_extract = TRUE;
          }  /* if */
          /* A member of an anonymous union or struct is selected through the
             unnamed member that holds it. */
          ok = C_mode() ||
               anonymous_member_path(rt, field, path, owners, &depth);
          while (ok && depth > 0) {
            uint32_t  orec = ecil_type_id(frame, owners[depth - 1]);

            depth--;
            if (orec == 0 || ecil_gid_of(path[depth]) == 0) {
              ecil_note_unsupported("a member of an incomplete type");
              ok = FALSE;
            } else {
              ecil_rec2(frame, use_extract ? eop_extract : eop_field, orec,
                        ecil_gid_of(path[depth]) - 1);
            }  /* if */
          }  /* while */
          if (ok && field_index(frame, C_mode() ? rt :
                                       skip_typerefs(parent_class_of(field)),
                                field, &rec, &index)) {
            ecil_rec2(frame, use_extract ? eop_extract : eop_field, rec,
                      index);
            if (!use_extract) emit_load_if_value(frame, node);
          }  /* if */
        }
        finish_value(frame, flags);
        break;
      case eok_ref_indirect:
        /* The operand is the reference (a pointer value): the referent is
           its target.  A reference to a function that is converted to a
           pointer to it (function-to-pointer conversion) is that pointer. */
        { a_type_ptr  rt = skip_typerefs(op1->type);

          if (skip_typerefs(node->type)->kind == tk_pointer &&
              rt->kind == tk_pointer &&
              skip_typerefs(rt->variant.pointer.type)->kind == tk_routine) {
            /* The pointer value of the reference is the result. */
          } else {
            ecil_rec0(frame, eop_deref);
            emit_load_if_value(frame, node);
          }  /* if */
        }
        finish_value(frame, flags);
        break;
      case eok_reference_to:
        /* Bind a reference: the address of the operand (a temporary is
           materialized first). */
        { an_expr_node_ptr  target = skip_parens(op1);

          if (target->kind == (an_expr_node_kind)enk_routine) {
            /* A reference to a function is the function. */
          } else if (!is_glvalue_node(target)) {
            uint32_t  tt = ecil_type_id(frame, target->type);

            if (ecil_class_has_nontrivial_dtor(target->type)) {
              ecil_note_unsupported("a temporary with a destructor");
            } else if (tt != 0) {
              uint32_t  temp = new_lid();

              emit_materialize_temp(frame, temp, tt);
              ecil_rec0(frame, eop_addr);
            }  /* if */
          } else {
            ecil_rec0(frame, eop_addr);
          }  /* if */
        }
        finish_value(frame, flags);
        break;
      case eok_base_class_cast:
      case eok_derived_class_cast:
        emit_class_cast(frame, node);
        finish_value(frame, flags);
        break;
      case eok_class_rvalue_adjust:
        /* Only the cv-qualification of the class prvalue changes (the
           operand may be the lvalue of a const object: its value). */
        if (is_glvalue_node(skip_parens(op1)) && !is_glvalue_node(node) &&
            (it->flags & CIF_GLV) == 0) {
          ecil_rec_begin(frame, eop_load);
          ecil_arg_uint(frame, access_is_volatile(op1) ?
                               ECIL_ACCESS_VOLATILE : 0u);
          ecil_rec_end(frame);
        }  /* if */
        break;
      case eok_dot_vacuous_destructor_call:
      case eok_points_to_vacuous_destructor_call:
        /* The "destructor" of a type without one: the object is evaluated. */
        if (value_is_pushed(op1, TRUE)) ecil_rec0(frame, eop_drop);
        break;
      case eok_lvalue_cast:
      case eok_ref_cast:
        /* The same object seen as another type: its address, converted. */
        { uint32_t  dst = ecil_type_id(frame, node->type);
          uint32_t  src = ecil_type_id(frame, op1->type);

          if (!is_glvalue_node(op1)) {
            ecil_note_unsupported("a reference cast of a value");
          } else if (dst != 0 && src != 0) {
            if (src != dst) {
              uint32_t  pt = ecil_ptr_type(frame, dst);

              ecil_rec0(frame, eop_addr);
              if (pt != 0) emit_cast(frame, (unsigned int)eck_bitcast, pt);
              ecil_rec0(frame, eop_deref);
            }  /* if */
            emit_load_if_value(frame, node);
            finish_value(frame, flags);
          }  /* if */
        }
        break;
      case eok_cast:
      case eok_bool_cast:
      case eok_lvalue_adjust:
        { uint32_t  dst = ecil_type_id(frame, node->type);
          uint32_t  src = ecil_type_id(frame, op1->type);

          if (is_void_node_type(node)) {
            if (value_is_pushed(op1, TRUE)) ecil_rec0(frame, eop_drop);
          } else if (dst != 0 && src != 0) {
            if (op == eok_bool_cast) {
              if (info_of(src)->kind != egk_bool) {
                ecil_rec0(frame, eop_to_bool);
              }  /* if */
            } else {
              emit_convert(frame, src, dst);
              if (op == eok_lvalue_adjust &&
                  is_glvalue_node(skip_parens(op1)) &&
                  (flags & CIF_GLV) == 0) {
                /* The operand is the lvalue of the object (a const one,
                   say); this node is its value. */
                emit_load_if_value(frame, node);
              }  /* if */
            }  /* if */
            if (op != eok_bool_cast || C_mode()) finish_value(frame, flags);
          }  /* if */
        }
        break;
      case eok_assign:
        if (it->phase == ASSIGN_RHS_FIRST) {
          /* The value is evaluated: store it in the temporary, then
             designate the target. */
          ecil_rec_begin(frame, eop_store);
          ecil_arg_uint(frame, ECIL_STORE_INIT);
          ecil_arg_uint(frame, (unsigned int)esr_none);
          ecil_rec_end(frame);
          it->phase = ASSIGN_LHS_NEXT;
          push_expr(op1, 0);
          done = FALSE;
          break;
        }  /* if */
        if (it->phase == ASSIGN_LHS_NEXT) {
          ecil_rec1(frame, eop_lref, it->a);
          ecil_rec1(frame, eop_load, 0);
        }  /* if */
        { unsigned int  dsize = class_dsize_flag(frame, op1->type);

          if (dsize != 0 && !node->result_is_not_used) {
            ecil_note_unsupported("the value of an assignment to a class "
                                  "with reusable tail padding");
          } else if (dsize != 0 && !source_is_load(op2) &&
                     it->phase != ASSIGN_LHS_NEXT) {
            /* The value is computed: put it in a temporary so that only
               its data size is copied (the destination may be a base
               subobject whose tail padding holds other members). */
            uint32_t  tt = ecil_type_id(frame, op1->type);
            uint32_t  temp = new_lid();

            if (tt != 0) {
              emit_materialize_temp(frame, temp, tt);
              ecil_rec1(frame, eop_load, 0);
            }  /* if */
          }  /* if */
          if (ecil_unsupported_reason == NULL) {
            ecil_rec_begin(frame, eop_store);
            ecil_arg_uint(frame, (access_is_volatile(op1) ?
                                  ECIL_ACCESS_VOLATILE : 0u) | dsize);
            ecil_arg_uint(frame, node->result_is_not_used ?
                                 (unsigned int)esr_none :
                                 store_result_kind(node));
            ecil_rec_end(frame);
            finish_value(frame, flags);
          }  /* if */
        }
        break;
      case eok_pre_incr:
      case eok_pre_decr:
      case eok_post_incr:
      case eok_post_decr:
        { uint32_t        ot = ecil_type_id(frame, op1->type);
          unsigned int    kind;
          unsigned int    iflags = 0;
          a_boolean       lvalue_result;

          kind = (op == eok_pre_incr) ? (unsigned int)eidk_pre_inc :
                 (op == eok_pre_decr) ? (unsigned int)eidk_pre_dec :
                 (op == eok_post_incr) ? (unsigned int)eidk_post_inc :
                                         (unsigned int)eidk_post_dec;
          if (ot != 0 && info_of(ot)->kind == egk_bool) {
            ecil_note_unsupported("++/-- on _Bool");
          } else if (ot != 0) {
            if (arith_flags(ot) != 0) iflags |= ECIL_INC_DEC_NSW;
            if (access_is_volatile(op1)) iflags |= ECIL_INC_DEC_VOLATILE;
            lvalue_result = is_glvalue_node(node) &&
                            (flags & (CIF_UNUSED | CIF_DROP)) == 0;
            if (lvalue_result) {
              /* C++ ++x and --x are lvalues: the object's address stays. */
              ecil_rec0(frame, eop_dup);
            }  /* if */
            ecil_rec_begin(frame, eop_inc_dec);
            ecil_arg_uint(frame, kind);
            ecil_arg_uint(frame, iflags);
            ecil_rec_end(frame);
            if (lvalue_result) ecil_rec0(frame, eop_drop);
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


static a_boolean ctor_needs_zeroing(a_dynamic_init_ptr di)
/*
Return TRUE for a value-initialization by a constructor that the class does
not provide, of a class with members that need zero initialization: the
storage is zeroed before the constructor runs (the front end's
need_zeroing_for_value_initialization).
*/
{
  a_routine_ptr  ctor = di->variant.constructor.ptr;

  return !C_mode() && di->kind == (a_dynamic_init_kind)dik_constructor &&
         ctor != NULL && di->variant.constructor.value_initialization &&
         (ctor->compiler_generated ||
          (ctor->is_defaulted && !ctor->defined_outside_of_parent)) &&
         parent_class_of(ctor)->variant.class_struct_union.
                                                    has_zero_init_component;
}  /* ctor_needs_zeroing */


static void emit_zero_object(an_ecil_frame *frame, uint32_t gid)
/*
Zero the object whose glvalue is on top of the stack (consumed).  A record or
array is zeroed byte by byte, so that its padding is zero too
(value-initialization zero-initializes the object representation).
*/
{
  if (gid != 0 && !C_mode() && (info_of(gid)->kind == egk_record ||
                                info_of(gid)->kind == egk_array) &&
      info_of(gid)->size != 0) {
    ecil_rec1(frame, eop_zero_bytes, info_of(gid)->size);
  } else {
    ecil_rec0(frame, eop_init_zero);
  }  /* if */
}  /* emit_zero_object */


static void emit_zero_pointee(an_ecil_frame *frame, a_type_ptr type,
                              a_boolean subobject)
/*
The object pointer on top of the stack stays; its pointee is zeroed first
(only the data size of a base subobject).
*/
{
  ecil_rec0(frame, eop_dup);
  ecil_rec0(frame, eop_deref);
  if (subobject) {
    ecil_rec1(frame, eop_zero_bytes, (uint64_t)ecil_data_size_of_type(
                                            skip_typerefs(type)));
  } else {
    emit_zero_object(frame, ecil_type_id(frame, type));
  }  /* if */
}  /* emit_zero_pointee */


static void setup_ctor_call(an_ecil_frame *frame,
                            a_code_item   *it,
                            a_dynamic_init_ptr di,
                            unsigned int  variant,
                            unsigned int  extra);

/* new and delete (spec 11.8). */

static a_routine_ptr default_operator_routine(an_opname_kind kind,
                                              a_boolean      is_new)
/*
The global one-parameter operator new, new[], delete or delete[] (the front
end leaves the routine implicit for array new of a class with a constructor
and for delete of a class with a destructor).
*/
{
  a_routine_ptr                   result = NULL;
  a_symbol_ptr                    sym = opname_function_symbol(kind);

  if (sym != NULL) {
    if (is_new) {
      a_boolean  ambiguous = FALSE;

      sym = find_default_operator_new_sym(sym, &ambiguous);
      if (sym != NULL && is_function_symbol(sym)) {
        result = sym->variant.routine.ptr;
      }  /* if */
    } else {
      an_overload_set_traversal_block  ostblock;
      a_symbol_ptr                     s2;

      for (s2 = set_up_overload_set_traversal_simple(sym, &ostblock);
           s2 != NULL && result == NULL;
           s2 = next_symbol_in_overload_set(&ostblock)) {
        if (is_function_symbol(s2)) {
          a_routine_ptr  r = s2->variant.routine.ptr;
          a_param_type_ptr
                         ptp = skip_typerefs(r->type)->variant.routine.
                                               extra_info->param_type_list;

          if (ptp != NULL && ptp->next == NULL &&
              !r->source_corresp.is_class_member) {
            result = r;
          }  /* if */
        }  /* if */
      }  /* for */
    }  /* if */
  }  /* if */
  return result;
}  /* default_operator_routine */


static a_type_ptr innermost_element_type(a_type_ptr type, uint64_t *total)
/*
The element type of an array of (arrays of) T, and the number of elements of
all the dimensions together (0 if a bound is unknown).
*/
{
  uint64_t  n = 1;

  type = skip_typerefs(type);
  while (type->kind == tk_array) {
    if (type->variant.array.is_vla ||
        type->variant.array.bound_is_zero) {
      n = 0;
    } else {
      n *= (uint64_t)type->variant.array.variant.number_of_elements;
    }  /* if */
    type = skip_typerefs(type->variant.array.element_type);
  }  /* while */
  if (total != NULL) *total = n;
  return type;
}  /* innermost_element_type */


static uint64_t new_inner_bounds(a_type_ptr type)
/*
For a new[] with a run-time count, the product of the constant bounds of the
dimensions after the first one (the front end keeps only the first bound in
number_of_elements: T[n][K] is T[][K]); 0 if one of them is not constant.
*/
{
  uint64_t  n = 1;

  type = skip_typerefs(type);
  if (type->kind == tk_array) {
    type = skip_typerefs(type->variant.array.element_type);
    while (n != 0 && type->kind == tk_array) {
      if (type->variant.array.is_vla || type->variant.array.bound_is_zero) {
        n = 0;
      } else {
        n *= (uint64_t)type->variant.array.variant.number_of_elements;
      }  /* if */
      type = skip_typerefs(type->variant.array.element_type);
    }  /* while */
  }  /* if */
  return n;
}  /* new_inner_bounds */


static a_routine_ptr new_array_default_ctor(a_dynamic_init_ptr di,
                                            a_dynamic_init_ptr *ctor_di)
/*
The constructor that default-constructs every element of a new[] (one
repeated constructor call without arguments), or NULL; *ctor_di is its
initialization.
*/
{
  a_routine_ptr   ctor = NULL;
  a_constant_ptr  agg = NULL;

  if (di != NULL &&
      di->kind == (a_dynamic_init_kind)dik_nonconstant_aggregate) {
    agg = di->variant.constant.ptr;
  }  /* if */
  if (agg != NULL && agg->kind == (a_constant_repr_kind)ck_aggregate &&
      agg->variant.aggregate.first_constant != NULL &&
      agg->variant.aggregate.first_constant ==
                                      agg->variant.aggregate.last_constant &&
      agg->variant.aggregate.first_constant->kind ==
                                        (a_constant_repr_kind)ck_init_repeat) {
    a_constant_ptr  rc = agg->variant.aggregate.first_constant->
                                               variant.init_repeat.constant;

    if (rc->kind == (a_constant_repr_kind)ck_dynamic_init &&
        rc->variant.dynamic_init.ptr->kind ==
                                (a_dynamic_init_kind)dik_constructor &&
        rc->variant.dynamic_init.ptr->variant.constructor.args == NULL) {
      ctor = rc->variant.dynamic_init.ptr->variant.constructor.ptr;
      *ctor_di = rc->variant.dynamic_init.ptr;
    }  /* if */
  }  /* if */
  return ctor;
}  /* new_array_default_ctor */


static a_routine_ptr new_routine_of(a_new_delete_supplement_ptr nds,
                                    a_boolean                   array)
/*
The allocation function a new expression calls: the routine the front end
recorded, else the one it folded into the constructor (the class's own
operator new, when it has one), else the global one.
*/
{
  a_routine_ptr  rout = nds->routine;

#if NEW_CAN_BE_FOLDED_INTO_CTOR
  if (rout == NULL && nds->type != NULL && !array &&
      is_immediate_class_type(skip_typerefs(nds->type))) {
    rout = class_type_supp(skip_typerefs(nds->type))->
                                                  assoc_operator_new_routine;
  }  /* if */
#endif /* NEW_CAN_BE_FOLDED_INTO_CTOR */
  if (rout == NULL) {
    rout = default_operator_routine(array ? (an_opname_kind)onk_array_new :
                                            (an_opname_kind)onk_new, TRUE);
  }  /* if */
  return rout;
}  /* new_routine_of */


static a_routine_ptr delete_routine_of(a_new_delete_supplement_ptr nds)
/*
The operator delete a delete expression calls: the routine the front end
recorded, else the one it folded into the destructor (a class-specific
operator delete), else the global one.
*/
{
  a_routine_ptr  del = nds->routine;
  a_type_ptr     cls = nds->type != NULL ? skip_typerefs(nds->type) : NULL;

  if (del == NULL && cls != NULL && !nds->array_delete &&
      is_immediate_class_type(cls)) {
    del = class_type_supp(cls)->assoc_operator_delete_routine;
  }  /* if */
  if (del == NULL) {
    del = default_operator_routine(nds->array_delete ?
                                   (an_opname_kind)onk_array_delete :
                                   (an_opname_kind)onk_delete, FALSE);
  }  /* if */
  return del;
}  /* delete_routine_of */


static void do_delete(an_ecil_frame *frame, size_t idx)
/*
A delete expression: <pointer> DELETE(operator delete, destructor, type,
flags) (null check, destructor and operator delete are the consumer's).
*/
{
  a_code_item                *it = &code_stack[idx];
  an_expr_node_ptr           node = (an_expr_node_ptr)it->entry;
  a_new_delete_supplement_ptr nds = node->variant.new_delete;

  if (it->phase == 0) {
    a_routine_ptr  del = delete_routine_of(nds);
    a_boolean      sized = FALSE;
    a_boolean      aligned = FALSE;
    a_boolean      destroying = FALSE;

    if (del == NULL || nds->arg == NULL) {
      ecil_note_unsupported("a delete without an operator delete");
    } else if (is_default_operator_delete(del, &sized, &aligned,
                                          &destroying) && destroying) {
      ecil_note_unsupported("a destroying operator delete");
    } else {
      it->a = (uint32_t)(nds->array_delete ? ECIL_DELETE_ARRAY : 0u) |
              (sized ? ECIL_DELETE_SIZED : 0u) |
              (aligned ? ECIL_DELETE_ALIGNED : 0u);
      it->phase = 1;
      push_expr(nds->arg, 0);
    }  /* if */
  } else {
    a_routine_ptr  del = delete_routine_of(nds);
    a_type_ptr     obj = innermost_element_type(nds->type, NULL);
    a_routine_ptr  dtor = nds->dynamic_init != NULL ?
                          nds->dynamic_init->destructor : NULL;
    uint32_t       tg = ecil_type_id(frame, obj);
    uint32_t       gd = ecil_function_id(frame, del);
    uint32_t       gdt = 0;

    if (dtor != NULL && dtor->is_virtual) {
      ecil_note_unsupported("the delete of an object with a virtual "
                            "destructor");
    } else if (dtor != NULL) {
      gdt = ecil_function_id(frame, dtor);
    }  /* if */
    if (ecil_unsupported_reason == NULL && tg != 0 && gd != 0) {
      a_type_ptr  at = skip_typerefs(nds->arg->type);

      if (at->kind == tk_pointer &&
          skip_typerefs(at->variant.pointer.type)->kind == tk_array) {
        /* delete[] of a pointer to an array (a multi-dimensional new[]):
           the operand is a pointer to the innermost element. */
        uint32_t  ep = ecil_ptr_type(frame, tg);

        if (ep != 0) emit_cast(frame, (unsigned int)eck_bitcast, ep);
      }  /* if */
      ecil_rec_begin(frame, eop_delete);
      ecil_arg_uint(frame, gd);
      if (gdt != 0) {
        ecil_arg_uint(frame, gdt);
      } else {
        ecil_arg_nil(frame);
      }  /* if */
      ecil_arg_uint(frame, tg);
      ecil_arg_uint(frame, it->a);
      ecil_rec_end(frame);
    }  /* if */
    code_stack_size--;
  }  /* if */
}  /* do_delete */


static void new_push_count(an_ecil_frame *frame, a_code_item *it)
/*
Push the element count of a new[] (size_t): the local that holds it, or the
constant bound.
*/
{
  uint32_t  sizet = ecil_target_uintptr_type(frame);

  if ((it->c & NEW_DYNAMIC) != 0) {
    ecil_rec1(frame, eop_lref, it->a);
    ecil_rec1(frame, eop_load, 0);
  } else {
    ecil_rec2(frame, eop_const_int, sizet, it->b);
  }  /* if */
}  /* new_push_count */


static void do_new_delete(an_ecil_frame *frame, size_t idx)
/*
cik_new: a delete (do_delete), or a new expression.  Its value is the
pointer.  Phases: 0 analyze (and the count of a new[] into a local), 1 convert
and store the count, 2 push the size, 3 push the placement operands and CALL
the allocation function, 4 cast (cookie) and the initialization.  The pointer
is duplicated for the constructor or the initializer (spec 11.8).
*/
{
  a_code_item                 *it = &code_stack[idx];
  an_expr_node_ptr            node = (an_expr_node_ptr)it->entry;
  a_new_delete_supplement_ptr nds = node->variant.new_delete;

  if (!nds->is_new) {
    do_delete(frame, idx);
  } else if (it->phase == 0) {
    a_type_ptr      type = nds->type;
    a_boolean       array = skip_typerefs(type)->kind == tk_array;
    a_routine_ptr   routine = new_routine_of(nds, array);
    uint64_t        total = 1;
    a_type_ptr      elem = array ? innermost_element_type(type, &total) : type;
    uint32_t        sizet = ecil_target_uintptr_type(frame);

    if (routine == NULL || elem == NULL) {
      ecil_note_unsupported("a new without an allocation function");
    } else if (routine->never_throws &&
               !(!routine->source_corresp.is_class_member &&
                 skip_typerefs(routine->type)->variant.routine.extra_info->
                                               param_type_list->next != NULL &&
                 is_void_star_type(skip_typerefs(routine->type)->variant.
                          routine.extra_info->param_type_list->next->type))) {
      /* The allocation can fail by returning null: the construction is
         skipped then (not supported). */
      ecil_note_unsupported("a new with a non-throwing allocation function");
    } else {
      uint32_t  cookie = 0;

      if (array && new_or_delete_type_requires_array_handling(elem, FALSE)) {
        a_param_type_ptr  param = skip_typerefs(routine->type)->variant.
                                           routine.extra_info->param_type_list;
        a_boolean         placement_void = FALSE;

        if (!routine->source_corresp.is_class_member && param != NULL &&
            param->next != NULL && param->next->next == NULL) {
          placement_void = is_void_star_type(param->next->type);
        }  /* if */

        if (!placement_void) {
          cookie = (uint32_t)ecil_gid_info(sizet)->size;
          if ((uint32_t)alignment_of_type(elem) > cookie) {
            cookie = (uint32_t)alignment_of_type(elem);
          }  /* if */
        }  /* if */
      }  /* if */
      it->c = 0;
      if (array) it->c |= NEW_ARRAY;
      it->b = (uint32_t)total;
      if (total > (uint64_t)0xFFFFFFFFu) {
        ecil_note_unsupported("a new[] with 2^32 or more elements");
      }  /* if */
      /* The cookie size travels in the high bits of c. */
      it->c |= cookie << 8;
      if (array && nds->number_of_elements != NULL) {
        uint32_t  lid = new_lid();
        uint64_t  inner = new_inner_bounds(type);

        if (inner == 0 || inner > (uint64_t)0xFFFFFFFFu) {
          ecil_note_unsupported("a new[] of an array of a very large or "
                                "unknown inner bound");
        } else {
          it->c |= NEW_DYNAMIC;
          it->a = lid;
          it->b = (uint32_t)inner;      /* The inner bounds' product. */
          it->phase = 1;
          emit_local(frame, lid, sizet, 0, 0);
          ecil_rec1(frame, eop_lref, lid);
          push_expr(nds->number_of_elements, 0);
        }  /* if */
      } else if (array && total == 0) {
        ecil_note_unsupported("a new[] with an unknown element count");
      } else {
        it->phase = 2;
      }  /* if */
    }  /* if */
  } else if (it->phase == 1) {
    /* The count expression is on the stack: size_t, then the local. */
    uint32_t  src = ecil_type_id(frame, nds->number_of_elements->type);

    if (src != 0) emit_convert(frame, src, ecil_target_uintptr_type(frame));
    ecil_rec_begin(frame, eop_store);
    ecil_arg_uint(frame, ECIL_STORE_INIT);
    ecil_arg_uint(frame, (unsigned int)esr_none);
    ecil_rec_end(frame);
    if (it->b > 1u) {
      /* new T[n][K]: the count of elements is n * K, SIZE_MAX if that
         overflows (so that the allocation function fails). */
      uint32_t  sizet = ecil_target_uintptr_type(frame);

      ecil_rec1(frame, eop_lref, it->a);
      ecil_rec1(frame, eop_lref, it->a);
      ecil_rec1(frame, eop_load, 0);
      ecil_rec2(frame, eop_const_int, sizet, ~(uint64_t)0 / it->b);
      ecil_rec1(frame, eop_cmp, (unsigned int)ecmp_gt);
      emit_tern_begin(frame, sizet);
      ecil_rec2(frame, eop_const_int, sizet, ~(uint64_t)0);
      ecil_rec0(frame, eop_tern_else);
      ecil_rec1(frame, eop_lref, it->a);
      ecil_rec1(frame, eop_load, 0);
      ecil_rec2(frame, eop_const_int, sizet, it->b);
      ecil_rec1(frame, eop_mul, 0);
      ecil_rec0(frame, eop_tern_end);
      ecil_rec_begin(frame, eop_store);
      ecil_arg_uint(frame, ECIL_STORE_INIT);
      ecil_arg_uint(frame, (unsigned int)esr_none);
      ecil_rec_end(frame);
    }  /* if */
    it->phase = 2;
  } else if (it->phase == 2) {
    a_type_ptr  type = nds->type;
    a_boolean   array = (it->c & NEW_ARRAY) != 0;
    uint32_t    sizet = ecil_target_uintptr_type(frame);

    if (array) {
      uint32_t  elem = ecil_type_id(frame, innermost_element_type(type, NULL));

      new_push_count(frame, it);
      ecil_rec2(frame, eop_new_array_size, elem, it->c >> 8);
    } else {
      uint32_t  t = ecil_type_id(frame, type);

      if (t != 0) {
        ecil_rec2(frame, eop_const_int, sizet, ecil_gid_info(t)->size);
      }  /* if */
    }  /* if */
    it->cursor = nds->arg;
    it->phase = 3;
    it->c &= ~NEW_NARGS_MASK;
  } else if (it->phase == 3) {
    if (it->cursor != NULL) {
      an_expr_node_ptr  arg = (an_expr_node_ptr)it->cursor;

      it->cursor = arg->next;
      it->c += NEW_NARGS_ONE;
      push_expr(arg, 0);
    } else {
      a_routine_ptr  routine = new_routine_of(nds,
                                              (it->c & NEW_ARRAY) != 0);
      uint32_t       gid = ecil_function_id(frame, routine);
      unsigned int   nargs = 1 + ((it->c & NEW_NARGS_MASK) >> NEW_NARGS_SHIFT);

      emit_call_record(frame, gid, routine, nargs);
      it->phase = 4;
    }  /* if */
  } else {
    /* The storage is allocated (void *): the object pointer, then the
       initialization. */
    a_boolean         array = (it->c & NEW_ARRAY) != 0;
    uint32_t          cookie = it->c >> 8;
    a_dynamic_init_ptr di = nds->dynamic_init;
    uint32_t          elem_gid = ecil_type_id(frame, array ?
                          innermost_element_type(nds->type, NULL) : nds->type);
    uint32_t          result_gid = ecil_type_id(frame, node->type);
    a_dynamic_init_ptr loop_di = NULL;
    a_routine_ptr     loop_ctor = array ?
                                  new_array_default_ctor(di, &loop_di) : NULL;

    /* A multi-dimensional array is one array of the innermost elements: the
       pointer is converted to the result type (a pointer to an array) for
       the value, and back to a pointer to the element for the loops. */
    a_boolean         multi = array && skip_typerefs(skip_typerefs(nds->type)->
                              variant.array.element_type)->kind == tk_array;
    uint32_t          elem_ptr = array ? ecil_ptr_type(frame, elem_gid) : 0;

    if (array && cookie != 0) {
      new_push_count(frame, it);
      ecil_rec2(frame, eop_new_array_cookie, elem_gid, cookie);
    } else if (multi) {
      if (elem_ptr != 0) emit_cast(frame, (unsigned int)eck_bitcast, elem_ptr);
    } else if (result_gid != 0) {
      emit_cast(frame, (unsigned int)eck_bitcast, result_gid);
    }  /* if */
    if (multi && result_gid != 0) {
      emit_cast(frame, (unsigned int)eck_bitcast, result_gid);
    }  /* if */
    code_stack_size--;                    /* The initialization follows. */
    if (ecil_unsupported_reason != NULL || di == NULL ||
        di->kind == (a_dynamic_init_kind)dik_none) {
      /* No initialization. */
    } else if (loop_ctor != NULL) {
      uint32_t  gid = ecil_function_id(frame, loop_ctor);

      if (loop_di != NULL && ctor_needs_zeroing(loop_di)) {
        /* new A[n](): the storage of members that need it is zeroed first
           (the front end's need_zeroing_for_value_initialization). */
        if ((it->c & NEW_DYNAMIC) != 0) {
          ecil_note_unsupported("a value-initialized new[] with a run-time "
                                "count");
        } else {
          ecil_rec0(frame, eop_dup);
          if (multi && elem_ptr != 0) {
            emit_cast(frame, (unsigned int)eck_bitcast, elem_ptr);
          }  /* if */
          ecil_rec0(frame, eop_deref);
          ecil_rec1(frame, eop_zero_bytes,
                    (uint64_t)it->b * ecil_gid_info(elem_gid)->size);
        }  /* if */
      }  /* if */
      ecil_rec0(frame, eop_dup);
      if (multi && elem_ptr != 0) {
        emit_cast(frame, (unsigned int)eck_bitcast, elem_ptr);
      }  /* if */
      new_push_count(frame, it);
      ecil_rec2(frame, eop_array_ctor_begin, elem_gid, 0);
      emit_call_record(frame, gid, loop_ctor, 1);
      ecil_rec0(frame, eop_array_ctor_end);
    } else if (array && (it->c & NEW_DYNAMIC) != 0) {
      ecil_note_unsupported("an initializer of a new[] with a run-time count");
    } else if (!array && di->kind == (a_dynamic_init_kind)dik_constructor) {
      if (ctor_needs_zeroing(di)) emit_zero_pointee(frame, nds->type, FALSE);
      ecil_rec0(frame, eop_dup);
      { a_code_item *call = push_item(cik_ctor_call, NULL, 0);

        if (call != NULL) {
          setup_ctor_call(frame, call, di, ECIL_VARIANT_COMPLETE, 0);
        }  /* if */
      }
    } else {
      /* A value or aggregate initialization of the object (or of the array,
         through a pointer to the array type). */
      ecil_rec0(frame, eop_dup);
      if (array) {
        uint32_t  ap = ecil_ptr_type(frame, ecil_type_id(frame, nds->type));

        if (ap != 0) emit_cast(frame, (unsigned int)eck_bitcast, ap);
      }  /* if */
      ecil_rec0(frame, eop_deref);
      { a_code_item *init = push_item(cik_init, di, 0);

        if (init != NULL) {
          init->a = DEST_ON_STACK;
          init->b = ecil_type_id(frame, nds->type);
        }  /* if */
      }
    }  /* if */
  }  /* if */
}  /* do_new_delete */


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
      if (!C_mode() && node_variable(node)->init_kind ==
                                           (an_init_kind)initk_binding) {
        /* A structured binding names an lvalue (a member or an element of
           the hidden object): visit that expression in its place. */
        an_expr_node_ptr  bound = node_variable(node)->initializer.bound_expr;

        if (!is_glvalue_node(node) && is_glvalue_node(bound)) {
          /* The binding is used as a value: convert a copy (the expression
             is shared by all the uses). */
          bound = rvalue_expr_for_lvalue(copy_expr_tree(bound,
                                  CE_COPYING_FROM_ONE_FUNC_TO_ANOTHER));
        }  /* if */
        it->entry = bound;
      } else {
        emit_variable_node(frame, node);
        if ((it->flags & CIF_GLV) == 0) emit_load_if_value(frame, node);
        finish_value(frame, it->flags);
        code_stack_size--;
      }  /* if */
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
    case enk_lambda:
    case enk_temp_init:
      if (it->phase == 0) {
        a_dynamic_init_ptr  di = node->variant.init.dynamic_init;
        uint32_t            t = ecil_type_id(frame, node->type);
        uint32_t            lid;

        if (di != NULL && di->master_entry != NULL) {
          /* One arm of a class ?: that initializes a variable: the value
             is built in the variable (the master entry's). */
          a_variable_ptr  mv = di->master_entry->variable;
          uint32_t        dest = 0;

          if (mv != NULL && variable_is_automatic(mv)) {
            dest = ecil_lid_of(mv);
          } else if (mv != NULL && ecil_gid_of(mv) != 0) {
            dest = DEST_GLOBAL_FLAG | ecil_gid_of(mv);  /* A static. */
          }  /* if */
          if (dest == 0 || t == 0) {
            ecil_note_unsupported("a class conditional that does not "
                                  "initialize a variable");
          } else {
            it->a = dest;
            it->b = t;
            it->phase = 1;
            it->c = 1u;                       /* No temporary of its own. */
            { a_code_item *init = push_item(cik_init, di, 0);

              if (init != NULL) {
                init->a = dest;
                init->b = t;
              }  /* if */
            }
          }  /* if */
        } else if (di != NULL && static_init_var != NULL && t != 0 &&
                   ((di->lifetime != NULL &&
                     di->lifetime->kind != (an_object_lifetime_kind)
                                                     olk_expr_temporary) ||
                    (di->lifetime == NULL &&
                     is_reference_type(static_init_var->type)))) {
          /* A temporary that a reference of static storage duration extends
             is a static object of its own (spec 11.7). */
          uint32_t  tg = make_static_temp(frame, t, static_init_var);

          it = &code_stack[idx];
          it->a = tg;
          it->b = t;
          it->c = 2u;
          it->phase = 1;
          { a_code_item *init = push_item(cik_init, di, 0);

            if (init != NULL) {
              init->a = DEST_GLOBAL_FLAG | tg;
              init->b = t;
            }  /* if */
          }
        } else if (di != NULL && di->destructor != NULL &&
                   di->lifetime != NULL &&
                   di->lifetime->kind != (an_object_lifetime_kind)
                                                     olk_expr_temporary &&
                   di->inside_conditional_expression) {
          ecil_note_unsupported("a conditionally lifetime-extended "
                                "temporary");
        } else if (t != 0) {
          lid = new_lid();
          emit_local_flags(frame, lid, t, 0, 0,
                           C_mode() ? 0u : (unsigned int)ECIL_LOCAL_TEMPORARY);
          it = &code_stack[idx];
          it->a = lid;
          it->b = t;
          it->phase = 1;
          { a_code_item *init = push_item(cik_init, di, 0);

            if (init != NULL) {
              init->a = lid;
              init->b = t;
            }  /* if */
          }
        }  /* if */
      } else {
        a_dynamic_init_ptr  di = node->variant.init.dynamic_init;

        if (it->c == 1u) {
          /* Built in the master entry's variable: nothing more. */
        } else if (it->c == 2u) {
          /* A static temporary: destroyed at program exit. */
          if (di != NULL && di->destructor != NULL) {
            a_destroy_target  tgt;

            memset(&tgt, 0, sizeof(tgt));
            tgt.kind = DT_GLOBAL;
            tgt.lid = it->a;
            tgt.array_elem = array_element_gid(frame, node->type);
            emit_cleanup(frame, (unsigned int)ecl_exit, &tgt, di->destructor,
                         ECIL_VARIANT_COMPLETE);
          }  /* if */
        } else if (di != NULL && di->destructor != NULL) {
          /* The temporary is complete: it is destroyed at the end of its
             full expression, or with the block if its lifetime is
             extended (spec 11.5). */
          if (di->lifetime != NULL &&
              di->lifetime->kind != (an_object_lifetime_kind)
                                                      olk_expr_temporary) {
            add_pending_cleanup(it->a, node->type, di->destructor);
          } else {
            emit_local_cleanup(frame, it->a, node->type, di->destructor);
          }  /* if */
        }  /* if */
        if (value_is_pushed(node, (it->flags & CIF_UNUSED) != 0)) {
          if ((it->a & DEST_GLOBAL_FLAG) != 0) {
            ecil_rec1(frame, eop_gref, it->a & ~DEST_GLOBAL_FLAG);
          } else {
            ecil_rec1(frame, it->c == 2u ? eop_gref : eop_lref, it->a);
          }  /* if */
          if ((it->flags & CIF_GLV) == 0) emit_load_if_value(frame, node);
          finish_value(frame, it->flags);
        }  /* if */
        code_stack_size--;
      }  /* if */
      break;
    case enk_object_lifetime:
      { an_expr_node_ptr  child = node->variant.object_lifetime.expr;
        a_boolean         wrap = lifetime_has_cleanups(node);

        if (!wrap) {
          it->entry = child;           /* Nothing to destroy: pass through. */
        } else if (it->phase == 0) {
          ecil_rec0(frame, eop_fullexpr_begin);
          it->phase = 1;
          push_expr(child, it->flags & ~CIF_DROP);
        } else {
          if ((it->flags & CIF_DROP) != 0 && value_is_pushed(child, TRUE)) {
            ecil_rec0(frame, eop_drop);
          }  /* if */
          ecil_rec0(frame, eop_fullexpr_end);
          code_stack_size--;
        }  /* if */
      }
      break;
    case enk_param_ref:
      if (node->variant.param_ref.param_num == 0 && this_lid != 0 &&
          in_field_initializer) {
        ecil_rec1(frame, eop_lref, this_lid);
        ecil_rec1(frame, eop_load, 0);
        finish_value(frame, it->flags);
      } else {
        ecil_note_unsupported("a reference to a parameter by position");
      }  /* if */
      code_stack_size--;
      break;
    case enk_new_delete:
      it->kind = cik_new;               /* See do_new_delete. */
      it->phase = 0;
      it->c = 0;
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
      } else if (node->variant.builtin_operation.kind ==
                         (a_builtin_operation_kind)bok_builtin_addressof &&
                 node->variant.builtin_operation.operands != NULL) {
        /* __builtin_addressof(x): the address of the object, whatever
           operator& it has. */
        an_expr_node_ptr  target = node->variant.builtin_operation.operands;

        if (it->phase == 0) {
          it->phase = 1;
          push_expr(target, 0);
        } else {
          if (skip_parens(target)->kind != (an_expr_node_kind)enk_routine) {
            ecil_rec0(frame, eop_addr);
          }  /* if */
          finish_value(frame, it->flags);
          code_stack_size--;
        }  /* if */
      } else {
        note_unsupported_kind("builtin operation",
                              (int)node->variant.builtin_operation.kind,
                              ecil_builtin_operation_name(
                                (int)node->variant.builtin_operation.kind));
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
      note_unsupported_kind("expression kind", (int)node->kind,
                            ecil_expr_kind_name((int)node->kind));
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


static void setup_ctor_call(an_ecil_frame *frame,
                            a_code_item   *it,
                            a_dynamic_init_ptr di,
                            unsigned int  variant,
                            unsigned int  extra)
/*
Turn the item into the translation of the constructor call of the dynamic
init: the address of the object (and "extra" further operands) is already on
the value stack.  The complete variant (C1) initializes a complete object;
the base variant (C2), a base subobject.
*/
{
  a_routine_ptr ctor = di->variant.constructor.ptr;

  it->kind = cik_ctor_call;
  it->entry = di;
  it->phase = 0;
  it->flags = 0;
  it->cursor = di->variant.constructor.args;
  it->a = 0;
  it->b = 1 + extra;
  it->c = 0;
  if (ctor == NULL) {
    ecil_note_unsupported("a constructor call without a constructor");
  } else if (di->variant.constructor.is_array_copy) {
    ecil_note_unsupported("the copy of an array of objects");
  } else {
    it->a = variant == ECIL_VARIANT_BASE ?
            ecil_function_base_id(frame, ctor) : ecil_function_id(frame, ctor);
  }  /* if */
}  /* setup_ctor_call */


#define CTOR_ARG_LOAD_PENDING 0x01u

static void do_ctor_call(an_ecil_frame *frame, size_t idx)
/*
cik_ctor_call: push the arguments of the constructor call in order (a by-value
argument of a class that is not trivially copyable is the load of its
storage), then CALL.  b counts the operands, the object's address included.
*/
{
  a_code_item         *it = &code_stack[idx];
  a_dynamic_init_ptr  di = (a_dynamic_init_ptr)it->entry;

  if ((it->c & CTOR_ARG_LOAD_PENDING) != 0) {
    it->c &= ~CTOR_ARG_LOAD_PENDING;
    ecil_rec_begin(frame, eop_load);
    ecil_arg_uint(frame, 0);
    ecil_rec_end(frame);
  }  /* if */
  if (it->cursor != NULL) {
    an_expr_node_ptr  arg = (an_expr_node_ptr)it->cursor;

    it->cursor = arg->next;
    it->b++;
    if (is_glvalue_node(skip_parens(arg)) &&
        ecil_class_is_nontrivial_abi(arg->type)) {
      it->c |= CTOR_ARG_LOAD_PENDING;
    }  /* if */
    push_expr(arg, 0);
  } else {
    emit_call_record(frame, it->a, di->variant.constructor.ptr, it->b);
    code_stack_size--;
  }  /* if */
}  /* do_ctor_call */


static a_boolean array_default_construct(an_ecil_frame     *frame,
                                         a_dynamic_init_ptr di)
/*
An array of class objects that are all default-constructed (one repeated
constructor call over the whole array): ARRAY_CTOR_BEGIN(fixed) over the
array glvalue already on the stack, the constructor call on the element
pointer, ARRAY_CTOR_END (spec 11.8).  Return TRUE if it was emitted.
*/
{
  a_boolean       handled = FALSE;
  a_constant_ptr  agg = di->variant.constant.ptr;

  if (agg != NULL && agg->kind == (a_constant_repr_kind)ck_aggregate &&
      agg->variant.aggregate.first_constant != NULL &&
      agg->variant.aggregate.first_constant ==
                              agg->variant.aggregate.last_constant &&
      agg->variant.aggregate.first_constant->kind ==
                              (a_constant_repr_kind)ck_init_repeat) {
    a_constant_ptr  rep = agg->variant.aggregate.first_constant;
    a_constant_ptr  rc = rep->variant.init_repeat.constant;
    a_type_ptr      t = skip_typerefs(agg->type);
    a_targ_size_t   total = 1;

    while (t->kind == tk_array) {
      total *= t->variant.array.variant.number_of_elements;
      t = skip_typerefs(t->variant.array.element_type);
    }  /* while */
    if (rc->kind == (a_constant_repr_kind)ck_dynamic_init &&
        rc->variant.dynamic_init.ptr->kind ==
                                (a_dynamic_init_kind)dik_constructor &&
        rc->variant.dynamic_init.ptr->variant.constructor.args == NULL &&
        rc->variant.dynamic_init.ptr->variant.constructor.ptr != NULL &&
        rep->variant.init_repeat.count == total) {
      a_routine_ptr ctor = rc->variant.dynamic_init.ptr->variant.constructor.
                                                                       ptr;

      if (ctor_needs_zeroing(rc->variant.dynamic_init.ptr)) {
        ecil_note_unsupported("the value-initialization of an array of "
                              "objects");
      }  /* if */
      uint32_t      elem = ecil_type_id(frame, t);
      uint32_t      gid = ecil_function_id(frame, ctor);

      if (elem != 0 && gid != 0 && ecil_unsupported_reason == NULL) {
        ecil_rec2(frame, eop_array_ctor_begin, elem, ECIL_ARRAY_LOOP_FIXED);
        emit_call_record(frame, gid, ctor, 1);
        ecil_rec0(frame, eop_array_ctor_end);
        handled = TRUE;
      }  /* if */
    }  /* if */
  }  /* if */
  return handled;
}  /* array_default_construct */


#define INIT_LOAD_FIRST 0x100u    /* it->c: LOAD the value before the STORE. */
#define INIT_EMPTY      0x200u    /* it->c: an empty class: nothing to copy. */

static a_boolean type_is_empty_class(an_ecil_frame *frame, a_type_ptr type)
/*
Return TRUE for a class with no members, bases or vptr (an object of it has
no value: copying one does not read or write it).
*/
{
  uint32_t  gid = C_mode() ? 0 : ecil_type_id(frame, type);

  return gid != 0 && info_of(gid)->kind == egk_record &&
         (info_of(gid)->flags & ECIL_GF_COMPLETE) != 0 &&
         info_of(gid)->count == 0 &&
         is_immediate_class_type(skip_typerefs(type));
}  /* type_is_empty_class */

static void emit_init_destination(an_ecil_frame *frame, uint32_t lid)
/*
Push the object being initialized: local (or temporary) number lid, the
return slot (lid 0), or nothing (DEST_ON_STACK: already pushed).
*/
{
  if (lid == DEST_ON_STACK) {
    /* The caller pushed it. */
  } else if ((lid & DEST_GLOBAL_FLAG) != 0) {
    ecil_rec1(frame, eop_gref, lid & ~DEST_GLOBAL_FLAG);
  } else if (lid == 0) {
    ecil_rec0(frame, eop_return_slot);
  } else {
    ecil_rec1(frame, eop_lref, lid);
  }  /* if */
}  /* emit_init_destination */


static void do_init(an_ecil_frame *frame, size_t idx)
/*
cik_init: initialize local (or temporary) number it->a (0: the return slot),
of ECIL type it->b, from the dynamic init in it->entry.
*/
{
  a_code_item     *it = &code_stack[idx];
  a_dynamic_init_ptr
                  di = (a_dynamic_init_ptr)it->entry;
  uint32_t        lid = it->a;

  if (it->phase == 1) {
    /* dik_expression, dik_class_result_via_ctor or dik_bitwise_copy: the
       value is on the stack (a glvalue to load for the last one). */
    if ((it->c & INIT_EMPTY) != 0) {
      ecil_rec0(frame, eop_drop);
      ecil_rec0(frame, eop_drop);
    } else {
      if ((it->c & INIT_LOAD_FIRST) != 0) {
        ecil_rec_begin(frame, eop_load);
        ecil_arg_uint(frame, 0);
        ecil_rec_end(frame);
      }  /* if */
      ecil_rec_begin(frame, eop_store);
      ecil_arg_uint(frame, it->c & ~(INIT_LOAD_FIRST | INIT_EMPTY));
      ecil_arg_uint(frame, (unsigned int)esr_none);
      ecil_rec_end(frame);
    }  /* if */
    code_stack_size--;
  } else {
    switch (di->kind) {
      case dik_none:
        code_stack_size--;
        break;
      case dik_zero:
        emit_init_destination(frame, lid);
        emit_zero_object(frame, it->b);
        code_stack_size--;
        break;
      case dik_constant:
        emit_init_destination(frame, lid);
        if (ecil_emit_constant_typed(frame, di->variant.constant.ptr,
                                     it->b)) {
          ecil_rec0(frame, eop_init_const);
        }  /* if */
        code_stack_size--;
        break;
      case dik_expression:
        if (di->class_rvalue_initialized_through_master_entry) {
          /* A class ?: whose arms build the value in the variable. */
          code_stack_size--;
          push_expr(di->variant.expression, 0);
          break;
        }  /* if */
        emit_init_destination(frame, lid);
        it->phase = 1;
        it->c = class_copy_flags(frame, di->variant.expression->type,
                                 di->variant.expression);
        if (type_is_empty_class(frame, di->variant.expression->type) &&
            !ecil_class_is_nontrivial_abi(di->variant.expression->type)) {
          it->c |= INIT_EMPTY;
        }  /* if */
        push_expr(di->variant.expression, 0);
        break;
      case dik_constructor:
        /* [zeroing] <object address> <arguments> CALL C1. */
        emit_init_destination(frame, lid);
        if (ctor_needs_zeroing(di)) {
          ecil_rec0(frame, eop_dup);
          emit_zero_object(frame, it->b);
        }  /* if */
        ecil_rec0(frame, eop_addr);
        setup_ctor_call(frame, it, di, ECIL_VARIANT_COMPLETE, 0);
        break;
      case dik_bitwise_copy:
        if (di->variant.bitwise_copy.source == NULL) {
          ecil_note_unsupported("a bitwise copy without a source");
        } else {
          an_expr_node_ptr  src = di->variant.bitwise_copy.source;

          emit_init_destination(frame, lid);
          it->phase = 1;
          it->c = ECIL_STORE_INIT | INIT_LOAD_FIRST |
                  class_dsize_flag(frame, src->type);
          if (type_is_empty_class(frame, src->type)) it->c |= INIT_EMPTY;
          push_expr(src, 0);
        }  /* if */
        break;
      case dik_class_result_via_ctor:
        /* The call constructs the object in place: the destination is
           pushed before the call's operands (spec 11.6). */
        emit_init_destination(frame, lid);
        it->phase = 1;
        it->c = ECIL_STORE_INIT;
        push_expr(di->variant.expression, CIF_DEST);
        break;
      case dik_lambda:
      case dik_nonconstant_aggregate:
        if (di->kind == (a_dynamic_init_kind)dik_lambda) {
          agg_lambda = di->variant.constant.lambda;
        }  /* if */
        emit_init_destination(frame, lid);
        if (di->kind == (a_dynamic_init_kind)dik_nonconstant_aggregate &&
            array_default_construct(frame, di)) {
          code_stack_size--;
          break;
        }  /* if */
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


static a_variable_ptr captured_variable_of(an_init_level *level,
                                           uint64_t      index)
/*
Return the variable that the closure member "index" of the lambda being
initialized copies (a simple capture by copy), or NULL.
*/
{
  a_lambda_capture_ptr  cap = NULL;
  a_variable_ptr        cvar = NULL;

  if (agg_lambda != NULL &&
      skip_typerefs(level->type) == skip_typerefs(agg_lambda->closure_class)) {
    for (cap = agg_lambda->capture_list; cap != NULL; cap = cap->next) {
      if (cap->closure_field != NULL &&
          ecil_gid_of(cap->closure_field) == index + 1) break;
    }  /* for */
  }  /* if */
  if (cap != NULL && !cap->is_init_capture && !cap->is_indirect_init_capture &&
      cap->capture_info.source_closure_field == NULL) {
    cvar = cap->captured.variable;
  }  /* if */
  return cvar;
}  /* captured_variable_of */


static a_field_ptr agg_nua_member(an_ecil_frame *frame,
                                  an_init_level *level,
                                  uint64_t      index)
/*
Return the member "index" of the record being initialized if it is a
[[no_unique_address]] member (it may share its address with an earlier
member), else NULL.
*/
{
  a_field_ptr  field = NULL;

  if (!level->is_array && index >= level->lead &&
      ecil_member_type_gid(frame, level, index) != 0 &&
      level->members != NULL && index - level->lead < level->num_members &&
      level->members[index - level->lead]->has_no_unique_address_attribute) {
    field = level->members[index - level->lead];
  }  /* if */
  return field;
}  /* agg_nua_member */


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
    a_field_ptr     nua = NULL;
    a_boolean       more = ecil_init_level_next(level, &index, &value);

    if (more && !C_mode()) nua = agg_nua_member(frame, level, index);
    if (!more) {
      ecil_rec0(frame, eop_agg_init_end);
      ecil_init_level_end(level);
      num_agg_levels--;
    } else if (nua != NULL &&
               !(value->kind == (a_constant_repr_kind)ck_dynamic_init &&
                 value->variant.dynamic_init.ptr->kind ==
                              (a_dynamic_init_kind)dik_constructor)) {
      /* A [[no_unique_address]] member shares bytes with an earlier member:
         nothing is written for an empty one, only the data size is zeroed
         for another, and any other initialization is not supported. */
      if (nua->is_optimized_empty_class) {
        /* Nothing to write. */
      } else if (value->kind == (a_constant_repr_kind)ck_dynamic_init &&
                 value->variant.dynamic_init.ptr->kind ==
                                        (a_dynamic_init_kind)dik_zero) {
        ecil_rec1(frame, eop_agg_sel, index);
        ecil_rec1(frame, eop_zero_bytes, (uint64_t)ecil_data_size_of_type(
                                              skip_typerefs(nua->type)));
      } else {
        ecil_note_unsupported("an initialization of a [[no_unique_address]] "
                              "member other than by a constructor");
      }  /* if */
    } else if (level->entry_is_char) {
      ecil_rec1(frame, eop_agg_sel, index);
      ecil_emit_string_char(frame, level, value, index);
      ecil_rec0(frame, eop_init_const);
    } else if (value->kind == (a_constant_repr_kind)ck_init_repeat) {
      uint64_t  count = level->entry_count;
      uint64_t  i;

      a_constant_ptr  rc = value->variant.init_repeat.constant;

      if (rc->kind == (a_constant_repr_kind)ck_dynamic_init &&
          rc->variant.dynamic_init.ptr->kind ==
                              (a_dynamic_init_kind)dik_constructor &&
          rc->variant.dynamic_init.ptr->variant.constructor.args == NULL &&
          rc->variant.dynamic_init.ptr->variant.constructor.ptr != NULL &&
          count <= 4096) {
        /* Repeated default construction of class elements: one call per
           element (a whole array uses ARRAY_CTOR: see do_init). */
        a_dynamic_init_ptr  rdi = rc->variant.dynamic_init.ptr;
        uint32_t            gid = ecil_function_id(frame,
                                     rdi->variant.constructor.ptr);

        for (i = 0; i < count && ecil_unsupported_reason == NULL; i++) {
          ecil_rec1(frame, eop_agg_sel, index + i);
          ecil_rec0(frame, eop_addr);
          emit_call_record(frame, gid, rdi->variant.constructor.ptr, 1);
        }  /* for */
      } else if (rc->kind == (a_constant_repr_kind)ck_aggregate ||
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
      } else if (di->kind == (a_dynamic_init_kind)dik_constructor) {
        /* A class member or element: <address> <arguments> CALL C1. */
        unsigned int      extra = 0;
        an_expr_node_ptr  binding_source = NULL;

        if (ctor_needs_zeroing(di) && nua == NULL) {
          ecil_rec0(frame, eop_dup);
          emit_zero_object(frame, ecil_member_type_gid(frame, level, index));
        } else if (ctor_needs_zeroing(di) && !nua->is_optimized_empty_class) {
          /* Only the data size: the tail padding may hold a later member. */
          ecil_rec0(frame, eop_dup);
          ecil_rec1(frame, eop_zero_bytes, (uint64_t)ecil_data_size_of_type(
                                                skip_typerefs(nua->type)));
        }  /* if */
        ecil_rec0(frame, eop_addr);
        if (di->variant.constructor.is_copy_constructor_with_implied_source) {
          /* The copy of a captured variable into the closure. */
          a_variable_ptr        cvar = captured_variable_of(level, index);
          if (cvar != NULL && cvar->init_kind ==
                                           (an_init_kind)initk_binding &&
              cvar->initializer.bound_expr != NULL &&
              is_glvalue_node(cvar->initializer.bound_expr)) {
            /* A captured structured binding: the address of the object it
               names is the source (evaluated before the call). */
            binding_source = add_address_of_to_node(copy_expr_tree(
                                         cvar->initializer.bound_expr,
                                         CE_COPYING_FROM_ONE_FUNC_TO_ANOTHER));
            extra = 1;
          } else if (cvar != NULL && variable_is_automatic(cvar) &&
              ecil_lid_of(cvar) != 0) {
            ecil_rec1(frame, eop_lref, ecil_lid_of(cvar));
            if (is_reference_type(cvar->type)) {
              ecil_rec1(frame, eop_load, 0);
              ecil_rec0(frame, eop_deref);
              ecil_rec0(frame, eop_addr);
            } else if (skip_typerefs(cvar->type)->kind == tk_pointer) {
              /* [*this]: the object the pointer designates. */
              ecil_rec1(frame, eop_load, 0);
            } else {
              ecil_rec0(frame, eop_addr);
            }  /* if */
            extra = 1;
          } else {
            ecil_note_unsupported("the copy of a captured variable that is "
                                  "not a local of the function");
          }  /* if */
        }  /* if */
        { a_code_item *call = push_item(cik_ctor_call, NULL, 0);

          if (call != NULL) {
            setup_ctor_call(frame, call, di, ECIL_VARIANT_COMPLETE, extra);
          }  /* if */
        }
        if (binding_source != NULL) push_expr(binding_source, 0u);
      } else if (di->kind == (a_dynamic_init_kind)dik_class_result_via_ctor) {
        { a_code_item *store = push_item(cik_store, NULL, 0);

          if (store != NULL) store->a = ECIL_STORE_INIT;
        }
        push_expr(di->variant.expression, CIF_DEST);
      } else if (di->kind == (a_dynamic_init_kind)dik_bitwise_copy &&
                 di->variant.bitwise_copy.source == NULL &&
                 captured_variable_of(level, index) != NULL &&
                 variable_is_automatic(captured_variable_of(level, index)) &&
                 ecil_lid_of(captured_variable_of(level, index)) != 0) {
        /* A trivially copyable object captured by copy: its value. */
        ecil_rec1(frame, eop_lref,
                  ecil_lid_of(captured_variable_of(level, index)));
        if (is_reference_type(captured_variable_of(level, index)->type)) {
          /* The referent is copied, not the reference. */
          ecil_rec1(frame, eop_load, 0);
          ecil_rec0(frame, eop_deref);
        }  /* if */
        ecil_rec1(frame, eop_load, 0);
        ecil_rec_begin(frame, eop_store);
        ecil_arg_uint(frame, ECIL_STORE_INIT);
        ecil_arg_uint(frame, (unsigned int)esr_none);
        ecil_rec_end(frame);
      } else if (di->kind == (a_dynamic_init_kind)dik_nonconstant_aggregate &&
                 di->variant.constant.ptr != NULL &&
                 di->variant.constant.ptr->kind ==
                                        (a_constant_repr_kind)ck_aggregate &&
                 is_immediate_class_type(
                    skip_typerefs(di->variant.constant.ptr->type))) {
        /* A nested aggregate (the element of an array of aggregates; not an
           array of arrays of objects: a capture of one lists its elements
           in a flat sequence). */
        begin_dynamic_aggregate(frame, di->variant.constant.ptr, NULL, 0);
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


/* Constructor bodies: member initializers. */

static void meminit_first_operands(an_ecil_frame *frame,
                                   a_code_item   *it,
                                   a_constructor_init_ptr ci,
                                   a_dynamic_init_ptr di,
                                   uint32_t      rec)
/*
Push the object being initialized by one constructor-initializer entry and
the item(s) that initialize it, after MEMINIT_BEGIN (the item "it" is the
meminit cursor, whose pointer is not valid after a push).
*/
{
  a_boolean     is_delegation = ci->kind ==
                                (a_constructor_init_kind)cik_delegation;
  a_boolean     is_base = ci->kind ==
                          (a_constructor_init_kind)cik_direct_base_class;
  uint32_t      src_lid = 0;
  uint32_t      index = 0;
  uint32_t      bg = 0;
  uint64_t      offset = 0;
  a_type_ptr    type = NULL;
  uint32_t      tgid = 0;
  a_variable_ptr
                param = function_scope->variant.routine.parameters;

  (void)it;
  if (param != NULL) src_lid = ecil_lid_of(param);
  if (is_base) {
    a_base_class_ptr  base = ci->variant.base_class;

    type = base->type;
    bg = ecil_type_id(frame, base->type);
    index = ecil_gid_of(base) - 1;
    offset = (uint64_t)base->offset;
  } else if (!is_delegation) {
    a_field_ptr   field = ci->variant.field;
    uint32_t      r;

    type = field->type;
    if (!field_index(frame, function_class, field, &r, &index)) type = NULL;
  }  /* if */
  if (!is_delegation && type != NULL) tgid = ecil_type_id(frame, type);
  if (ecil_unsupported_reason != NULL || di == NULL ||
      (!is_delegation && (type == NULL || tgid == 0))) {
    ecil_note_unsupported("a member initializer that is not supported");
  } else if (!is_delegation && !is_base &&
             ci->variant.field->has_no_unique_address_attribute &&
             class_dsize_flag(frame, type) != 0 &&
             (di->kind == (a_dynamic_init_kind)dik_expression ||
              di->kind == (a_dynamic_init_kind)dik_class_result_via_ctor)) {
    /* A full-size store into a [[no_unique_address]] member would overwrite
       the next member (spec 11.3). */
    ecil_note_unsupported("the initialization of a [[no_unique_address]] "
                          "member by a class value");
  } else if (di->kind == (a_dynamic_init_kind)dik_none) {
    /* Nothing to do (only a destruction). */
  } else if (!is_delegation && !is_base &&
             ci->variant.field->has_no_unique_address_attribute &&
             (di->kind == (a_dynamic_init_kind)dik_zero ||
              di->kind == (a_dynamic_init_kind)dik_constant)) {
    /* A [[no_unique_address]] member may share its address (and bytes) with
       an earlier member: an empty one has no storage to write, and only the
       data size of another is zeroed (spec 11.3). */
    if (ci->variant.field->is_optimized_empty_class) {
      /* Nothing to write. */
    } else if (di->kind == (a_dynamic_init_kind)dik_zero) {
      emit_this_member(frame, this_lid, rec, index);
      ecil_rec1(frame, eop_zero_bytes, (uint64_t)ecil_data_size_of_type(
                                            skip_typerefs(type)));
    } else {
      ecil_note_unsupported("a constant initialization of a "
                            "[[no_unique_address]] member");
    }  /* if */
  } else if (di->kind == (a_dynamic_init_kind)dik_constructor) {
    unsigned int  extra = 0;

    if (is_delegation) {
      ecil_rec1(frame, eop_lref, this_lid);
      ecil_rec1(frame, eop_load, 0);
    } else if (is_base) {
      emit_this_base(frame, this_lid, rec, bg, offset);
      if (ctor_needs_zeroing(di) &&
          !ci->variant.base_class->is_optimized_empty_base) {
        emit_zero_pointee(frame, type, TRUE);
      }  /* if */
      if (di->variant.constructor.is_copy_constructor_with_implied_source) {
        emit_this_base(frame, src_lid, rec, bg, offset);
        extra = 1;
      }  /* if */
    } else {
      emit_this_member(frame, this_lid, rec, index);
      if (ctor_needs_zeroing(di)) {
        if (!ci->variant.field->has_no_unique_address_attribute) {
          ecil_rec0(frame, eop_dup);
          emit_zero_object(frame, tgid);
        } else if (!ci->variant.field->is_optimized_empty_class) {
          /* Only the data size: the tail padding may hold a later member. */
          ecil_rec0(frame, eop_dup);
          ecil_rec1(frame, eop_zero_bytes, (uint64_t)ecil_data_size_of_type(
                                                skip_typerefs(type)));
        }  /* if */
      }  /* if */
      ecil_rec0(frame, eop_addr);
      if (di->variant.constructor.is_copy_constructor_with_implied_source) {
        emit_this_member(frame, src_lid, rec, index);
        ecil_rec0(frame, eop_addr);
        extra = 1;
      }  /* if */
    }  /* if */
    { a_code_item *call = push_item(cik_ctor_call, NULL, 0);

      if (call != NULL) {
        setup_ctor_call(frame, call, di, is_base ? ECIL_VARIANT_BASE :
                        ECIL_VARIANT_COMPLETE, extra);
      }  /* if */
    }
  } else if (di->kind == (a_dynamic_init_kind)dik_bitwise_copy &&
             di->variant.bitwise_copy.source == NULL && !is_delegation) {
    /* The same member or base of the source object, copied bitwise. */
    if (is_base) {
      emit_this_base(frame, this_lid, rec, bg, offset);
      ecil_rec0(frame, eop_deref);
      emit_this_base(frame, src_lid, rec, bg, offset);
      ecil_rec0(frame, eop_deref);
    } else {
      emit_this_member(frame, this_lid, rec, index);
      emit_this_member(frame, src_lid, rec, index);
    }  /* if */
    ecil_rec1(frame, eop_load, 0);
    ecil_rec_begin(frame, eop_store);
    ecil_arg_uint(frame, ECIL_STORE_INIT | class_dsize_flag(frame, type));
    ecil_arg_uint(frame, (unsigned int)esr_none);
    ecil_rec_end(frame);
  } else if (is_base && di->kind == (a_dynamic_init_kind)dik_zero &&
             ci->variant.base_class->is_optimized_empty_base) {
    /* An empty base occupies no storage: nothing to zero (the byte at its
       address belongs to another subobject). */
  } else if (is_base && di->kind == (a_dynamic_init_kind)dik_zero) {
    /* Only the data size of a base subobject is zeroed: its tail padding may
       hold members of the derived class (spec 11.6). */
    emit_this_base(frame, this_lid, rec, bg, offset);
    ecil_rec0(frame, eop_deref);
    ecil_rec1(frame, eop_zero_bytes, (uint64_t)ecil_data_size_of_type(
                                          skip_typerefs(type)));
  } else if (is_delegation && di->kind == (a_dynamic_init_kind)dik_zero) {
    /* The delegated constructor is trivial: its value initialization zeroes
       the object. */
    ecil_rec1(frame, eop_lref, this_lid);
    ecil_rec1(frame, eop_load, 0);
    ecil_rec0(frame, eop_deref);
    ecil_rec1(frame, eop_zero_bytes, (uint64_t)ecil_data_size_of_type(
                                          skip_typerefs(function_class)));
  } else if (!is_delegation) {
    /* A value, zero or aggregate initialization of the member. */
    if (is_base) {
      emit_this_base(frame, this_lid, rec, bg, offset);
      ecil_rec0(frame, eop_deref);
    } else {
      emit_this_member(frame, this_lid, rec, index);
    }  /* if */
    { a_code_item *init = push_item(cik_init, di, 0);

      if (init != NULL) {
        init->a = DEST_ON_STACK;
        init->b = tgid;
      }  /* if */
    }
  } else {
    ecil_note_unsupported("a delegating constructor initializer that is not "
                          "a constructor call");
  }  /* if */
}  /* meminit_first_operands */


static void do_meminit(an_ecil_frame *frame, size_t idx)
/*
cik_meminit: one constructor-initializer entry per round.  Phase 0:
MEMINIT_BEGIN, the object and the initialization (pushed as items); phase 1:
MEMINIT_END, then the cleanup that runs only when an exception propagates
(CLEANUP_BEGIN(eh), at function scope, so it protects the later initializers
and the body: spec 11.1).
*/
{
  a_code_item             *it = &code_stack[idx];
  a_constructor_init_ptr  ci = (a_constructor_init_ptr)it->entry;

  if (ci == NULL) {
    code_stack_size--;
  } else {
    a_boolean         is_delegation = ci->kind ==
                                      (a_constructor_init_kind)cik_delegation;
    a_boolean         is_base = ci->kind ==
                                (a_constructor_init_kind)cik_direct_base_class;
    a_dynamic_init_ptr
                      di = (ci->kind == (a_constructor_init_kind)cik_field &&
                            ci->use_field_initializer) ?
                           ci->variant.field->initializer : ci->initializer;
    uint32_t          rec = ecil_type_id(frame, function_class);

    if (it->phase == 1) {
      it->phase = 0;
      it->entry = ci->next;
      in_field_initializer = FALSE;
      if ((it->c & 1u) != 0) ecil_rec0(frame, eop_fullexpr_end);
      it->c = 0;
      ecil_rec0(frame, eop_meminit_end);
      if (di != NULL && di->destructor != NULL) {
        a_destroy_target  tgt;

        memset(&tgt, 0, sizeof(tgt));
        tgt.lid = this_lid;
        tgt.rec = rec;
        if (is_delegation) {
          tgt.kind = DT_THIS;
        } else if (is_base) {
          a_base_class_ptr  base = ci->variant.base_class;

          tgt.kind = DT_BASE;
          tgt.base_gid = ecil_type_id(frame, base->type);
          tgt.offset = (uint64_t)base->offset;
        } else {
          uint32_t  r;

          tgt.kind = DT_MEMBER;
          (void)field_index(frame, function_class, ci->variant.field, &r,
                            &tgt.index);
          tgt.array_elem = array_element_gid(frame,
                                             ci->variant.field->type);
        }  /* if */
        emit_cleanup(frame, (unsigned int)ecl_eh, &tgt, di->destructor,
                     is_base ? ECIL_VARIANT_BASE : ECIL_VARIANT_COMPLETE);
      }  /* if */
    } else if (ci->kind == (a_constructor_init_kind)cik_virtual_base_class) {
      ecil_note_unsupported("a virtual base class initializer");
    } else if (!is_base && !is_delegation &&
               parent_class_of(ci->variant.field) != function_class) {
      /* A member of an anonymous union or struct is initialized through the
         member that holds it. */
      ecil_note_unsupported("the initializer of a member of an anonymous "
                            "union or struct");
    } else {
      unsigned int  kind = is_delegation ? (unsigned int)emk_delegation :
                           is_base ? (unsigned int)emk_base :
                                     (unsigned int)emk_field;
      unsigned int  flags = 0;
      uint32_t      index = 0;

      if (is_base) {
        index = ecil_gid_of(ci->variant.base_class) - 1;
      } else if (!is_delegation) {
        uint32_t  r;

        (void)field_index(frame, function_class, ci->variant.field, &r,
                          &index);
      }  /* if */
      if (ci->compiler_generated) flags |= ECIL_MEMINIT_COMPILER_GENERATED;
      if (ci->use_field_initializer) flags |= ECIL_MEMINIT_DEFAULT_MEMBER_INIT;
      if (ecil_unsupported_reason == NULL) {
        ecil_rec_begin(frame, eop_meminit_begin);
        ecil_arg_uint(frame, kind);
        ecil_arg_uint(frame, index);
        ecil_arg_uint(frame, flags);
        ecil_rec_end(frame);
        it->phase = 1;
        in_field_initializer = ci->use_field_initializer;
        if (di != NULL && lifetime_needs_cleanups(di->init_expr_lifetime)) {
          /* The temporaries of the initializer are destroyed at the end of
             the initialization. */
          ecil_rec0(frame, eop_fullexpr_begin);
          it->c |= 1u;
        }  /* if */
        meminit_first_operands(frame, it, ci, di, rec);
      }  /* if */
    }  /* if */
  }  /* if */
}  /* do_meminit */


static void emit_dtor_prologue(an_ecil_frame *frame, a_scope_ptr scope)
/*
The start of a destructor body: CLEANUP_BEGIN(all) for every member and base
with a non-trivial destructor, in construction order, so that they are
destroyed in reverse after the body, also on early returns (spec 11.4).  The
front end's list is in destruction order: walk it from its end.
*/
{
  a_constructor_init_ptr  ci;
  size_t                  count = 0;
  size_t                  i;
  a_constructor_init_ptr  *entries;
  uint32_t                rec = ecil_type_id(frame, function_class);

  for (ci = scope->variant.routine.constructor_inits; ci != NULL;
       ci = ci->next) {
    count++;
  }  /* for */
  entries = count == 0 ? NULL : (a_constructor_init_ptr *)malloc(
                                  count * sizeof(a_constructor_init_ptr));
  if (count != 0 && entries == NULL) {
    ecil_note_unsupported("out of memory in the ECIL writer");
  } else {
    i = 0;
    for (ci = scope->variant.routine.constructor_inits; ci != NULL;
         ci = ci->next) {
      entries[i++] = ci;
    }  /* for */
    for (i = count; i > 0 && ecil_unsupported_reason == NULL; i--) {
      a_dynamic_init_ptr  di;
      a_destroy_target    tgt;

      ci = entries[i - 1];
      di = ci->initializer;
      if (di == NULL || di->destructor == NULL) {
        /* Trivially destructible: nothing to destroy. */
      } else if (ci->kind == (a_constructor_init_kind)cik_field) {
        uint32_t  r;

        memset(&tgt, 0, sizeof(tgt));
        tgt.kind = DT_MEMBER;
        tgt.lid = this_lid;
        tgt.rec = rec;
        tgt.base_gid = 0;
        tgt.offset = 0;
        tgt.index = 0;
        if (field_index(frame, function_class, ci->variant.field, &r,
                        &tgt.index)) {
          tgt.array_elem = array_element_gid(frame,
                                             ci->variant.field->type);
          emit_cleanup(frame, (unsigned int)ecl_all, &tgt, di->destructor,
                       ECIL_VARIANT_COMPLETE);
        }  /* if */
      } else if (ci->kind == (a_constructor_init_kind)cik_direct_base_class) {
        a_base_class_ptr  base = ci->variant.base_class;

        memset(&tgt, 0, sizeof(tgt));
        tgt.kind = DT_BASE;
        tgt.lid = this_lid;
        tgt.rec = rec;
        tgt.index = 0;
        tgt.base_gid = ecil_type_id(frame, base->type);
        tgt.offset = (uint64_t)base->offset;
        emit_cleanup(frame, (unsigned int)ecl_all, &tgt, di->destructor,
                     ECIL_VARIANT_BASE);
      } else {
        ecil_note_unsupported("a virtual base class destructor");
      }  /* if */
    }  /* for */
  }  /* if */
  free(entries);
}  /* emit_dtor_prologue */


/* Statements. */

static void declare_variable(an_ecil_frame *frame, a_variable_ptr var)
/*
Declare a variable of a block: LOCAL for an automatic one, the definition of
a static one.
*/
{
  if (variable_is_automatic(var) && ecil_lid_of(var) != 0) {
    /* Declared already (a for-init variable of the old scope rule). */
  } else if (var->init_kind == (an_init_kind)initk_binding) {
    /* A structured binding is not an object: its uses are replaced. */
  } else if (var->storage_class == (a_storage_class)sc_static) {
    ecil_define_variable(frame, var, NULL);
  } else if (var->storage_class == (a_storage_class)sc_extern ||
             !var->source_corresp.is_local_to_function) {
    /* A block-scope declaration (of an entity of the file scope, in C++):
       introduced when referenced. */
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
      emit_local_flags(frame, lid, t, name_sid,
                       (uint32_t)ecil_variable_alignment(var),
                       var == nrvo_variable ?
                              (unsigned int)ECIL_LOCAL_RETURN_SLOT : 0u);
    }  /* if */
  }  /* if */
}  /* declare_variable */


static void declare_generated_loop_variable(an_ecil_frame   *frame,
                                            an_expr_node_ptr expr)
/*
The front end builds the element loop of an implicit assignment of an array
of class objects around a temporary index that no declaration statement
introduces ("tmp = 0;" starts it).  Declare it just before that first use.
*/
{
  an_expr_node_ptr  node = skip_parens(expr);

  if (!C_mode() && node->kind == (an_expr_node_kind)enk_operation &&
      node->variant.operation.operands != NULL &&
      node->variant.operation.operands->kind ==
                                           (an_expr_node_kind)enk_variable) {
    a_variable_ptr  var = node_variable(node->variant.operation.operands);

    if (var->compiler_generated && var->source_corresp.name == NULL &&
        variable_is_automatic(var) && ecil_lid_of(var) == 0) {
      declare_variable(frame, var);
    }  /* if */
  }  /* if */
}  /* declare_generated_loop_variable */


static void declare_variable_and_bindings(an_ecil_frame *frame,
                                          a_variable_ptr var)
/*
Declare a variable and, if it is the hidden object of a structured binding,
the bindings that are objects (they are not on the declaration statement's
list: the references of a tuple-like binding).
*/
{
  declare_variable(frame, var);
  if (var->is_struct_binding_container && variable_is_automatic(var)) {
    an_il_entity_list_entry_ptr  b;

    for (b = var->variant.bindings; b != NULL; b = b->next) {
      if (b->entity.kind == (an_il_entry_kind)iek_variable) {
        declare_variable(frame, (a_variable_ptr)b->entity.ptr);
      }  /* if */
    }  /* for */
  }  /* if */
}  /* declare_variable_and_bindings */


static void do_decl(an_ecil_frame *frame, a_statement_ptr s)
/*
stmk_decl: declare the locals (LOCAL) and define the static variables.
*/
{
  an_il_entity_list_entry_ptr e;

  for (e = s->variant.decl.entities; e != NULL; e = e->next) {
    if (e->entity.kind == (an_il_entry_kind)iek_variable) {
      declare_variable_and_bindings(frame, (a_variable_ptr)e->entity.ptr);
    }  /* if */
  }  /* for */
}  /* do_decl */


static void hoist_for_init_declarations(an_ecil_frame *frame,
                                        a_statement_ptr init)
/*
With the old (cfront and Microsoft) scope rule the variables of a for-init
declaration belong to the block that contains the for statement, and are used
after it: declare them before FOR_BEGIN (their initializations stay in the
loop's scope).  The declaration statement is skipped when it is reached.
*/
{
  a_statement_ptr  st = NULL;

  if (init != NULL && init->kind == (a_statement_kind)stmk_block) {
    st = init->variant.block.statements;
  } else {
    st = init;
  }  /* if */
  for (; st != NULL; st = st->next) {
    if (st->kind == (a_statement_kind)stmk_decl) do_decl(frame, st);
    if (st->kind == (a_statement_kind)stmk_init &&
        st->variant.dynamic_init != NULL &&
        (st->variant.dynamic_init->destructor != NULL ||
         (st->variant.dynamic_init->variable != NULL &&
          is_reference_type(st->variant.dynamic_init->variable->type) &&
          ecil_class_has_nontrivial_dtor(type_pointed_to(
                  st->variant.dynamic_init->variable->type))))) {
      /* The variable's cleanup (or that of a temporary that a reference to
         a class with a destructor may extend) would end with the loop, but
         the variable is used after it. */
      ecil_note_unsupported("a for-init variable with a destructor under "
                            "the old scope rule");
    }  /* if */
    if (st == init) break;                    /* A single statement. */
  }  /* for */
}  /* hoist_for_init_declarations */


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

  if (!case_is_directly_in_switch(s)) {
    ecil_note_unsupported("a case label nested in other control flow");
  } else if (entry->case_value == NULL) {
    ecil_rec0(frame, eop_case_default);
  } else {
    uint32_t  t = ecil_type_id(frame, entry->case_value->type);
    a_boolean is_signed = t != 0 && info_of(t)->is_signed;
    unsigned  bits = (t != 0 && info_of(t)->kind == egk_int) ? info_of(t)->bits
                                                              : 64;

    if (entry->range_end != NULL) {
      ecil_rec_begin(frame, eop_case_range);
      ecil_arg_integer_constant(frame, entry->case_value, bits, is_signed);
      ecil_arg_integer_constant(frame, entry->range_end, bits, is_signed);
      ecil_rec_end(frame);
    } else {
      ecil_rec_begin(frame, eop_case_value);
      ecil_arg_integer_constant(frame, entry->case_value, bits, is_signed);
      ecil_rec_end(frame);
    }  /* if */
  }  /* if */
}  /* emit_case_label */


static a_condition_supplement_ptr condition_of(an_expr_node_ptr node)
/*
The supplement of a condition that declares a variable (or has an
initialization statement), else NULL.
*/
{
  a_condition_supplement_ptr  cond = NULL;

  if (node != NULL) {
    node = skip_parens(node);
    if (node->kind == (an_expr_node_kind)enk_condition) {
      cond = node->variant.condition;
    }  /* if */
  }  /* if */
  return cond;
}  /* condition_of */


static void push_condition_initialization(a_condition_supplement_ptr cond)
/*
Push the init-statement of an if or switch (C++17: if (T v = e; cond)).  The
front end gives it as declarations only: a variable with a dynamic
initialization has no stmk_init after its declaration, so its initialization
item (with its cleanup) is pushed below the statements.
*/
{
  a_statement_ptr             st;
  an_il_entity_list_entry_ptr e;
  a_variable_ptr              *vars = NULL;
  size_t                      n = 0;
  size_t                      count = 0;

  for (st = cond->initialization; st != NULL; st = st->next) {
    if (st->kind == (a_statement_kind)stmk_decl) {
      for (e = st->variant.decl.entities; e != NULL; e = e->next) count++;
    }  /* if */
  }  /* for */
  if (count != 0) {
    vars = (a_variable_ptr *)malloc(count * sizeof(a_variable_ptr));
  }  /* if */
  if (count != 0 && vars == NULL) {
    ecil_note_unsupported("out of memory in the ECIL writer");
  } else {
    for (st = cond->initialization; st != NULL; st = st->next) {
      if (st->kind == (a_statement_kind)stmk_decl) {
        for (e = st->variant.decl.entities; e != NULL; e = e->next) {
          if (e->entity.kind == (an_il_entry_kind)iek_variable) {
            a_variable_ptr  v = (a_variable_ptr)e->entity.ptr;

            if (v->init_kind == (an_init_kind)initk_dynamic &&
                v->initializer.dynamic != NULL) {
              vars[n++] = v;
            }  /* if */
          }  /* if */
        }  /* for */
      } else if (st->kind == (a_statement_kind)stmk_init) {
        /* An explicit initialization statement: not needed again. */
        size_t  i;

        for (i = 0; i < n; i++) {
          if (vars[i] == st->variant.dynamic_init->variable) {
            for (n--; i < n; i++) vars[i] = vars[i + 1];
            break;
          }  /* if */
        }  /* for */
      }  /* if */
    }  /* for */
    while (n > 0) {
      (void)push_item(cik_var_init, vars[--n]->initializer.dynamic, 0);
    }  /* while */
    if (cond->initialization != NULL) {
      (void)push_item(cik_stmt_list, cond->initialization, 0);
    }  /* if */
  }  /* if */
  free(vars);
}  /* push_condition_initialization */


static void push_condition_init(an_ecil_frame            *frame,
                                a_condition_supplement_ptr cond)
/*
After the declaration of the condition's variable (LOCAL): push the item
that initializes it (and destroys it with its block).
*/
{
  if (cond->dynamic_init != NULL && cond->dynamic_init->variable != NULL) {
    declare_variable(frame, cond->dynamic_init->variable);
    if (ecil_unsupported_reason == NULL) {
      (void)push_item(cik_var_init, cond->dynamic_init, 0);
    }  /* if */
  }  /* if */
}  /* push_condition_init */


static void emit_bool_not_break(an_ecil_frame *frame)
/*
The rewritten loop test: if the condition (a bool on the stack) is false the
loop ends.
*/
{
  ecil_rec0(frame, eop_log_not);
  rec_bool(frame, eop_if_begin, FALSE);
  ecil_rec0(frame, eop_break_stmt);
  ecil_rec0(frame, eop_if_end);
}  /* emit_bool_not_break */


static void do_static_var_init(an_ecil_frame *frame, size_t idx)
/*
cik_var_init for a variable of static storage duration whose definition has a
STATIC_INIT (spec 11.7): the initialization (unless the initializer is
constant and the region only registers the destructor) between
STATIC_INIT_BEGIN and STATIC_INIT_END, followed by the exit cleanup.
*/
{
  a_code_item         *it = &code_stack[idx];
  a_dynamic_init_ptr  di = (a_dynamic_init_ptr)it->entry;
  a_variable_ptr      var = di->variable;
  uint32_t            gid = ecil_gid_of(var);

  if (it->phase == 1) {
    if ((it->c & 1u) != 0) ecil_rec0(frame, eop_fullexpr_end);
    if (di->destructor != NULL) {
      a_destroy_target  tgt;

      memset(&tgt, 0, sizeof(tgt));
      tgt.kind = DT_GLOBAL;
      tgt.lid = gid;
      tgt.array_elem = array_element_gid(frame, var->type);
      emit_cleanup(frame, (unsigned int)ecl_exit, &tgt, di->destructor,
                   ECIL_VARIANT_COMPLETE);
    }  /* if */
    ecil_rec0(frame, eop_static_init_end);
    static_init_var = NULL;
    code_stack_size--;
  } else if (gid == 0 ||
             (ecil_gid_info(gid)->flags & ECIL_GF_NEEDS_STATIC_INIT) == 0) {
    code_stack_size--;        /* Defined and initialized by the DECL_VAR. */
  } else {
    unsigned int  flags = 0;
    uint32_t      t = ecil_gid_info(gid)->decl_type;
    a_boolean     constant = (ecil_gid_info(gid)->flags &
                                                   ECIL_GF_CONST_INIT) != 0;

    if (!var->source_corresp.is_local_to_function) {
      if (var->is_inline) flags |= ECIL_STATIC_INIT_INLINE_VAR;
      if (ecil_variable_may_have_multiple_copies(var) && !var->is_inline) {
        flags |= ECIL_STATIC_INIT_TEMPLATE_INSTANCE;
      }  /* if */
    }  /* if */
    ecil_rec2(frame, eop_static_init_begin, gid, flags);
    static_init_var = var;
    static_temp_count = 0;
    it->phase = 1;
    if (!constant) {
      if (lifetime_needs_cleanups(di->init_expr_lifetime)) {
        ecil_rec0(frame, eop_fullexpr_begin);
        it->c |= 1u;
      }  /* if */
      { a_code_item *init = push_item(cik_init, di, 0);

        if (init != NULL) {
          init->a = DEST_GLOBAL_FLAG | gid;
          init->b = t;
        }  /* if */
      }
    }  /* if */
  }  /* if */
}  /* do_static_var_init */


static void do_var_init(an_ecil_frame *frame, size_t idx)
/*
cik_var_init: the initialization of a declared variable (a stmk_init, or the
declaration of a condition).  Phase 0 opens the FULLEXPR of the initializer
if it has temporaries to destroy and pushes the initialization; phase 1
closes the FULLEXPR, begins the cleanups of lifetime-extended temporaries and
then the variable's own cleanup (the object is completely constructed here:
spec 11.5).  The named return value gets the cleanup too: the consumer
guards it with an nrvo flag (spec 11.6).
*/
{
  a_code_item         *it = &code_stack[idx];
  a_dynamic_init_ptr  di = (a_dynamic_init_ptr)it->entry;
  a_variable_ptr      var = di->variable;

  if (var != NULL && !variable_is_automatic(var) && !C_mode()) {
    do_static_var_init(frame, idx);
  } else if (it->phase == 1) {
    if ((it->c & 1u) != 0) ecil_rec0(frame, eop_fullexpr_end);
    flush_pending_cleanups(frame, it->b);
    if (di->destructor != NULL && var != NULL && ecil_lid_of(var) != 0) {
      emit_local_cleanup(frame, ecil_lid_of(var), var->type, di->destructor);
    }  /* if */
    code_stack_size--;
  } else if (var == NULL) {
    ecil_note_unsupported("an initialization without a variable");
  } else if (var->storage_class == (a_storage_class)sc_static) {
    /* Initialized statically (defined at the declaration). */
    code_stack_size--;
  } else {
    uint32_t  lid = ecil_lid_of(var);

    if (lid == 0) {
      ecil_note_unsupported("the initialization of an undeclared local "
                            "variable");
    } else {
      it->phase = 1;
      it->b = (uint32_t)num_pending_cleanups;
      if (lifetime_needs_cleanups(di->init_expr_lifetime)) {
        ecil_rec0(frame, eop_fullexpr_begin);
        it->c |= 1u;
      }  /* if */
      { a_code_item *init = push_item(cik_init, di, 0);

        if (init != NULL) init->a = lid;
      }
    }  /* if */
  }  /* if */
}  /* do_var_init */


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
        declare_generated_loop_variable(frame, s->expr);
        /* A full expression with temporaries drops its value inside the
           FULLEXPR (a record prvalue is never carried out of it). */
        push_expr(s->expr, (lifetime_has_cleanups(s->expr) ? CIF_DROP : 0u) |
                           CIF_UNUSED);
        done = FALSE;
      } else if (!lifetime_has_cleanups(s->expr) &&
                 value_is_pushed(s->expr, TRUE)) {
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
      /* The initialization of a variable: see do_var_init. */
      it->kind = cik_var_init;
      it->entry = s->variant.dynamic_init;
      it->phase = 0;
      done = FALSE;
      break;
    case stmk_if:
      { a_condition_supplement_ptr  cond = condition_of(s->expr);

        if (it->phase == 0 && cond != NULL) {
          /* if (init; T v = e): a block around the statement holds the
             variables, which are destroyed when it ends. */
          ecil_rec0(frame, eop_block_begin);
          it->c = 1u;
          it->phase = 10;
          push_condition_initialization(cond);
          done = FALSE;
        } else if (it->phase == 10) {
          it->phase = 11;
          push_condition_init(frame, cond);
          done = FALSE;
        } else if (it->phase == 11) {
          it->phase = 1;
          push_expr(cond->expr, CIF_COND);
          done = FALSE;
        } else if (it->phase == 0) {
          it->phase = 1;
          push_expr(s->expr, CIF_COND);
          done = FALSE;
        } else if (it->phase == 1) {
          rec_bool(frame, eop_if_begin,
                   s->variant.if_stmt.else_statement != NULL);
          it->phase = 2;
          push_stmt(s->variant.if_stmt.then_statement);
          done = FALSE;
        } else if (it->phase == 2 &&
                   s->variant.if_stmt.else_statement != NULL) {
          ecil_rec0(frame, eop_if_else);
          it->phase = 3;
          push_stmt(s->variant.if_stmt.else_statement);
          done = FALSE;
        } else {
          ecil_rec0(frame, eop_if_end);
          if (it->c == 1u) ecil_rec0(frame, eop_block_end);
        }  /* if */
      }
      break;
    case stmk_while:
      { a_condition_supplement_ptr  cond = condition_of(s->expr);

        if (it->phase == 0) {
          ecil_rec0(frame, eop_while_begin);
          it->phase = 1;
          if (cond != NULL) {
            /* while (T v = e) body: while (true) { T v = e; if (!v) break;
               body } -- the variable is destroyed in each iteration. */
            ecil_rec2(frame, eop_const_int, ecil_target_int_type(frame), 1);
            ecil_rec0(frame, eop_to_bool);
          } else {
            push_expr(s->expr, CIF_COND);
          }  /* if */
          done = FALSE;
        } else if (it->phase == 1) {
          ecil_rec0(frame, eop_while_body);
          if (cond != NULL) {
            ecil_rec0(frame, eop_block_begin);
            it->phase = 10;
          } else {
            it->phase = 2;
            push_stmt(s->variant.loop_statement);
          }  /* if */
          done = FALSE;
        } else if (it->phase == 10) {
          it->phase = 11;
          push_condition_init(frame, cond);
          done = FALSE;
        } else if (it->phase == 11) {
          it->phase = 12;
          push_expr(cond->expr, CIF_COND);
          done = FALSE;
        } else if (it->phase == 12) {
          emit_bool_not_break(frame);
          it->phase = 2;
          it->c = 1u;
          push_stmt(s->variant.loop_statement);
          done = FALSE;
        } else {
          if (it->c == 1u) ecil_rec0(frame, eop_block_end);
          ecil_rec0(frame, eop_while_end);
        }  /* if */
      }
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
          if (!C_mode() && use_nonstandard_for_init_scope) {
            hoist_for_init_declarations(frame, loop->initialization);
          }  /* if */
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
          /* for (init; T v = e; step) body: the declaration of the
             condition moves into the body: { T v = e; if (!v) break; ... }
             so that it is destroyed in each iteration. */
          a_condition_supplement_ptr  cond = condition_of(s->expr);

          if (cond != NULL && loop->increment != NULL) {
            /* The variable must outlive the step: not expressible by the
               rewrite below. */
            ecil_note_unsupported("a for loop with a condition declaration "
                                  "and an increment");
          }  /* if */
          rec_bool(frame, eop_for_cond, s->expr != NULL && cond == NULL);
          it->phase = 2;
          if (s->expr != NULL && cond == NULL) push_expr(s->expr, CIF_COND);
          done = FALSE;
        } else if (it->phase == 2) {
          rec_bool(frame, eop_for_step, loop->increment != NULL);
          it->phase = 3;
          if (loop->increment != NULL) {
            unsigned int  fl = CIF_UNUSED;

            if (lifetime_has_cleanups(loop->increment)) fl |= CIF_DROP;
            push_expr(loop->increment, fl);
          }  /* if */
          done = FALSE;
        } else if (it->phase == 3) {
          a_condition_supplement_ptr  cond = condition_of(s->expr);

          if (loop->increment != NULL &&
              !lifetime_has_cleanups(loop->increment) &&
              value_is_pushed(loop->increment, TRUE)) {
            ecil_rec0(frame, eop_drop);
          }  /* if */
          ecil_rec0(frame, eop_for_body);
          if (cond != NULL) {
            ecil_rec0(frame, eop_block_begin);
            it->phase = 10;
          } else {
            it->phase = 4;
            if (s->variant.for_loop.statement != NULL) {
              push_stmt(s->variant.for_loop.statement);
            }  /* if */
          }  /* if */
          done = FALSE;
        } else if (it->phase == 10) {
          it->phase = 11;
          push_condition_init(frame, condition_of(s->expr));
          done = FALSE;
        } else if (it->phase == 11) {
          it->phase = 12;
          push_expr(condition_of(s->expr)->expr, CIF_COND);
          done = FALSE;
        } else if (it->phase == 12) {
          emit_bool_not_break(frame);
          it->phase = 13;
          if (s->variant.for_loop.statement != NULL) {
            push_stmt(s->variant.for_loop.statement);
          }  /* if */
          done = FALSE;
        } else if (it->phase == 13) {
          ecil_rec0(frame, eop_block_end);
          it->phase = 4;
          done = FALSE;
        } else {
          ecil_rec0(frame, eop_for_end);
        }  /* if */
      }
      break;
    case stmk_constexpr_if:
      /* Only the branch the front end selected exists in the program. */
      { a_constexpr_if_ptr  cip = s->variant.constexpr_if;
        a_statement_ptr     live = cip->value ? cip->then_statement :
                                                cip->else_statement;

        if (!cip->value_known || condition_of(s->expr) != NULL) {
          ecil_note_unsupported("an if constexpr that is not resolved or "
                                "has an init-statement");
        } else if (live != NULL) {
          it->entry = live;            /* Visit it in place of this one. */
          it->phase = 0;
          done = FALSE;
        }  /* if */
      }
      break;
    case stmk_range_based_for:
      /* { init; T&& __range = e; auto __begin = ..; auto __end = ..;
           for (; __begin != __end; ++__begin) { T v = *__begin; body } }
         The variables have no stmk_init: their initialization items are
         pushed here, one per visit, each with its own cleanup. */
      { a_range_based_for_loop_ptr  rb =
                            s->variant.range_based_for_loop.extra_info;
        a_variable_ptr              var = NULL;

        if (rb == NULL || rb->range == NULL || rb->begin == NULL ||
            rb->end == NULL || rb->iterator == NULL ||
            rb->ne_call_expr == NULL || rb->incr_call_expr == NULL ||
            rb->use_await) {
          ecil_note_unsupported("a range-based for statement that is not "
                                "complete");
          done = TRUE;
        } else if (it->phase == 0) {
          ecil_rec0(frame, eop_block_begin);
          it->phase = 1;
          if (rb->initialization != NULL &&
              rb->initialization->kind == (a_statement_kind)stmk_block) {
            /* The init-statement's declarations stay in scope for the loop
               (as for the init of a for). */
            (void)push_item(cik_stmt_list,
                            rb->initialization->variant.block.statements, 0);
          } else if (rb->initialization != NULL) {
            push_stmt(rb->initialization);
          }  /* if */
          done = FALSE;
        } else if (it->phase >= 1 && it->phase <= 4) {
          /* 1: __range, 2: __begin, 3: __end; 4: FOR_BEGIN and the test. */
          if (it->phase == 1) var = rb->range;
          if (it->phase == 2) var = rb->begin;
          if (it->phase == 3) var = rb->end;
          if (var != NULL) {
            it->phase++;
            declare_variable(frame, var);
            if (ecil_unsupported_reason == NULL &&
                var->init_kind == (an_init_kind)initk_dynamic &&
                var->initializer.dynamic != NULL) {
              (void)push_item(cik_var_init, var->initializer.dynamic, 0);
            } else if (ecil_unsupported_reason == NULL) {
              ecil_note_unsupported("a range-based for variable without a "
                                    "dynamic initialization");
            }  /* if */
          } else {
            ecil_rec0(frame, eop_for_begin);
            rec_bool(frame, eop_for_cond, TRUE);
            it->phase = 5;
            push_expr(rb->ne_call_expr, CIF_COND);
          }  /* if */
          done = FALSE;
        } else if (it->phase == 5) {
          rec_bool(frame, eop_for_step, TRUE);
          it->phase = 6;
          { unsigned int  fl = CIF_UNUSED;

            if (lifetime_has_cleanups(rb->incr_call_expr)) fl |= CIF_DROP;
            push_expr(rb->incr_call_expr, fl);
          }
          done = FALSE;
        } else if (it->phase == 6) {
          if (!lifetime_has_cleanups(rb->incr_call_expr) &&
              value_is_pushed(rb->incr_call_expr, TRUE)) {
            ecil_rec0(frame, eop_drop);
          }  /* if */
          ecil_rec0(frame, eop_for_body);
          ecil_rec0(frame, eop_block_begin);
          it->phase = 7;
          declare_variable_and_bindings(frame, rb->iterator);
          if (ecil_unsupported_reason == NULL &&
              rb->iterator->init_kind == (an_init_kind)initk_dynamic &&
              rb->iterator->initializer.dynamic != NULL) {
            /* The body follows the initialization of the loop variable. */
            if (s->variant.range_based_for_loop.statement != NULL) {
              push_stmt(s->variant.range_based_for_loop.statement);
            }  /* if */
            (void)push_item(cik_var_init,
                            rb->iterator->initializer.dynamic, 0);
          } else if (ecil_unsupported_reason == NULL) {
            ecil_note_unsupported("a range-based for variable without a "
                                  "dynamic initialization");
          }  /* if */
          done = FALSE;
        } else if (it->phase == 7) {
          ecil_rec0(frame, eop_block_end);
          ecil_rec0(frame, eop_for_end);
          ecil_rec0(frame, eop_block_end);
        }  /* if */
      }
      break;
    case stmk_switch:
      { a_condition_supplement_ptr  cond = condition_of(s->expr);

        if (it->phase == 0 && cond != NULL) {
          ecil_rec0(frame, eop_block_begin);
          it->c = 1u;
          it->phase = 10;
          push_condition_initialization(cond);
          done = FALSE;
        } else if (it->phase == 10) {
          it->phase = 11;
          push_condition_init(frame, cond);
          done = FALSE;
        } else if (it->phase == 11) {
          it->phase = 1;
          push_expr(cond->expr, 0);
          done = FALSE;
        } else if (it->phase == 0) {
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
          if (it->c == 1u) ecil_rec0(frame, eop_block_end);
        }  /* if */
      }
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
      if (it->phase == 0) {
        an_expr_node_ptr  value = s->expr;

        if (nrvo_variable != NULL) {
          /* The named return value was built in the return slot. */
          ecil_rec0(frame, eop_return_from_slot);
        } else if (value == NULL && s->variant.return_dynamic_init != NULL) {
          a_dynamic_init_ptr  rdi = s->variant.return_dynamic_init;

          /* C++ builds the return value with a dynamic initialization.  For
             a trivially copyable class it is the value of an expression. */
          if (rdi->kind == (a_dynamic_init_kind)dik_expression &&
              rdi->destructor == NULL && !return_is_nontrivial_abi &&
              !(return_is_record &&
                lifetime_needs_cleanups(rdi->init_expr_lifetime))) {
            value = rdi->variant.expression;
          } else if (rdi->destructor == NULL && return_is_record &&
                     (rdi->kind == (a_dynamic_init_kind)dik_expression ||
                      rdi->kind == (a_dynamic_init_kind)dik_zero ||
                      rdi->kind == (a_dynamic_init_kind)dik_constant ||
                      rdi->kind == (a_dynamic_init_kind)dik_constructor ||
                      rdi->kind == (a_dynamic_init_kind)dik_bitwise_copy ||
                      rdi->kind == (a_dynamic_init_kind)dik_lambda ||
                      rdi->kind == (a_dynamic_init_kind)
                                              dik_nonconstant_aggregate)) {
            /* [FULLEXPR_BEGIN] <initialization of the return slot>
               [FULLEXPR_END] RETURN_FROM_SLOT: the slot is pushed inside the
               full expression. */
            it->phase = 2;
            if (lifetime_needs_cleanups(rdi->init_expr_lifetime)) {
              ecil_rec0(frame, eop_fullexpr_begin);
              it->c |= 1u;
            }  /* if */
            { a_code_item *init = push_item(cik_init, rdi, 0);

              if (init != NULL) {
                init->a = 0;
                init->b = ecil_type_id(frame, return_type);
              }  /* if */
            }
            done = FALSE;
          } else if (rdi->kind ==
                         (a_dynamic_init_kind)dik_class_result_via_ctor &&
                     rdi->destructor == NULL && return_is_record) {
            /* RETURN_SLOT <call> STORE(INIT) RETURN_FROM_SLOT: the call
               constructs the result in the return slot. */
            it->phase = 2;
            if (lifetime_needs_cleanups(rdi->init_expr_lifetime)) {
              ecil_rec0(frame, eop_fullexpr_begin);
              it->c |= 1u;
            }  /* if */
            ecil_rec0(frame, eop_return_slot);
            { a_code_item *store = push_item(cik_store, NULL, 0);

              if (store != NULL) store->a = ECIL_STORE_INIT;
            }
            push_expr(rdi->variant.expression, CIF_DEST);
            done = FALSE;
          } else {
            ecil_note_unsupported("a return value built by a constructor");
          }  /* if */
        }  /* if */
        if (!done) {
          /* The result is being built in the return slot. */
        } else if (nrvo_variable != NULL) {
          /* Done above. */
        } else if (value != NULL && return_is_nontrivial_abi) {
          ecil_note_unsupported("a class returned by value that is not "
                                "trivially copyable");
        } else if (value != NULL && !C_mode() && is_void_type(return_type)) {
          /* return void_expression; the expression is a statement. */
          it->phase = 3;
          push_expr(value, lifetime_has_cleanups(value) ? CIF_DROP : 0u);
          done = FALSE;
        } else if (value != NULL) {
          it->phase = 1;
          push_expr(value, 0);
          done = FALSE;
        } else if (ecil_unsupported_reason == NULL) {
          rec_bool(frame, eop_return_stmt, FALSE);
        }  /* if */
      } else if (it->phase == 3) {
        if (!lifetime_has_cleanups(s->expr) && node_pushes_value(s->expr)) {
          ecil_rec0(frame, eop_drop);
        }  /* if */
        rec_bool(frame, eop_return_stmt, FALSE);
      } else if (it->phase == 2) {
        if ((it->c & 1u) != 0) ecil_rec0(frame, eop_fullexpr_end);
        ecil_rec0(frame, eop_return_from_slot);
      } else {
        rec_bool(frame, eop_return_stmt, TRUE);
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
      { const char  *name = ecil_statement_kind_name((int)s->kind);
        char        reason[120];

        if (name != NULL) {
          (void)snprintf(reason, sizeof(reason), "the statement \"%.60s\"",
                         name);
          ecil_note_unsupported(reason);
        } else {
          ecil_note_unsupported("this kind of statement");
        }  /* if */
      }
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
      case cik_ctor_call: do_ctor_call(frame, idx); break;
      case cik_var_init:  do_var_init(frame, idx); break;
      case cik_new:       do_new_delete(frame, idx); break;
      case cik_meminit:   do_meminit(frame, idx); break;
      case cik_store:
        ecil_rec_begin(frame, eop_store);
        ecil_arg_uint(frame, code_stack[idx].a);
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


static void check_cxx_function(a_routine_ptr rout, a_scope_ptr scope)
/*
Diagnose the C++ function-level constructs the writer does not translate yet,
so that none is silently dropped (a wrong function is never sent).  Each
check goes when its feature is implemented.
*/
{
  if (!C_mode()) {
    if (rout->is_virtual) {
      ecil_note_unsupported("a virtual function");
    } else if (rout->special_kind ==
                               (a_special_function_kind)sfk_constructor ||
               rout->special_kind ==
                               (a_special_function_kind)sfk_destructor) {
      a_type_ptr  cls = function_class;

      if (cls == NULL || !is_immediate_class_type(cls)) {
        ecil_note_unsupported("a constructor or destructor without a class");
      } else if (cls->variant.class_struct_union.
                          any_virtual_functions_including_in_base_classes ||
                 cls->variant.class_struct_union.any_virtual_base_classes) {
        /* The constructor must set the vptr and construct virtual bases. */
        ecil_note_unsupported("a constructor or destructor of a class with "
                              "virtual functions or virtual bases");
      }  /* if */
    }  /* if */
  }  /* if */
  (void)scope;
}  /* check_cxx_function */


static a_boolean routine_is_root(a_routine_ptr rout)
/*
Return TRUE for a routine that is needed although nothing refers to it: one
with the constructor, destructor or used attribute.  Dropping its definition
must be an error, not a warning (the library erases dropped definitions that
nothing references).
*/
{
#if GNU_EXTENSIONS_ALLOWED
  return rout->is_initialization_routine || rout->is_finalization_routine ||
         rout->has_gnu_used_attribute;
#else /* !GNU_EXTENSIONS_ALLOWED */
  (void)rout;
  return FALSE;
#endif /* GNU_EXTENSIONS_ALLOWED */
}  /* routine_is_root */


a_boolean ecil_variable_is_root(a_variable_ptr var)
/*
Return TRUE for a variable that is needed although nothing refers to it (the
used attribute).  See routine_is_root.
*/
{
#if GNU_EXTENSIONS_ALLOWED
#if GNU_INIT_PRIORITY_ATTRIBUTE_ALLOWED
  return var->has_gnu_used_attribute || var->init_priority != 0;
#else /* !GNU_INIT_PRIORITY_ATTRIBUTE_ALLOWED */
  return var->has_gnu_used_attribute;
#endif /* GNU_INIT_PRIORITY_ATTRIBUTE_ALLOWED */
#else /* !GNU_EXTENSIONS_ALLOWED */
  (void)var;
  return FALSE;
#endif /* GNU_EXTENSIONS_ALLOWED */
}  /* ecil_variable_is_root */


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
    if (ecil_gid_info(gid)->partner != 0) {
      ecil_gid_info(ecil_gid_info(gid)->partner)->entity = rout;
    }  /* if */
    ecil_reissue_function(frame, gid, TRUE, FALSE);
  }  /* if */
  ecil_frame_begin_body(frame);
  mark = ecil_frame_mark(frame);
  static_init_var = NULL;
  static_temp_count = 0;
  agg_lambda = NULL;
  next_lid = 0;
  this_lid = 0;
  function_scope = scope;
  function_class = NULL;
  num_pending_cleanups = 0;
  if (!C_mode() && scope->variant.routine.this_param_variable != NULL &&
      skip_typerefs(scope->variant.routine.this_param_variable->type)->kind ==
                                                               tk_pointer) {
    function_class = skip_typerefs(skip_typerefs(
        scope->variant.routine.this_param_variable->type)->variant.pointer.
                                                                       type);
  }  /* if */
  nrvo_variable = C_mode() ? NULL :
                  scope->variant.routine.return_value_variable;
  return_type = skip_typerefs(rout->type)->variant.routine.return_type;
  in_field_initializer = FALSE;
  return_is_nontrivial_abi = !C_mode() &&
                             ecil_class_is_nontrivial_abi(return_type);
  return_is_record = !C_mode() &&
                     (is_immediate_class_type(skip_typerefs(return_type)) ||
                      skip_typerefs(return_type)->kind == tk_array);
  if (gid != 0 && ecil_unsupported_reason == NULL) {
    a_variable_ptr  param;
    a_boolean       prototyped = rout_type_supp(
                                   skip_typerefs(rout->type))->prototyped;
    a_boolean       reachable = TRUE;

    check_definition_return_type(frame, rout);
    check_cxx_function(rout, scope);
    if (ecil_unsupported_reason == NULL) {
      ecil_rec2(frame, eop_func_begin, ecil_function_body_id(gid), 0);
    }  /* if */
    if (ecil_unsupported_reason == NULL &&
        scope->variant.routine.this_param_variable != NULL) {
      /* The object pointer of a non-static member function comes first. */
      a_variable_ptr  this_var = scope->variant.routine.this_param_variable;
      uint32_t        this_type = ecil_type_id(frame, this_var->type);

      if (this_type != 0 && ecil_unsupported_reason == NULL) {
        uint32_t  lid = new_lid();
        uint32_t  this_name = ecil_intern_cstring(frame, "this");

        ecil_set_lid(this_var, lid);
        this_lid = lid;
        ecil_rec_begin(frame, eop_param);
        ecil_arg_uint(frame, lid);
        ecil_arg_uint(frame, this_type);
        ecil_arg_uint(frame, this_type);
        ecil_arg_uint(frame, this_name);
        ecil_arg_uint(frame, ECIL_PARAM_THIS);
        ecil_rec_end(frame);
      }  /* if */
    }  /* if */
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
    if (ecil_unsupported_reason == NULL && !C_mode() &&
        rout->special_kind == (a_special_function_kind)sfk_destructor) {
      emit_dtor_prologue(frame, scope);
    }  /* if */
    if (ecil_unsupported_reason == NULL && scope->assoc_block != NULL) {
      push_stmt(scope->assoc_block);
      if (!C_mode() &&
          rout->special_kind == (a_special_function_kind)sfk_constructor &&
          scope->variant.routine.constructor_inits != NULL) {
        (void)push_item(cik_meminit, scope->variant.routine.constructor_inits,
                        0);
      }  /* if */
      run_code_stack(frame);
    }  /* if */
    if (ecil_unsupported_reason == NULL && num_pending_cleanups != 0) {
      /* In a constructor: a temporary that a mem-initializer binds to a
         reference member lives until the constructor exits, but a MEMINIT
         ends its locals (spec 11.5). */
      ecil_note_unsupported("a lifetime-extended temporary outside an "
                            "initialization");
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
    if (routine_is_root(rout)) {
      cir_be_error(message);
    } else {
      cir_be_warning(message);
    }  /* if */
  }  /* if */
  code_stack_size = 0;
  reset_agg_levels();
  static_init_var = NULL;
  static_temp_count = 0;
  agg_lambda = NULL;
  nrvo_variable = NULL;
  return_type = NULL;
  return_is_nontrivial_abi = FALSE;
  return_is_record = FALSE;
  this_lid = 0;
  function_scope = NULL;
  function_class = NULL;
  num_pending_cleanups = 0;
  ecil_end_lid_epoch(next_lid);
  ecil_set_function_state(rout, dropped ? efs_dropped : efs_streamed);
}  /* emit_function */


/* The static invokers of lambdas. */

STATIC_THREAD a_routine_ptr
                *pending_invokers;
STATIC_THREAD size_t
                num_pending_invokers;
STATIC_THREAD size_t
                pending_invokers_capacity;

void ecil_queue_invoker(a_routine_ptr entry_point)
/*
Remember a lambda's static invoker (its entry point routine has no body in
the IL) whose G-id was just introduced.
*/
{
  if (num_pending_invokers == pending_invokers_capacity) {
    size_t         new_capacity = (pending_invokers_capacity == 0) ?
                                  8 : pending_invokers_capacity * 2;
    a_routine_ptr  *new_list = (a_routine_ptr *)realloc(pending_invokers,
                                    new_capacity * sizeof(a_routine_ptr));

    if (new_list != NULL) {
      pending_invokers = new_list;
      pending_invokers_capacity = new_capacity;
    }  /* if */
  }  /* if */
  if (num_pending_invokers < pending_invokers_capacity) {
    pending_invokers[num_pending_invokers++] = entry_point;
  }  /* if */
}  /* ecil_queue_invoker */


static void emit_invoker_body(an_ecil_frame *frame, a_routine_ptr ep)
/*
Synthesize the body of a lambda's static invoker: it calls the call operator
with a null object pointer and forwards its parameters (spec 11.9).  A
variadic lambda has no invoker that forwards its arguments.
*/
{
  a_routine_ptr   callop = ep->variant.lambda_call_operator;
  uint32_t        gid = ecil_gid_of(ep);
  a_type_ptr      ft = skip_typerefs(ep->type);
  a_type_ptr      rt = ft->variant.routine.return_type;
  an_ecil_mark    mark;
  a_param_type_ptr
                  ptp;
  uint32_t        first_param = 0;
  uint32_t        n_params = 0;
  uint32_t        closure_ptr;
  a_boolean       is_void;
  a_boolean       by_slot;

  ecil_unsupported_reason = NULL;
  ecil_complete_pending_records(frame);
  ecil_reissue_function(frame, gid, TRUE, FALSE);
  ecil_frame_begin_body(frame);
  mark = ecil_frame_mark(frame);
  next_lid = 0;
  this_lid = 0;
  function_scope = NULL;
  function_class = NULL;
  static_init_var = NULL;
  static_temp_count = 0;
  agg_lambda = NULL;
  nrvo_variable = NULL;
  return_type = NULL;
  num_pending_cleanups = 0;
  closure_ptr = ecil_ptr_type(frame, ecil_type_id(frame,
                                                  parent_class_of(callop)));
  if (rout_type_supp(ft)->has_ellipsis) {
    ecil_note_unsupported("the static invoker of a variadic lambda");
  }  /* if */
  is_void = is_void_type(rt);
  by_slot = !is_void && ecil_class_is_nontrivial_abi(rt);
  if (ecil_unsupported_reason == NULL) {
    uint32_t  callee = ecil_function_id(frame, callop);
    uint32_t  rtg = is_void ? 0 : ecil_type_id(frame, rt);

    ecil_rec2(frame, eop_func_begin, gid, 0);
    for (ptp = rout_type_supp(ft)->param_type_list; ptp != NULL;
         ptp = ptp->next) {
      uint32_t  pt = ecil_type_id(frame, ptp->type);
      uint32_t  lid = new_lid();

      if (n_params == 0) first_param = lid;
      n_params++;
      ecil_rec_begin(frame, eop_param);
      ecil_arg_uint(frame, lid);
      ecil_arg_uint(frame, pt);
      ecil_arg_uint(frame, pt);
      ecil_arg_nil(frame);
      ecil_arg_uint(frame, 0);
      ecil_rec_end(frame);
    }  /* for */
    if (by_slot) ecil_rec0(frame, eop_return_slot);
    ecil_rec1(frame, eop_const_null, closure_ptr);
    for (ptp = rout_type_supp(ft)->param_type_list, n_params = 0;
         ptp != NULL; ptp = ptp->next) {
      ecil_rec1(frame, eop_lref, first_param + n_params);
      ecil_rec1(frame, eop_load, 0);
      n_params++;
    }  /* for */
    emit_call_record(frame, callee, callop, 1 + n_params);
    if (by_slot) {
      ecil_rec_begin(frame, eop_store);
      ecil_arg_uint(frame, ECIL_STORE_INIT);
      ecil_arg_uint(frame, (unsigned int)esr_none);
      ecil_rec_end(frame);
      ecil_rec0(frame, eop_return_from_slot);
    } else {
      rec_bool(frame, eop_return_stmt, !is_void);
    }  /* if */
    (void)rtg;
#if CHECKING
    if (ecil_unsupported_reason == NULL) ecil_check_function_lids(next_lid);
#endif /* CHECKING */
    ecil_rec1(frame, eop_func_end, 0u);
  }  /* if */
  if (ecil_unsupported_reason != NULL) {
    ecil_frame_truncate(frame, mark);
    ecil_unsupported_reason = NULL;
    ecil_reissue_function(frame, gid, FALSE, TRUE);
    ecil_unsupported_reason = NULL;
  }  /* if */
  ecil_end_lid_epoch(next_lid);
}  /* emit_invoker_body */


static void emit_invoker(an_ecil_frame *frame, a_routine_ptr ep)
/*
Synthesize the body of a lambda's static invoker unless it is defined.
*/
{
  uint32_t  gid = ecil_gid_of(ep);

  if (gid != 0 && ep->variant.lambda_call_operator != NULL &&
      (ecil_gid_info(gid)->flags & ECIL_GF_DEFINED) == 0) {
    emit_invoker_body(frame, ep);
  }  /* if */
}  /* emit_invoker */


a_boolean ecil_invokers_pending(void)
/*
Return TRUE if a static invoker is queued.
*/
{
  return num_pending_invokers != 0;
}  /* ecil_invokers_pending */


void ecil_drain_invokers(an_ecil_frame *frame)
/*
Synthesize the queued static invokers (emitting one can queue no more).
*/
{
  size_t  i;

  for (i = 0; i < num_pending_invokers; i++) {
    emit_invoker(frame, pending_invokers[i]);
  }  /* for */
  num_pending_invokers = 0;
}  /* ecil_drain_invokers */


void ecil_stream_static_init(an_ecil_frame *frame, a_dynamic_init_ptr di)
/*
One entry of the file scope's list of dynamic initializations, in the body
section of F_GLOBALS: a STATIC_INIT with a fresh L-id space.  If it cannot be
expressed, its records are discarded and the variable is re-issued as an
unsupported definition (spec 8.11).
*/
{
  a_variable_ptr  var = di->variable;
  uint32_t        gid = var != NULL ? ecil_gid_of(var) : 0;

  if (gid != 0 &&
      (ecil_gid_info(gid)->flags & ECIL_GF_NEEDS_STATIC_INIT) != 0 &&
      (ecil_gid_info(gid)->flags & ECIL_GF_DEFINED) != 0) {
    an_ecil_mark  mark = ecil_frame_mark(frame);

    ecil_unsupported_reason = NULL;
    next_lid = 0;
    this_lid = 0;
    function_scope = NULL;
    function_class = NULL;
    static_init_var = NULL;
    static_temp_count = 0;
    agg_lambda = NULL;
    nrvo_variable = NULL;
    return_type = NULL;
    num_pending_cleanups = 0;
#if CHECKING
    ecil_check_begin_lids();
#endif /* CHECKING */
    (void)push_item(cik_var_init, di, 0);
    run_code_stack(frame);
#if CHECKING
    if (ecil_unsupported_reason == NULL) ecil_check_function_lids(next_lid);
#endif /* CHECKING */
    ecil_end_lid_epoch(next_lid);
    static_init_var = NULL;
    if (ecil_unsupported_reason != NULL) {
      char                    message[400];
      const an_ecil_gid_info  *info;

      ecil_frame_truncate(frame, mark);
      (void)snprintf(message, sizeof(message),
                     "the initializer of \"%.200s\" uses %.100s, which the "
                     "CIR back end does not support; the definition is "
                     "dropped",
                     var->source_corresp.name != NULL ?
                                 var->source_corresp.name : "?",
                     ecil_unsupported_reason);
      ecil_unsupported_reason = NULL;
      info = ecil_gid_info(gid);
      ecil_emit_dropped_decl_var(frame, gid, info->decl_type,
                                 info->decl_linkage, info->decl_flags,
                                 (uint32_t)ecil_variable_alignment(var),
                                 info->decl_name);
      ecil_gid_info(gid)->flags &= ~(ECIL_GF_DEFINED |
                                    ECIL_GF_NEEDS_STATIC_INIT);
      if (ecil_variable_is_root(var)) {
        cir_be_error(message);
      } else {
        cir_be_warning(message);
      }  /* if */
    }  /* if */
  }  /* if */
}  /* ecil_stream_static_init */


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
  ecil_drain_invokers(frame);
  ecil_forget_local_pending_records();
  innermost_function_scope = saved_function_scope;
  curr_il_region_number = saved_region;
}  /* ecil_stream_function_region */


/* Conditionally close the "edg" namespace. */
END_EDG_NAMESPACE

#endif /* ECIL_WRITER_ENABLED */
