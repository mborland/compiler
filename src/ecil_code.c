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
  cik_meminit,      /* The member initializers of a constructor body. */
  cik_emit          /* Emit the record without operands whose opcode is a. */
};

#define NO_SELF ((size_t)-1)   /* emit_ctor_loop: no item asks. */

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

/* Phases of the assignment of a class without data: nothing is copied. */
#define EMPTY_ASSIGN_RHS 9u     /* The right operand, discarded. */
#define EMPTY_ASSIGN_LHS 10u    /* ... then the target. */

/* Phases of a subscript with the integer first, as in 2[p]: the integer is
   evaluated before the pointer (C++17), so it is kept in a temporary. */
#define SUBSCRIPT_INT_FIRST 7u  /* The integer: value in a temporary. */
#define SUBSCRIPT_PTR_NEXT  8u  /* ... and the pointer is evaluated next. */

/* A for statement under the old scope rule: its init was sent before
   FOR_BEGIN. */
#define FOR_OLD_INIT_DONE 20u

/* cik_new state (it->c): the cookie size is in the bits above 8. */
#define NEW_ARRAY       0x01u
#define NEW_DYNAMIC     0x02u     /* The count is in the local it->a. */
#define NEW_LIST        0x20000000u  /* new T[n]{...} with a run-time n. */
#define NEW_FULLEXPR    0x40000000u  /* ... with its own FULLEXPR. */
#define NEW_NOTHROW     0x80000000u  /* The allocation function returns null on
                                     failure. */
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
  uint32_t      d;                /* More scratch (a new[] list: the local
                                     that holds the pointer). */
  /* A member call devirtualized to the final overrider of the function it
     names (spec 12.6): the object expression without the front end's
     conversion to the class that declares the function, the function that
     is called, the entry of its class in the base list of the object's
     class (NULL: the object's class itself) and the object's class. */
  void          *object;
  a_routine_ptr devirt_routine;
  a_base_class_ptr
                devirt_base;
  a_type_ptr    devirt_class;
} a_code_item;

STATIC_THREAD a_code_item
                *code_stack;
STATIC_THREAD size_t
                code_stack_size;
STATIC_THREAD size_t
                code_stack_capacity;
STATIC_THREAD a_boolean
                function_try_open;/* TRY_BEGIN(FUNCTION_TRY) of the function's
                                     try block was sent with the header (a
                                     constructor or destructor). */
STATIC_THREAD size_t
                fullexpr_depth;   /* How many FULLEXPRs are open. */

/* A new expression whose storage is freed if its initialization throws: the
   locals that keep what the free needs (the allocation function's result, the
   size and the placement operands), evaluated once (spec 12.6).  The entries
   nest as the new expressions do. */
#define NEW_MAX_ARGS 8
typedef struct a_new_cleanup {
  uint32_t      size_lid;
  uint32_t      ptr_lid;
  uint32_t      arg_lids[NEW_MAX_ARGS];
  unsigned int  num_args;
  a_routine_ptr del;
  a_boolean     sized;
} a_new_cleanup;
STATIC_THREAD a_new_cleanup
                *new_cleanups;
STATIC_THREAD size_t
                num_new_cleanups;
STATIC_THREAD size_t
                new_cleanups_capacity;

/* The destination that an arm of a class ?: builds its value in when the
   conditional initializes a temporary: the temporary that the master entry
   (the dynamic initialization of that temporary) stands for. */
typedef struct a_master_dest {
  a_dynamic_init_ptr  master;
  uint32_t            dest;
} a_master_dest;
STATIC_THREAD a_master_dest
                *master_dests;
STATIC_THREAD size_t
                num_master_dests;
STATIC_THREAD size_t
                master_dests_capacity;
STATIC_THREAD size_t
                cond_depth;       /* How many conditional regions (a TERN
                                     arm, the right operand of LAND or LOR)
                                     enclose the expression being
                                     translated. */
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
/* An entry of the label search stack: a statement, and whether the
   statements after it in its list are searched too. */
typedef struct a_label_search_item {
  a_statement_ptr  stmt;
  a_boolean        siblings;
} a_label_search_item;

STATIC_THREAD an_expr_node_ptr
                suppress_load_node;  /* The node whose lvalue-to-rvalue LOAD
                                        is not emitted (the glvalue stays),
                                        or NULL; see do_operation. */
STATIC_THREAD a_boolean
                load_suppressed;     /* ... and the load was suppressed. */
STATIC_THREAD a_label_search_item
                *label_stack;     /* The stack of statement_may_have_label. */
STATIC_THREAD size_t
                label_stack_capacity;
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
/* The static temporaries made while one function is translated, so that the
   second walk of a constructor of a class with virtual bases finds them
   (spec 12.3: a second walk introduces no new G-id). */
typedef struct a_static_temp {
  a_variable_ptr  owner;
  unsigned int    seq;
  uint32_t        gid;
} a_static_temp;

STATIC_THREAD a_static_temp
                *static_temps;
STATIC_THREAD size_t
                num_static_temps;
STATIC_THREAD size_t
                static_temps_capacity;

STATIC_THREAD unsigned int
                walk_variant;     /* The variant of the structor of a class
                                     with virtual bases being walked
                                     (ECIL_VARIANT_COMPLETE for C1,
                                     ECIL_VARIANT_BASE for C2 and D2); else
                                     ECIL_VARIANT_NONE (spec 12.3). */

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
STATIC_THREAD a_boolean
                vptr_init_pending;/* The constructor being translated is of
                                     a dynamic class and has not had its
                                     VPTR_INIT yet. */

void ecil_code_reset(void)
/*
Release the writer's function-body storage at the end of a compilation.
*/
{
  while (num_agg_levels > 0) {
    ecil_init_level_end(&agg_levels[--num_agg_levels]);
  }  /* while */
  free(code_stack);
  free(new_cleanups);
  new_cleanups = NULL;
  num_new_cleanups = new_cleanups_capacity = 0;
  free(function_states);
  free(static_temps);
  static_temps = NULL;
  num_static_temps = static_temps_capacity = 0;
  free(agg_levels);
  free(pending_cleanups);
  free(pure_stack);
  pure_stack = NULL;
  pure_stack_capacity = 0;
  free(label_stack);
  label_stack = NULL;
  label_stack_capacity = 0;
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


static void emit_vptr_init(an_ecil_frame *frame)
/*
VPTR_INIT in a constructor or destructor of a dynamic class (the pointee of
this): the vtable facts first, and the vtable is used (spec 12.4, 12.8).
*/
{
  ecil_note_vtable_used(frame, function_class);
  if (ecil_unsupported_reason == NULL) ecil_rec0(frame, eop_vptr_init);
}  /* emit_vptr_init */


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
    item->d = 0;
    item->object = NULL;
    item->devirt_routine = NULL;
    item->devirt_base = NULL;
    item->devirt_class = NULL;
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


static a_boolean is_array_temp_init(an_expr_node_ptr node)
/*
Return TRUE for the initialization of a temporary of array type (a reference
binds to the temporary itself).
*/
{
  return node->kind == (an_expr_node_kind)enk_temp_init &&
         skip_typerefs(node->type)->kind == tk_array;
}  /* is_array_temp_init */


static a_boolean is_discarded_conditional_temp(an_expr_node_ptr node,
                                               a_boolean        unused)
/*
Return TRUE for the temporary that holds the value of a class conditional
(?:) that the context discards: it is initialized by the conditional itself
(not through a master entry), whose arms are the objects.
*/
{
  a_dynamic_init_ptr  di = NULL;
  an_expr_node_ptr    expr;
  a_boolean           result = FALSE;

  if ((node->kind == (an_expr_node_kind)enk_temp_init ||
       node->kind == (an_expr_node_kind)enk_lambda) &&
      (node->result_is_not_used || unused)) {
    di = node->variant.init.dynamic_init;
  }  /* if */
  if (di != NULL && di->master_entry == NULL &&
      di->kind == (a_dynamic_init_kind)dik_expression &&
      !di->class_rvalue_initialized_through_master_entry) {
    expr = skip_parens(di->variant.expression);
    result = expr->kind == (an_expr_node_kind)enk_operation &&
             expr->variant.operation.kind == eok_question &&
             !is_glvalue_node(expr);
  }  /* if */
  return result;
}  /* is_discarded_conditional_temp */


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
    } else if (node->kind == (an_expr_node_kind)enk_operation &&
               node->variant.operation.kind == eok_class_rvalue_adjust &&
               node->variant.operation.operands != NULL &&
               (is_glvalue_node(node) ||
                !is_glvalue_node(skip_parens(
                                     node->variant.operation.operands)))) {
      /* The adjustment changes only the cv-qualification of a class
         prvalue.  Translation passes through it without the unused flag,
         and so does this.  The adjustment of a glvalue to a prvalue is a
         load, which pushes. */
      node = node->variant.operation.operands;
      unused = FALSE;
      walking = TRUE;
    }  /* if */
  }  /* while */
  if (is_void_node_type(node)) {
    pushes = FALSE;
  } else if (node->kind == (an_expr_node_kind)enk_operation &&
             is_assignment_operator(node->variant.operation.kind) &&
             node->result_is_not_used) {
    pushes = FALSE;
  } else if (node->kind == (an_expr_node_kind)enk_operation &&
             node->variant.operation.kind == eok_question &&
             !is_glvalue_node(node) && !C_mode() &&
             is_immediate_class_type(skip_typerefs(node->type)) &&
             (node->result_is_not_used || unused)) {
    /* A discarded class conditional is a void TERN: each arm is discarded
       (a trivial class value is dropped in its arm). */
    pushes = FALSE;
  } else if (is_discarded_conditional_temp(node, unused)) {
    /* Its conditional is translated as a discarded one. */
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


static void emit_fullexpr_begin(an_ecil_frame *frame)
/*
FULLEXPR_BEGIN, counted: a throw in a conditional region needs one around the
region (spec 12.5), and does not add one when a full expression is open.
*/
{
  ecil_rec0(frame, eop_fullexpr_begin);
  fullexpr_depth++;
}  /* emit_fullexpr_begin */


static void emit_fullexpr_end(an_ecil_frame *frame)
/*
FULLEXPR_END.
*/
{
  ecil_rec0(frame, eop_fullexpr_end);
  if (fullexpr_depth > 0) fullexpr_depth--;
}  /* emit_fullexpr_end */


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

          if (g != 0 && c->variant.address.kind == (an_address_base_kind)
                                                              abk_typeid) {
            /* Typeinfo is named only by TYPEID and C_ADDR: &typeid(T) is
               the address of the glvalue that TYPEID yields. */
            if (info_of(t)->kind != egk_ptr) {
              ecil_note_unsupported("a typeid address of integer type");
            } else {
              ecil_rec_begin(frame, eop_typeid);
              ecil_arg_uint(frame, g);
              ecil_arg_uint(frame, info_of(t)->ref);
              ecil_arg_uint(frame, 0);
              ecil_rec_end(frame);
              ecil_rec0(frame, eop_addr);
            }  /* if */
          } else if (g != 0) {
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
      case ck_ptr_to_member:
        (void)ecil_emit_ptr_to_member_constant(frame, c, FALSE);
        break;
      case ck_void:
        /* A value of type void: nothing is pushed. */
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
  if (node == suppress_load_node) {
    /* The glvalue itself is wanted (the address of the right operand of a
       class assignment). */
    suppress_load_node = NULL;
    load_suppressed = TRUE;
  } else if (!is_glvalue_node(node)) {
    uint32_t    gid = ecil_type_id(frame, node->type);
    a_type_ptr  t = skip_typerefs(node->type);

    if (gid != 0 && (info_of(gid)->kind == egk_opaque ||
                     (info_of(gid)->flags & ECIL_GF_FAILED) != 0)) {
      ecil_note_unsupported_type(gid, "a value of an unsupported type");
    } else if (gid != 0 && (info_of(gid)->flags & ECIL_GF_DYNAMIC) != 0 &&
               info_of(gid)->kind == egk_record) {
      /* Never a LOAD of a record with a vptr (a by-value argument of a
         NONTRIVIAL_ABI class has its own LOAD): a copy is a constructor
         call (spec 12.2). */
      ecil_note_unsupported("a value of a class with virtual functions "
                            "(copied other than by a constructor)");
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


static void emit_base_chain(an_ecil_frame *frame,
                            a_type_ptr    derived,
                            a_base_class_ptr bcp,
                            a_boolean     is_base_cast,
                            unsigned int  flags,
                            a_boolean     complete)
/*
The casts of a pointer between a class and one of its bases (the entry bcp of
its base list): BASE_CAST (derived to base) or DERIVED_CAST (base to derived),
one per step of the derivation, since each cast is to or from a direct base
(spec 11.3).  Each cast has the byte offset of its base subobject from the
base list of the class the step starts at.  A virtual base, or a base reached
through one, starts with VBASE_CAST to the virtual base (spec 12.4), COMPLETE
when the object is known to be the complete object (complete); a conversion
from a virtual base to its derived class is ill-formed (a static_cast) and
unsupported.
*/
{
  a_base_class_ptr  *steps = NULL;
  size_t            nsteps = 0;
  a_derivation_step_ptr
                    step;
  a_boolean         done = FALSE;
  a_boolean         virtual_start = bcp->is_virtual ||
                                    (bcp->derivation != NULL &&
                                     any_virtual_steps_in_derivation(bcp));

  if (bcp->derivation == NULL) {
    ecil_note_unsupported("a cast to a base class without a derivation");
  } else if (virtual_start && !is_base_cast) {
    ecil_note_unsupported("a cast from a virtual base class to its derived "
                          "class");
  } else if (bcp->is_virtual) {
    /* One step: the virtual base itself (the last step of its derivation). */
    steps = (a_base_class_ptr *)malloc(2 * sizeof(a_base_class_ptr));
    if (steps == NULL) {
      ecil_note_unsupported("out of memory in the ECIL writer");
    } else {
      steps[0] = bcp;
      nsteps = 1;
    }  /* if */
  } else {
    for (step = bcp->derivation->path; step != NULL && !done;
         step = step->next) {
      nsteps++;
      done = step == bcp->derivation->path_tail;
    }  /* for */
    steps = (a_base_class_ptr *)malloc((nsteps + 1) *
                                       sizeof(a_base_class_ptr));
    if (steps == NULL) {
      ecil_note_unsupported("out of memory in the ECIL writer");
    } else {
      size_t  i = 0;

      done = FALSE;
      for (step = bcp->derivation->path; step != NULL && !done;
           step = step->next) {
        steps[i++] = step->base_class;
        done = step == bcp->derivation->path_tail;
      }  /* for */
    }  /* if */
  }  /* if */
  if (ecil_unsupported_reason == NULL) {
    uint32_t  tder = ecil_type_id(frame, derived);
    uint32_t  prev = tder;
    uint64_t  total = 0;
    size_t    i;

    for (i = 0; i < nsteps && tder != 0 && ecil_unsupported_reason == NULL;
         i++) {
      /* The base-to-derived chain walks the steps backwards. */
      a_base_class_ptr  sb = steps[is_base_cast ? i : nsteps - 1 - i];
      uint32_t          sg = ecil_type_id(frame, sb->type);
      uint32_t          from_gid = 0;
      uint32_t          to_gid = 0;

      if (is_base_cast) {
        from_gid = prev;
        to_gid = sg;
      } else {
        a_base_class_ptr  below = nsteps - 1 - i > 0 ?
                                  steps[nsteps - 2 - i] : NULL;

        from_gid = sg;
        to_gid = below != NULL ? ecil_type_id(frame, below->type) : tder;
      }  /* if */
      if (sg != 0 && to_gid != 0 && ecil_unsupported_reason == NULL) {
        if (i == 0 && virtual_start && sb->is_virtual) {
          /* The facts of the class come before VBASE_CAST (spec 12.2). */
          ecil_class_vtable_facts(frame, derived);
        }  /* if */
        if (ecil_unsupported_reason != NULL) {
          /* (The reason is noted.) */
        } else if (i == 0 && virtual_start && sb->is_virtual) {
          ecil_rec_begin(frame, eop_vbase_cast);
          ecil_arg_uint(frame, from_gid);
          ecil_arg_uint(frame, to_gid);
          ecil_arg_uint(frame, (flags & ECIL_CAST_NONNULL) |
                               (complete ? ECIL_VBASE_CAST_COMPLETE : 0u));
          ecil_rec_end(frame);
        } else {
          ecil_rec_begin(frame, is_base_cast ? eop_base_cast :
                                               eop_derived_cast);
          ecil_arg_uint(frame, from_gid);
          ecil_arg_uint(frame, to_gid);
          ecil_arg_uint(frame, (uint64_t)sb->offset);
          ecil_arg_uint(frame, flags);
          ecil_rec_end(frame);
          total += (uint64_t)sb->offset;
        }  /* if */
        prev = sg;
      }  /* if */
    }  /* for */
#if CHECKING
    if (ecil_unsupported_reason == NULL && !virtual_start) {
      /* The offsets of the steps add up to the offset of the base. */
      check_assertion(total == (uint64_t)bcp->offset);
    }  /* if */
#endif /* CHECKING */
    (void)total;
  }  /* if */
  free(steps);
}  /* emit_base_chain */


static a_boolean operand_is_this_parameter(an_expr_node_ptr node)
/*
Return TRUE if the node is the this pointer of the function being translated
(possibly under parentheses).
*/
{
  node = skip_parens(node);
  return node->kind == (an_expr_node_kind)enk_variable &&
         node_variable(node)->is_this_parameter;
}  /* operand_is_this_parameter */


static void emit_class_cast(an_ecil_frame *frame, an_expr_node_ptr node)
/*
eok_base_class_cast and eok_derived_class_cast between a class and one of its
non-virtual bases: [ADDR] BASE_CAST|DERIVED_CAST... [DEREF].  The operand
(already translated) is a pointer, or a class glvalue (then the address is
taken and the result is a glvalue again).  A conversion through several
levels is a chain of casts (emit_base_chain).
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
    for (bcp = class_type_supp(derived)->base_classes; bcp != NULL;
         bcp = bcp->next) {
      if (skip_typerefs(bcp->type) == base_class) break;
    }  /* for */
    if (bcp == NULL) {
      ecil_note_unsupported("a cast to a class that is not a base class");
    } else if (bcp->derivation == NULL) {
      ecil_note_unsupported("a cast to a base class without a derivation");
    } else if (bcp->ambiguous) {
      ecil_note_unsupported("a cast to an ambiguous base class");
    }  /* if */
  }  /* if */
  if (ecil_unsupported_reason == NULL) {
    unsigned int  flags = 0;

    if (glvalue_operand) {
      ecil_rec0(frame, eop_addr);
      flags |= ECIL_CAST_NONNULL;
    } else if (node->variant.operation.implicit_in_member_naming ||
               operand_is_known_non_null(op)) {
      flags |= ECIL_CAST_NONNULL;
    }  /* if */
    emit_base_chain(frame, derived, bcp, is_base_cast, flags,
                    walk_variant == ECIL_VARIANT_COMPLETE &&
                    derived == function_class &&
                    operand_is_this_parameter(op));
    if (glvalue_operand && ecil_unsupported_reason == NULL) {
      ecil_rec0(frame, eop_deref);
      emit_load_if_value(frame, node);
    }  /* if */
  }  /* if */
}  /* emit_class_cast */


/* Per-call item bits (a_code_item.c). */
#define COND_FULLEXPR        0x100u /* ? :, && or ||: its FULLEXPR is open
                                       (a throw in its operands). */
#define CALL_MEMBER_DOT      0x01u  /* x.f(...): the object is a class glvalue
                                       (or prvalue) whose address is passed. */
#define CALL_MEMBER_PTR      0x02u  /* p->f(...): the object is a pointer. */
#define CALL_OBJECT_PENDING  0x10u  /* The object operand was just pushed. */
#define CALL_ARG_LOAD_PENDING \
                             0x20u  /* The operand just pushed is a class
                                       that is not trivially copyable: its
                                       storage is passed (a LOAD). */
#define CALL_VIRTUAL         0x04u  /* A call through the vtable. */
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


static a_boolean rhs_designates_object(an_expr_node_ptr rhs)
/*
Return TRUE if the value of the node is the load of an object that it
designates (a variable, an indirection, a member, an element or a conversion
of one of them to a value): the glvalue of the object is what is below the
load.  The right operand of a class assignment that has to be evaluated
before the target is kept as that glvalue's address.
*/
{
  a_boolean  designates = FALSE;

  rhs = skip_parens(rhs);
  if (!is_glvalue_node(rhs)) {
    if (rhs->kind == (an_expr_node_kind)enk_variable) {
      designates = TRUE;
    } else if (rhs->kind == (an_expr_node_kind)enk_operation) {
      an_expr_operator_kind  k = rhs->variant.operation.kind;

      designates = k == eok_indirect || k == eok_ref_indirect ||
                   k == eok_dot_field || k == eok_points_to_field ||
                   k == eok_subscript || k == eok_ref_cast ||
                   k == eok_lvalue_cast || k == eok_lvalue_adjust;
    }  /* if */
  }  /* if */
  return designates;
}  /* rhs_designates_object */


static a_boolean rhs_is_adjusted_glvalue(an_expr_node_ptr rhs)
/*
Return TRUE if the node is a cv-adjustment (eok_class_rvalue_adjust, possibly
nested) of an explicit class glvalue: translated with CIF_GLV it is the
glvalue of that object, with no load.
*/
{
  a_boolean  adjusted = FALSE;

  rhs = skip_parens(rhs);
  if (rhs->kind == (an_expr_node_kind)enk_operation &&
      rhs->variant.operation.kind == eok_class_rvalue_adjust) {
    an_expr_node_ptr  inner = skip_parens(rhs->variant.operation.operands);

    while (inner->kind == (an_expr_node_kind)enk_operation &&
           inner->variant.operation.kind == eok_class_rvalue_adjust) {
      inner = skip_parens(inner->variant.operation.operands);
    }  /* while */
    /* (A constant that is a glvalue has no object whose address could be
       kept: its value is spilled, rhs_is_fixed_value.) */
    adjusted = is_glvalue_node(inner) &&
               inner->kind != (an_expr_node_kind)enk_constant;
  }  /* if */
  return adjusted;
}  /* rhs_is_adjusted_glvalue */


static a_boolean rhs_is_fixed_value(an_expr_node_ptr rhs)
/*
Return TRUE if the node is a class prvalue whose value is fixed when it is
evaluated, whatever the left operand of an assignment does afterwards: a call
(of a function or a member function; one that returns a reference is wrapped
in an indirection, which designates an object) or a constant (an aggregate
that is built in a temporary), possibly cv-adjusted.
*/
{
  a_boolean  fixed = FALSE;

  rhs = skip_parens(rhs);
  while (rhs->kind == (an_expr_node_kind)enk_operation &&
         rhs->variant.operation.kind == eok_class_rvalue_adjust) {
    rhs = skip_parens(rhs->variant.operation.operands);
  }  /* while */
  if (rhs->kind == (an_expr_node_kind)enk_constant) {
    fixed = TRUE;
  } else if (rhs->kind == (an_expr_node_kind)enk_operation) {
    an_expr_operator_kind  k = rhs->variant.operation.kind;

    fixed = k == eok_call || k == eok_dot_member_call ||
            k == eok_points_to_member_call;
  }  /* if */
  return fixed;
}  /* rhs_is_fixed_value */


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
#define DT_VBASE  5u    /* A virtual base of *lid: record rec, base base_gid,
                           VIRTUAL_BASE field index. */

typedef struct a_destroy_target {
  unsigned int  kind;
  uint32_t      lid;
  uint32_t      rec;
  uint32_t      index;
  uint32_t      base_gid;
  uint64_t      offset;
  uint32_t      array_elem;     /* An array: the G-id of its element type. */
  a_type_ptr    base_type;      /* A base: its class (for the VTT). */
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


static a_boolean class_has_vbases(a_type_ptr type)
/*
Return TRUE for a class that has virtual base classes, direct or indirect (its
record has VIRTUAL_BASE fields, its structors are a has_vbases pair).
*/
{
  type = skip_typerefs(type);
  return !C_mode() && is_immediate_class_type(type) &&
         type->variant.class_struct_union.any_virtual_base_classes;
}  /* class_has_vbases */


static void emit_this_vbase(an_ecil_frame *frame,
                            uint32_t      lid,
                            uint32_t      rec,
                            uint32_t      vbase_gid,
                            a_boolean     complete)
/*
Push the address of a virtual base subobject (direct or indirect) of the object
that the pointer variable lid points to: VBASE_CAST, with COMPLETE when the
object is known to be the complete object (the construction and destruction
of the virtual bases in C1 and D1, spec 12.4).
*/
{
  ecil_class_vtable_facts(frame, function_class);
  ecil_rec1(frame, eop_lref, lid);
  ecil_rec1(frame, eop_load, 0);
  ecil_rec_begin(frame, eop_vbase_cast);
  ecil_arg_uint(frame, rec);
  ecil_arg_uint(frame, vbase_gid);
  ecil_arg_uint(frame, ECIL_CAST_NONNULL |
                       (complete ? ECIL_VBASE_CAST_COMPLETE : 0u));
  ecil_rec_end(frame);
}  /* emit_this_vbase */


static void emit_this_subobject(an_ecil_frame *frame,
                                uint32_t      lid,
                                uint32_t      rec,
                                uint32_t      base_gid,
                                uint64_t      offset,
                                a_boolean     is_virtual,
                                uint32_t      index)
/*
Push the address of a base class subobject of the object that the pointer
variable lid points to: a direct base (BASE_CAST at its offset), or a virtual
base (VBASE_CAST, with COMPLETE for the constructor's own object: the
mem-initializers of the virtual bases are only in C1).
*/
{
  if (is_virtual) {
    (void)index;
    emit_this_vbase(frame, lid, rec, base_gid, lid == this_lid);
  } else {
    emit_this_base(frame, lid, rec, base_gid, offset);
  }  /* if */
}  /* emit_this_subobject */


static void emit_vtt_addr(an_ecil_frame *frame,
                          unsigned int  kind,
                          uint32_t      index)
/*
VTT_ADDR(kind, idx): the VTT operand of a call of the base variant of a
constructor or destructor of a class with virtual bases (spec 12.4): self (nil
idx), a base (BASE field idx) or a virtual base (VIRTUAL_BASE field idx) of the
class being constructed or destroyed.  The class's vtable is used.
*/
{
  ecil_note_vtable_used(frame, function_class);
  if (ecil_unsupported_reason == NULL) {
    ecil_rec_begin(frame, eop_vtt_addr);
    ecil_arg_uint(frame, kind);
    if (kind == (unsigned int)evak_self) {
      ecil_arg_nil(frame);
    } else {
      ecil_arg_uint(frame, index);
    }  /* if */
    ecil_rec_end(frame);
  }  /* if */
}  /* emit_vtt_addr */


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
  } else if (tgt->kind == DT_VBASE) {
    /* Only in C1 and D1, where this is the complete object. */
    emit_this_vbase(frame, tgt->lid, tgt->rec, tgt->base_gid, TRUE);
  } else {
    emit_this_base(frame, tgt->lid, tgt->rec, tgt->base_gid, tgt->offset);
  }  /* if */
}  /* emit_target_address */


static void emit_call_flags(an_ecil_frame *frame,
                            uint32_t      gid,
                            a_routine_ptr rout,
                            uint64_t      nargs,
                            unsigned int  flags)
/*
CALL of a constructor or destructor G-id with nargs arguments and the CALL
flags (VTT: the VTT_ADDR operand follows argument 0, spec 12.6).
*/
{
  uint32_t  t_call = ecil_func_type_id(frame, rout->type, FALSE);

  if (gid != 0 && t_call != 0 && ecil_unsupported_reason == NULL) {
    ecil_rec_begin(frame, eop_call);
    ecil_arg_uint(frame, gid);
    ecil_arg_uint(frame, t_call);
    ecil_arg_uint(frame, nargs);
    ecil_arg_uint(frame, flags);
    ecil_rec_end(frame);
  }  /* if */
}  /* emit_call_flags */


static void emit_call_record(an_ecil_frame *frame,
                             uint32_t      gid,
                             a_routine_ptr rout,
                             uint64_t      nargs)
/*
CALL of a constructor or destructor G-id with nargs arguments.
*/
{
  emit_call_flags(frame, gid, rout, nargs, 0u);
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

  if (ecil_has_exceptions() && kind != (unsigned int)ecl_eh &&
      kind != (unsigned int)ecl_exit && !is_non_throwing_routine(dtor) &&
      tgt->array_elem != 0) {
    /* A throw from the destructor of an element of an array: the library
       (the pin) skips the elements before the one that threw, while clang
       and g++ destroy them: diagnosed until that is fixed upstream.  A
       throwing destructor of a local, a temporary or an enclosing scope
       runs the cleanups that remain. */
    ecil_note_unsupported("the destructor of an array element that may "
                          "throw (noexcept(false))");
  } else if (gid != 0 && ecil_unsupported_reason == NULL) {
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
      unsigned int  flags = 0;

      emit_target_address(frame, tgt);
      if (variant == ECIL_VARIANT_BASE &&
          (info_of(gid)->flags & ECIL_GF_STRUCTOR_VBASES) != 0) {
        /* The base variant of a destructor of a class with virtual bases
           takes the VTT (spec 12.3). */
        emit_vtt_addr(frame, tgt->kind == DT_BASE ?
                             (unsigned int)evak_base :
                             tgt->kind == DT_VBASE ?
                             (unsigned int)evak_vbase :
                             (unsigned int)evak_self, tgt->index);
        flags |= ECIL_CALL_VTT;
      }  /* if */
      emit_call_flags(frame, gid, dtor, 1, flags);
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


static uint32_t new_static_temp(an_ecil_frame *frame,
                                uint32_t      t,
                                a_variable_ptr owner,
                                unsigned int  seq)
/*
Introduce the static-duration object that holds a temporary which the
reference variable "owner" extends: an internal definition named _ZGR<the
name of the owner>[<seq-id>]_ (Itanium 5.2.4), constructed by the owner's
STATIC_INIT and registered for destruction at exit there.
*/
{
  char          text[1500];
  const char    *base;
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
}  /* new_static_temp */


static uint32_t make_static_temp(an_ecil_frame *frame,
                                 uint32_t      t,
                                 a_variable_ptr owner)
/*
The static temporary number static_temp_count of the owner: the one the first
walk made, if this is the second walk of a function, else a new one.
*/
{
  unsigned int  seq = static_temp_count++;
  uint32_t      gid = 0;
  size_t        k;

  for (k = 0; k < num_static_temps; k++) {
    if (static_temps[k].owner == owner && static_temps[k].seq == seq) {
      gid = static_temps[k].gid;
    }  /* if */
  }  /* for */
  if (gid == 0) {
    gid = new_static_temp(frame, t, owner, seq);
    if (gid != 0 && num_static_temps == static_temps_capacity) {
      size_t         new_capacity = static_temps_capacity == 0 ?
                                    8 : static_temps_capacity * 2;
      a_static_temp  *new_list = (a_static_temp *)realloc(static_temps,
                                        new_capacity * sizeof(a_static_temp));

      if (new_list != NULL) {
        static_temps = new_list;
        static_temps_capacity = new_capacity;
      }  /* if */
    }  /* if */
    if (gid != 0 && num_static_temps < static_temps_capacity) {
      static_temps[num_static_temps].owner = owner;
      static_temps[num_static_temps].seq = seq;
      static_temps[num_static_temps].gid = gid;
      num_static_temps++;
    }  /* if */
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


static a_boolean object_is_complete_object(an_expr_node_ptr node)
/*
Return TRUE if the object expression of a member call designates a complete
object of its static type, neither through a reference nor an indirection: a
variable, a temporary, or a member of one (spec 12.6).
*/
{
  a_boolean  result = FALSE;
  a_boolean  walking = TRUE;

  node = skip_parens(node);
  while (walking) {
    walking = FALSE;
    if (node->kind == (an_expr_node_kind)enk_variable) {
      result = TRUE;
    } else if (node->kind == (an_expr_node_kind)enk_temp_init) {
      result = TRUE;
    } else if (node->kind == (an_expr_node_kind)enk_operation &&
               node->variant.operation.kind == eok_dot_field &&
               node->variant.operation.operands != NULL) {
      node = skip_parens(node->variant.operation.operands);
      walking = TRUE;
    }  /* if */
  }  /* while */
  return result;
}  /* object_is_complete_object */


static a_type_ptr object_class_of(an_expr_node_ptr node, a_boolean is_pointer)
/*
The class of the object a member call is on: the class of the expression, or
the class its pointer points to; NULL if it is not a class.
*/
{
  a_type_ptr  t = skip_typerefs(node->type);

  if (is_pointer) {
    t = t->kind == tk_pointer ? skip_typerefs(t->variant.pointer.type) : NULL;
  }  /* if */
  return (t != NULL && is_immediate_class_type(t)) ? t : NULL;
}  /* object_class_of */


static void setup_virtual_call(an_ecil_frame *frame, a_code_item *it,
                               an_expr_node_ptr callee, a_routine_ptr direct,
                               a_boolean is_dot)
/*
A member call with is_virtual_call set (spec 12.6): the call is CALL VIRTUAL
of the routine the front end named, unless the writer devirtualizes it as
clang does.  The call is plain when the object is a complete object that is
neither a reference nor an indirection, when its class is final (after the
front end's conversion to the class that declares the function is looked
through) or when the function is final; a plain call calls the final overrider
of the named function in the object's class, which is another function when
the object's class overrides it, or inherits a using-declaration of a base's
function; a final overrider with another return type (a covariant one) keeps
the dispatch.
*/
{
  an_expr_node_ptr  sel = skip_parens(callee->next);
  an_expr_node_ptr  obj = sel;
  a_type_ptr        decl_class = parent_class_of(direct);
  a_type_ptr        cobj;
  uint64_t          cast_offset = 0;
  a_boolean         plain = FALSE;
  a_boolean         keep_virtual = FALSE;

  /* The object expression: without the front end's base-class conversions
     (and the offsets of their steps, to find the base class entry). */
  while (ecil_unsupported_reason == NULL &&
         obj->kind == (an_expr_node_kind)enk_operation &&
         obj->variant.operation.kind == eok_base_class_cast) {
    an_expr_node_ptr  operand = skip_parens(obj->variant.operation.operands);
    a_type_ptr        src = object_class_of(operand, !is_dot);
    a_type_ptr        tgt = object_class_of(obj, !is_dot);
    a_base_class_ptr  bp = NULL;

    if (src != NULL && tgt != NULL) {
      for (bp = class_type_supp(src)->base_classes; bp != NULL;
           bp = bp->next) {
        if (skip_typerefs(bp->type) == tgt) break;
      }  /* for */
    }  /* if */
    if (bp == NULL) {
      ecil_note_unsupported("a virtual call through a conversion that is "
                            "not to a base class");
    } else {
      cast_offset += (uint64_t)bp->offset;
      obj = operand;
    }  /* if */
  }  /* while */
  cobj = ecil_unsupported_reason == NULL ? object_class_of(obj, !is_dot) :
                                           NULL;
  if (ecil_unsupported_reason == NULL && cobj == NULL) {
    ecil_note_unsupported("a virtual call on an object that is not a class");
  }  /* if */
  if (ecil_unsupported_reason == NULL) {
    plain = (is_dot && object_is_complete_object(obj)) ||
            cobj->variant.class_struct_union.final || direct->final;
  }  /* if */
  if (plain) {
    /* The final overrider of the named function in the object's class. */
    a_base_class_ptr  entry = NULL;
    a_routine_ptr     target = direct;
    a_base_class_ptr  obc = NULL;

    if (decl_class != cobj) {
      for (entry = class_type_supp(cobj)->base_classes; entry != NULL;
           entry = entry->next) {
        if (skip_typerefs(entry->type) == decl_class &&
            (uint64_t)entry->offset == cast_offset) {
          break;
        }  /* if */
      }  /* for */
      if (entry == NULL) {
        ecil_note_unsupported("a virtual call whose class is not a base of "
                              "the object's class");
      }  /* if */
    }  /* if */
    obc = entry;
    if (ecil_unsupported_reason == NULL && entry != NULL) {
      an_overriding_virtual_function_ptr  ovr;

      for (ovr = entry->variant.overriding_virtual_functions; ovr != NULL;
           ovr = ovr->next) {
        if (ovr->primary_function == direct) {
          a_type_ptr  ovr_return = skip_typerefs(
                  ovr->overriding_function->type)->variant.routine.return_type;
          a_type_ptr  named_return = skip_typerefs(
                  direct->type)->variant.routine.return_type;

          if (!identical_types(ovr_return, named_return)) {
            keep_virtual = TRUE;     /* Covariant: the dispatch stays. */
          } else {
            target = ovr->overriding_function;
            obc = ovr->base_class;
          }  /* if */
          break;
        }  /* if */
      }  /* for */
    }  /* if */
#if CHECKING
    if (ecil_unsupported_reason == NULL && !keep_virtual) {
      /* The front end's own answer (it walks the chain of conversions of the
         selector) is the same function. */
      check_assertion(final_overrider(direct, sel, cobj) == target);
    }  /* if */
#endif /* CHECKING */
    if (ecil_unsupported_reason == NULL && !keep_virtual) {
      it->object = obj;
      it->devirt_routine = target;
      it->devirt_base = obc;
      it->devirt_class = cobj;
    }  /* if */
  }  /* if */
  if (ecil_unsupported_reason == NULL && (!plain || keep_virtual)) {
    /* A call through the vtable: the facts of the class that declares the
       function (spec 12.2). */
    ecil_class_vtable_facts(frame, decl_class);
    it->c |= CALL_VIRTUAL;
  }  /* if */
}  /* setup_virtual_call */


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
      if (direct == NULL || callee->next == NULL ||
          !routine_type_is_nonstatic_member_function(direct->type)) {
        ecil_note_unsupported("a member call of a static or unknown "
                              "member function");
      } else {
        it->c |= (call_op == eok_dot_member_call) ? CALL_MEMBER_DOT :
                                                    CALL_MEMBER_PTR;
        if (node->variant.operation.is_virtual_call) {
          setup_virtual_call(frame, it, callee, direct,
                             call_op == eok_dot_member_call);
        }  /* if */
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
               strcmp(direct->source_corresp.name, "__builtin_launder") == 0 &&
               callee->next != NULL && callee->next->next == NULL) {
      /* std::launder is the identity on the pointer: the argument is the
         expression. */
      it->entry = callee->next;
      it->phase = 0;
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
        it->a = ecil_function_id(frame, it->devirt_routine != NULL ?
                                        it->devirt_routine : direct);
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
    an_expr_node_ptr  object = skip_parens(code_stack[idx].object != NULL ?
                                   (an_expr_node_ptr)code_stack[idx].object :
                                   callee->next);

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
    if (ecil_unsupported_reason == NULL && it->devirt_base != NULL) {
      /* A devirtualized call: the object's address converted to the class of
         the final overrider (spec 12.6). */
      emit_base_chain(frame, it->devirt_class, it->devirt_base, TRUE,
                      ECIL_CAST_NONNULL, FALSE);
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
      {
        an_expr_node_ptr  pushed = (arg == callee->next &&
                                    code_stack[idx].object != NULL) ?
                                   (an_expr_node_ptr)code_stack[idx].object :
                                   arg;

        push_expr(pushed, (code_stack[idx].c & CALL_OBJECT_PENDING) != 0 &&
                          (code_stack[idx].c & CALL_MEMBER_DOT) != 0 &&
                          is_class_temp_object(pushed) ? CIF_GLV : 0u);
      }
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
    fn_type = skip_typerefs(it->devirt_routine != NULL ?
                            it->devirt_routine->type : callee->type);
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
      if ((it->c & CALL_VIRTUAL) != 0) flags |= ECIL_CALL_VIRTUAL;
      if (ecil_unsupported_reason == NULL && t_call != 0) {
        it = &code_stack[idx];
        if (it->b >= ECIL_SPECIAL_FP_FIRST && it->b <= ECIL_SPECIAL_FP_LAST) {
          emit_fp_call(frame, it, node, callee);
        } else if (it->b == ECIL_SPECIAL_CLZG || it->b == ECIL_SPECIAL_CTZG) {
          emit_clzg_call(frame, it, node, callee);
        } else if (it->b == ECIL_SPECIAL_C23_VA_START &&
                   callee->next == NULL) {
          /* va_start () without a va_list (an error in C, accepted by the
             front end in some modes). */
          ecil_note_unsupported("__builtin_c23_va_start without an "
                                "argument");
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

          if (node_is_bit_field(callee->next->next->next)) {
            /* The check is made against the width of the bit-field. */
            ecil_note_unsupported("an overflow check against a bit-field");
          } else if (tt != 0) {
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
          if (ecil_has_exceptions() && is_nothrow_type(fn_type)) {
            flags |= ECIL_CALL_NOTHROW;
          }  /* if */
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


static a_boolean spaceship_shape(an_ecil_frame   *frame,
                                 an_expr_node_ptr node,
                                 uint32_t        *rec,
                                 uint32_t        *index,
                                 uint32_t        *tint,
                                 int64_t         values[4],
                                 a_boolean       *has_unordered)
/*
Describe the result of a built-in <=> (a comparison category: a record with
one integer member): its record G-id, the member's index and ECIL type, and
the values of less, equal (or equivalent), greater and, for the partial
ordering, unordered, read from the constants that initialize the category's
static members, not assumed.  FALSE (with the reason noted) if the category
is not one that CMP3WAY can produce.
*/
{
  a_type_ptr      ct = skip_typerefs(node->type);
  a_constant_ptr  lt = NULL;
  a_constant_ptr  eq = NULL;
  a_constant_ptr  gt = NULL;
  a_constant_ptr  un = NULL;
  a_field_ptr     field = NULL;
  a_boolean       ok = FALSE;

  *has_unordered = FALSE;
  initialize_ordering_constants();
  if (f_identical_types(ct, strong_ordering_type(), ITF_NO_FLAGS)) {
    lt = strong_ordering_less;
    eq = strong_ordering_equal;
    gt = strong_ordering_greater;
  } else if (f_identical_types(ct, weak_ordering_type(), ITF_NO_FLAGS)) {
    lt = weak_ordering_less;
    eq = weak_ordering_equivalent;
    gt = weak_ordering_greater;
  } else if (f_identical_types(ct, partial_ordering_type(), ITF_NO_FLAGS)) {
    lt = partial_ordering_less;
    eq = partial_ordering_equivalent;
    gt = partial_ordering_greater;
    un = partial_ordering_unordered;
    *has_unordered = TRUE;
  }  /* if */
  if (lt == NULL) {
    ecil_note_unsupported("a three-way comparison with an equality "
                          "category result");
  } else if (ct->kind != tk_struct && ct->kind != tk_class) {
    ecil_note_unsupported("a comparison category that is not a class");
  } else {
    field = ct->variant.class_struct_union.field_list;
    if (field == NULL || field->next != NULL) {
      ecil_note_unsupported("a comparison category that does not have "
                            "exactly one member");
    } else if (field_index(frame, ct, field, rec, index)) {
      *tint = ecil_type_id(frame, field->type);
      if (*tint == 0 || info_of(*tint)->kind != egk_int) {
        ecil_note_unsupported("a comparison category whose member is not an "
                              "integer");
      } else {
        a_constant_ptr  all[4];
        unsigned int    i;
        a_boolean       overflow = FALSE;

        all[0] = lt;
        all[1] = eq;
        all[2] = gt;
        all[3] = un;
        ok = TRUE;
        for (i = 0; i < (un != NULL ? 4u : 3u) && ok; i++) {
          a_constant_ptr  c = all[i];

          if (c == NULL || c->kind != (a_constant_repr_kind)ck_aggregate ||
              c->variant.aggregate.first_constant == NULL ||
              c->variant.aggregate.first_constant->kind !=
                                     (a_constant_repr_kind)ck_integer) {
            ecil_note_unsupported("a comparison category constant that is "
                                  "not a one-member aggregate");
            ok = FALSE;
          } else {
            values[i] = (int64_t)value_of_integer_constant(
                      c->variant.aggregate.first_constant, &overflow);
            if (overflow) {
              ecil_note_unsupported("a comparison category constant that "
                                    "does not fit");
              ok = FALSE;
            }  /* if */
          }  /* if */
        }  /* for */
      }  /* if */
    }  /* if */
  }  /* if */
  return ok;
}  /* spaceship_shape */


static a_boolean new_has_throwing_initializer(a_new_delete_supplement_ptr nds);

static a_boolean push_throw_scan(an_expr_node_ptr **work, size_t *size,
                                 size_t *capacity, an_expr_node_ptr node)
/*
Push an expression node on the work list of expr_may_contain_throw; FALSE if
memory ran out.
*/
{
  a_boolean  ok = TRUE;

  if (*size == *capacity) {
    size_t            new_capacity = *capacity == 0 ? 32 : *capacity * 2;
    an_expr_node_ptr  *new_work = (an_expr_node_ptr *)realloc(*work,
                                   new_capacity * sizeof(an_expr_node_ptr));

    if (new_work == NULL) {
      ok = FALSE;
    } else {
      *work = new_work;
      *capacity = new_capacity;
    }  /* if */
  }  /* if */
  if (ok) (*work)[(*size)++] = node;
  return ok;
}  /* push_throw_scan */


static a_boolean push_dynamic_init_scan(an_expr_node_ptr **work,
                                        size_t *size, size_t *capacity,
                                        a_dynamic_init_ptr di,
                                        a_boolean *unknown)
/*
Push the expressions a dynamic initialization evaluates on the work list of
expr_may_contain_throw; *unknown is set for a kind that is not looked into
(an aggregate).  FALSE if memory ran out.
*/
{
  a_boolean         ok = TRUE;
  an_expr_node_ptr  e;

  if (di != NULL) {
    switch (di->kind) {
      case dik_none:
      case dik_zero:
      case dik_constant:
        break;
      case dik_expression:
      case dik_class_result_via_ctor:
        ok = push_throw_scan(work, size, capacity, di->variant.expression);
        break;
      case dik_bitwise_copy:
        if (di->variant.bitwise_copy.source != NULL) {
          ok = push_throw_scan(work, size, capacity,
                               di->variant.bitwise_copy.source);
        }  /* if */
        break;
      case dik_constructor:
        for (e = di->variant.constructor.args; e != NULL && ok;
             e = e->next) {
          ok = push_throw_scan(work, size, capacity, e);
        }  /* for */
        break;
      default:
        *unknown = TRUE;
        break;
    }  /* switch */
  }  /* if */
  return ok;
}  /* push_dynamic_init_scan */


static a_boolean expr_may_contain_throw(an_expr_node_ptr root)
/*
Return TRUE if the expression contains a throw expression (or has a form
that is not looked into: the answer only decides whether a conditional
region gets its own FULLEXPR).  The walk uses a heap list.
*/
{
  an_expr_node_ptr  *work = NULL;
  size_t            size = 0;
  size_t            capacity = 0;
  a_boolean         found = FALSE;
  a_boolean         ok = push_throw_scan(&work, &size, &capacity, root);

  while (ok && !found && size > 0) {
    an_expr_node_ptr  node = skip_parens(work[--size]);
    an_expr_node_ptr  operand;

    switch (node->kind) {
      case enk_constant:
      case enk_variable:
      case enk_routine:
      case enk_param_ref:
      case enk_type_operand:
      case enk_field:
      case enk_lambda:
      case enk_reuse_value:
      case enk_sizeof:
      case enk_alignof:
      case enk_datasizeof:
        break;
      case enk_throw:
        found = TRUE;
        break;
      case enk_operation:
        for (operand = node->variant.operation.operands;
             operand != NULL && ok; operand = operand->next) {
          ok = push_throw_scan(&work, &size, &capacity, operand);
        }  /* for */
        break;
      case enk_object_lifetime:
        ok = push_throw_scan(&work, &size, &capacity,
                             node->variant.object_lifetime.expr);
        break;
      case enk_temp_init:
        ok = push_dynamic_init_scan(&work, &size, &capacity,
                                    node->variant.init.dynamic_init, &found);
        break;
      case enk_new_delete:
        { a_new_delete_supplement_ptr  nds = node->variant.new_delete;

          if (nds->is_new && nds->freeing_of_storage_on_exception != NULL &&
              new_has_throwing_initializer(nds)) {
            found = TRUE;      /* Its free cleanup needs a FULLEXPR too. */
          }  /* if */
          for (operand = nds->arg; operand != NULL && ok;
               operand = operand->next) {
            ok = push_throw_scan(&work, &size, &capacity, operand);
          }  /* for */
          if (ok && nds->number_of_elements != NULL) {
            ok = push_throw_scan(&work, &size, &capacity,
                                 nds->number_of_elements);
          }  /* if */
          if (ok) {
            ok = push_dynamic_init_scan(&work, &size, &capacity,
                                        nds->dynamic_init, &found);
          }  /* if */
        }
        break;
      case enk_c11_generic:
        ok = push_throw_scan(&work, &size, &capacity,
                             node->variant.c11_generic.result);
        break;
      case enk_typeid:
        for (operand = node->variant.typeid_info.type_with_opt_expr;
             operand != NULL && ok; operand = operand->next) {
          ok = push_throw_scan(&work, &size, &capacity, operand);
        }  /* for */
        break;
      case enk_builtin_operation:
        for (operand = node->variant.builtin_operation.operands;
             operand != NULL && ok; operand = operand->next) {
          ok = push_throw_scan(&work, &size, &capacity, operand);
        }  /* for */
        break;
      default:
        found = TRUE;                    /* Not known. */
        break;
    }  /* switch */
  }  /* while */
  free(work);
  return found || !ok;
}  /* expr_may_contain_throw */


static a_boolean conditional_needs_fullexpr(an_expr_node_ptr node,
                                            an_expr_node_ptr op2)
/*
Return TRUE for the outermost conditional operation (? :, && or ||) of an
expression translated with exceptions that has no full expression open and
whose conditional operands may throw: the throw's implicit free needs a
FULLEXPR around the whole conditional region (spec 12.5).  op2 is its first
conditional operand.
*/
{
  a_boolean  wrap = FALSE;

  if (ecil_has_exceptions() && cond_depth == 0 && fullexpr_depth == 0 &&
      op2 != NULL) {
    an_expr_node_ptr  arm;

    for (arm = op2; arm != NULL && !wrap; arm = arm->next) {
      if (node->variant.operation.kind == eok_question || arm == op2) {
        wrap = expr_may_contain_throw(arm);
      }  /* if */
    }  /* for */
  }  /* if */
  return wrap;
}  /* conditional_needs_fullexpr */


static void emit_pm_object_address(an_ecil_frame *frame,
                                   an_expr_node_ptr object)
/*
The class object of a .* is evaluated: pass its address (a temporary is
materialized for a value).
*/
{
  object = skip_parens(object);
  if (!is_glvalue_node(object) && !is_class_temp_object(object)) {
    uint32_t  ot = ecil_type_id(frame, object->type);

    if (ecil_class_has_nontrivial_dtor(object->type)) {
      ecil_note_unsupported("a temporary with a destructor");
    } else if (ot != 0) {
      emit_materialize_temp(frame, new_lid(), ot);
    }  /* if */
  }  /* if */
  if (ecil_unsupported_reason == NULL) ecil_rec0(frame, eop_addr);
}  /* emit_pm_object_address */


static void emit_pm_field(an_ecil_frame *frame, an_expr_node_ptr node,
                          an_expr_node_ptr pm)
/*
PM_FIELD of the object pointer and the pointer to a data member that are on
the stack: a glvalue of the member (read if the node is a value).
*/
{
  uint32_t  t_dm = ecil_type_id(frame, pm->type);

  if (t_dm != 0) {
    ecil_rec1(frame, eop_pm_field, t_dm);
    emit_load_if_value(frame, node);
  }  /* if */
}  /* emit_pm_field */


static void emit_pm_cast(an_ecil_frame *frame, an_expr_node_ptr node)
/*
eok_pm_base_class_cast and eok_pm_derived_class_cast: one PM_CAST step to a
direct base class or from one (spec 12.4).  A virtual base is ill-formed.
*/
{
  an_expr_node_ptr  op = node->variant.operation.operands;
  a_boolean         to_base = node->variant.operation.kind ==
                              (an_expr_operator_kind)eok_pm_base_class_cast;
  a_type_ptr        from_class = skip_typerefs(pm_class_type(op->type));
  a_type_ptr        to_class = skip_typerefs(pm_class_type(node->type));
  a_type_ptr        derived = to_base ? from_class : to_class;
  a_type_ptr        base_class = to_base ? to_class : from_class;
  a_base_class_ptr  bcp = NULL;
  uint32_t          t_from = ecil_type_id(frame, op->type);
  uint32_t          t_to = ecil_type_id(frame, node->type);

  if (!is_immediate_class_type(derived) ||
      !is_immediate_class_type(base_class)) {
    ecil_note_unsupported("a pointer to member cast of unsupported types");
  } else {
    for (bcp = class_type_supp(derived)->base_classes; bcp != NULL;
         bcp = bcp->next) {
      if (bcp->direct && skip_typerefs(bcp->type) == base_class) break;
    }  /* for */
    if (bcp == NULL) {
      ecil_note_unsupported("a pointer to member cast to a class that is "
                            "not a direct base class");
    } else if (bcp->is_virtual) {
      ecil_note_unsupported("a pointer to member cast through a virtual "
                            "base class");
    } else if (bcp->ambiguous) {
      ecil_note_unsupported("a pointer to member cast to an ambiguous base "
                            "class");
    }  /* if */
  }  /* if */
  if (ecil_unsupported_reason == NULL && t_from != 0 && t_to != 0) {
    ecil_rec_begin(frame, eop_pm_cast);
    ecil_arg_uint(frame, to_base ? (unsigned int)epck_to_base :
                                   (unsigned int)epck_to_derived);
    ecil_arg_uint(frame, t_from);
    ecil_arg_uint(frame, t_to);
    ecil_arg_uint(frame, (uint64_t)bcp->offset);
    ecil_rec_end(frame);
  }  /* if */
}  /* emit_pm_cast */


static a_boolean rtti_is_enabled(void)
/*
Return TRUE if typeid and dynamic_cast are available (the front end rejects
them without RTTI; the typeinfo objects of exceptions exist all the same).
*/
{
  return rtti_enabled;
}  /* rtti_is_enabled */


static a_type_ptr rtti_class_of(a_type_ptr type)
/*
The class of a pointer to a class, or of a class (references are looked
through); NULL for another type (void, a non-class pointee).
*/
{
  a_type_ptr  t = skip_typerefs(type);
  a_type_ptr  result = NULL;

  if (t->kind == tk_pointer) t = skip_typerefs(t->variant.pointer.type);
  if (is_immediate_class_type(t)) result = t;
  return result;
}  /* rtti_class_of */


static uint32_t rtti_class_typeinfo(an_ecil_frame *frame, a_type_ptr cls)
/*
The typeinfo of a class that a dynamic_cast or a typeid names, with the facts
of the class (the library computes the offset hint from the class graph).
*/
{
  uint32_t  ti = 0;

  if (!rtti_is_enabled()) {
    ecil_note_unsupported("an RTTI operation with RTTI disabled");
  } else if (cls == NULL || cls->incomplete) {
    ecil_note_unsupported("an RTTI operation on an incomplete class");
  } else {
    ecil_class_layout_facts(frame, cls);
    if (ecil_unsupported_reason == NULL && ecil_class_is_dynamic(cls)) {
      ecil_class_vtable_facts(frame, cls);
    }  /* if */
    if (ecil_unsupported_reason == NULL) ti = ecil_typeinfo_id(frame, cls);
  }  /* if */
  return ti;
}  /* rtti_class_typeinfo */


static void emit_dynamic_cast(an_ecil_frame *frame, an_expr_node_ptr node)
/*
eok_dynamic_cast (pointers, void *) and eok_ref_dynamic_cast (the operand and
the result are glvalues, and the node's type is T, not T &): DYNAMIC_CAST of
the operand's pointer, with the typeinfo of its class and of the target
class (nil for void *).  A reference cast throws bad_cast (REF) and takes the
address of its operand first (spec 12.4).
*/
{
  an_expr_node_ptr  op = node->variant.operation.operands;
  a_boolean         is_ref = node->variant.operation.kind ==
                             (an_expr_operator_kind)eok_ref_dynamic_cast;
  a_type_ptr        src_class = rtti_class_of(op->type);
  a_type_ptr        dst_type = skip_typerefs(node->type);
  a_type_ptr        dst_class;
  a_boolean         to_void = FALSE;
  uint32_t          t_result;
  uint32_t          g_src;
  uint32_t          g_dst = 0;

  if (!is_ref && dst_type->kind == tk_pointer) {
    dst_type = skip_typerefs(dst_type->variant.pointer.type);
  }  /* if */
  to_void = !is_ref && dst_type->kind == tk_void;
  dst_class = to_void ? NULL : rtti_class_of(dst_type);
  if (is_ref && !is_glvalue_node(skip_parens(op))) {
    ecil_note_unsupported("a dynamic_cast to a reference of a value");
  } else if (!to_void && dst_class == NULL) {
    ecil_note_unsupported("a dynamic_cast to an unsupported type");
  }  /* if */
  if (ecil_unsupported_reason == NULL) {
    g_src = rtti_class_typeinfo(frame, src_class);
    if (ecil_unsupported_reason == NULL && !to_void) {
      g_dst = rtti_class_typeinfo(frame, dst_class);
    }  /* if */
    /* The result is the node's pointer, or the address of the referent. */
    t_result = is_ref ? ecil_ptr_type(frame, ecil_type_id(frame,
                                                          node->type)) :
                        ecil_type_id(frame, node->type);
    if (ecil_unsupported_reason == NULL && g_src != 0 &&
        (to_void || g_dst != 0) && t_result != 0) {
      if (is_ref) ecil_rec0(frame, eop_addr);
      ecil_rec_begin(frame, eop_dynamic_cast);
      ecil_arg_uint(frame, g_src);
      if (to_void) {
        ecil_arg_nil(frame);
      } else {
        ecil_arg_uint(frame, g_dst);
      }  /* if */
      ecil_arg_uint(frame, t_result);
      ecil_arg_uint(frame, is_ref ? (unsigned int)ECIL_DYNAMIC_CAST_REF : 0u);
      ecil_rec_end(frame);
      if (is_ref) {
        ecil_rec0(frame, eop_deref);
        emit_load_if_value(frame, node);
      }  /* if */
    }  /* if */
  }  /* if */
}  /* emit_dynamic_cast */


static a_boolean glvalue_from_pointer_deref(an_expr_node_ptr root)
/*
Clang's isGLValueFromPointerDeref, for the null check of typeid(*p): the
operand, through parentheses, the right operand of a comma, either arm of a
conditional and the casts of a glvalue, is an indirection through a pointer
(not through a reference).  The walk uses a heap list.
*/
{
  an_expr_node_ptr  *work = NULL;
  size_t            size = 0;
  size_t            capacity = 0;
  a_boolean         result = FALSE;
  a_boolean         ok = push_throw_scan(&work, &size, &capacity, root);

  while (ok && !result && size > 0) {
    an_expr_node_ptr  node = skip_parens(work[--size]);

    if (node->kind == (an_expr_node_kind)enk_operation) {
      an_expr_operator_kind  op = node->variant.operation.kind;
      an_expr_node_ptr       ops = node->variant.operation.operands;

      if (op == eok_indirect) {
        result = TRUE;
      } else if (op == eok_comma && ops != NULL && ops->next != NULL) {
        ok = push_throw_scan(&work, &size, &capacity, ops->next);
      } else if (op == eok_question && ops != NULL && ops->next != NULL &&
                 ops->next->next != NULL) {
        ok = push_throw_scan(&work, &size, &capacity, ops->next) &&
             push_throw_scan(&work, &size, &capacity, ops->next->next);
      } else if ((op == eok_lvalue_cast || op == eok_lvalue_adjust ||
                  op == eok_base_class_cast ||
                  op == eok_derived_class_cast) && ops != NULL &&
                 is_glvalue_node(skip_parens(ops))) {
        ok = push_throw_scan(&work, &size, &capacity, ops);
      }  /* if */
    }  /* if */
  }  /* while */
  free(work);
  return result;
}  /* glvalue_from_pointer_deref */


static void do_typeid(an_ecil_frame *frame, size_t idx)
/*
enk_typeid: TYPEID with the typeinfo of the type operand (the static form,
nothing popped), or with the glvalue of a polymorphic operand on the stack
(the dynamic form, nil typeinfo; NULL_CHECK iff the operand is an indirection
through a pointer, typeid(*p)).  The result is a glvalue of std::type_info.
*/
{
  a_code_item       *it = &code_stack[idx];
  an_expr_node_ptr  node = (an_expr_node_ptr)it->entry;
  an_expr_node_ptr  type_op = node->variant.typeid_info.type_with_opt_expr;
  an_expr_node_ptr  operand = type_op != NULL ? type_op->next : NULL;
  a_boolean         dynamic = node->variant.typeid_info.is_dynamic &&
                              operand != NULL;
  uint32_t          t_result = ecil_type_id(frame, node->type);
  uint32_t          ti = 0;
  unsigned int      cflags = 0;

  if (type_op == NULL ||
      type_op->kind != (an_expr_node_kind)enk_type_operand) {
    ecil_note_unsupported("a typeid without a type operand");
    code_stack_size--;
  } else if (dynamic && it->phase == 0) {
    a_type_ptr  cls = rtti_class_of(operand->type);

    it->phase = 1;
    if (cls == NULL) {
      ecil_note_unsupported("a typeid of a value that is not a class");
    } else {
      (void)rtti_class_typeinfo(frame, cls);
      if (ecil_unsupported_reason == NULL) {
        push_expr(operand, is_class_temp_object(operand) ? CIF_GLV : 0u);
      }  /* if */
    }  /* if */
  } else {
    if (dynamic) {
      an_expr_node_ptr  op = skip_parens(operand);

      if (!is_glvalue_node(op) && !is_class_temp_object(op)) {
        uint32_t  ot = ecil_type_id(frame, op->type);

        if (ecil_class_has_nontrivial_dtor(op->type)) {
          ecil_note_unsupported("a temporary with a destructor");
        } else if (ot != 0) {
          emit_materialize_temp(frame, new_lid(), ot);
        }  /* if */
      }  /* if */
      if (glvalue_from_pointer_deref(op)) cflags |= ECIL_TYPEID_NULL_CHECK;
    } else if (!rtti_is_enabled()) {
      ecil_note_unsupported("a typeid with RTTI disabled");
    } else {
      ti = ecil_typeinfo_id(frame, type_op->variant.type_operand.type);
    }  /* if */
    if (ecil_unsupported_reason == NULL && t_result != 0 &&
        (dynamic || ti != 0)) {
      ecil_rec_begin(frame, eop_typeid);
      if (dynamic) {
        ecil_arg_nil(frame);
      } else {
        ecil_arg_uint(frame, ti);
      }  /* if */
      ecil_arg_uint(frame, t_result);
      ecil_arg_uint(frame, cflags);
      ecil_rec_end(frame);
    }  /* if */
    code_stack_size--;
  }  /* if */
}  /* do_typeid */


static void do_pm_call(an_ecil_frame *frame, size_t idx)
/*
eok_dot_pm_call and eok_points_to_pm_call, (x.*pmf)(args) and (p->*pmf)(args):
the object (its address for x) comes first (C++17 evaluates it first), then
the pointer to the member function, then the arguments, then PM_CALL.  The
first operand of the node is the pointer, the second is the object.
*/
{
  a_code_item       *it = &code_stack[idx];
  an_expr_node_ptr  node = (an_expr_node_ptr)it->entry;
  an_expr_node_ptr  pmf = node->variant.operation.operands;
  an_expr_node_ptr  object = pmf != NULL ? pmf->next : NULL;
  a_boolean         is_dot = node->variant.operation.kind ==
                             (an_expr_operator_kind)eok_dot_pm_call;
  a_type_ptr        fn_type = NULL;

  if (object == NULL) {
    ecil_note_unsupported("a call through a pointer to member without an "
                          "object");
    code_stack_size--;
  } else if (it->phase == 0) {
    it->phase = 1;
    push_expr(object, is_dot && is_class_temp_object(object) ? CIF_GLV : 0u);
  } else if (it->phase == 1) {
    if (is_dot) emit_pm_object_address(frame, object);
    it->phase = 2;
    push_expr(pmf, 0);
  } else {
    if (it->phase == 2) {
      it->phase = 3;
      it->cursor = object->next;
    } else if ((it->c & CALL_ARG_LOAD_PENDING) != 0) {
      /* A by-value argument of a NONTRIVIAL_ABI class is the load of the
         storage that holds it (spec 11.6). */
      it->c &= ~CALL_ARG_LOAD_PENDING;
      ecil_rec1(frame, eop_load, 0);
    }  /* if */
    fn_type = skip_typerefs(pm_member_type(pmf->type));
    if (ecil_unsupported_reason == NULL && it->cursor != NULL) {
      an_expr_node_ptr  arg = (an_expr_node_ptr)it->cursor;

      it->cursor = arg->next;
      if (ecil_class_is_nontrivial_abi(arg->type) &&
          rout_type_supp(fn_type)->has_ellipsis) {
        ecil_note_unsupported("a class that is not trivially copyable "
                              "passed through an ellipsis");
      } else if (is_glvalue_node(skip_parens(arg)) &&
                 ecil_class_is_nontrivial_abi(arg->type)) {
        it->c |= CALL_ARG_LOAD_PENDING;
      }  /* if */
      push_expr(arg, 0);
    } else {
      /* All operands are on the stack: emit the call. */
      uint32_t          t_call = ecil_func_type_id(frame, fn_type, FALSE);
      uint32_t          result_gid = ecil_type_id(frame, node->type);
      an_expr_node_ptr  arg;
      uint64_t          nargs = 1;
      unsigned int      cflags = 0;

      for (arg = object->next; arg != NULL; arg = arg->next) nargs++;
      if (ecil_class_is_nontrivial_abi(node->type) &&
          (it->flags & CIF_DEST) == 0) {
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
      if (rout_type_supp(fn_type)->has_ellipsis) {
        cflags |= ECIL_CALL_VARIADIC_CALLEE;
      }  /* if */
      if (rout_type_supp(fn_type)->does_not_return) {
        cflags |= ECIL_CALL_NORETURN;
      }  /* if */
      if (node->result_is_not_used) cflags |= ECIL_CALL_RESULT_UNUSED;
      if (ecil_has_exceptions() && is_nothrow_type(fn_type)) {
        cflags |= ECIL_CALL_NOTHROW;
      }  /* if */
      if (ecil_unsupported_reason == NULL && t_call != 0) {
        ecil_rec_begin(frame, eop_pm_call);
        ecil_arg_uint(frame, t_call);
        ecil_arg_uint(frame, nargs);
        ecil_arg_uint(frame, cflags);
        ecil_rec_end(frame);
        finish_value(frame, it->flags);
      }  /* if */
      code_stack_size--;
    }  /* if */
  }  /* if */
}  /* do_pm_call */


static a_boolean type_is_empty_class(an_ecil_frame *frame, a_type_ptr type);


static an_expr_node_ptr empty_assign_operand(an_expr_node_ptr operand)
/*
The operand of the assignment of a class without data that is evaluated for
its effects only: the adjustments that read the object (an implicit indirection
through a reference, a cv adjustment of the class value) are skipped, so that
no value of the class is produced.
*/
{
  an_expr_node_ptr  node = skip_parens(operand);
  a_boolean         walking = TRUE;

  while (walking) {
    walking = FALSE;
    if (node->kind == (an_expr_node_kind)enk_operation &&
        (node->variant.operation.kind == eok_ref_indirect ||
         node->variant.operation.kind == eok_class_rvalue_adjust ||
         node->variant.operation.kind == eok_lvalue_adjust) &&
        node->variant.operation.operands != NULL) {
      node = skip_parens(node->variant.operation.operands);
      walking = TRUE;
    }  /* if */
  }  /* while */
  return node;
}  /* empty_assign_operand */




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
  } else if (op == eok_dot_pm_call || op == eok_points_to_pm_call) {
    do_pm_call(frame, idx);
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
    if ((op == eok_question || op == eok_land || op == eok_lor) &&
        conditional_needs_fullexpr(node, op2)) {
      uint32_t  vt = (is_void_node_type(node) || is_glvalue_node(node) ||
                      !value_is_pushed(node, (flags & CIF_UNUSED) != 0)) ? 0 :
                     ecil_type_id(frame, node->type);

      if (vt != 0 && (info_of(vt)->kind == egk_record ||
                      info_of(vt)->kind == egk_array)) {
        ecil_note_unsupported("a throw in a conditional operand of a class "
                              "value");
      } else {
        emit_fullexpr_begin(frame);
        it->c |= COND_FULLEXPR;
      }  /* if */
    }  /* if */
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
      case eok_pm_field:
        /* The object's address first (a temporary is materialized for a
           value), then the pointer to the member. */
        push_expr(op1, is_class_temp_object(op1) ? CIF_GLV : 0u);
        break;
      case eok_pm_points_to_field:
        push_expr(op2, 0);
        push_expr(op1, 0);
        break;
      case eok_pm_base_class_cast:
      case eok_pm_derived_class_cast:
      case eok_dynamic_cast:
      case eok_ref_dynamic_cast:
        push_expr(op1, 0);
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
                        is_void_node_type(node)) ? CIF_UNUSED :
                       ((op == eok_lvalue_adjust && is_glvalue_node(node) &&
                         skip_parens(op1)->kind ==
                                  (an_expr_node_kind)enk_temp_init) ||
                        ((op == eok_reference_to ||
                          op == eok_array_to_pointer) &&
                         is_array_temp_init(skip_parens(op1)))) ?
                                                         CIF_GLV : 0u);
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
            if (op == eok_subscript && int_op == op1 && !C_mode() &&
                skip_parens(int_op)->kind != (an_expr_node_kind)enk_constant) {
              /* The integer operand is written first (C++17 sequences it
                 before the pointer; C does not). */
              uint32_t  tmp = new_lid();

              emit_local(frame, tmp, ecil_type_id(frame, int_op->type), 0, 0);
              it->a = tmp;
              it->phase = SUBSCRIPT_INT_FIRST;
              ecil_rec1(frame, eop_lref, tmp);
              push_expr(int_op, 0);
            } else {
              push_expr(int_op, 0);
              push_expr(ptr_op, 0);
            }  /* if */
          }  /* if */
        }
        break;
      case eok_spaceship:
        /* The category object is built in a temporary: its member is the
           target of the CMP3WAY result (spec 11.12), which is read back as
           a record value. */
        { uint32_t   rec = 0;
          uint32_t   index = 0;
          uint32_t   tint = 0;
          int64_t    values[4];
          a_boolean  has_unordered = FALSE;

          if (spaceship_shape(frame, node, &rec, &index, &tint, values,
                              &has_unordered)) {
            uint32_t  tmp = new_lid();

            emit_local(frame, tmp, rec, 0, 0);
            ecil_rec1(frame, eop_lref, tmp);
            ecil_rec2(frame, eop_field, rec, index);
            it = &code_stack[idx];
            it->a = tmp;
            push_expr(op2, 0);
            push_expr(op1, 0);
          }  /* if */
        }
        break;
      case eok_assign:
        if (!C_mode() && is_immediate_class_type(skip_typerefs(op1->type)) &&
            type_is_empty_class(frame, op1->type) &&
            (node->result_is_not_used || is_glvalue_node(node))) {
          /* A class without data: the assignment copies nothing (a store
             would write a byte of whatever shares its address).  The
             operands are evaluated, the right one first. */
          an_expr_node_ptr  rhs = empty_assign_operand(op2);

          it->phase = EMPTY_ASSIGN_RHS;
          it->cursor = rhs;
          push_expr(rhs, (is_glvalue_node(rhs) &&
                          is_immediate_class_type(skip_typerefs(rhs->type))) ?
                         CIF_GLV : CIF_UNUSED);
        } else if (!C_mode() && node->variant.operation.eval_right_to_left &&
            !target_may_be_evaluated_first(op1, op2) &&
            skip_typerefs(op1->type)->kind != tk_array &&
            ecil_type_id(frame, op2->type) != 0) {
          /* C++17: the right operand is evaluated before the left one, which
             has side effects: keep the value in a temporary.  For a class
             the right operand is bound to the parameter of the (trivial)
             assignment and read when it is called, after the target is
             designated, so what is kept is its address: the object itself,
             or the temporary of the full expression that holds a prvalue
             (which has its normal cleanup). */
          a_boolean  is_class = !C_mode() &&
                                (skip_typerefs(op2->type)->kind == tk_struct ||
                                 skip_typerefs(op2->type)->kind == tk_class ||
                                 skip_typerefs(op2->type)->kind == tk_union);
          an_expr_node_ptr  rhs = skip_parens(op2);
          uint32_t  tmp = new_lid();

          if (!is_class) {
            emit_local(frame, tmp, ecil_type_id(frame, op2->type), 0, 0);
            it->a = tmp;
            it->phase = ASSIGN_RHS_FIRST;
            ecil_rec1(frame, eop_lref, tmp);
            push_expr(op2, 0);
          } else if (rhs->kind == (an_expr_node_kind)enk_temp_init ||
                     rhs_is_adjusted_glvalue(rhs) ||
                     rhs_designates_object(rhs)) {
            uint32_t  pt = ecil_ptr_type(frame,
                                         ecil_type_id(frame, op2->type));
            a_boolean glv = rhs->kind == (an_expr_node_kind)enk_temp_init ||
                            rhs_is_adjusted_glvalue(rhs);

            if (!glv && suppress_load_node != NULL) {
              /* The load of an outer right operand is still suppressed: the
                 state is a single node, not a stack. */
              ecil_note_unsupported("a class assignment nested in the right "
                                    "operand of another");
            }  /* if */
            emit_local(frame, tmp, pt, 0, 0);
            it->a = tmp;
            /* Its address is kept: 1 if the load of the designating node is
               suppressed, 2 if the node is translated as a glvalue. */
            it->b = glv ? 2u : 1u;
            it->phase = ASSIGN_RHS_FIRST;
            ecil_rec1(frame, eop_lref, tmp);
            if (!glv) {
              suppress_load_node = rhs;
              load_suppressed = FALSE;
            }  /* if */
            push_expr(op2, glv ? CIF_GLV : 0u);
          } else if (rhs_is_fixed_value(rhs)) {
            /* A prvalue that is not held in an object (the result of a call
               of a trivially copyable class): its value is fixed. */
            emit_local(frame, tmp, ecil_type_id(frame, op2->type), 0, 0);
            it->a = tmp;
            it->phase = ASSIGN_RHS_FIRST;
            ecil_rec1(frame, eop_lref, tmp);
            push_expr(op2, 0);
          } else {
            /* Another right operand (a conditional, a comma expression, the
               result of an assignment) is neither an object whose address
               can be kept nor a value that is fixed when it is evaluated. */
            ecil_note_unsupported("an assignment of a class whose right "
                                  "operand is evaluated before a target with "
                                  "side effects");
          }  /* if */
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
          cond_depth++;                 /* The right operand is conditional. */
          push_expr(op2, CIF_COND);
          done = FALSE;
        } else {
          if (cond_depth > 0) cond_depth--;
          ecil_rec0(frame, op == eok_land ? eop_land_end : eop_lor_end);
          if ((it->c & COND_FULLEXPR) != 0) emit_fullexpr_end(frame);
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
          /* A class value that the context discards (a statement, a void
             cast, a comma's left operand) is not carried: the conditional is
             a void TERN and each arm is discarded in its own region. */
          a_boolean  discarded_class = !lvalue && !is_void_node_type(node) &&
                  !C_mode() &&
                  is_immediate_class_type(skip_typerefs(node->type)) &&
                  (node->result_is_not_used ||
                   (flags & (CIF_UNUSED | CIF_DROP)) != 0);
          a_boolean  unused_class = !lvalue &&
                                    (discarded_class || master_arms);
          unsigned int  arm_flags = (discarded_class && !master_arms) ?
                                    CIF_UNUSED : 0u;

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
              cond_depth++;             /* The first arm is conditional. */
              push_expr(op2, arm_flags);
            }  /* if */
            done = FALSE;
          } else if (it->phase == 2) {
            if (cond_depth > 0) cond_depth--;
            if (lvalue) ecil_rec0(frame, eop_addr);
            if (arm_flags != 0 && value_is_pushed(op2, TRUE)) {
              ecil_rec0(frame, eop_drop);
            }  /* if */
            ecil_rec0(frame, eop_tern_else);
            it->phase = 3;
            cond_depth++;               /* ... and so is the second. */
            push_expr(op2->next, arm_flags);
            done = FALSE;
          } else {
            if (cond_depth > 0) cond_depth--;
            if (lvalue) ecil_rec0(frame, eop_addr);
            if (arm_flags != 0 && value_is_pushed(op2->next, TRUE)) {
              ecil_rec0(frame, eop_drop);
            }  /* if */
            ecil_rec0(frame, eop_tern_end);
            if ((it->c & COND_FULLEXPR) != 0) emit_fullexpr_end(frame);
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
        if (it->phase == SUBSCRIPT_INT_FIRST) {
          /* The integer is evaluated: store it, then evaluate the pointer. */
          ecil_rec_begin(frame, eop_store);
          ecil_arg_uint(frame, ECIL_STORE_INIT);
          ecil_arg_uint(frame, (unsigned int)esr_none);
          ecil_rec_end(frame);
          it->phase = SUBSCRIPT_PTR_NEXT;
          push_expr(op2, 0);
          done = FALSE;
          break;
        }  /* if */
        if (it->phase == SUBSCRIPT_PTR_NEXT) {
          ecil_rec1(frame, eop_lref, it->a);
          ecil_rec1(frame, eop_load, 0);
        }  /* if */
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
                                      is_glvalue_node(skip_parens(op1)) ||
                                      is_array_temp_init(skip_parens(op1));
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
      case eok_pm_field:
        if (it->phase == 1) {
          emit_pm_object_address(frame, op1);
          it->phase = 2;
          push_expr(op2, 0);
          done = FALSE;
        } else {
          emit_pm_field(frame, node, op2);
          finish_value(frame, flags);
        }  /* if */
        break;
      case eok_pm_points_to_field:
        emit_pm_field(frame, node, op2);
        finish_value(frame, flags);
        break;
      case eok_pm_base_class_cast:
      case eok_pm_derived_class_cast:
        emit_pm_cast(frame, node);
        finish_value(frame, flags);
        break;
      case eok_dynamic_cast:
      case eok_ref_dynamic_cast:
        emit_dynamic_cast(frame, node);
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
          } else if (is_array_temp_init(target)) {
            /* The array temporary itself (its storage was pushed). */
            ecil_rec0(frame, eop_addr);
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
          } else if (dst != 0 && src != 0 && op == eok_lvalue_adjust &&
                     src != dst && is_glvalue_node(skip_parens(op1)) &&
                     is_glvalue_node(node) &&
                     (info_of(src)->kind == egk_record ||
                      info_of(src)->kind == egk_array) &&
                     (info_of(dst)->kind == egk_record ||
                      info_of(dst)->kind == egk_array)) {
            /* The same object seen as another aggregate type (the array
               temporary that a reference binds to): its address,
               converted. */
            uint32_t  pt = ecil_ptr_type(frame, dst);

            ecil_rec0(frame, eop_addr);
            if (pt != 0) emit_cast(frame, (unsigned int)eck_bitcast, pt);
            ecil_rec0(frame, eop_deref);
            if ((flags & CIF_GLV) == 0) emit_load_if_value(frame, node);
            finish_value(frame, flags);
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
      case eok_spaceship:
        { uint32_t   rec = 0;
          uint32_t   index = 0;
          uint32_t   tint = 0;
          int64_t    values[4];
          a_boolean  has_unordered = FALSE;

          if (spaceship_shape(frame, node, &rec, &index, &tint, values,
                              &has_unordered)) {
            ecil_rec_begin(frame, eop_cmp3way);
            ecil_arg_uint(frame, tint);
            ecil_arg_int(frame, values[0]);
            ecil_arg_int(frame, values[1]);
            ecil_arg_int(frame, values[2]);
            if (has_unordered) {
              ecil_arg_int(frame, values[3]);
            } else {
              ecil_arg_nil(frame);
            }  /* if */
            ecil_rec_end(frame);
            ecil_rec_begin(frame, eop_store);
            ecil_arg_uint(frame, ECIL_STORE_INIT);
            ecil_arg_uint(frame, (unsigned int)esr_none);
            ecil_rec_end(frame);
            ecil_rec1(frame, eop_lref, it->a);
            ecil_rec1(frame, eop_load, 0);
            finish_value(frame, flags);
          }  /* if */
        }
        break;
      case eok_assign:
        if (it->phase == EMPTY_ASSIGN_RHS) {
          if (value_is_pushed((an_expr_node_ptr)it->cursor, TRUE)) {
            ecil_rec0(frame, eop_drop);
          }  /* if */
          it->phase = EMPTY_ASSIGN_LHS;
          push_expr(op1, 0);
          done = FALSE;
          break;
        }  /* if */
        if (it->phase == EMPTY_ASSIGN_LHS) {
          if (node->result_is_not_used) ecil_rec0(frame, eop_drop);
          finish_value(frame, flags);
          break;
        }  /* if */
        if (it->phase == ASSIGN_RHS_FIRST) {
          /* The value (or, for a class, the glvalue of the right operand)
             is evaluated: store it in the temporary, then designate the
             target. */
          if (it->b != 0) {
            if (it->b == 1u) {
              /* Only this assignment's own suppression ends here: the
                 state of an outer one (b == 1 there) is not touched by an
                 assignment that kept a glvalue (b == 2). */
              if (!load_suppressed) {
                ecil_note_unsupported("an assignment of a class whose right "
                                      "operand cannot be designated first");
              }  /* if */
              suppress_load_node = NULL;
              load_suppressed = FALSE;
            }  /* if */
            if (ecil_unsupported_reason == NULL) ecil_rec0(frame, eop_addr);
          }  /* if */
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
          if (it->b != 0) {
            /* The kept address: the value is read from the object now. */
            ecil_rec0(frame, eop_deref);
            ecil_rec1(frame, eop_load, 0);
          }  /* if */
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
          if (ecil_unsupported_reason != NULL) {
            /* Noted. */
          } else if (type_is_empty_class(frame, op1->type)) {
            /* Its value is used as a prvalue: not expressed. */
            ecil_note_unsupported("the value of an assignment of a class "
                                  "without data");
          } else {
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


static a_boolean gid_is_empty_class(uint32_t gid)
/*
Return TRUE for the ECIL type of a class without data (no member or vptr, and
bases that are empty themselves): an object of it has no value and nothing is
written for it (clang's data size is 0).
*/
{
  return gid != 0 && !C_mode() && info_of(gid)->kind == egk_record &&
         (info_of(gid)->flags & ECIL_GF_COMPLETE) != 0 &&
         (info_of(gid)->count == 0 || info_of(gid)->data_size == 0);
}  /* gid_is_empty_class */


static void emit_zero_object(an_ecil_frame *frame, uint32_t gid)
/*
Zero the object whose glvalue is on top of the stack (consumed).  An empty
class is not written.  A record or
array is zeroed byte by byte, so that its padding is zero too
(value-initialization zero-initializes the object representation).  One that
contains a pointer to a data member is not zero-initializable (its null is
-1): INIT_ZERO writes the null constants (spec 12.7).
*/
{
  if (gid_is_empty_class(gid)) {
    /* An empty class has no value: its byte may belong to another object
       (an empty base or a [[no_unique_address]] member at the address of a
       member), and nothing is written for it. */
    ecil_rec0(frame, eop_drop);
  } else if (gid != 0 && !C_mode() && !ecil_gid_has_data_member_pointer(gid) &&
      (info_of(gid)->kind == egk_record ||
       info_of(gid)->kind == egk_array) &&
      info_of(gid)->size != 0) {
    ecil_rec1(frame, eop_zero_bytes, info_of(gid)->size);
  } else {
    ecil_rec0(frame, eop_init_zero);
  }  /* if */
}  /* emit_zero_object */


static void emit_zero_data_size(an_ecil_frame *frame, a_type_ptr type)
/*
Zero the data size of the object whose glvalue is on top of the stack
(consumed): its tail padding may hold a later member.  A class that is not
zero-initializable (a pointer to a data member) uses INIT_ZERO_BASE, which
writes the null constants of the first nvsize bytes (spec 12.7).
*/
{
  uint32_t  gid = ecil_type_id(frame, type);

  if (gid != 0 && ecil_gid_has_data_member_pointer(gid)) {
    if (info_of(gid)->kind == egk_record) {
      ecil_rec0(frame, eop_init_zero_base);
    } else {
      ecil_note_unsupported("the zero-initialization of an array of data "
                            "member pointers in a [[no_unique_address]] "
                            "member");
    }  /* if */
  } else {
    ecil_rec1(frame, eop_zero_bytes, (uint64_t)ecil_data_size_of_type(
                                          skip_typerefs(type)));
  }  /* if */
}  /* emit_zero_data_size */


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
    /* Only the base's nvsize bytes: its tail padding may hold members of the
       derived class (spec 12.7). */
    ecil_rec0(frame, eop_init_zero_base);
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
repeated constructor call, with the default arguments if it has them), or
NULL; *ctor_di is its initialization.
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
                                (a_dynamic_init_kind)dik_constructor) {
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


static a_routine_ptr class_destructor_of(a_type_ptr type);


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
                                          &destroying) && destroying &&
               nds->array_delete) {
      ecil_note_unsupported("a destroying operator delete of an array");
    } else {
      it->a = (uint32_t)(nds->array_delete ? ECIL_DELETE_ARRAY : 0u) |
              (sized ? ECIL_DELETE_SIZED : 0u) |
              (aligned ? ECIL_DELETE_ALIGNED : 0u) |
              (destroying ? ECIL_DELETE_DESTROYING : 0u);
      it->phase = 1;
      push_expr(nds->arg, 0);
    }  /* if */
  } else {
    a_routine_ptr  del = delete_routine_of(nds);
    a_type_ptr     obj = innermost_element_type(nds->type, NULL);
    a_routine_ptr  dtor = nds->dynamic_init != NULL ?
                          nds->dynamic_init->destructor : NULL;
    uint32_t       tg = ecil_type_id(frame, obj);

    if (dtor == NULL && (it->a & ECIL_DELETE_DESTROYING) != 0 &&
        is_immediate_class_type(skip_typerefs(obj))) {
      /* The front end records no destructor call for a destroying delete,
         but D0 of a virtual destructor is what runs. */
      dtor = class_destructor_of(obj);
    }  /* if */
    uint32_t       gd = ecil_function_id(frame, del);
    uint32_t       gdt = 0;

    if (dtor != NULL && dtor->is_virtual) {
      /* D0 of the dynamic type, through the vtable (spec 12.6): with the
         operator delete that the destructor's class chose, so not a
         ::delete that bypasses it, and not an array. */
      a_type_ptr  dcls = parent_class_of(dtor);

      if (nds->array_delete) {
        /* The elements are destroyed through the static type's destructor,
           not through the vtable. */
        gdt = ecil_function_id(frame, dtor);
      } else if (del == NULL) {
        ecil_note_unsupported("a delete without an operator delete");
      } else {
        ecil_class_vtable_facts(frame, dcls);
        gdt = ecil_function_id(frame, dtor);
        it->a = (it->a & ~ECIL_DELETE_DESTROYING) | ECIL_DELETE_VIRTUAL;
        if (nds->global_new_or_delete ||
            del != class_type_supp(dcls)->assoc_operator_delete_routine) {
          /* ::delete bypasses the class's operator delete, which D0 calls:
             D1 through the vtable, then the operator delete that the front
             end chose, on the complete object (spec 12.6). */
          it->a |= ECIL_DELETE_GLOBAL;
        }  /* if */
      }  /* if */
    } else if (dtor != NULL && (it->a & ECIL_DELETE_DESTROYING) == 0) {
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


static void note_master_dest(a_dynamic_init_ptr master, uint32_t dest)
/*
Remember where the arms of a class ?: that initializes the temporary of the
master entry build their value.
*/
{
  if (num_master_dests == master_dests_capacity) {
    size_t         new_capacity = master_dests_capacity == 0 ?
                                  8 : master_dests_capacity * 2;
    a_master_dest  *new_list = (a_master_dest *)realloc(master_dests,
                                    new_capacity * sizeof(a_master_dest));

    if (new_list == NULL) {
      ecil_note_unsupported("out of memory in the ECIL writer");
    } else {
      master_dests = new_list;
      master_dests_capacity = new_capacity;
    }  /* if */
  }  /* if */
  if (num_master_dests < master_dests_capacity) {
    master_dests[num_master_dests].master = master;
    master_dests[num_master_dests].dest = dest;
    num_master_dests++;
  }  /* if */
}  /* note_master_dest */


static a_boolean master_dest_of(a_dynamic_init_ptr master, uint32_t *dest)
/*
The destination noted for a master entry (0 is the return slot): FALSE if
there is none.
*/
{
  a_boolean  found = FALSE;
  size_t     i;

  for (i = num_master_dests; i > 0 && !found; i--) {
    if (master_dests[i - 1].master == master) {
      *dest = master_dests[i - 1].dest;
      found = TRUE;
    }  /* if */
  }  /* for */
  return found;
}  /* master_dest_of */


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


static a_boolean new_array_size_throws(a_code_item *it)
/*
Return TRUE if the size of a new[] with a run-time count is computed by
NEW_ARRAY_SIZE_THROW: from C++11 on, for an allocation that is not a nothrow
one (a negative or overflowing count throws std::bad_array_new_length).
*/
{
  return (it->c & NEW_DYNAMIC) != 0 && (it->c & NEW_NOTHROW) == 0 &&
         std_version >= 201103L;
}  /* new_array_size_throws */


static a_boolean new_one_byte_elements(an_ecil_frame             *frame,
                                       a_new_delete_supplement_ptr nds,
                                       a_code_item                 *it)
/*
Return TRUE if every element of the new[] is one byte (the inner bounds of
a multi-dimensional array are in it->b).
*/
{
  uint32_t  eg = ecil_type_id(frame, innermost_element_type(nds->type, NULL));

  return eg != 0 && ecil_gid_info(eg)->size * it->b == 1u;
}  /* new_one_byte_elements */


static void emit_throw_bad_array_new_length(an_ecil_frame *frame)
/*
The bool on the stack says that the count of a new[] is bad:
__cxa_throw_bad_array_new_length is called if it is true.  It is a void
conditional, not an IF, because the new expression may be inside a FULLEXPR.
*/
{
  uint32_t  fg = ecil_throw_bad_array_new_length_function(frame);
  uint32_t  ft = fg != 0 ? ecil_gid_info(fg)->decl_type : 0;

  if (fg != 0 && ft != 0) {
    ecil_rec_begin(frame, eop_tern_begin);
    ecil_arg_nil(frame);
    ecil_rec_end(frame);
    ecil_rec_begin(frame, eop_call);
    ecil_arg_uint(frame, fg);
    ecil_arg_uint(frame, ft);
    ecil_arg_uint(frame, 0);
    ecil_arg_uint(frame, ECIL_CALL_NORETURN | ECIL_CALL_RESULT_UNUSED);
    ecil_rec_end(frame);
    ecil_rec0(frame, eop_tern_else);
    ecil_rec0(frame, eop_tern_end);
  } else {
    ecil_rec0(frame, eop_drop);
  }  /* if */
}  /* emit_throw_bad_array_new_length */


static void emit_zero_new_array_loop(an_ecil_frame *frame, a_code_item *it,
                                     uint32_t elem_gid, uint32_t elem_ptr,
                                     uint64_t skip)
/*
The new[] pointer is on the stack and stays: every one of its elements (the
run-time count in it) is zero-initialized.  A record element is in an
ARRAY_CTOR_BEGIN/END loop of INIT_ZERO (which writes the null constants of a
data member pointer, spec 12.7); another element (cir.array.ctor takes only
records) is in a WHILE loop over a counter and a local pointer.  elem_ptr is
the pointer to the innermost element, for a multi-dimensional array, else 0.
The first skip elements are left alone (the list of a new[] initialized them).
*/
{
  uint32_t  sizet = ecil_target_uintptr_type(frame);

  if (info_of(elem_gid)->kind == egk_record) {
    ecil_rec0(frame, eop_dup);
    if (elem_ptr != 0) emit_cast(frame, (unsigned int)eck_bitcast, elem_ptr);
    if (skip != 0) {
      ecil_rec2(frame, eop_const_int, sizet, skip);
      ecil_rec1(frame, eop_ptr_stride,
                (unsigned int)ECIL_PTR_STRIDE_INDEX_UNSIGNED);
    }  /* if */
    new_push_count(frame, it);
    if (skip != 0) {
      ecil_rec2(frame, eop_const_int, sizet, skip);
      ecil_rec1(frame, eop_sub, 0);
    }  /* if */
    ecil_rec2(frame, eop_array_ctor_begin, elem_gid, 0);
    ecil_rec0(frame, eop_deref);
    ecil_rec0(frame, eop_init_zero);
    ecil_rec0(frame, eop_array_ctor_end);
  } else {
    uint32_t  pt = ecil_ptr_type(frame, elem_gid);
    uint32_t  lp = new_lid();
    uint32_t  li = new_lid();

    emit_local(frame, li, sizet, 0, 0);
    ecil_rec0(frame, eop_dup);
    if (elem_ptr != 0) emit_cast(frame, (unsigned int)eck_bitcast, elem_ptr);
    if (skip != 0) {
      ecil_rec2(frame, eop_const_int, sizet, skip);
      ecil_rec1(frame, eop_ptr_stride,
                (unsigned int)ECIL_PTR_STRIDE_INDEX_UNSIGNED);
    }  /* if */
    emit_materialize_temp(frame, lp, pt);     /* Introduces lp. */
    ecil_rec0(frame, eop_drop);
    ecil_rec1(frame, eop_lref, li);
    ecil_rec2(frame, eop_const_int, sizet, 0);
    ecil_rec_begin(frame, eop_store);
    ecil_arg_uint(frame, ECIL_STORE_INIT);
    ecil_arg_uint(frame, (unsigned int)esr_none);
    ecil_rec_end(frame);
    ecil_rec0(frame, eop_while_begin);
    ecil_rec1(frame, eop_lref, li);
    ecil_rec1(frame, eop_load, 0);
    new_push_count(frame, it);
    if (skip != 0) {
      ecil_rec2(frame, eop_const_int, sizet, skip);
      ecil_rec1(frame, eop_sub, 0);
    }  /* if */
    ecil_rec1(frame, eop_cmp, (unsigned int)ecmp_lt);
    ecil_rec0(frame, eop_while_body);
    ecil_rec1(frame, eop_lref, lp);
    ecil_rec1(frame, eop_load, 0);
    ecil_rec1(frame, eop_lref, li);
    ecil_rec1(frame, eop_load, 0);
    ecil_rec1(frame, eop_ptr_stride,
              (unsigned int)ECIL_PTR_STRIDE_INDEX_UNSIGNED);
    ecil_rec0(frame, eop_deref);
    ecil_rec0(frame, eop_init_zero);
    ecil_rec1(frame, eop_lref, li);
    ecil_rec_begin(frame, eop_inc_dec);
    ecil_arg_uint(frame, (unsigned int)eidk_pre_inc);
    ecil_arg_uint(frame, 0);
    ecil_rec_end(frame);
    ecil_rec0(frame, eop_drop);
    ecil_rec0(frame, eop_while_end);
  }  /* if */
}  /* emit_zero_new_array_loop */


static a_boolean new_has_throwing_initializer(a_new_delete_supplement_ptr nds)
/*
Return TRUE if the initialization of a new expression may throw: anything
but no initialization, zero and a constant, and a constructor that cannot
throw (and takes no arguments, which could).
*/
{
  a_dynamic_init_ptr  di = nds->dynamic_init;
  a_boolean           may_throw = FALSE;

  if (di != NULL) {
    switch (di->kind) {
      case dik_none:
      case dik_zero:
      case dik_constant:
        break;
      case dik_constructor:
        may_throw = di->variant.constructor.args != NULL ||
                    di->variant.constructor.ptr == NULL ||
                    !is_non_throwing_routine(di->variant.constructor.ptr);
        break;
      case dik_expression:
        may_throw = skip_parens(di->variant.expression)->kind !=
                                              (an_expr_node_kind)enk_constant;
        break;
      default:
        may_throw = TRUE;
        break;
    }  /* switch */
  }  /* if */
  return may_throw;
}  /* new_has_throwing_initializer */


static a_new_cleanup *new_cleanup_of(a_code_item *it)
/*
The entry of the new expression item, or NULL (it has no free cleanup).
*/
{
  return it->object == NULL ? NULL :
         &new_cleanups[(size_t)(uintptr_t)it->object - 1];
}  /* new_cleanup_of */


static a_boolean push_new_cleanup(a_code_item *it,
                                  a_routine_ptr del,
                                  unsigned int  num_new_args)
/*
Make the entry of a new expression with a free cleanup; FALSE (reason noted)
if the delete function does not match the operands of the allocation.
*/
{
  a_boolean         ok = TRUE;
  a_param_type_ptr  p;
  unsigned int      nparams = 0;
  a_new_cleanup     *entry;
  a_boolean         sized_dummy = FALSE;
  a_boolean         aligned_dummy = FALSE;
  a_boolean         destroying_dummy = FALSE;

  if (num_new_cleanups == new_cleanups_capacity) {
    size_t         new_capacity = new_cleanups_capacity == 0 ?
                                  8 : new_cleanups_capacity * 2;
    a_new_cleanup  *new_list = (a_new_cleanup *)realloc(new_cleanups,
                                    new_capacity * sizeof(a_new_cleanup));

    if (new_list == NULL) {
      ecil_note_unsupported("out of memory in the ECIL writer");
      ok = FALSE;
    } else {
      new_cleanups = new_list;
      new_cleanups_capacity = new_capacity;
    }  /* if */
  }  /* if */
  if (ok) {
    for (p = rout_type_supp(skip_typerefs(del->type))->param_type_list;
         p != NULL; p = p->next) {
      nparams++;
    }  /* for */
    if (num_new_args > NEW_MAX_ARGS) {
      ecil_note_unsupported("a new expression with many placement operands "
                            "and an operator delete");
      ok = FALSE;
    } else if (is_default_operator_delete(del, &sized_dummy, &aligned_dummy,
                                          &destroying_dummy) &&
               destroying_dummy) {
      ecil_note_unsupported("a destroying operator delete that frees the "
                            "storage of a new expression");
      ok = FALSE;
    } else if (nparams != 1 + num_new_args && nparams != 2 + num_new_args) {
      ecil_note_unsupported("a new expression whose operator delete does "
                            "not match the allocation function");
      ok = FALSE;
    }  /* if */
  }  /* if */
  if (ok) {
    entry = &new_cleanups[num_new_cleanups++];
    memset(entry, 0, sizeof(*entry));
    entry->del = del;
    entry->sized = nparams == 2 + num_new_args;
    it->object = (void *)(uintptr_t)num_new_cleanups;
  }  /* if */
  return ok;
}  /* push_new_cleanup */


static a_routine_ptr class_destructor_of(a_type_ptr type)
/*
The destructor of a class (declared or implicit), or NULL.
*/
{
  a_routine_ptr  result = NULL;

  type = skip_typerefs(type);
  if (is_immediate_class_type(type) &&
      class_type_supp(type)->assoc_scope != NULL) {
    a_routine_ptr  r;

    for (r = class_type_supp(type)->assoc_scope->routines;
         r != NULL && result == NULL; r = r->next) {
      if (r->special_kind == (a_special_function_kind)sfk_destructor) {
        result = r;
      }  /* if */
    }  /* for */
  }  /* if */
  return result;
}  /* class_destructor_of */


static void emit_array_partial_dtor(an_ecil_frame *frame,
                                    a_type_ptr    elem_type,
                                    a_routine_ptr dtor)
/*
ARRAY_PARTIAL_DTOR, directly after an ARRAY_CTOR_BEGIN with exceptions: the
elements already constructed are destroyed (by the element's D1) if a later
constructor throws (spec 12.4).  A class without a non-trivial destructor
needs none.
*/
{
  if (ecil_has_exceptions() && ecil_class_has_nontrivial_dtor(elem_type)) {
    if (dtor == NULL) dtor = class_destructor_of(elem_type);
    if (dtor == NULL) {
      ecil_note_unsupported("an array of objects whose destructor is not "
                            "known");
    } else {
      uint32_t  g = ecil_function_id(frame, dtor);

      if (g != 0) ecil_rec1(frame, eop_array_partial_dtor, g);
    }  /* if */
  }  /* if */
}  /* emit_array_partial_dtor */


static a_type_ptr innermost_destructible_class(a_type_ptr type);

static a_type_ptr new_list_type(a_new_delete_supplement_ptr nds)
/*
The type of the initializer list of a new[] (the array of the elements it
lists, whatever the run-time count), or NULL if there is no list.
*/
{
  a_dynamic_init_ptr  di = nds->dynamic_init;
  a_type_ptr          type = NULL;

  if (di != NULL && (di->kind == (a_dynamic_init_kind)dik_constant ||
                     di->kind == (a_dynamic_init_kind)
                                              dik_nonconstant_aggregate) &&
      di->variant.constant.ptr != NULL &&
      di->variant.constant.ptr->kind == (a_constant_repr_kind)ck_aggregate &&
      di->variant.constant.ptr->variant.aggregate.first_constant != NULL) {
    a_type_ptr  t = skip_typerefs(di->variant.constant.ptr->type);

    if (t->kind == tk_array && !t->incomplete) type = t;
  }  /* if */
  return type;
}  /* new_list_type */


static a_routine_ptr class_default_constructor_of(a_type_ptr type)
/*
The constructor of a class that takes no parameters, or NULL.
*/
{
  a_routine_ptr  result = NULL;

  type = skip_typerefs(type);
  if (is_immediate_class_type(type) &&
      class_type_supp(type)->assoc_scope != NULL) {
    a_routine_ptr  r;

    for (r = class_type_supp(type)->assoc_scope->routines;
         r != NULL && result == NULL; r = r->next) {
      if (r->special_kind == (a_special_function_kind)sfk_constructor &&
          rout_type_supp(skip_typerefs(r->type))->param_type_list == NULL &&
          !r->is_deleted) {
        result = r;
      }  /* if */
    }  /* for */
  }  /* if */
  return result;
}  /* class_default_constructor_of */


static void new_list_rest(an_ecil_frame *frame, a_code_item *it,
                          a_new_delete_supplement_ptr nds)
/*
After the first k elements of new T[n]{...} are initialized (the pointer to
them is in the local it->d): the destruction of those if a later element
throws, the value-initialization of the other n - k elements (a call of the
default constructor, or zeros), then the end of that cleanup.
*/
{
  a_type_ptr     lt = new_list_type(nds);
  uint64_t       k = (uint64_t)lt->variant.array.variant.number_of_elements;
  a_type_ptr     elem = innermost_element_type(nds->type, NULL);
  uint32_t       eg = ecil_type_id(frame, elem);
  uint32_t       at = ecil_type_id(frame, lt);
  uint32_t       sizet = ecil_target_uintptr_type(frame);
  a_type_ptr     cls = innermost_destructible_class(elem);
  a_routine_ptr  dtor = cls != NULL ? class_destructor_of(cls) : NULL;
  a_routine_ptr  ctor = class_default_constructor_of(elem);
  a_boolean      begun = FALSE;

  if (eg == 0 || at == 0) {
    ecil_note_unsupported("a new[] with a list and an unsupported element");
  } else if (dtor != NULL && ecil_has_exceptions()) {
    uint32_t  g = ecil_function_id(frame, dtor);

    ecil_rec_begin(frame, eop_cleanup_begin);
    ecil_arg_uint(frame, (unsigned int)ecl_eh);
    ecil_arg_nil(frame);
    ecil_arg_uint(frame, ECIL_CLEANUP_DEACTIVATABLE);
    ecil_rec_end(frame);
    ecil_rec1(frame, eop_lref, it->d);
    ecil_rec1(frame, eop_load, 0);
    emit_cast(frame, (unsigned int)eck_bitcast, ecil_ptr_type(frame, at));
    ecil_rec0(frame, eop_deref);
    ecil_rec2(frame, eop_array_dtor_begin, eg, ECIL_ARRAY_LOOP_FIXED);
    emit_call_record(frame, g, dtor, 1);
    ecil_rec0(frame, eop_array_dtor_end);
    ecil_rec0(frame, eop_cleanup_body);
    begun = TRUE;
  }  /* if */
  if (ecil_unsupported_reason == NULL && ctor != NULL) {
    uint32_t  gc = ecil_function_id(frame, ctor);

    ecil_rec1(frame, eop_lref, it->d);
    ecil_rec1(frame, eop_load, 0);
    ecil_rec2(frame, eop_const_int, sizet, k);
    ecil_rec1(frame, eop_ptr_stride,
              (unsigned int)ECIL_PTR_STRIDE_INDEX_UNSIGNED);
    ecil_rec1(frame, eop_lref, it->a);
    ecil_rec1(frame, eop_load, 0);
    ecil_rec2(frame, eop_const_int, sizet, k);
    ecil_rec1(frame, eop_sub, 0);
    ecil_rec2(frame, eop_array_ctor_begin, eg, 0);
    emit_array_partial_dtor(frame, elem, dtor);
    emit_call_record(frame, gc, ctor, 1);
    ecil_rec0(frame, eop_array_ctor_end);
  } else if (ecil_unsupported_reason == NULL) {
    ecil_rec1(frame, eop_lref, it->d);
    ecil_rec1(frame, eop_load, 0);
    emit_zero_new_array_loop(frame, it, eg, 0, k);
    ecil_rec0(frame, eop_drop);
  }  /* if */
  if (begun) ecil_rec0(frame, eop_cleanup_end);
}  /* new_list_rest */


static a_boolean emit_ctor_loop(an_ecil_frame *frame, a_dynamic_init_ptr cdi,
                                uint32_t elem_gid, unsigned int flags,
                                a_type_ptr elem_type, a_routine_ptr dtor,
                                size_t self_idx);

static void new_null_test_begin(an_ecil_frame *frame, an_expr_node_ptr node,
                                uint32_t ptr_lid, a_boolean push_pointer)
/*
A new whose allocation function can return null: the construction is in the
arm of a test of the result (kept in the local ptr_lid): TERN_BEGIN of the
pointer type, then (push_pointer) the pointer for the code that follows (the
null arm is added when the initialization is complete).
*/
{
  uint32_t  rt = ecil_type_id(frame, node->type);

  ecil_rec1(frame, eop_lref, ptr_lid);
  ecil_rec1(frame, eop_load, 0);
  ecil_rec0(frame, eop_to_bool);
  if (rt != 0) emit_tern_begin(frame, rt);
  if (push_pointer) {
    ecil_rec1(frame, eop_lref, ptr_lid);
    ecil_rec1(frame, eop_load, 0);
  }  /* if */
}  /* new_null_test_begin */


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
      if (routine->never_throws &&
          !(!routine->source_corresp.is_class_member &&
            skip_typerefs(routine->type)->variant.routine.extra_info->
                                               param_type_list->next != NULL &&
            is_void_star_type(skip_typerefs(routine->type)->variant.
                          routine.extra_info->param_type_list->next->type))) {
        /* The allocation can fail by returning null: the construction is
           skipped then, and the value is null. */
        it->c |= NEW_NOTHROW;
      }  /* if */
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
          if (inner == 1 && new_list_type(nds) != NULL) {
            it->c |= NEW_LIST;
          }  /* if */
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

    if (src != 0 && new_array_size_throws(it) && info_of(src)->is_signed &&
        info_of(src)->kind == egk_int &&
        new_one_byte_elements(frame, nds, it)) {
      /* The elements are one byte each: a negative count does not overflow
         the size of the allocation, so it is tested here. */
      ecil_rec0(frame, eop_dup);
      ecil_rec2(frame, eop_const_int, src, 0);
      ecil_rec1(frame, eop_cmp, (unsigned int)ecmp_lt);
      emit_throw_bad_array_new_length(frame);
    }  /* if */
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
    if ((it->c & NEW_LIST) != 0) {
      /* More initializers than elements: std::bad_array_new_length, before
         anything is allocated. */
      uint32_t  sizet = ecil_target_uintptr_type(frame);

      ecil_rec1(frame, eop_lref, it->a);
      ecil_rec1(frame, eop_load, 0);
      ecil_rec2(frame, eop_const_int, sizet, (uint64_t)new_list_type(nds)->
                                variant.array.variant.number_of_elements);
      ecil_rec1(frame, eop_cmp, (unsigned int)ecmp_lt);
      emit_throw_bad_array_new_length(frame);
    }  /* if */
    it->phase = 2;
  } else if (it->phase == 2) {
    a_type_ptr  type = nds->type;
    a_boolean   array = (it->c & NEW_ARRAY) != 0;
    uint32_t    sizet = ecil_target_uintptr_type(frame);
    a_routine_ptr  del = nds->freeing_of_storage_on_exception != NULL ?
                         nds->freeing_of_storage_on_exception->destructor :
                         NULL;
    a_new_cleanup  *entry = NULL;
    an_expr_node_ptr  a;
    unsigned int   num_new_args = 0;

    for (a = nds->arg; a != NULL; a = a->next) num_new_args++;
    if (ecil_has_exceptions() && del != NULL &&
        new_has_throwing_initializer(nds) &&
        !(!del->source_corresp.is_class_member && num_new_args == 1 &&
          is_void_star_type(nds->arg->type) &&
          seq_is_in_system_header(del->source_corresp.decl_position.seq))) {
      /* Free the storage if the initialization throws (spec 12.6), except
         for the standard global placement operator (nothing to free); one
         that the program declares itself is called. */
      if (push_new_cleanup(it, del, num_new_args)) {
        entry = new_cleanup_of(it);
        if ((it->c & NEW_NOTHROW) != 0 && fullexpr_depth == 0) {
          /* The free cleanup is in the arm of the null test: a conditional
             region needs a FULLEXPR around it. */
          emit_fullexpr_begin(frame);
          it->c |= NEW_FULLEXPR;
        }  /* if */
        entry->size_lid = new_lid();
        emit_local(frame, entry->size_lid, sizet, 0, 0);
        ecil_rec1(frame, eop_lref, entry->size_lid);
      }  /* if */
    }  /* if */
    if (array) {
      uint32_t  elem = ecil_type_id(frame, innermost_element_type(type, NULL));

      new_push_count(frame, it);
      ecil_rec2(frame, new_array_size_throws(it) ? eop_new_array_size_throw :
                                                   eop_new_array_size,
                elem, (it->c >> 8) & 0xFFFFu);
    } else {
      uint32_t  t = ecil_type_id(frame, type);

      if (t != 0) {
        ecil_rec2(frame, eop_const_int, sizet, ecil_gid_info(t)->size);
      }  /* if */
    }  /* if */
    if (entry != NULL) {
      ecil_rec_begin(frame, eop_store);
      ecil_arg_uint(frame, ECIL_STORE_INIT);
      ecil_arg_uint(frame, (unsigned int)esr_none);
      ecil_rec_end(frame);
    }  /* if */
    it->cursor = nds->arg;
    it->phase = 3;
    it->c &= ~NEW_NARGS_MASK;
  } else if (it->phase == 6) {
    /* A placement operand is on the stack above its local: store it. */
    ecil_rec_begin(frame, eop_store);
    ecil_arg_uint(frame, ECIL_STORE_INIT);
    ecil_arg_uint(frame, (unsigned int)esr_none);
    ecil_rec_end(frame);
    it->phase = 3;
  } else if (it->phase == 3) {
    a_new_cleanup  *entry = new_cleanup_of(it);

    if (it->cursor != NULL) {
      an_expr_node_ptr  arg = (an_expr_node_ptr)it->cursor;

      it->cursor = arg->next;
      it->c += NEW_NARGS_ONE;
      if (entry != NULL) {
        /* The operand is kept in a local: the delete function takes it
           again. */
        uint32_t  at = ecil_type_id(frame, arg->type);

        if (at == 0 || info_of(at)->kind == egk_record ||
            info_of(at)->kind == egk_array) {
          ecil_note_unsupported("a placement operand of a class type with "
                                "an operator delete");
        } else {
          uint32_t  lid = new_lid();

          entry->arg_lids[entry->num_args++] = lid;
          emit_local(frame, lid, at, 0, 0);
          ecil_rec1(frame, eop_lref, lid);
          it->phase = 6;
        }  /* if */
      }  /* if */
      push_expr(arg, 0);
    } else {
      a_routine_ptr  routine = new_routine_of(nds,
                                              (it->c & NEW_ARRAY) != 0);
      uint32_t       gid = ecil_function_id(frame, routine);
      unsigned int   nargs = 1 + ((it->c & NEW_NARGS_MASK) >> NEW_NARGS_SHIFT);

      if (entry != NULL) {
        /* <local> <size> <operands> CALL, stored; the free cleanup follows
           the allocation (spec 12.6, worked encoding 10). */
        uint32_t      sizet = ecil_target_uintptr_type(frame);
        uint32_t      gdel = ecil_function_id(frame, entry->del);
        unsigned int  k;
        unsigned int  ndel = 1 + (entry->sized ? 1u : 0u) + entry->num_args;

        entry->ptr_lid = new_lid();
        emit_local(frame, entry->ptr_lid, ecil_ptr_type(frame,
                                          ecil_void_type(frame)), 0, 0);
        ecil_rec1(frame, eop_lref, entry->ptr_lid);
        ecil_rec1(frame, eop_lref, entry->size_lid);
        ecil_rec1(frame, eop_load, 0);
        for (k = 0; k < entry->num_args; k++) {
          ecil_rec1(frame, eop_lref, entry->arg_lids[k]);
          ecil_rec1(frame, eop_load, 0);
        }  /* for */
        emit_call_record(frame, gid, routine, nargs);
        ecil_rec_begin(frame, eop_store);
        ecil_arg_uint(frame, ECIL_STORE_INIT);
        ecil_arg_uint(frame, (unsigned int)esr_none);
        ecil_rec_end(frame);
        if ((it->c & NEW_NOTHROW) != 0) {
          new_null_test_begin(frame, node, entry->ptr_lid, FALSE);
        }  /* if */
        ecil_rec_begin(frame, eop_cleanup_begin);
        ecil_arg_uint(frame, (unsigned int)ecl_eh);
        ecil_arg_nil(frame);
        ecil_arg_uint(frame, ECIL_CLEANUP_DEACTIVATABLE);
        ecil_rec_end(frame);
        ecil_rec1(frame, eop_lref, entry->ptr_lid);
        ecil_rec1(frame, eop_load, 0);
        if (entry->sized) {
          ecil_rec1(frame, eop_lref, entry->size_lid);
          ecil_rec1(frame, eop_load, 0);
        }  /* if */
        for (k = 0; k < entry->num_args; k++) {
          ecil_rec1(frame, eop_lref, entry->arg_lids[k]);
          ecil_rec1(frame, eop_load, 0);
        }  /* for */
        emit_call_record(frame, gdel, entry->del, ndel);
        ecil_rec0(frame, eop_cleanup_body);
        ecil_rec1(frame, eop_lref, entry->ptr_lid);
        ecil_rec1(frame, eop_load, 0);
        (void)sizet;
      } else {
        emit_call_record(frame, gid, routine, nargs);
        if ((it->c & NEW_NOTHROW) != 0) {
          uint32_t  nl = new_lid();

          emit_materialize_temp(frame, nl, ecil_ptr_type(frame,
                                                  ecil_void_type(frame)));
          ecil_rec0(frame, eop_drop);
          new_null_test_begin(frame, node, nl, TRUE);
        }  /* if */
      }  /* if */
      it->phase = 4;
    }  /* if */
  } else if (it->phase == 5) {
    /* The initialization is complete: the object belongs to the program. */
    if ((it->c & NEW_LIST) != 0) new_list_rest(frame, it, nds);
    if (it->object != NULL) {
      ecil_rec0(frame, eop_cleanup_end);
      if (num_new_cleanups > 0) num_new_cleanups--;
    }  /* if */
    if ((it->c & NEW_NOTHROW) != 0) {
      /* The null case: the allocation function failed. */
      uint32_t  rt = ecil_type_id(frame, node->type);

      ecil_rec0(frame, eop_tern_else);
      if (rt != 0) ecil_rec1(frame, eop_const_null, rt);
      ecil_rec0(frame, eop_tern_end);
    }  /* if */
    if ((it->c & NEW_FULLEXPR) != 0) emit_fullexpr_end(frame);
    code_stack_size--;
  } else {
    /* The storage is allocated (void *): the object pointer, then the
       initialization. */
    a_boolean         array = (it->c & NEW_ARRAY) != 0;
    uint32_t          cookie = (it->c >> 8) & 0xFFFFu;
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
    if (it->object != NULL || (it->c & (NEW_NOTHROW | NEW_LIST)) != 0) {
      it->phase = 5;       /* The initialization follows; then CLEANUP_END. */
    } else {
      code_stack_size--;                  /* The initialization follows. */
    }  /* if */
    if (ecil_unsupported_reason != NULL || di == NULL ||
        di->kind == (a_dynamic_init_kind)dik_none) {
      /* No initialization. */
    } else if (loop_ctor != NULL) {
      if (loop_di != NULL && ctor_needs_zeroing(loop_di)) {
        /* new A[n](): the storage of members that need it is zeroed first
           (the front end's need_zeroing_for_value_initialization). */
        if ((it->c & NEW_DYNAMIC) != 0) {
          emit_zero_new_array_loop(frame, it, elem_gid,
                                   multi ? elem_ptr : 0, 0);
        } else {
          ecil_rec0(frame, eop_dup);
          if (ecil_gid_has_data_member_pointer(elem_gid)) {
            /* Not zero-initializable: INIT_ZERO of the array of the
               elements writes the null constants (spec 12.7). */
            emit_cast(frame, (unsigned int)eck_bitcast, ecil_ptr_type(
                      frame, ecil_array_of(frame, elem_gid, it->b)));
            ecil_rec0(frame, eop_deref);
            ecil_rec0(frame, eop_init_zero);
          } else {
            if (multi && elem_ptr != 0) {
              emit_cast(frame, (unsigned int)eck_bitcast, elem_ptr);
            }  /* if */
            ecil_rec0(frame, eop_deref);
            ecil_rec1(frame, eop_zero_bytes,
                      (uint64_t)it->b * ecil_gid_info(elem_gid)->size);
          }  /* if */
        }  /* if */
      }  /* if */
      ecil_rec0(frame, eop_dup);
      if (multi && elem_ptr != 0) {
        emit_cast(frame, (unsigned int)eck_bitcast, elem_ptr);
      }  /* if */
      new_push_count(frame, it);
      if (!emit_ctor_loop(frame, loop_di, elem_gid, 0u,
                          innermost_element_type(nds->type, NULL),
                          di->destructor != NULL ? di->destructor :
                          nds->dynamic_init->destructor, NO_SELF)) {
        ecil_rec0(frame, eop_array_ctor_end);
      }  /* if */
    } else if (array && (it->c & NEW_DYNAMIC) != 0 &&
               (di->kind == (a_dynamic_init_kind)dik_zero ||
                (di->kind == (a_dynamic_init_kind)dik_constant &&
                 di->variant.constant.ptr->kind ==
                                      (a_constant_repr_kind)ck_aggregate &&
                 di->variant.constant.ptr->variant.aggregate.
                                                   first_constant == NULL))) {
      /* new T[n]() and new T[n]{}: every element is value-initialized to
         zero. */
      emit_zero_new_array_loop(frame, it, elem_gid, multi ? elem_ptr : 0, 0);
    } else if ((it->c & NEW_LIST) != 0) {
      /* The first k elements from the list, then the rest in phase 5. */
      a_type_ptr  lt = new_list_type(nds);
      uint32_t    at = ecil_type_id(frame, lt);
      uint32_t    lp = new_lid();

      if (at != 0 && result_gid != 0) {
        it->d = lp;
        ecil_rec0(frame, eop_dup);
        emit_materialize_temp(frame, lp, result_gid);
        ecil_rec0(frame, eop_drop);
        ecil_rec0(frame, eop_dup);
        emit_cast(frame, (unsigned int)eck_bitcast, ecil_ptr_type(frame, at));
        ecil_rec0(frame, eop_deref);
        { a_code_item *init = push_item(cik_init, di, 0);

          if (init != NULL) {
            init->a = DEST_ON_STACK;
            init->b = at;
          }  /* if */
        }
      }  /* if */
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


static void do_throw(an_ecil_frame *frame, size_t idx)
/*
enk_throw: a rethrow (no object) is RETHROW; otherwise THROW_BEGIN(type)
pushes the glvalue of the exception object, the initialization consumes it
(the same shapes as any other initialization with the destination on the
stack), and THROW_END(typeinfo, destructor) throws (spec 12.5).  THROW_BEGIN is
not a region: the temporaries of the operand belong to the enclosing full
expression.  A throw in a conditional region needs a FULLEXPR around the
region (conditional_needs_fullexpr).
*/
{
  a_code_item                *it = &code_stack[idx];
  an_expr_node_ptr           node = (an_expr_node_ptr)it->entry;
  a_throw_supplement_ptr     info = node->variant.throw_info;

  if (!ecil_has_exceptions()) {
    ecil_note_unsupported("a throw expression");
    code_stack_size--;
  } else if (info == NULL) {
    ecil_rec0(frame, eop_rethrow);
    code_stack_size--;
  } else if (it->phase == 0) {
    uint32_t  t = ecil_type_id(frame, info->type);
    uint32_t  ti = t != 0 ? ecil_typeinfo_id(frame, info->type) : 0;
    uint32_t  dtor = (t != 0 && ti != 0 && info->destructor != NULL) ?
                     ecil_function_id(frame, info->destructor) : 0;

    if (t == 0 || ti == 0 || ecil_unsupported_reason != NULL) {
      code_stack_size--;
    } else {
      a_dynamic_init_ptr  di = info->dynamic_init;

      ecil_rec1(frame, eop_throw_begin, t);
      it->a = ti;
      it->b = dtor;
      it->phase = 1;
      if (di == NULL || di->kind == (a_dynamic_init_kind)dik_none) {
        ecil_rec0(frame, eop_drop);       /* Nothing to construct. */
      } else {
        a_code_item  *init = push_item(cik_init, di, 0);

        if (init != NULL) {
          init->a = DEST_ON_STACK;
          init->b = t;
        }  /* if */
      }  /* if */
    }  /* if */
  } else {
    ecil_rec_begin(frame, eop_throw_end);
    ecil_arg_uint(frame, it->a);
    if (it->b != 0) {
      ecil_arg_uint(frame, it->b);
    } else {
      ecil_arg_nil(frame);
    }  /* if */
    ecil_rec_end(frame);
    code_stack_size--;
  }  /* if */
}  /* do_throw */


/* Builtins on pointers to data members (is_corresponding_member and
   is_pointer_interconvertible_with_class, run-time operands). */

#define PMB_STATIC_FALSE 0x01u  /* The answer is false whatever the values. */
#define PMB_TWO          0x02u  /* Two pointer operands (corresponding). */

static a_boolean is_pm_builtin(an_expr_node_ptr node)
/*
Return TRUE for a builtin operation on pointers to members that the front
end could not fold.
*/
{
  a_builtin_operation_kind  k = node->variant.builtin_operation.kind;

  return k == bok_is_corresponding_member ||
         k == bok_builtin_is_corresponding_member ||
         k == bok_is_pointer_interconvertible_with_class ||
         k == bok_builtin_is_pointer_interconvertible_with_class;
}  /* is_pm_builtin */


static void emit_pm_to_int(an_ecil_frame *frame, uint32_t pm_gid)
/*
A data member pointer prvalue becomes the integer that holds its offset (the
null value is the integer -1): its storage is read as a pointer-sized
integer.
*/
{
  uint32_t  wide = ecil_target_uintptr_type(frame);
  uint32_t  pw = ecil_ptr_type(frame, wide);

  emit_materialize_temp(frame, new_lid(), pm_gid);
  ecil_rec0(frame, eop_addr);
  if (pw != 0) emit_cast(frame, (unsigned int)eck_bitcast, pw);
  ecil_rec0(frame, eop_deref);
  ecil_rec1(frame, eop_load, 0);
}  /* emit_pm_to_int */


static void do_pm_builtin(an_ecil_frame *frame, size_t idx)
/*
__builtin_is_pointer_interconvertible_with_class (the member's offset is 0)
and __builtin_is_corresponding_member (the offsets are equal and below the
limit of the common initial sequence, which also rejects the null value), on
operands that are not constants (the front end's own lowering).  A pointer to
a member function or a class that is not standard-layout gives false.
Phases: 0 first operand pushed, 1 its integer kept (two operands: second
pushed), 2 compare.
*/
{
  a_code_item       *it = &code_stack[idx];
  an_expr_node_ptr  node = (an_expr_node_ptr)it->entry;
  an_expr_node_ptr  arg = node->variant.builtin_operation.operands;
  a_builtin_operation_kind
                    op = node->variant.builtin_operation.kind;
  a_boolean         ms_form = op == bok_is_corresponding_member ||
                              op == bok_is_pointer_interconvertible_with_class;
  a_boolean         two = op == bok_is_corresponding_member ||
                          op == bok_builtin_is_corresponding_member;
  an_expr_node_ptr  pm1;
  an_expr_node_ptr  pm2 = NULL;
  a_type_ptr        class1 = NULL;
  a_type_ptr        class2 = NULL;

  if (ms_form) {
    class1 = skip_typerefs(arg->variant.type_operand.type);
    arg = arg->next;
    if (two) {
      class2 = skip_typerefs(arg->variant.type_operand.type);
      arg = arg->next;
    }  /* if */
  }  /* if */
  pm1 = arg;
  if (two) pm2 = arg->next;
  if (it->phase == 0) {
    a_type_ptr  t1 = skip_typerefs(pm1->type);
    a_boolean   static_false;

    if (class1 == NULL) class1 = pm_class_type(t1);
    static_false = skip_typerefs(pm_member_type(t1))->kind == tk_routine ||
                   (!is_union_type(class1) && !two &&
                    !class_symbol_supp(symbol_for(class1))->standard_layout) ||
                   (two &&
                    !class_symbol_supp(symbol_for(class1))->standard_layout);
    if (two) {
      a_type_ptr  t2 = skip_typerefs(pm2->type);

      if (class2 == NULL) class2 = pm_class_type(t2);
      static_false = static_false ||
                     skip_typerefs(pm_member_type(t2))->kind == tk_routine ||
                     !class_symbol_supp(symbol_for(class2))->standard_layout;
      it->b = (uint32_t)common_initial_sequence_limit(class1, class2);
    }  /* if */
    it->c = (static_false ? PMB_STATIC_FALSE : 0u) | (two ? PMB_TWO : 0u);
    it->phase = 1;
    if (!static_false && two) {
      it->a = new_lid();
      emit_local(frame, it->a, ecil_target_uintptr_type(frame), 0, 0);
      ecil_rec1(frame, eop_lref, it->a);
    }  /* if */
    push_expr(pm1, 0);
  } else if (it->phase == 1) {
    uint32_t  g1 = ecil_type_id(frame, pm1->type);

    if ((it->c & PMB_STATIC_FALSE) != 0) {
      if (value_is_pushed(pm1, TRUE)) ecil_rec0(frame, eop_drop);
      if (two) {
        it->phase = 2;
        push_expr(pm2, 0);
      } else {
        ecil_rec2(frame, eop_const_int, ecil_type_id(frame, node->type), 0);
        finish_bool_result(frame, node, it->flags | CIF_COND);
        code_stack_size--;
      }  /* if */
    } else if (!two) {
      emit_pm_to_int(frame, g1);
      ecil_rec2(frame, eop_const_int, ecil_target_uintptr_type(frame), 0);
      ecil_rec1(frame, eop_cmp, (unsigned int)ecmp_eq);
      finish_bool_result(frame, node, it->flags);
      code_stack_size--;
    } else {
      emit_pm_to_int(frame, g1);
      ecil_rec_begin(frame, eop_store);
      ecil_arg_uint(frame, ECIL_STORE_INIT);
      ecil_arg_uint(frame, (unsigned int)esr_none);
      ecil_rec_end(frame);
      it->phase = 2;
      push_expr(pm2, 0);
    }  /* if */
  } else {
    uint32_t  wide = ecil_target_uintptr_type(frame);

    if ((it->c & PMB_STATIC_FALSE) != 0) {
      if (value_is_pushed(pm2, TRUE)) ecil_rec0(frame, eop_drop);
      ecil_rec2(frame, eop_const_int, ecil_type_id(frame, node->type), 0);
      finish_bool_result(frame, node, it->flags | CIF_COND);
    } else {
      /* a == b && a < limit, a the first offset (null is all ones). */
      emit_pm_to_int(frame, ecil_type_id(frame, pm2->type));
      ecil_rec1(frame, eop_lref, it->a);
      ecil_rec1(frame, eop_load, 0);
      ecil_rec1(frame, eop_cmp, (unsigned int)ecmp_eq);
      ecil_rec0(frame, eop_land_begin);
      ecil_rec1(frame, eop_lref, it->a);
      ecil_rec1(frame, eop_load, 0);
      ecil_rec2(frame, eop_const_int, wide, (uint64_t)it->b);
      ecil_rec1(frame, eop_cmp, (unsigned int)ecmp_lt);
      ecil_rec0(frame, eop_land_end);
      finish_bool_result(frame, node, it->flags);
    }  /* if */
    code_stack_size--;
  }  /* if */
}  /* do_pm_builtin */


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
    case enk_typeid:
      do_typeid(frame, idx);
      break;
    case enk_throw:
      do_throw(frame, idx);
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

        if (is_discarded_conditional_temp(node,
                                          (it->flags & CIF_UNUSED) != 0)) {
          /* The temporary that holds the discarded value of a class
             conditional: its arms are the objects, each destroyed with its
             own temporaries, so the conditional is translated as a discarded
             one. */
          it->entry = skip_parens(di->variant.expression);
          it->flags |= CIF_UNUSED;
        } else if (di != NULL && di->master_entry != NULL) {
          /* One arm of a class ?: that initializes a variable: the value
             is built in the variable (the master entry's). */
          a_variable_ptr  mv = di->master_entry->variable;
          uint32_t        dest = 0;
          a_boolean       have_dest = FALSE;

          if (mv != NULL && variable_is_automatic(mv)) {
            dest = ecil_lid_of(mv);
            have_dest = dest != 0;
          } else if (mv != NULL && ecil_gid_of(mv) != 0) {
            dest = DEST_GLOBAL_FLAG | ecil_gid_of(mv);  /* A static. */
            have_dest = TRUE;
          } else if (mv == NULL) {
            /* A temporary (or the return slot). */
            have_dest = master_dest_of(di->master_entry, &dest) &&
                        dest != DEST_ON_STACK;
          }  /* if */
          if (!have_dest || t == 0) {
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
          emit_fullexpr_begin(frame);
          it->phase = 1;
          push_expr(child, it->flags & ~CIF_DROP);
        } else {
          if ((it->flags & CIF_DROP) != 0 && value_is_pushed(child, TRUE)) {
            ecil_rec0(frame, eop_drop);
          }  /* if */
          emit_fullexpr_end(frame);
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
      if (is_pm_builtin(node)) {
        do_pm_builtin(frame, idx);
      } else if (node->variant.builtin_operation.kind ==
                                  (a_builtin_operation_kind)bok_offsetof &&
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


static a_boolean entry_has_dynamic_part(an_init_level  *level,
                                        a_constant_ptr value)
/*
Return TRUE if the entry of the level just returned by ecil_init_level_next
(the constant "value", or the merged sources of the level) is built by code
rather than only by the VAR_INIT of a partly constant aggregate: a dynamic
element, a repeat of one, an aggregate with a dynamic part (also repeated),
or merged sources one of which has a dynamic part.  The one predicate that
decides which elements a STATIC_INIT with the PARTIAL flag skips.
*/
{
  a_constant_ptr  c = value;
  a_boolean       found;

  while (c->kind == (a_constant_repr_kind)ck_init_repeat) {
    c = c->variant.init_repeat.constant;
  }  /* while */
  found = c->kind == (a_constant_repr_kind)ck_dynamic_init ||
          constant_has_dynamic_part(c);
  if (!found && level->entry_sources != NULL) {
    found = merged_has_dynamic_part(level->entry_sources,
                                    level->entry_num_sources);
  }  /* if */
  return found;
}  /* entry_has_dynamic_part */


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


static a_boolean agg_needs_partial_cleanups(an_ecil_frame *frame,
                                            an_init_level *level);

static void begin_dynamic_aggregate(an_ecil_frame *frame, a_constant_ptr c,
                                    a_constant_ptr *sources, size_t n,
                                    a_boolean skip_constants)
/*
AGG_INIT_BEGIN for a nested dynamic aggregate (the target glvalue is on the
stack) and push its cursor.  It is initialized by c, or by the n merged
constants in sources.  With skip_constants the aggregate is part of the
constant of a variable whose VAR_INIT has the constant elements: they are
neither zero-filled nor stored (see an_init_level).
*/
{
  uint32_t      t = ecil_type_id(frame, c->type);
  unsigned int  flags = ((c->partial_aggr_value ||
                          c->is_partially_initialized) &&
                         !skip_constants) ? ECIL_AGG_INIT_ZERO_FILL : 0u;

  if (t != 0 && ecil_gid_info(t)->kind != egk_record &&
      ecil_gid_info(t)->kind != egk_array) {
    ecil_note_unsupported_type(t, "an aggregate initializer of an opaque "
                                  "type");
  } else if (t != 0 && push_agg_level(sources != NULL ? sources : &c,
                                      sources != NULL ? n : 1)) {
    agg_levels[num_agg_levels - 1].skip_constants = skip_constants;
    if (agg_needs_partial_cleanups(frame, &agg_levels[num_agg_levels - 1])) {
      /* The destruction of the elements already built (if a later one
         throws) finds the aggregate through a local with its address. */
      uint32_t  la = new_lid();

      ecil_rec0(frame, eop_dup);
      ecil_rec0(frame, eop_addr);
      emit_materialize_temp(frame, la, ecil_ptr_type(frame, t));
      ecil_rec0(frame, eop_drop);
      agg_levels[num_agg_levels - 1].addr_lid = la;
    }  /* if */
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
#define CTOR_CALL_VTT         0x100u    /* (it->flags) CALL VTT. */

static an_expr_node_ptr forwarded_parameter(an_expr_node_ptr arg)
/*
In an inheriting constructor, the argument of the call of the inherited
constructor that the front end builds for a by-value parameter of a class
that is not trivially copyable is a temporary that copies the parameter.  The
inherited constructor is called as if with the original arguments (C++17
[class.inhctor.init], applied to every mode by g++ and clang), so the
parameter itself is passed.  Return the node of the parameter, or NULL if the
argument is not such a copy.
*/
{
  an_expr_node_ptr  result = NULL;
  an_expr_node_ptr  node = skip_parens(arg);

  if (function_scope != NULL && !C_mode() &&
      function_scope->variant.routine.ptr->is_inheriting_ctor &&
      node->kind == (an_expr_node_kind)enk_temp_init &&
      node->variant.init.dynamic_init != NULL &&
      node->variant.init.dynamic_init->kind ==
                                  (a_dynamic_init_kind)dik_constructor &&
      node->variant.init.dynamic_init->variant.constructor.args != NULL &&
      node->variant.init.dynamic_init->variant.constructor.args->next ==
                                                                      NULL &&
      ecil_class_is_nontrivial_abi(node->type)) {
    an_expr_node_ptr  src = skip_parens(node->variant.init.dynamic_init->
                                        variant.constructor.args);

    while (src->kind == (an_expr_node_kind)enk_operation &&
           (src->variant.operation.kind == eok_reference_to ||
            src->variant.operation.kind == eok_lvalue_adjust ||
            src->variant.operation.kind == eok_ref_cast ||
            src->variant.operation.kind == eok_lvalue_cast ||
            src->variant.operation.kind == eok_class_rvalue_adjust ||
            src->variant.operation.kind == eok_parens)) {
      src = skip_parens(src->variant.operation.operands);
    }  /* while */
    if (src->kind == (an_expr_node_kind)enk_variable) {
      a_variable_ptr  param;

      for (param = function_scope->variant.routine.parameters;
           param != NULL && param != node_variable(src);
           param = param->next) {
      }  /* for */
      if (param != NULL && !is_reference_type(param->type)) {
        /* The temporary copies the parameter: it has its class (a copy of
           another object would not be the parameter forwarded). */
        check_assertion(identical_types_ignoring_qualifiers(node->type,
                                                           param->type));
        result = src;
      }  /* if */
    }  /* if */
  }  /* if */
  return result;
}  /* forwarded_parameter */


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

    an_expr_node_ptr  pushed = forwarded_parameter(arg);

    it->cursor = arg->next;
    it->b++;
    if (pushed == NULL) pushed = arg;
    if (is_glvalue_node(skip_parens(pushed)) &&
        ecil_class_is_nontrivial_abi(pushed->type)) {
      it->c |= CTOR_ARG_LOAD_PENDING;
    }  /* if */
    push_expr(pushed, 0);
  } else {
    emit_call_flags(frame, it->a, di->variant.constructor.ptr, it->b,
                    (it->flags & CTOR_CALL_VTT) != 0 ? ECIL_CALL_VTT : 0u);
    code_stack_size--;
  }  /* if */
}  /* do_ctor_call */


static a_boolean emit_ctor_loop(an_ecil_frame *frame, a_dynamic_init_ptr cdi,
                                uint32_t elem_gid, unsigned int flags,
                                a_type_ptr elem_type, a_routine_ptr dtor,
                                size_t self_idx)
/*
The loop that default-constructs the elements of an array (the glvalue of
the array, or the element pointer and the count, is on the stack):
ARRAY_CTOR_BEGIN, the partial destruction, the constructor call and
ARRAY_CTOR_END.  A call with arguments (default arguments, which may have
temporaries) keeps the element pointer in a local and is built by items: its
FULLEXPR and the ARRAY_CTOR_END follow it; when self_idx is that of the item
that asked, that item becomes the ARRAY_CTOR_END.  Return TRUE if items were
pushed (the caller emits no end).
*/
{
  uint32_t       gid = ecil_function_id(frame, cdi->variant.constructor.ptr);
  a_boolean      pushed = FALSE;

  ecil_rec2(frame, eop_array_ctor_begin, elem_gid, flags);
  emit_array_partial_dtor(frame, elem_type, dtor);
  if (cdi->variant.constructor.args == NULL) {
    emit_call_record(frame, gid, cdi->variant.constructor.ptr, 1);
  } else if (ecil_unsupported_reason == NULL) {
    uint32_t     lp = new_lid();
    a_boolean    wrap = lifetime_needs_cleanups(cdi->init_expr_lifetime);
    a_code_item  *item;

    /* The body consumes the element pointer: keep it in a local, so that the
       default arguments can have their own FULLEXPR around the call. */
    emit_materialize_temp(frame, lp, ecil_ptr_type(frame, elem_gid));
    ecil_rec0(frame, eop_drop);
    if (wrap) emit_fullexpr_begin(frame);
    ecil_rec1(frame, eop_lref, lp);
    ecil_rec1(frame, eop_load, 0);
    if (self_idx == NO_SELF) {
      item = push_item(cik_emit, NULL, 0);
      if (item != NULL) item->a = (uint32_t)eop_array_ctor_end;
    }  /* if */
    if (wrap) {
      item = push_item(cik_emit, NULL, 0);
      if (item != NULL) item->a = (uint32_t)eop_fullexpr_end;
    }  /* if */
    item = push_item(cik_ctor_call, NULL, 0);
    if (item != NULL) {
      setup_ctor_call(frame, item, cdi, ECIL_VARIANT_COMPLETE, 0);
    }  /* if */
    if (self_idx != NO_SELF) {
      code_stack[self_idx].kind = cik_emit;
      code_stack[self_idx].a = (uint32_t)eop_array_ctor_end;
    }  /* if */
    pushed = TRUE;
  }  /* if */
  return pushed;
}  /* emit_ctor_loop */


static int array_default_construct(an_ecil_frame     *frame,
                                     a_dynamic_init_ptr di,
                                     size_t             self_idx)
/*
An array of class objects that are all default-constructed (one repeated
constructor call over the whole array): ARRAY_CTOR_BEGIN(fixed) over the
array glvalue already on the stack, the constructor call on the element
pointer, ARRAY_CTOR_END (spec 11.8).  A constructor with default arguments is
built by items (emit_ctor_loop; self_idx is the item that asks).  Return 0 if
it does not apply, 1 if it was emitted, 2 if items were pushed.
*/
{
  int             handled = 0;
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
        rc->variant.dynamic_init.ptr->variant.constructor.ptr != NULL &&
        rep->variant.init_repeat.count == total) {
      a_dynamic_init_ptr  cdi = rc->variant.dynamic_init.ptr;
      uint32_t            elem = ecil_type_id(frame, t);

      if (ctor_needs_zeroing(cdi)) {
        ecil_note_unsupported("the value-initialization of an array of "
                              "objects");
      }  /* if */
      if (elem != 0 && ecil_unsupported_reason == NULL) {
        a_boolean  pushed = emit_ctor_loop(frame, cdi, elem,
                                           ECIL_ARRAY_LOOP_FIXED, t,
                                           di->destructor, self_idx);

        if (ecil_unsupported_reason == NULL && !pushed) {
          ecil_rec0(frame, eop_array_ctor_end);
        }  /* if */
        handled = pushed ? 2 : 1;
      }  /* if */
    }  /* if */
  }  /* if */
  return handled;
}  /* array_default_construct */


#define INIT_LOAD_FIRST 0x100u    /* it->c: LOAD the value before the STORE. */
#define INIT_EMPTY      0x200u    /* it->c: an empty class: nothing to copy. */

static a_boolean type_is_empty_class(an_ecil_frame *frame, a_type_ptr type)
/*
Return TRUE for a class without data (see gid_is_empty_class): copying one
does not read or write it.
*/
{
  uint32_t  gid = C_mode() ? 0 : ecil_type_id(frame, type);

  return gid_is_empty_class(gid) &&
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


static a_boolean constant_is_local_address(a_constant_ptr c)
/*
Return TRUE if the constant is the address of an automatic variable of the
function being translated (plus an offset): a reference bound to a base
subobject of a local, which the front end folds into an address constant.
*/
{
  a_boolean  result = FALSE;

  if (!C_mode() && c != NULL && c->kind == (a_constant_repr_kind)ck_address &&
      c->variant.address.kind == abk_variable) {
    a_variable_ptr  var = c->variant.address.variant.variable;

    result = var->source_corresp.is_local_to_function &&
             var->storage_class != (a_storage_class)sc_static &&
             var->storage_class != (a_storage_class)sc_extern &&
             ecil_lid_of(var) != 0;
  }  /* if */
  return result;
}  /* constant_is_local_address */


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
        if (constant_is_local_address(di->variant.constant.ptr)) {
          /* The address of an automatic variable (a reference bound to a
             base subobject of a local): not a constant, but the address of
             the local at the byte offset (spec 11.6). */
          a_constant_ptr  ac = di->variant.constant.ptr;
          uint32_t        pt = ecil_type_id(frame, ac->type);

          if (pt != 0 && ecil_unsupported_reason == NULL) {
            ecil_rec1(frame, eop_lref,
                      ecil_lid_of(ac->variant.address.variant.variable));
            ecil_rec0(frame, eop_addr);
            ecil_rec2(frame, eop_addr_offset, pt,
                      (uint64_t)ac->variant.address.offset);
            ecil_rec_begin(frame, eop_store);
            ecil_arg_uint(frame, ECIL_STORE_INIT);
            ecil_arg_uint(frame, (unsigned int)esr_none);
            ecil_rec_end(frame);
          }  /* if */
        } else if (di->variant.constant.ptr->kind ==
                       (a_constant_repr_kind)ck_ptr_to_member &&
                   di->variant.constant.ptr->variant.ptr_to_member.
                                                       is_function_ptr) {
          /* A pointer to a member function that may need a this adjustment:
             CONST_METHOD and a STORE (a C_METHOD with an adjustment is only
             valid in a VAR_INIT, spec 12.4). */
          if (ecil_emit_ptr_to_member_constant(frame,
                                               di->variant.constant.ptr,
                                               FALSE)) {
            ecil_rec_begin(frame, eop_store);
            ecil_arg_uint(frame, ECIL_STORE_INIT);
            ecil_arg_uint(frame, (unsigned int)esr_none);
            ecil_rec_end(frame);
          }  /* if */
        } else if (gid_is_empty_class(it->b)) {
          /* An empty class: its byte may belong to another object. */
          ecil_rec0(frame, eop_drop);
        } else if (ecil_emit_constant_typed(frame, di->variant.constant.ptr,
                                            it->b)) {
          ecil_rec0(frame, eop_init_const);
        }  /* if */
        code_stack_size--;
        break;
      case dik_expression:
        if (di->class_rvalue_initialized_through_master_entry) {
          /* A class ?: whose arms build the value in the variable (or in the
             temporary that this initializes). */
          note_master_dest(di, it->a);
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
        } else if (ecil_class_is_dynamic(di->variant.bitwise_copy.source->
                                                                      type)) {
          ecil_note_unsupported("a bitwise copy of a class with virtual "
                                "functions");
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
        if (di->kind == (a_dynamic_init_kind)dik_nonconstant_aggregate) {
          int  built = array_default_construct(frame, di, idx);

          if (built == 1) code_stack_size--;
          if (built != 0) break;
        }  /* if */
        it->kind = cik_agg;
        it->phase = 0;
        it->cursor = (void *)(uintptr_t)num_agg_levels;
        /* Only the variable's own STATIC_INIT (whose VAR_INIT has the
           constant elements) skips them; a temporary, a closure or a new
           is initialized completely. */
        begin_dynamic_aggregate(frame, di->variant.constant.ptr, NULL, 0,
                                static_init_var != NULL &&
                                (lid & DEST_GLOBAL_FLAG) != 0 &&
                                lid != DEST_ON_STACK &&
                                (lid & ~DEST_GLOBAL_FLAG) ==
                                         ecil_gid_of(static_init_var) &&
                                (ecil_gid_info(lid & ~DEST_GLOBAL_FLAG)->
                                       flags & ECIL_GF_PARTIAL_INIT) != 0);
        break;
      default:
        ecil_note_unsupported("this kind of initialization");
        break;
    }  /* switch */
  }  /* if */
}  /* do_init */


static a_lambda_capture_ptr capture_of(an_init_level *level, uint64_t index)
/*
Return the capture whose closure member is "index" of the lambda being
initialized, or NULL.
*/
{
  a_lambda_capture_ptr  cap = NULL;

  if (agg_lambda != NULL &&
      skip_typerefs(level->type) == skip_typerefs(agg_lambda->closure_class)) {
    for (cap = agg_lambda->capture_list; cap != NULL; cap = cap->next) {
      if (cap->closure_field != NULL &&
          ecil_gid_of(cap->closure_field) == index + 1) break;
    }  /* for */
  }  /* if */
  return cap;
}  /* capture_of */


static a_variable_ptr captured_variable_of(an_init_level *level,
                                           uint64_t      index)
/*
Return the variable that the closure member "index" of the lambda being
initialized copies (a simple capture by copy), or NULL.
*/
{
  a_lambda_capture_ptr  cap = capture_of(level, index);
  a_variable_ptr        cvar = NULL;

  if (cap != NULL && !cap->is_init_capture && !cap->is_indirect_init_capture &&
      cap->capture_info.source_closure_field == NULL) {
    cvar = cap->captured.variable;
  }  /* if */
  return cvar;
}  /* captured_variable_of */


static a_field_ptr captured_source_field_of(an_init_level *level,
                                            uint64_t      index)
/*
Return the member of the enclosing closure that the closure member "index" of
the lambda being initialized copies, when the captured variable is reachable
only because the enclosing lambda captured it (a capture in a nested lambda);
the member is read through the enclosing lambda's this.  NULL otherwise.
*/
{
  a_lambda_capture_ptr  cap = capture_of(level, index);
  a_field_ptr           field = NULL;

  if (cap != NULL && !cap->is_init_capture && !cap->is_indirect_init_capture &&
      !cap->field_pending && this_lid != 0) {
    field = cap->capture_info.source_closure_field;
  }  /* if */
  return field;
}  /* captured_source_field_of */


static a_boolean emit_source_field_glvalue(an_ecil_frame *frame,
                                           a_field_ptr   field)
/*
Push the glvalue of a member of the enclosing closure, read through this
(the lambda being translated is nested in that closure's lambda); if the
member is a reference, the glvalue of its referent.  FALSE (reason noted) if
the closure record cannot be named.
*/
{
  uint32_t  rec = 0;
  uint32_t  index = 0;
  a_boolean ok = field_index(frame, skip_typerefs(parent_class_of(field)),
                             field, &rec, &index);

  if (ok) {
    ecil_rec1(frame, eop_lref, this_lid);
    ecil_rec1(frame, eop_load, 0);
    ecil_rec0(frame, eop_deref);
    ecil_rec2(frame, eop_field, rec, index);
    if (is_reference_type(field->type)) {
      ecil_rec1(frame, eop_load, 0);
      ecil_rec0(frame, eop_deref);
    }  /* if */
  }  /* if */
  return ok;
}  /* emit_source_field_glvalue */


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


static a_type_ptr innermost_destructible_class(a_type_ptr type)
/*
The class whose destructor destroys an object (an element of an array of any
depth) of the type, or NULL if the type needs no destruction.
*/
{
  type = skip_typerefs(type);
  while (type->kind == tk_array) {
    type = skip_typerefs(type->variant.array.element_type);
  }  /* while */
  return ecil_class_has_nontrivial_dtor(type) ? type : NULL;
}  /* innermost_destructible_class */


static a_type_ptr agg_entry_type(an_ecil_frame *frame, an_init_level *level,
                                 uint64_t index)
/*
The front end's type of the member or element "index" of the aggregate of
level; NULL for a base class entry or when it cannot be found.
*/
{
  a_type_ptr  type = NULL;

  if (level->is_array) {
    type = skip_typerefs(level->type->variant.array.element_type);
  } else if (index >= level->lead) {
    (void)ecil_member_type_gid(frame, level, index);
    if (level->members != NULL && index - level->lead < level->num_members) {
      type = skip_typerefs(level->members[index - level->lead]->type);
    }  /* if */
  }  /* if */
  return type;
}  /* agg_entry_type */


static a_boolean agg_needs_partial_cleanups(an_ecil_frame *frame,
                                            an_init_level *level)
/*
Return TRUE if, with exceptions, some member or element of the aggregate has a
class type with a non-trivial destructor (it must be destroyed when a later
one throws).
*/
{
  a_boolean  needed = FALSE;

  if (ecil_has_exceptions() && !C_mode()) {
    if (level->is_array) {
      needed = innermost_destructible_class(
                      level->type->variant.array.element_type) != NULL;
    } else {
      a_field_ptr  f;

      for (f = level->type->variant.class_struct_union.field_list;
           f != NULL && !needed; f = f->next) {
        needed = innermost_destructible_class(f->type) != NULL;
      }  /* for */
    }  /* if */
  }  /* if */
  return needed;
}  /* agg_needs_partial_cleanups */


static void agg_note_initialized(an_ecil_frame *frame, an_init_level *level,
                                 uint64_t index, uint64_t count)
/*
The entry of the aggregate (count elements from "index") was initialized, by
a construction, a constant or an empty list: if its type has a destructor its
cleanup follows (at the next visit of the level, when the entry is complete).
*/
{
  a_type_ptr  et = agg_entry_type(frame, level, index);

  if (level->addr_lid != 0 && !level->is_union && et != NULL &&
      innermost_destructible_class(et) != NULL) {
    level->partial_pending = TRUE;
    level->partial_index = index;
    level->partial_count = count;
    level->partial_type = et;
  }  /* if */
}  /* agg_note_initialized */


static void agg_emit_partial_cleanup(an_ecil_frame *frame,
                                     an_init_level *level)
/*
CLEANUP_BEGIN(eh, DEACTIVATABLE) of each element that was just initialized:
its destructor (an array member's elements in an ARRAY_DTOR loop), on the
element found through the local that holds the aggregate's address (spec
12.6).
*/
{
  a_type_ptr     cls = innermost_destructible_class(level->partial_type);
  a_routine_ptr  dtor = cls != NULL ? class_destructor_of(cls) : NULL;
  uint32_t       g = dtor != NULL ? ecil_function_id(frame, dtor) : 0;
  uint32_t       agg_gid = ecil_type_id(frame, level->type);
  uint32_t       eg = ecil_type_id(frame, level->partial_type);
  a_boolean      elem_is_array = skip_typerefs(level->partial_type)->kind ==
                                 tk_array;
  uint64_t       count = level->partial_count == 0 ? 1 : level->partial_count;
  uint64_t       first = level->partial_index;
  uint64_t       k;

  level->partial_pending = FALSE;
  if (g == 0 || agg_gid == 0 || eg == 0) {
    ecil_note_unsupported("a partially constructed aggregate whose "
                          "destructor is not known");
  } else if (elem_is_array && level->is_array) {
    ecil_note_unsupported("a partially constructed array of arrays of "
                          "objects");
  }  /* if */
  for (k = 0; k < count && ecil_unsupported_reason == NULL; k++) {
    level->partial_index = first + k;
    ecil_rec_begin(frame, eop_cleanup_begin);
    ecil_arg_uint(frame, (unsigned int)ecl_eh);
    ecil_arg_nil(frame);
    ecil_arg_uint(frame, ECIL_CLEANUP_DEACTIVATABLE);
    ecil_rec_end(frame);
    ecil_rec1(frame, eop_lref, level->addr_lid);
    ecil_rec1(frame, eop_load, 0);
    ecil_rec0(frame, eop_deref);
    if (level->is_array) {
      uint32_t  sizet = ecil_target_uintptr_type(frame);

      ecil_rec0(frame, eop_addr);
      emit_cast(frame, (unsigned int)eck_array_to_ptrdecay,
                ecil_ptr_type(frame, eg));
      ecil_rec2(frame, eop_const_int, sizet, level->partial_index);
      ecil_rec1(frame, eop_ptr_stride,
                (unsigned int)ECIL_PTR_STRIDE_INDEX_UNSIGNED);
    } else {
      ecil_rec2(frame, eop_field, agg_gid,
                level->partial_index + level->vptr_shift);
      if (!elem_is_array) ecil_rec0(frame, eop_addr);
    }  /* if */
    if (elem_is_array) {
      ecil_rec2(frame, eop_array_dtor_begin,
                array_element_gid(frame, level->partial_type),
                ECIL_ARRAY_LOOP_FIXED);
      emit_call_record(frame, g, dtor, 1);
      ecil_rec0(frame, eop_array_dtor_end);
    } else {
      emit_call_record(frame, g, dtor, 1);
    }  /* if */
    ecil_rec0(frame, eop_cleanup_body);
    level->num_partial++;
  }  /* for */
  level->partial_index = first;
}  /* agg_emit_partial_cleanup */


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
    a_boolean       more;
    size_t          my_level = num_agg_levels - 1;
    uint64_t        entry_n = 1;
    a_boolean       initialized = FALSE;

    if (level->partial_pending) agg_emit_partial_cleanup(frame, level);
    more = ecil_init_level_next(level, &index, &value);
    if (more && !C_mode()) nua = agg_nua_member(frame, level, index);
    if (!more) {
      while (level->num_partial > 0) {
        /* The aggregate is complete: its elements are the program's. */
        ecil_rec0(frame, eop_cleanup_end);
        level->num_partial--;
      }  /* while */
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
        ecil_rec1(frame, eop_agg_sel, index + level->vptr_shift);
        emit_zero_data_size(frame, nua->type);
      } else {
        ecil_note_unsupported("an initialization of a [[no_unique_address]] "
                              "member other than by a constructor");
      }  /* if */
    } else if (level->skip_constants &&
               (level->entry_is_char ||
                !entry_has_dynamic_part(level, value))) {
      /* A constant element: the VAR_INIT has it. */
    } else if (level->entry_is_char) {
      ecil_rec1(frame, eop_agg_sel, index + level->vptr_shift);
      ecil_emit_string_char(frame, level, value, index);
      ecil_rec0(frame, eop_init_const);
    } else if (value->kind == (a_constant_repr_kind)ck_init_repeat) {
      uint64_t  count = level->entry_count;
      uint64_t  i;

      initialized = TRUE;
      entry_n = count;
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
          ecil_rec1(frame, eop_agg_sel, index + i + level->vptr_shift);
          ecil_rec0(frame, eop_addr);
          emit_call_record(frame, gid, rdi->variant.constructor.ptr, 1);
        }  /* for */
      } else if (rc->kind == (a_constant_repr_kind)ck_aggregate ||
                 count > 4096) {
        ecil_note_unsupported("a large or aggregate repeated initializer");
      } else {
        for (i = 0; i < count && ecil_unsupported_reason == NULL; i++) {
          ecil_rec1(frame, eop_agg_sel, index + i + level->vptr_shift);
          if (ecil_emit_constant_typed(frame,
                          value->variant.init_repeat.constant,
                          ecil_member_type_gid(frame, level, index))) {
            ecil_rec0(frame, eop_init_const);
          }  /* if */
        }  /* for */
      }  /* if */
    } else if (value->kind == (a_constant_repr_kind)ck_dynamic_init) {
      a_dynamic_init_ptr  di = value->variant.dynamic_init.ptr;

      initialized = TRUE;
      ecil_rec1(frame, eop_agg_sel, index + level->vptr_shift);
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
          emit_zero_data_size(frame, nua->type);
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
          } else if (captured_source_field_of(level, index) != NULL) {
            /* A variable of the enclosing lambda's closure: copy from the
               member of this. */
            a_field_ptr  sf = captured_source_field_of(level, index);

            if (emit_source_field_glvalue(frame, sf)) {
              if (skip_typerefs(sf->type)->kind == tk_pointer) {
                ecil_rec1(frame, eop_load, 0);    /* [*this]: the object. */
              } else {
                ecil_rec0(frame, eop_addr);
              }  /* if */
              extra = 1;
            }  /* if */
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
      } else if (di->kind == (a_dynamic_init_kind)dik_bitwise_copy &&
                 di->variant.bitwise_copy.source == NULL &&
                 captured_source_field_of(level, index) != NULL) {
        /* A trivially copyable object that the enclosing lambda captured:
           its value, from the member of this. */
        if (emit_source_field_glvalue(frame,
                           captured_source_field_of(level, index))) {
          ecil_rec1(frame, eop_load, 0);
          ecil_rec_begin(frame, eop_store);
          ecil_arg_uint(frame, ECIL_STORE_INIT);
          ecil_arg_uint(frame, (unsigned int)esr_none);
          ecil_rec_end(frame);
        }  /* if */
      } else if (di->kind == (a_dynamic_init_kind)dik_nonconstant_aggregate &&
                 di->variant.constant.ptr != NULL &&
                 di->variant.constant.ptr->kind ==
                                        (a_constant_repr_kind)ck_aggregate &&
                 is_immediate_class_type(
                    skip_typerefs(di->variant.constant.ptr->type))) {
        /* A nested aggregate (the element of an array of aggregates; not an
           array of arrays of objects: a capture of one lists its elements
           in a flat sequence). */
        begin_dynamic_aggregate(frame, di->variant.constant.ptr, NULL, 0,
                                FALSE);
      } else {
        ecil_note_unsupported("this kind of aggregate member initializer");
      }  /* if */
    } else if (constant_has_dynamic_part(value) ||
               (level->entry_sources != NULL &&
                merged_has_dynamic_part(level->entry_sources,
                                        level->entry_num_sources))) {
      a_constant_ptr  *srcs = level->entry_sources;
      size_t          num_srcs = level->entry_num_sources;

      initialized = TRUE;
      ecil_rec1(frame, eop_agg_sel, index + level->vptr_shift);
      begin_dynamic_aggregate(frame, value, srcs, num_srcs,
                              level->skip_constants);
    } else {
      initialized = TRUE;
      ecil_rec1(frame, eop_agg_sel, index + level->vptr_shift);
      if (ecil_emit_constant_sources(frame, value, level->entry_sources,
                           level->entry_num_sources,
                           ecil_member_type_gid(frame, level, index))) {
        ecil_rec0(frame, eop_init_const);
      }  /* if */
    }  /* if */
    if (initialized && ecil_unsupported_reason == NULL &&
        my_level < num_agg_levels) {
      agg_note_initialized(frame, &agg_levels[my_level], index, entry_n);
    }  /* if */
  }  /* if */
}  /* do_agg */


/* Constructor bodies: member initializers. */

static void emit_this_field(an_ecil_frame *frame,
                            uint32_t      lid,
                            a_field_ptr   field)
/*
Push the glvalue of a data member of the object pointer variable lid (this
or a copy constructor's parameter) points to, in the class being constructed:
a member of an anonymous union or struct is selected through the unnamed
members that hold it.
*/
{
  a_field_ptr   path[MAX_ANON_DEPTH];
  a_type_ptr    owners[MAX_ANON_DEPTH];
  unsigned int  depth = 0;
  a_boolean     ok = TRUE;
  uint32_t      rec;
  uint32_t      index;

  ecil_rec1(frame, eop_lref, lid);
  ecil_rec1(frame, eop_load, 0);
  ecil_rec0(frame, eop_deref);
  if (parent_class_of(field) != function_class) {
    ok = anonymous_member_path(function_class, field, path, owners, &depth);
  }  /* if */
  while (ok && depth > 0) {
    uint32_t  orec = ecil_type_id(frame, owners[depth - 1]);

    depth--;
    if (orec == 0 || ecil_gid_of(path[depth]) == 0) {
      ecil_note_unsupported("a member of an incomplete type");
      ok = FALSE;
    } else {
      ecil_rec2(frame, eop_field, orec, ecil_gid_of(path[depth]) - 1);
    }  /* if */
  }  /* while */
  if (ok && field_index(frame, skip_typerefs(parent_class_of(field)), field,
                        &rec, &index)) {
    ecil_rec2(frame, eop_field, rec, index);
  }  /* if */
}  /* emit_this_field */


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
  a_boolean     is_vbase = ci->kind ==
                           (a_constructor_init_kind)cik_virtual_base_class;
  a_boolean     is_base = ci->kind ==
                          (a_constructor_init_kind)cik_direct_base_class ||
                          is_vbase;
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
      emit_this_field(frame, this_lid, ci->variant.field);
      emit_zero_data_size(frame, type);
    } else {
      ecil_note_unsupported("a constant initialization of a "
                            "[[no_unique_address]] member");
    }  /* if */
  } else if (di->kind == (a_dynamic_init_kind)dik_constructor) {
    unsigned int  extra = 0;
    a_boolean     vtt = FALSE;

    if (is_delegation) {
      ecil_rec1(frame, eop_lref, this_lid);
      ecil_rec1(frame, eop_load, 0);
      if (walk_variant == ECIL_VARIANT_BASE) {
        /* C2 of a class with virtual bases delegates to C2 with its own VTT
           (spec 12.3). */
        emit_vtt_addr(frame, (unsigned int)evak_self, 0);
        vtt = TRUE;
      }  /* if */
    } else if (is_base) {
      emit_this_subobject(frame, this_lid, rec, bg, offset, is_vbase, index);
      if (ctor_needs_zeroing(di) &&
          !ci->variant.base_class->is_optimized_empty_base) {
        emit_zero_pointee(frame, type, TRUE);
      }  /* if */
      if (class_has_vbases(type)) {
        /* The base variant of a class with virtual bases takes the VTT of
           the class being constructed (spec 12.3). */
        emit_vtt_addr(frame, is_vbase ? (unsigned int)evak_vbase :
                                        (unsigned int)evak_base, index);
        vtt = TRUE;
      }  /* if */
      if (di->variant.constructor.is_copy_constructor_with_implied_source) {
        emit_this_subobject(frame, src_lid, rec, bg, offset, is_vbase, index);
        extra = 1;
      }  /* if */
    } else {
      emit_this_field(frame, this_lid, ci->variant.field);
      if (ctor_needs_zeroing(di)) {
        if (!ci->variant.field->has_no_unique_address_attribute) {
          ecil_rec0(frame, eop_dup);
          emit_zero_object(frame, tgid);
        } else if (!ci->variant.field->is_optimized_empty_class) {
          /* Only the data size: the tail padding may hold a later member. */
          ecil_rec0(frame, eop_dup);
          emit_zero_data_size(frame, type);
        }  /* if */
      }  /* if */
      ecil_rec0(frame, eop_addr);
      if (di->variant.constructor.is_copy_constructor_with_implied_source) {
        emit_this_field(frame, src_lid, ci->variant.field);
        ecil_rec0(frame, eop_addr);
        extra = 1;
      }  /* if */
    }  /* if */
    { a_code_item *call = push_item(cik_ctor_call, NULL, 0);

      if (call != NULL) {
        setup_ctor_call(frame, call, di,
                        (is_base || (is_delegation &&
                                     walk_variant == ECIL_VARIANT_BASE)) ?
                        ECIL_VARIANT_BASE : ECIL_VARIANT_COMPLETE, extra);
        if (vtt) call->flags |= CTOR_CALL_VTT;
      }  /* if */
    }
  } else if (di->kind == (a_dynamic_init_kind)dik_bitwise_copy &&
             di->variant.bitwise_copy.source == NULL && !is_delegation &&
             ecil_class_is_dynamic(type)) {
    ecil_note_unsupported("a bitwise copy of a class with virtual "
                          "functions");
  } else if (di->kind == (a_dynamic_init_kind)dik_bitwise_copy &&
             di->variant.bitwise_copy.source == NULL && !is_delegation) {
    /* The same member or base of the source object, copied bitwise. */
    if (is_base) {
      emit_this_subobject(frame, this_lid, rec, bg, offset, is_vbase, index);
      ecil_rec0(frame, eop_deref);
      emit_this_subobject(frame, src_lid, rec, bg, offset, is_vbase, index);
      ecil_rec0(frame, eop_deref);
    } else {
      emit_this_field(frame, this_lid, ci->variant.field);
      emit_this_field(frame, src_lid, ci->variant.field);
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
    /* Only the base's nvsize bytes are zeroed: its tail padding may hold
       members of the derived class (spec 12.7). */
    emit_this_subobject(frame, this_lid, rec, bg, offset, is_vbase, index);
    ecil_rec0(frame, eop_deref);
    ecil_rec0(frame, eop_init_zero_base);
  } else if (is_delegation && di->kind == (a_dynamic_init_kind)dik_zero) {
    /* The delegated constructor is trivial: its value initialization zeroes
       the object. */
    ecil_rec1(frame, eop_lref, this_lid);
    ecil_rec1(frame, eop_load, 0);
    ecil_rec0(frame, eop_deref);
    emit_zero_data_size(frame, function_class);
  } else if (!is_delegation) {
    /* A value, zero or aggregate initialization of the member. */
    if (is_base) {
      emit_this_subobject(frame, this_lid, rec, bg, offset, is_vbase, index);
      ecil_rec0(frame, eop_deref);
    } else {
      emit_this_field(frame, this_lid, ci->variant.field);
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


static a_boolean anon_member_holder(an_ecil_frame          *frame,
                                     a_constructor_init_ptr ci,
                                     uint32_t               *holder)
/*
Return TRUE if the entry initializes a member of an anonymous union or struct
of the class being constructed; *holder is the position of the outermost
unnamed member that holds it (what the MEMINIT group is numbered by).
*/
{
  a_boolean  anon = FALSE;

  if (ci != NULL && ci->kind == (a_constructor_init_kind)cik_field &&
      parent_class_of(ci->variant.field) != function_class) {
    a_field_ptr   path[MAX_ANON_DEPTH];
    a_type_ptr    owners[MAX_ANON_DEPTH];
    unsigned int  depth = 0;

    (void)frame;
    if (anonymous_member_path(function_class, ci->variant.field, path,
                              owners, &depth) && depth > 0 &&
        ecil_gid_of(path[depth - 1]) != 0) {
      *holder = ecil_gid_of(path[depth - 1]) - 1;
      anon = TRUE;
    }  /* if */
  }  /* if */
  return anon;
}  /* anon_member_holder */


static a_boolean merge_anon_meminit(an_ecil_frame          *frame,
                                    a_code_item            *it,
                                    a_constructor_init_ptr ci,
                                    uint32_t               rec)
/*
The entry ci (a member of an anonymous union or struct) is complete: if the
next entry initializes a member held by the same unnamed member, it joins the
same MEMINIT group (the groups are numbered by the REC_FIELD index, which
strictly increases).  Return TRUE if it was started (the item stays in phase
1).
*/
{
  uint32_t                holder = 0;
  uint32_t                next_holder = 0;
  a_constructor_init_ptr  n = ci->next;
  a_boolean               merged = FALSE;

  if (anon_member_holder(frame, ci, &holder) &&
      anon_member_holder(frame, n, &next_holder) && holder == next_holder &&
      ecil_unsupported_reason == NULL) {
    a_dynamic_init_ptr  ndi = n->use_field_initializer ?
                              n->variant.field->initializer : n->initializer;

    if (ndi != NULL && ndi->destructor != NULL) {
      ecil_note_unsupported("the initializer of a member of an anonymous "
                            "union or struct that has a destructor");
    } else {
      it->entry = n;
      it->phase = 1;
      in_field_initializer = n->use_field_initializer;
      if (ndi != NULL && lifetime_needs_cleanups(ndi->init_expr_lifetime)) {
        emit_fullexpr_begin(frame);
        it->c |= 1u;
      }  /* if */
      meminit_first_operands(frame, it, n, ndi, rec);
      merged = TRUE;
    }  /* if */
  }  /* if */
  return merged;
}  /* merge_anon_meminit */


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

  if (ci != NULL &&
      ci->kind == (a_constructor_init_kind)cik_virtual_base_class &&
      walk_variant != ECIL_VARIANT_COMPLETE) {
    /* Only the complete variant constructs the virtual bases (spec 12.3). */
    it->entry = ci->next;
    it->phase = 0;
  } else if (ci == NULL) {
    if (vptr_init_pending) {
      /* No member initializer: after the bases (spec 12.4). */
      vptr_init_pending = FALSE;
      emit_vptr_init(frame);
    }  /* if */
    code_stack_size--;
  } else {
    a_boolean         is_delegation = ci->kind ==
                                      (a_constructor_init_kind)cik_delegation;
    a_boolean         is_vbase = ci->kind ==
                              (a_constructor_init_kind)cik_virtual_base_class;
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
      if ((it->c & 1u) != 0) emit_fullexpr_end(frame);
      it->c = 0;
      if (merge_anon_meminit(frame, it, ci, rec)) return;
      ecil_rec0(frame, eop_meminit_end);
      if (di != NULL && di->destructor != NULL) {
        a_destroy_target  tgt;

        memset(&tgt, 0, sizeof(tgt));
        tgt.lid = this_lid;
        tgt.rec = rec;
        if (is_delegation) {
          tgt.kind = DT_THIS;
        } else if (is_base || is_vbase) {
          a_base_class_ptr  base = ci->variant.base_class;

          tgt.kind = is_vbase ? DT_VBASE : DT_BASE;
          tgt.base_gid = ecil_type_id(frame, base->type);
          tgt.offset = (uint64_t)base->offset;
          tgt.index = ecil_gid_of(base) - 1;
          tgt.base_type = base->type;
        } else {
          uint32_t  r;

          tgt.kind = DT_MEMBER;
          (void)field_index(frame, function_class, ci->variant.field, &r,
                            &tgt.index);
          tgt.array_elem = array_element_gid(frame,
                                             ci->variant.field->type);
        }  /* if */
        emit_cleanup(frame, (unsigned int)ecl_eh, &tgt, di->destructor,
                     (is_base || is_vbase ||
                      (is_delegation && walk_variant == ECIL_VARIANT_BASE)) ?
                     ECIL_VARIANT_BASE : ECIL_VARIANT_COMPLETE);
      }  /* if */
    } else if (!is_base && !is_vbase && !is_delegation &&
               parent_class_of(ci->variant.field) != function_class &&
               di != NULL && di->destructor != NULL) {
      /* Its cleanup would have to follow a group that holds several. */
      ecil_note_unsupported("the initializer of a member of an anonymous "
                            "union or struct that has a destructor");
    } else {
      unsigned int  kind = is_delegation ? (unsigned int)emk_delegation :
                           is_vbase ? (unsigned int)emk_vbase :
                           is_base ? (unsigned int)emk_base :
                                     (unsigned int)emk_field;
      unsigned int  flags = 0;
      uint32_t      index = 0;

      if (kind == (unsigned int)emk_field && vptr_init_pending) {
        /* After the base groups and their cleanups, before the first
           member (spec 12.4). */
        vptr_init_pending = FALSE;
        emit_vptr_init(frame);
      }  /* if */

      if (is_base || is_vbase) {
        index = ecil_gid_of(ci->variant.base_class) - 1;
      } else if (!is_delegation && anon_member_holder(frame, ci, &index)) {
        /* The group of the member that holds it. */
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
          emit_fullexpr_begin(frame);
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
        tgt.index = ecil_gid_of(base) - 1;
        tgt.base_type = base->type;
        tgt.base_gid = ecil_type_id(frame, base->type);
        tgt.offset = (uint64_t)base->offset;
        emit_cleanup(frame, (unsigned int)ecl_all, &tgt, di->destructor,
                     ECIL_VARIANT_BASE);
      } else {
        /* A virtual base: D2 does not destroy it (D1 does, spec 12.3). */
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
    if ((it->c & 1u) != 0) emit_fullexpr_end(frame);
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
    if ((ecil_gid_info(gid)->flags & ECIL_GF_PARTIAL_INIT) != 0) {
      flags |= ECIL_STATIC_INIT_PARTIAL;
    }  /* if */
    ecil_rec2(frame, eop_static_init_begin, gid, flags);
    static_init_var = var;
    static_temp_count = 0;
    it->phase = 1;
    if (!constant) {
      if (lifetime_needs_cleanups(di->init_expr_lifetime)) {
        emit_fullexpr_begin(frame);
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
    if ((it->c & 1u) != 0) emit_fullexpr_end(frame);
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

    if (lid == 0 && variable_is_automatic(var) && !C_mode() &&
        var->compiler_generated) {
      /* A local that the front end synthesizes (the result variable of a
         defaulted <=>, say) has an initialization but no declaration
         statement: declare it here, in the block that it is initialized
         in.  (A variable that the source declared always has its
         declaration statement first.) */
      declare_variable(frame, var);
      lid = ecil_lid_of(var);
    }  /* if */
    if (lid == 0) {
      ecil_note_unsupported("the initialization of an undeclared local "
                            "variable");
    } else {
      it->phase = 1;
      it->b = (uint32_t)num_pending_cleanups;
      if (lifetime_needs_cleanups(di->init_expr_lifetime)) {
        emit_fullexpr_begin(frame);
        it->c |= 1u;
      }  /* if */
      { a_code_item *init = push_item(cik_init, di, 0);

        if (init != NULL) init->a = lid;
      }
    }  /* if */
  }  /* if */
}  /* do_var_init */


static a_boolean push_label_search(size_t        *size,
                                   a_statement_ptr stmt,
                                   a_boolean       siblings)
/*
Push a statement on the stack of statement_may_have_label; FALSE if memory
ran out.
*/
{
  a_boolean  ok = TRUE;

  if (stmt != NULL) {
    if (*size == label_stack_capacity) {
      size_t               new_capacity = label_stack_capacity == 0 ? 64 :
                                          label_stack_capacity * 2;
      a_label_search_item  *new_stack = (a_label_search_item *)realloc(
                     label_stack, new_capacity * sizeof(a_label_search_item));

      if (new_stack == NULL) {
        ok = FALSE;
      } else {
        label_stack = new_stack;
        label_stack_capacity = new_capacity;
      }  /* if */
    }  /* if */
    if (ok) {
      label_stack[*size].stmt = stmt;
      label_stack[*size].siblings = siblings;
      (*size)++;
    }  /* if */
  }  /* if */
  return ok;
}  /* push_label_search */


static a_boolean statement_may_have_label(a_statement_ptr start,
                                          a_boolean       siblings)
/*
Return TRUE if the statement (and, with siblings, the statements after it in
its list) contains a label that a goto can reach or a case or default label,
or a statement that this search does not know the structure of.  Folding a
constant if drops the arm that is not taken only if this is FALSE.  The walk
uses an explicit stack that is kept between the calls.
*/
{
  size_t     size = 0;
  a_boolean  found = FALSE;

  found = !push_label_search(&size, start, siblings);
  while (!found && size > 0) {
    a_label_search_item  item = label_stack[--size];
    a_statement_ptr      st = item.stmt;

    if (item.siblings && st->next != NULL) {
      found = !push_label_search(&size, st->next, TRUE);
    }  /* if */
    switch (st->kind) {
      case stmk_label:
      case stmk_switch_case:
      case stmk_assigned_goto:
      case stmk_coroutine:
      case stmk_coroutine_return:
      case stmk_if_consteval:
      case stmk_if_not_consteval:
#if MICROSOFT_EXTENSIONS_ALLOWED
      case stmk_microsoft_try:
      case stmk_for_each:
#endif /* MICROSOFT_EXTENSIONS_ALLOWED */
        found = TRUE;
        break;
      case stmk_block:
        found = found || !push_label_search(&size,
                                    st->variant.block.statements, TRUE);
        break;
      case stmk_if:
        found = found ||
                !push_label_search(&size, st->variant.if_stmt.then_statement,
                                   FALSE) ||
                !push_label_search(&size, st->variant.if_stmt.else_statement,
                                   FALSE);
        break;
      case stmk_try_block:
        { a_handler_ptr  h;

          found = found ||
                  !push_label_search(&size, st->variant.try_block->statement,
                                     FALSE);
          for (h = st->variant.try_block->handlers; h != NULL && !found;
               h = h->next) {
            found = !push_label_search(&size, h->statement, FALSE);
          }  /* for */
        }
        break;
      case stmk_constexpr_if:
        found = found ||
                !push_label_search(&size, st->variant.constexpr_if->
                                                         then_statement,
                                   FALSE) ||
                !push_label_search(&size, st->variant.constexpr_if->
                                                         else_statement,
                                   FALSE);
        break;
      case stmk_while:
      case stmk_end_test_while:
        found = found || !push_label_search(&size,
                                            st->variant.loop_statement,
                                            FALSE);
        break;
      case stmk_for:
        found = found ||
                !push_label_search(&size, st->variant.for_loop.statement,
                                   FALSE) ||
                (st->variant.for_loop.extra_info != NULL &&
                 !push_label_search(&size, st->variant.for_loop.extra_info->
                                                            initialization,
                                    FALSE));
        break;
      case stmk_range_based_for:
        found = found ||
                !push_label_search(&size,
                                   st->variant.range_based_for_loop.statement,
                                   FALSE);
        break;
      case stmk_switch:
        found = found ||
                !push_label_search(&size, st->variant.switch_stmt.
                                                         body_statement,
                                   FALSE);
        break;
      case stmk_expr:
      case stmk_goto:
      case stmk_return:
      case stmk_asm:
      case stmk_asm_func_body:
      case stmk_decl:
      case stmk_init:
      case stmk_set_vla_size:
      case stmk_vla_decl:
      case stmk_empty:
      case stmk_stmt_expr_result:
        /* No substatements (expressions, declarations, initializations,
           jumps, returns, asm, empty). */
        break;
      default:
        /* A statement kind that this search does not know the structure of
           (see the cases above that may have a label): assume it does. */
        found = TRUE;
        break;
    }  /* switch */
  }  /* while */
  return found;
}  /* statement_may_have_label */


static a_boolean condition_is_constant(an_expr_node_ptr cond,
                                       a_boolean       *value)
/*
Return TRUE if the condition of an if is an integral constant, as the front
end folded it; its truth is in *value.
*/
{
  a_boolean  known = FALSE;

  if (cond != NULL) {
    an_expr_node_ptr  node = skip_parens(cond);

    if (node->kind == (an_expr_node_kind)enk_constant &&
        node_constant(node)->kind == (a_constant_repr_kind)ck_integer) {
      a_boolean  overflow = FALSE;
      a_targ_ptrdiff_t  v = 0;

      v = value_of_integer_constant(node_constant(node), &overflow);
      if (!overflow) {
        *value = (v != 0);
        known = TRUE;
      }  /* if */
    }  /* if */
  }  /* if */
  return known;
}  /* condition_is_constant */


static unsigned int catch_param_kind(an_ecil_frame   *frame,
                                     a_handler_ptr    h,
                                     uint32_t         *copy_gid)
/*
The kind of CATCH_PARAM of a handler's parameter (spec 12.5), chosen from its
type: a reference to a pointer to a record 7, a reference to another pointer
6, any other reference 0, a pointer 1, a scalar (arithmetic, enum, bool,
nullptr_t, a pointer to a member) 2, a class initialized bitwise 4 or by a
copy constructor 5 (*copy_gid its C1).  0xFF if it cannot be expressed.
*/
{
  a_type_ptr    t = skip_typerefs(h->parameter->type);
  unsigned int  kind = 0xFFu;

  *copy_gid = 0;
  if (t->kind == tk_pointer && is_reference_type(t)) {
    a_type_ptr  referent = skip_typerefs(type_pointed_to(t));

    if (referent->kind == tk_pointer && !is_nullptr_type(referent) &&
        !is_reference_type(referent)) {
      a_type_ptr  pointee = skip_typerefs(referent->variant.pointer.type);

      kind = is_immediate_class_type(pointee) ?
             (unsigned int)ecat_reference_to_record_pointer :
             (unsigned int)ecat_reference_to_pointer;
    } else {
      kind = (unsigned int)ecat_reference;
    }  /* if */
  } else if (t->kind == tk_pointer && !is_nullptr_type(t)) {
    kind = (unsigned int)ecat_pointer;
  } else if (is_immediate_class_type(t)) {
    a_dynamic_init_ptr  di = h->dynamic_init;

    if (di != NULL && di->kind == (a_dynamic_init_kind)dik_constructor &&
        di->variant.constructor.ptr != NULL) {
      if (di->variant.constructor.args != NULL ||
          lifetime_needs_cleanups(di->init_expr_lifetime)) {
        ecil_note_unsupported("a catch parameter copy with default "
                              "arguments or temporaries");
      } else {
        *copy_gid = ecil_function_id(frame, di->variant.constructor.ptr);
        kind = (unsigned int)ecat_non_trivial_copy;
      }  /* if */
    } else {
      kind = (unsigned int)ecat_trivial_copy;
    }  /* if */
  } else if (t->kind == tk_integer || t->kind == tk_float ||
             t->kind == tk_ptr_to_member || is_nullptr_type(t) ||
             is_enum_type(t)) {
    kind = (unsigned int)ecat_scalar;
  }  /* if */
  return kind;
}  /* catch_param_kind */


static void do_try_block(an_ecil_frame *frame, size_t idx)
/*
stmk_try_block: TRY_BEGIN, the body, then each handler as CATCH_BEGIN(typeinfo
or nil), LOCAL and CATCH_PARAM for its parameter (an unnamed one too), the
cleanup of a destructible parameter (inside the handler), the handler's block,
and TRY_END (spec 12.5).  The try block of a constructor's or destructor's
function-try-block was begun with the header (function_try_open): the
initializers and the destruction prologue are inside it.
*/
{
  a_code_item         *it = &code_stack[idx];
  a_statement_ptr     s = (a_statement_ptr)it->entry;
  a_try_supplement_ptr  tb = s->variant.try_block;

  if (!ecil_has_exceptions()) {
    ecil_note_unsupported("a try statement");
    code_stack_size--;
  } else if (it->phase == 0) {
    if (function_try_open && function_scope != NULL &&
        s == function_scope->assoc_block) {
      function_try_open = FALSE;       /* Begun with the header. */
    } else {
      ecil_rec1(frame, eop_try_begin, 0);
    }  /* if */
    it->phase = 1;
    it->cursor = tb->handlers;
    push_stmt(tb->statement);
  } else if (it->cursor == NULL) {
    ecil_rec0(frame, eop_try_end);
    code_stack_size--;
  } else {
    a_handler_ptr  h = (a_handler_ptr)it->cursor;
    uint32_t       gti = 0;

    it->cursor = h->next;
    if (h->parameter != NULL) {
      gti = ecil_typeinfo_id(frame, h->parameter->type);
    }  /* if */
    if (ecil_unsupported_reason == NULL) {
      ecil_rec_begin(frame, eop_catch_begin);
      if (h->parameter != NULL) {
        ecil_arg_uint(frame, gti);
      } else {
        ecil_arg_nil(frame);
      }  /* if */
      ecil_rec_end(frame);
    }  /* if */
    if (ecil_unsupported_reason == NULL && h->parameter != NULL) {
      uint32_t      copy = 0;
      unsigned int  kind = catch_param_kind(frame, h, &copy);

      if (ecil_unsupported_reason == NULL && kind == 0xFFu) {
        ecil_note_unsupported("a catch parameter of this type");
      }  /* if */
      if (ecil_unsupported_reason == NULL) {
        declare_variable(frame, h->parameter);
        if (ecil_lid_of(h->parameter) == 0) {
          ecil_note_unsupported("a catch parameter that is not a local");
        }  /* if */
      }  /* if */
      if (ecil_unsupported_reason == NULL) {
        ecil_rec_begin(frame, eop_catch_param);
        ecil_arg_uint(frame, ecil_lid_of(h->parameter));
        ecil_arg_uint(frame, kind);
        if (copy != 0) {
          ecil_arg_uint(frame, copy);
        } else {
          ecil_arg_nil(frame);
        }  /* if */
        ecil_rec_end(frame);
        if (h->dynamic_init != NULL && h->dynamic_init->destructor != NULL) {
          emit_local_cleanup(frame, ecil_lid_of(h->parameter),
                             h->parameter->type,
                             h->dynamic_init->destructor);
        }  /* if */
      }  /* if */
    }  /* if */
    if (ecil_unsupported_reason == NULL) push_stmt(h->statement);
  }  /* if */
}  /* do_try_block */


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
        a_boolean                   folded_value = FALSE;

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
        } else if (it->phase == 0 &&
                   condition_is_constant(s->expr, &folded_value) &&
                   !statement_may_have_label(
                       folded_value ? s->variant.if_stmt.else_statement :
                                      s->variant.if_stmt.then_statement,
                       FALSE)) {
          /* An integral constant condition: only the arm that is taken is
             sent, in place of the if (the other arm has no label that a
             goto or a switch can reach).  The condition has no side
             effects. */
          a_statement_ptr  live = folded_value ?
                                  s->variant.if_stmt.then_statement :
                                  s->variant.if_stmt.else_statement;

          if (live != NULL) {
            it->entry = live;
            it->phase = 0;
            done = FALSE;
          }  /* if */
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

        if (it->phase == 0 || it->phase == FOR_OLD_INIT_DONE) {
          /* FOR_BEGIN opens the scope of the init declarations (ECIL 0.7):
             they follow it directly and stay in scope to FOR_END.  The
             front end wraps them in a block (in C there is no
             for_init_scope); the block's statements are sent without a
             BLOCK around them.  With the old (cfront and Microsoft) scope
             rule the variables of the init belong to the block that
             contains the for statement and are used after it, so the init
             is sent in that block, before FOR_BEGIN: the variables, their
             initializations and their cleanups end with the block, not
             with the loop.  The unbraced body of a case label or of an if
             is a scope region of its own. */
          a_boolean  old_scope = !C_mode() && loop->for_init_scope == NULL &&
                                 loop->initialization != NULL &&
                                 loop->initialization->kind ==
                                              (a_statement_kind)stmk_block;
          a_boolean  send_init = TRUE;

          if (it->phase == FOR_OLD_INIT_DONE) {
            ecil_rec0(frame, eop_for_begin);
            it->phase = 1;
            send_init = FALSE;
          } else if (old_scope) {
            it->phase = FOR_OLD_INIT_DONE;
          } else {
            ecil_rec0(frame, eop_for_begin);
            it->phase = 1;
          }  /* if */
          if (send_init && loop->initialization != NULL &&
              loop->initialization->kind == (a_statement_kind)stmk_block) {
            (void)push_item(cik_stmt_list,
                            loop->initialization->variant.block.statements,
                            0);
          } else if (send_init && loop->initialization != NULL) {
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
    case stmk_try_block:
      do_try_block(frame, idx);
      done = FALSE;                   /* do_try_block pops its own item. */
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
    case stmk_if_consteval:
    case stmk_if_not_consteval:
      /* Code that runs is not being evaluated as a constant expression (as
         for __builtin_is_constant_evaluated): "if consteval" takes its else
         arm, "if !consteval" its then arm.  The other arm must have no label
         that a goto or a switch can reach. */
      { a_boolean         negated = s->kind ==
                                    (a_statement_kind)stmk_if_not_consteval;
        a_statement_ptr   live = negated ? s->variant.if_stmt.then_statement :
                                           s->variant.if_stmt.else_statement;
        a_statement_ptr   dead = negated ? s->variant.if_stmt.else_statement :
                                           s->variant.if_stmt.then_statement;

        if (statement_may_have_label(dead, FALSE)) {
          ecil_note_unsupported("an if consteval whose constant-evaluated "
                                "arm has a label");
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
              emit_fullexpr_begin(frame);
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
              emit_fullexpr_begin(frame);
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
        if ((it->c & 1u) != 0) emit_fullexpr_end(frame);
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
  cond_depth = 0;
  fullexpr_depth = 0;
  num_new_cleanups = 0;
  suppress_load_node = NULL;
  load_suppressed = FALSE;
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
      case cik_emit:
        if (code_stack[idx].a == (uint32_t)eop_fullexpr_end) {
          emit_fullexpr_end(frame);
        } else {
          ecil_rec0(frame, code_stack[idx].a);
        }  /* if */
        code_stack_size--;
        break;
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
  cond_depth = 0;
  fullexpr_depth = 0;
  num_new_cleanups = 0;
  num_master_dests = 0;
  reset_agg_levels();
  /* No suppressed load outlives the code that set it (a construct that was
     diagnosed may have left one pending). */
  suppress_load_node = NULL;
  load_suppressed = FALSE;
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
    if (rout->special_kind == (a_special_function_kind)sfk_constructor ||
        rout->special_kind == (a_special_function_kind)sfk_destructor) {
      a_type_ptr  cls = function_class;

      if (cls == NULL || !is_immediate_class_type(cls)) {
        ecil_note_unsupported("a constructor or destructor without a class");
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


static a_boolean definition_must_exist(a_routine_ptr rout)
/*
Return TRUE for a function definition that no other translation unit can
supply and that cannot be discarded: external linkage and not shared with
other translation units (not inline, a template or an entity of the
translation unit's own).
*/
{
  a_boolean  result = ecil_linkage_for(rout->storage_class,
                                       rout->source_corresp.name_linkage,
                                       rout->is_weak) ==
                                                   (unsigned int)elk_external;

  if (C_mode()) {
    result = result && !rout->is_inline;
  } else {
#if BACK_END_TYPE == BACK_END_TYPE_CIR
    result = result && !routine_might_exist_in_multiple_copies(rout) &&
             !cir_entity_is_tu_local(iek_routine, rout);
#else /* BACK_END_TYPE != BACK_END_TYPE_CIR */
    result = FALSE;
#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */
  }  /* if */
  return result;
}  /* definition_must_exist */


static void emit_func_eh_spec(an_ecil_frame *frame, a_routine_ptr rout)
/*
FUNC_EH_SPEC of a definition (after the parameters, before any body record:
spec 12.4): noexcept for "noexcept", "noexcept(true)" and an instance with a
true argument (the implicit destructor and deallocation function with their
compiler-generated specification included), and for "throw()" in C++17 and
later; dynamic with the typeinfo of each listed type otherwise.  An
attribute's nothrow and a trivial member are NOTHROW only, with no terminate
scope.
*/
{
  if (ecil_has_exceptions()) {
    a_type_ptr                      t = skip_typerefs(rout->type);
    a_routine_type_supplement_ptr   rtsp = t->variant.routine.extra_info;
    an_exception_specification_ptr  esp;

    /* Resolves an indeterminate specification (a defaulted function's). */
    (void)is_non_throwing_routine(rout);
    esp = rtsp->exception_specification;
    if (esp != NULL && !esp->indeterminate && !esp->arg_cached &&
        !esp->throw_any && !esp->from_attribute) {
      if (esp->is_noexcept ||
          (cpp17_mode &&
           esp->variant.exception_specification_type_list == NULL)) {
        ecil_rec_begin(frame, eop_func_eh_spec);
        ecil_arg_uint(frame, (unsigned int)eesk_noexcept);
        ecil_arg_array(frame, NULL, 0);
        ecil_rec_end(frame);
      } else {
        an_exception_specification_type_ptr  e;
        uint32_t                             *words = NULL;
        size_t                               n = 0;
        size_t                               count = 0;

        for (e = esp->variant.exception_specification_type_list; e != NULL;
             e = e->next) {
          if (!e->redundant) count++;
        }  /* for */
        if (count != 0) {
          words = (uint32_t *)malloc(count * sizeof(uint32_t));
          if (words == NULL) {
            ecil_note_unsupported("out of memory in the ECIL writer");
          }  /* if */
        }  /* if */
        for (e = esp->variant.exception_specification_type_list;
             e != NULL && ecil_unsupported_reason == NULL &&
             (count == 0 || words != NULL); e = e->next) {
          if (e->is_pack_expansion) {
            ecil_note_unsupported("a dynamic exception specification with "
                                  "a pack expansion");
          } else if (!e->redundant) {
            uint32_t  ti = ecil_typeinfo_id(frame, e->type);

            if (ti != 0 && n < count) words[n++] = ti;
          }  /* if */
        }  /* for */
        if (ecil_unsupported_reason == NULL) {
          ecil_rec_begin(frame, eop_func_eh_spec);
          ecil_arg_uint(frame, (unsigned int)eesk_dynamic);
          ecil_arg_array(frame, words, n);
          ecil_rec_end(frame);
        }  /* if */
        free(words);
      }  /* if */
    }  /* if */
  }  /* if */
}  /* emit_func_eh_spec */


static void emit_synthesized_d1(an_ecil_frame *frame,
                                a_scope_ptr   scope,
                                a_routine_ptr rout,
                                uint32_t      gid)
/*
The complete destructor D1 of a class with virtual bases (spec 12.3): a body
the writer makes, as the front end has none for it.  A CLEANUP_BEGIN(all) for
each virtual base with a non-trivial destructor, in the order of C1's
MEMINIT(vbase) groups (so they run in reverse construction order: the front
end's destruction list walked from its end), each calling the base's D2 on
VBASE_CAST COMPLETE; then the call of D2 with VTT_ADDR(self).  No VPTR_INIT.
*/
{
  a_variable_ptr          this_var =
                                   scope->variant.routine.this_param_variable;
  uint32_t                this_type = this_var != NULL ?
                                      ecil_type_id(frame, this_var->type) : 0;
  a_constructor_init_ptr  ci;
  a_constructor_init_ptr  *entries = NULL;
  size_t                  count = 0;
  size_t                  i;

  function_scope = scope;
  function_class = NULL;
  if (this_var != NULL &&
      skip_typerefs(this_var->type)->kind == tk_pointer) {
    function_class = skip_typerefs(skip_typerefs(this_var->type)->
                                                         variant.pointer.type);
  }  /* if */
  next_lid = 0;
  this_lid = 0;
  num_pending_cleanups = 0;
  if (this_type == 0 || function_class == NULL) {
    ecil_note_unsupported("a destructor without a this parameter");
  } else {
    uint32_t  rec = ecil_type_id(frame, function_class);
    uint32_t  this_name = ecil_intern_cstring(frame, "this");

    this_lid = new_lid();
    ecil_rec2(frame, eop_func_begin, gid, 0);
    ecil_rec_begin(frame, eop_param);
    ecil_arg_uint(frame, this_lid);
    ecil_arg_uint(frame, this_type);
    ecil_arg_uint(frame, this_type);
    ecil_arg_uint(frame, this_name);
    ecil_arg_uint(frame, ECIL_PARAM_THIS);
    ecil_rec_end(frame);
    emit_func_eh_spec(frame, rout);
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

        ci = entries[i - 1];
        di = ci->initializer;
        if (ci->kind == (a_constructor_init_kind)cik_virtual_base_class &&
            di != NULL && di->destructor != NULL) {
          a_base_class_ptr  base = ci->variant.base_class;
          a_destroy_target  tgt;

          memset(&tgt, 0, sizeof(tgt));
          tgt.kind = DT_VBASE;
          tgt.lid = this_lid;
          tgt.rec = rec;
          tgt.index = ecil_gid_of(base) - 1;
          tgt.base_type = base->type;
          tgt.base_gid = ecil_type_id(frame, base->type);
          emit_cleanup(frame, (unsigned int)ecl_all, &tgt, di->destructor,
                       ECIL_VARIANT_BASE);
        }  /* if */
      }  /* for */
    }  /* if */
    free(entries);
    if (ecil_unsupported_reason == NULL) {
      ecil_rec1(frame, eop_lref, this_lid);
      ecil_rec1(frame, eop_load, 0);
      emit_vtt_addr(frame, (unsigned int)evak_self, 0);
      emit_call_flags(frame, ecil_gid_info(gid)->partner, rout, 1,
                      ECIL_CALL_VTT);
    }  /* if */
    if (ecil_unsupported_reason == NULL) {
#if CHECKING
      ecil_check_function_lids(next_lid);
#endif /* CHECKING */
      ecil_rec1(frame, eop_func_end, ECIL_FUNC_END_REACHABLE);
    }  /* if */
  }  /* if */
  ecil_end_lid_epoch(next_lid);
  this_lid = 0;
  num_pending_cleanups = 0;
}  /* emit_synthesized_d1 */


static void emit_function_walk(an_ecil_frame *frame,
                               a_scope_ptr   scope,
                               a_routine_ptr rout,
                               uint32_t      stream_gid,
                               unsigned int  variant)
/*
One walk of a function scope: FUNC_BEGIN of stream_gid, the PARAMs, the body
and FUNC_END (nothing if stream_gid is 0 or something is unsupported: the
caller drops the records).  A constructor of a class with virtual bases is
walked twice, once for each variant (variant: ECIL_VARIANT_COMPLETE for C1,
with the mem-initializers of the virtual bases; ECIL_VARIANT_BASE for C2); a
destructor's body is walked once, as D2 (spec 12.3); ECIL_VARIANT_NONE for any
other function.  The state of the walk is reset at its end.
*/
{
  walk_variant = variant;
  function_try_open = FALSE;
  static_init_var = NULL;
  static_temp_count = 0;
  agg_lambda = NULL;
  next_lid = 0;
  this_lid = 0;
  vptr_init_pending = FALSE;
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
  if (stream_gid != 0 && ecil_unsupported_reason == NULL) {
    a_variable_ptr  param;
    a_boolean       prototyped = rout_type_supp(
                                   skip_typerefs(rout->type))->prototyped;
    a_boolean       reachable = TRUE;

    check_definition_return_type(frame, rout);
    check_cxx_function(rout, scope);
    if (ecil_unsupported_reason == NULL) {
      ecil_rec2(frame, eop_func_begin, stream_gid, 0);
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
    if (ecil_unsupported_reason == NULL) emit_func_eh_spec(frame, rout);
    if (ecil_unsupported_reason == NULL && ecil_has_exceptions() &&
        scope->assoc_block != NULL &&
        scope->assoc_block->kind == (a_statement_kind)stmk_try_block &&
        scope->assoc_block->variant.try_block->is_function_try_block &&
        (rout->special_kind == (a_special_function_kind)sfk_constructor ||
         rout->special_kind == (a_special_function_kind)sfk_destructor)) {
      /* The initializers and a destructor's cleanup prologue are inside the
         function-try-block (spec 12.5). */
      ecil_rec1(frame, eop_try_begin, ECIL_TRY_FUNCTION_TRY);
      function_try_open = TRUE;
    }  /* if */
    if (scope->assoc_block != NULL &&
        scope->assoc_block->kind == (a_statement_kind)stmk_block &&
        scope->assoc_block->variant.block.extra_info != NULL) {
      reachable = scope->assoc_block->variant.block.extra_info->
                                                    end_of_block_reachable;
    }  /* if */
    if (ecil_unsupported_reason == NULL && !C_mode() &&
        rout->special_kind == (a_special_function_kind)sfk_destructor) {
      emit_dtor_prologue(frame, scope);
      if (ecil_unsupported_reason == NULL &&
          ecil_class_is_dynamic(function_class)) {
        /* After the cleanup prologue (spec 12.4). */
        emit_vptr_init(frame);
      }  /* if */
    }  /* if */
    if (ecil_unsupported_reason == NULL && !C_mode() &&
        rout->special_kind == (a_special_function_kind)sfk_constructor &&
        ecil_class_is_dynamic(function_class) &&
        !(scope->variant.routine.constructor_inits != NULL &&
          scope->variant.routine.constructor_inits->kind ==
                              (a_constructor_init_kind)cik_delegation)) {
      /* The initializers of the bases come first, then VPTR_INIT, then
         those of the members (do_meminit); with no initializer at all,
         now. */
      if (scope->variant.routine.constructor_inits == NULL) {
        emit_vptr_init(frame);
      } else {
        vptr_init_pending = TRUE;
      }  /* if */
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
  walk_variant = ECIL_VARIANT_NONE;
  function_try_open = FALSE;
  vptr_init_pending = FALSE;
  code_stack_size = 0;
  reset_agg_levels();
  static_init_var = NULL;
  static_temp_count = 0;
  agg_lambda = NULL;
  num_pending_cleanups = 0;
  in_field_initializer = FALSE;
}  /* emit_function_walk */


static void emit_function_rest(an_ecil_frame *frame,
                               a_scope_ptr   scope,
                               uint32_t      gid)
/*
The rest of emit_function, for a routine whose G-id (0 if it has none) is
introduced: re-issue its DECL_FUNC if the definition's facts differ, then
FUNC_BEGIN, PARAMs, the body and FUNC_END.  If the body uses a construct v0
cannot express, discard the body records (spec 8.11: the declaration section
is kept), re-issue the declaration as unsupported and warn.
*/
{
  a_routine_ptr rout = scope->variant.routine.ptr;
  an_ecil_mark  mark;
  size_t        use_mark;
  a_boolean     dropped = FALSE;
  a_boolean     vbases = FALSE;
  uint32_t      walk_gid;

  if (gid != 0 && ecil_unsupported_reason == NULL) {
    ecil_gid_info(gid)->entity = rout;
    if (ecil_gid_info(gid)->partner != 0) {
      ecil_gid_info(ecil_gid_info(gid)->partner)->entity = rout;
    }  /* if */
    ecil_reissue_function(frame, gid, TRUE, FALSE);
  }  /* if */
  if (!C_mode() && rout->is_virtual && gid != 0 &&
      ecil_unsupported_reason == NULL &&
      is_immediate_class_type(parent_class_of(rout))) {
    /* The definition of a virtual function of a dynamic class needs the facts
       of its class, so that the library emits the thunks to it (another
       translation unit's vtable refers to them).  The vtable is not used.  A
       failure does not drop the function. */
    an_ecil_saved_reason  saved;

    ecil_save_reason(&saved);
    ecil_class_vtable_facts(frame, parent_class_of(rout));
    ecil_unsupported_reason = NULL;
    ecil_restore_reason(&saved);
  }  /* if */
  ecil_frame_begin_body(frame);
  mark = ecil_frame_mark(frame);
  use_mark = ecil_vtable_use_mark();
  num_static_temps = 0;
  vbases = gid != 0 && (ecil_gid_info(gid)->flags &
                        ECIL_GF_STRUCTOR_VBASES) != 0 &&
           ecil_unsupported_reason == NULL;
  walk_gid = gid != 0 ? (vbases && rout->special_kind ==
                                  (a_special_function_kind)sfk_constructor ?
                         gid : ecil_function_body_id(gid)) : 0;
  emit_function_walk(frame, scope, rout, walk_gid,
                     !vbases ? ECIL_VARIANT_NONE :
                     rout->special_kind ==
                          (a_special_function_kind)sfk_constructor ?
                     ECIL_VARIANT_COMPLETE : ECIL_VARIANT_BASE);
  if (vbases && ecil_unsupported_reason == NULL && walk_gid != 0) {
    if (rout->special_kind == (a_special_function_kind)sfk_constructor) {
      /* The base variant: the same walk without the virtual bases. */
      uint32_t  before;

      ecil_end_lid_epoch(next_lid);
      before = ecil_num_gids();
      emit_function_walk(frame, scope, rout, ecil_gid_info(gid)->partner,
                         ECIL_VARIANT_BASE);
#if CHECKING
      /* Everything the second walk needs was introduced by the first. */
      check_assertion(ecil_unsupported_reason != NULL ||
                      ecil_num_gids() == before);
#endif /* CHECKING */
      (void)before;
    } else {
      /* The complete destructor is synthesized from the front end's list. */
      emit_synthesized_d1(frame, scope, rout, gid);
    }  /* if */
  }  /* if */
  /* The static locals met on the way were introduced (DECL_VAR) where they
     were first used; their initializers are built now, whether or not the
     body survives.  A failure among them makes the function unsupported. */
  ecil_drain_variable_definitions(frame);
  if (ecil_unsupported_reason != NULL) {
    char  message[ECIL_DIAGNOSTIC_MAX];

    dropped = TRUE;
    ecil_frame_truncate(frame, mark);
    ecil_vtable_use_rollback(use_mark);
    (void)snprintf(message, sizeof(message),
                   "the body of \"%.200s\" uses %.*s, which the CIR back "
                   "end does not support; the function is dropped",
                   rout->source_corresp.name != NULL ?
                               rout->source_corresp.name : "?",
                   ECIL_REASON_MAX - 1, ecil_unsupported_reason);
    if (gid != 0) ecil_remember_drop_reason(gid, ecil_unsupported_reason);
    ecil_unsupported_reason = NULL;
    if (gid != 0) ecil_reissue_function(frame, gid, FALSE, TRUE);
    ecil_unsupported_reason = NULL;
    if (routine_is_root(rout) ||
        (gid == 0 && ecil_symbol_clash && definition_must_exist(rout))) {
      /* (With no G-id the library cannot be told that the definition is
         missing, so nothing else would report it.  Another reason that no
         G-id could be made leaves the function unreferenced or dropped with
         the functions that refer to it, as before.) */
      cir_be_error(message);
    } else {
      cir_be_warning(message);
    }  /* if */
  }  /* if */
  if (!dropped) ecil_vtable_use_commit(use_mark);
  vptr_init_pending = FALSE;
  ecil_jump_check_enabled = FALSE;
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
}  /* emit_function_rest */

static void emit_function(an_ecil_frame *frame, a_scope_ptr scope)
/*
Translate one function scope (emit_function_rest), unless another routine with
the same symbol has its body in the stream already: overloads that share an
asm label (glibc's gnu_inline memchr and its const overload) have one G-id,
which has one body (spec 8.2).  If the routine is inline-only, as those are,
it is left with the first one's body (the static locals and lambdas of the
skipped body are never introduced, which is harmless: the consumer keeps no
inline-only body).  Otherwise it is a real definition of a symbol that has a
body: a second definition with the same asm label, or a definition after an
inline-only body.  That is an error (gcc fails in the assembler).  The
routine is not dropped through emit_function_rest, whose drop would re-issue
the shared G-id as unsupported and take the first body away.
*/
{
  a_routine_ptr rout = scope->variant.routine.ptr;
  uint32_t      gid;

  /* A reason noted while introducing the declaration (an unsupported
     parameter type) is reported with the dropped body (emit_function_rest). */
  ecil_unsupported_reason = NULL;
  /* Records declared incomplete in an earlier frame and completed since get
     their bodies before anything here can need them complete. */
  ecil_complete_pending_records(frame);
#if BACK_END_TYPE == BACK_END_TYPE_CIR
  ecil_jump_check_enabled = !C_mode() && cfront_2_1_mode;
#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */
  ecil_symbol_clash = FALSE;
  gid = ecil_function_id(frame, rout);
  if (gid != 0 && ecil_unsupported_reason == NULL &&
      (ecil_gid_info(gid)->flags & ECIL_GF_DEFINED) != 0 &&
      ecil_gid_info(gid)->entity != NULL &&
      ecil_gid_info(gid)->entity != (void *)rout) {
    ecil_jump_check_enabled = FALSE;
    if (rout->definition_for_inlining_only || rout->suppress_inline_body) {
      ecil_set_function_state(rout, efs_streamed);
    } else {
      char  message[ECIL_DIAGNOSTIC_MAX];

      (void)snprintf(message, sizeof(message),
                     "the definition of \"%.200s\" is a definition of a "
                     "symbol that already has a body in this translation "
                     "unit (an asm label or gnu_inline)",
                     rout->source_corresp.name != NULL ?
                               rout->source_corresp.name : "?");
      cir_be_error(message);
      ecil_set_function_state(rout, efs_dropped);
    }  /* if */
  } else {
    emit_function_rest(frame, scope, gid);
  }  /* if */
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
    emit_func_eh_spec(frame, ep);
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
    ecil_remember_drop_reason(gid, ecil_unsupported_reason);
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
      char                    message[ECIL_DIAGNOSTIC_MAX];
      const an_ecil_gid_info  *info;

      ecil_frame_truncate(frame, mark);
      (void)snprintf(message, sizeof(message),
                     "the initializer of \"%.200s\" uses %.*s, which the "
                     "CIR back end does not support; the definition is "
                     "dropped",
                     var->source_corresp.name != NULL ?
                                 var->source_corresp.name : "?",
                     ECIL_REASON_MAX - 1, ecil_unsupported_reason);
      ecil_remember_drop_reason(gid, ecil_unsupported_reason);
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
  if (!C_mode()) {
    /* The vtables and typeinfo of the classes local to the function: their
       entities are alive now (spec 12.8). */
    ecil_emit_class_abi_data(frame, TRUE);
  }  /* if */
  ecil_forget_local_pending_records();
  ecil_forget_local_signatures();
  innermost_function_scope = saved_function_scope;
  curr_il_region_number = saved_region;
}  /* ecil_stream_function_region */


/* Conditionally close the "edg" namespace. */
END_EDG_NAMESPACE

#endif /* ECIL_WRITER_ENABLED */
