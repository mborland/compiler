/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*

ecil_write.h -- Declarations for the ECIL stream writer (ecil_write.c), its
                output sinks (ecil_sink.c) and the CIR back end shim
                (cir_be.c).

ECIL is the streamable serialization of the non-lowered IL that the CIR back
end consumes (see cir_src/doc/ecil_format.md).  The stream never contains
pointer values.

*/

/* Avoid including these declarations more than once: */
#ifndef ECIL_WRITE_H
#define ECIL_WRITE_H 1

/*
The ECIL writer is part of the front end only in the CIR configuration.  The
test macro ECIL_COMPILE_FOR_TEST lets the files be syntax checked in other
configurations; it must not be set in a real build.
*/
#if BACK_END_TYPE == BACK_END_TYPE_CIR || defined(ECIL_COMPILE_FOR_TEST)
#define ECIL_WRITER_ENABLED TRUE
#else /* !(BACK_END_TYPE == BACK_END_TYPE_CIR || ...) */
#define ECIL_WRITER_ENABLED FALSE
#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR || ... */

/*
Front-end macros that the CIR configuration pins (cmake/macro-conf/default/
cpfe-cir.cmakedef) for every build type, because the stream depends on them:

  NEW_CAN_BE_FOLDED_INTO_CTOR is 0: with it the front end folds the allocation
  of a class new expression into its constructor and records no operator
  delete to call if the initialization throws, so the free-on-throw cleanup of
  the new expression cannot be sent (the debug build type sets it to 0, the
  others leave it TRUE, and the streams differed).

  DELETE_CAN_BE_FOLDED_INTO_DTOR is 1: the writer uses the class's associated
  operator delete (assoc_operator_delete_routine, which exists only then).

  NEW_AND_DELETE_FOR_ARRAY_CAN_BE_FOLDED_INTO_RUNTIME_ROUTINE is 1: with 0 the
  front end chooses another delete[] for the free-on-throw cleanup of a new[]
  (the sized one), which the goldens and probes have not been checked with.
*/
#if BACK_END_TYPE == BACK_END_TYPE_CIR
#if NEW_CAN_BE_FOLDED_INTO_CTOR
#error NEW_CAN_BE_FOLDED_INTO_CTOR must be 0 for the CIR back end
#endif /* NEW_CAN_BE_FOLDED_INTO_CTOR */
#if !DELETE_CAN_BE_FOLDED_INTO_DTOR
#error DELETE_CAN_BE_FOLDED_INTO_DTOR must be 1 for the CIR back end
#endif /* !DELETE_CAN_BE_FOLDED_INTO_DTOR */
#if !NEW_AND_DELETE_FOR_ARRAY_CAN_BE_FOLDED_INTO_RUNTIME_ROUTINE
#error NEW_AND_DELETE_FOR_ARRAY_CAN_BE_FOLDED_INTO_RUNTIME_ROUTINE must be 1
#endif /* !NEW_AND_DELETE_FOR_ARRAY_CAN_BE_FOLDED_INTO_RUNTIME_ROUTINE */
#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */

#ifndef ECIL_ID_IN_PREFIX
#define ECIL_ID_IN_PREFIX FALSE
#endif /* ifndef ECIL_ID_IN_PREFIX */

#if ECIL_WRITER_ENABLED

/*
The contract headers are plain C and are included outside the "edg"
namespace on purpose.
*/
#include "edg_cir/ecil_format.h"
#include "edg_cir/edg_cir.h"

/* Conditionally open the "edg" namespace. */
BEGIN_EDG_NAMESPACE

/*
A growable byte buffer.  Allocation failure sets "failed" and discards later
writes; the stream notices at the next submit.
*/
typedef struct an_ecil_buffer {
  char          *data;
  size_t        size;
  size_t        capacity;
  a_boolean     failed;
} an_ecil_buffer;

/* A frame that grew beyond this many bytes in a section gives the storage
   back when it is reset (the frame of the stream is reused). */
#define ECIL_FRAME_KEEP_BYTES (256u * 1024u)

/*
A frame under construction: a declaration section, then (for F_REGION) a
body section.  Records go to the "current" section.  At submit the two
sections are concatenated, so every id a body uses is introduced earlier in
the same frame (or in an earlier frame).
*/
typedef struct an_ecil_frame {
  an_ecil_buffer  decl;
  an_ecil_buffer  body;
  an_ecil_buffer  *current;
  unsigned int    kind;           /* An an_ecil_frame_kind value. */
  unsigned int    flags;          /* ECIL_FRAME_FLAG_*. */
  int             pending_operands;
                        /* CHECKING: operands still owed by the open
                           record, or -1 if no record is open. */
#if CHECKING
  unsigned int    intro_kind;
                        /* CHECKING: what the first operand of the open
                           record is (an an_ecil_intro_kind). */
#endif /* CHECKING */
} an_ecil_frame;

/*
A position in a frame, taken before a sequence of records so that the
sequence can be abandoned (spec rule F4: the stacks are empty at every
frame end, so a writer that gives up on a body truncates to a mark).
*/
typedef struct an_ecil_mark {
  size_t        body_size;
  a_boolean     in_body;
} an_ecil_mark;

#if CHECKING
/*
Introduction checks (spec 7, id density): the writer verifies, from the
records it writes, that G-ids and sids are introduced densely in increasing
order at assignment time and that nothing it allocated is left unintroduced.
*/
extern void ecil_check_begin_lids(void);

extern void ecil_check_function_lids(uint32_t num_lids);

extern void ecil_check_all_introduced(void);
#endif /* CHECKING */

/* Buffer and tag encoders (spec section 5; shortest encodings). */

extern void ecil_buf_init(an_ecil_buffer *buf);

extern void ecil_buf_free(an_ecil_buffer *buf);

extern void ecil_buf_put_bytes(an_ecil_buffer *buf,
                               const void     *bytes,
                               size_t         length);

extern void ecil_put_uint(an_ecil_buffer *buf, uint64_t value);

extern void ecil_put_int(an_ecil_buffer *buf, int64_t value);

extern void ecil_put_nil(an_ecil_buffer *buf);

extern void ecil_put_bool(an_ecil_buffer *buf, a_boolean value);

extern void ecil_put_bin(an_ecil_buffer *buf,
                         const void     *bytes,
                         size_t         length);

extern void ecil_put_f32_bits(an_ecil_buffer *buf, uint32_t bits);

extern void ecil_put_f64_bits(an_ecil_buffer *buf, uint64_t bits);

extern void ecil_put_fbits(an_ecil_buffer      *buf,
                           const unsigned char *little_endian_bytes,
                           unsigned int        length);

extern void ecil_put_u128(an_ecil_buffer *buf, uint64_t low, uint64_t high);

extern void ecil_put_i128(an_ecil_buffer *buf, uint64_t low, uint64_t high);

extern void ecil_put_uint_array(an_ecil_buffer *buf,
                                const uint32_t *elements,
                                size_t         count);

/*
Returns the operand count of an opcode, or -1 if the value is not an
assigned opcode.
*/
extern int ecil_opcode_arity(unsigned int opcode);

/* Frame builder. */

extern void ecil_frame_init(an_ecil_frame *frame, unsigned int kind);

extern void ecil_frame_reset(an_ecil_frame *frame, unsigned int kind);

extern void ecil_frame_free(an_ecil_frame *frame);

extern void ecil_frame_begin_body(an_ecil_frame *frame);

/*
Introductions always go to the declaration section.  Bracket them with these
when the frame's current section may be the body.
*/
extern an_ecil_buffer *ecil_frame_enter_decl(an_ecil_frame *frame);

extern void ecil_frame_leave_decl(an_ecil_frame *frame,
                                  an_ecil_buffer *saved);

extern an_ecil_mark ecil_frame_mark(const an_ecil_frame *frame);

extern void ecil_frame_truncate(an_ecil_frame *frame, an_ecil_mark mark);

/*
Record helpers.  A record is ecil_rec_begin, then exactly the opcode's
arity of operand calls, then ecil_rec_end (checked in CHECKING builds).
*/
extern void ecil_rec_begin(an_ecil_frame *frame, unsigned int opcode);

extern void ecil_arg_uint(an_ecil_frame *frame, uint64_t value);

extern void ecil_arg_int(an_ecil_frame *frame, int64_t value);

extern void ecil_arg_nil(an_ecil_frame *frame);

extern void ecil_arg_bool(an_ecil_frame *frame, a_boolean value);

extern void ecil_arg_bin(an_ecil_frame *frame,
                         const void    *bytes,
                         size_t        length);

extern void ecil_arg_array(an_ecil_frame  *frame,
                           const uint32_t *elements,
                           size_t         count);

extern void ecil_arg_f32_bits(an_ecil_frame *frame, uint32_t bits);

extern void ecil_arg_f64_bits(an_ecil_frame *frame, uint64_t bits);

extern void ecil_arg_fbits(an_ecil_frame       *frame,
                           const unsigned char *little_endian_bytes,
                           unsigned int        length);

extern void ecil_arg_int128(an_ecil_frame *frame,
                            uint64_t      low,
                            uint64_t      high,
                            a_boolean     is_signed);

extern void ecil_rec_end(an_ecil_frame *frame);

/*
A supp blob under construction (spec 6): the key and value pairs of one
record.  A record has at most one blob, so what a record can carry (the
unsupported_reason and the noinline of a DECL_FUNC) goes into the same
builder: begin, add the pairs in increasing key order (each key at most once;
CHECKING builds assert it), then end it with the record just completed.
*/
typedef struct an_ecil_supp {
  an_ecil_buffer  payload;
  unsigned int    num_pairs;
  unsigned int    last_key;
} an_ecil_supp;

extern void ecil_supp_begin(an_ecil_supp *supp);

/* A flag-like key (value true). */
extern void ecil_supp_true(an_ecil_supp *supp, an_ecil_supp_key key);

extern void ecil_supp_uint(an_ecil_supp *supp,
                           an_ecil_supp_key key,
                           uint64_t         value);

/* A text key (a bin): cut to at most 255 bytes at a UTF-8 boundary. */
extern void ecil_supp_text(an_ecil_supp *supp,
                           an_ecil_supp_key key,
                           const char       *text);

/* Follow the record just completed (all its operands written, before
   ecil_rec_end) with the blob, if it has a pair, and set HAS_SUPP on the
   frame.  The builder is empty afterwards. */
extern void ecil_rec_supp_end(an_ecil_frame *frame, an_ecil_supp *supp);

extern void ecil_rec0(an_ecil_frame *frame, unsigned int opcode);

extern void ecil_rec1(an_ecil_frame *frame,
                      unsigned int  opcode,
                      uint64_t      a);

extern void ecil_rec2(an_ecil_frame *frame,
                      unsigned int  opcode,
                      uint64_t      a,
                      uint64_t      b);

/*
Writer internals shared by ecil_write.c, ecil_type.c, ecil_const.c and
ecil_code.c.
*/

/* What a G-id names. */
typedef enum an_ecil_gid_kind {
  egk_none,
  egk_void,
  egk_bool,
  egk_int,
  egk_float,
  egk_ptr,
  egk_array,
  egk_func_type,
  egk_record,
  egk_opaque,
  egk_func,       /* A function (DECL_FUNC). */
  egk_var,        /* A variable (DECL_VAR). */
  egk_vptr,       /* The type of a vptr field (TY_VPTR). */
  egk_data_member,/* A pointer to a data member (TY_DATA_MEMBER). */
  egk_method,     /* A pointer to a member function (TY_METHOD). */
  egk_abi_data    /* A vtable or a class typeinfo (DECL_ABI_DATA). */
} an_ecil_gid_kind;

#define ECIL_GF_OPAQUE      0x01u   /* An opaque scalar (TY_OPAQUE). */
#define ECIL_GF_ABI_OPAQUE  0x02u   /* Contains opaque storage by value. */
#define ECIL_GF_COMPLETE    0x04u   /* A record whose body was emitted. */
#define ECIL_GF_DEFINED     0x08u   /* egk_func: a body was streamed.
                                       egk_var: DECL_VAR(definition) was
                                       sent (its initializer may still be
                                       queued). */
#define ECIL_GF_VECTOR      0x40u   /* An opaque GNU vector type. */
#define ECIL_GF_COMPLEX     0x80u   /* An opaque complex/imaginary type. */
#define ECIL_GF_FAILED      0x100u  /* A record whose body could not be
                                       written (an unsupported member). */
#define ECIL_GF_LATE_NAME   0x200u  /* Sent without a name that is still
                                       to come (NAME_BIND). */
#define ECIL_GF_NAME_BOUND  0x400u  /* The late name has been bound. */
#define ECIL_GF_NEEDS_STATIC_INIT 0x1000u
                                    /* A variable whose definition has a
                                       STATIC_INIT (a dynamic initializer or
                                       a destructor). */
#define ECIL_GF_CONST_INIT  0x2000u /* ... with a constant (or no) initializer:
                                       only its destructor is registered. */
#define ECIL_GF_PARTIAL_INIT 0x4000u
                                    /* ... whose aggregate initializer is
                                       partly constant: VAR_INIT has the
                                       constant elements (the others zero)
                                       and STATIC_INIT only the others. */
#define ECIL_GF_HAS_DMP     0x8000u
                                    /* A pointer to a data member, or an
                                       array or record that contains one
                                       (its null value is -1, not 0). */
#define ECIL_GF_FUNCTION_LOCAL 0x800u
                                    /* A routine of a function's memory
                                       region: its IL entry is gone when
                                       the file scope is complete. */
#define ECIL_GF_LAYOUT_FACTS 0x10000u
                                    /* A record whose layout facts
                                       (CLASS_MANGLE, CLASS_INFO,
                                       CLASS_BASES) were sent. */
#define ECIL_GF_VTABLE_FACTS 0x20000u
                                    /* A dynamic class whose vtable facts
                                       (CLASS_VFUNCS, CLASS_OVERRIDES, its
                                       DECL_FUNCs and STRUCTOR_DELETING)
                                       were sent. */
#define ECIL_GF_VTABLE_USED  0x40000u
                                    /* A dynamic class whose vtable is used
                                       by a record that was kept (VPTR_INIT,
                                       a constant). */
#define ECIL_GF_DYNAMIC      0x80000u
                                    /* A record of a dynamic class (a
                                       TY_VPTR, a VIRTUAL_BASE or the base
                                       of a dynamic class). */
#define ECIL_GF_VISITED      0x1000000u
                                    /* A class being looked at by a walk over
                                       the base classes (always cleared). */
#define ECIL_GF_FACTS_BUSY   0x2000000u
                                    /* A class whose vtable facts are being
                                       sent (a covariant return class that is
                                       the class itself is not asked again). */
#define ECIL_GF_STRUCTOR_VBASES 0x800000u
                                    /* A constructor or destructor G-id of a
                                       STRUCTOR pair with has_vbases. */
#define ECIL_GF_TI_INCOMPLETE 0x4000000u
                                    /* A record whose class typeinfo was
                                       introduced while it was incomplete: a
                                       plain internal __class_type_info that
                                       is never re-planned (sticky, as in
                                       clang). */
#define ECIL_GF_ABI_DONE     0x400000u
                                    /* A dynamic class whose vtable and
                                       typeinfo were decided. */

/* The variant of a constructor or destructor G-id. */
#define ECIL_VARIANT_NONE     0u
#define ECIL_VARIANT_COMPLETE 1u  /* C1, D1: complete objects. */
#define ECIL_VARIANT_BASE     2u  /* C2, D2: base subobjects. */
#define ECIL_VARIANT_DELETING 3u  /* D0: the deleting destructor. */

/* Everything the writer remembers about a G-id. */
typedef struct an_ecil_gid_info {
  an_ecil_gid_kind
                kind;
  unsigned int  flags;            /* ECIL_GF_*. */
  unsigned int  bits;             /* Integers: width. */
  a_boolean     is_signed;
  uint64_t      size;
  uint32_t      align;
  uint32_t      data_size;        /* Record: the data size sent (Itanium
                                     dsize). */
  uint32_t      ref;              /* Pointer: pointee; array: element;
                                     function type: return type. */
  uint64_t      count;            /* Array: elements.  Record: members. */
  unsigned int  float_format;     /* An an_ecil_float_format value. */
  void          *entity;          /* Record: its IL type; function and
                                     variable: the IL entry. */
  /* The last emitted DECL_FUNC / DECL_VAR (spec 8.2, re-issue rules). */
  uint32_t      decl_type;
  uint32_t      decl_name;
  unsigned int  decl_flags;
  unsigned int  decl_linkage;
  a_boolean     decl_noinline;    /* DECL_FUNC: the noinline key was sent. */
  a_boolean     decl_emitted;
  /* A constructor or destructor has two G-ids (spec 11.4): the complete
     variant C1/D1 and the base variant C2/D2; each names the other. */
  uint32_t      partner;
  unsigned int  variant;          /* ECIL_VARIANT_*. */
  /* A virtual destructor's complete variant: its deleting variant D0.  A
     record of a dynamic class: the G-ids of its vtable and of its class
     typeinfo (DECL_ABI_DATA), once introduced. */
  uint32_t      aux;
  uint32_t      aux2;
  uint32_t      nvsize;           /* A record whose CLASS_INFO was sent: its
                                     nvsize (a base ends there, spec 12.2). */
} an_ecil_gid_info;

/* Why the current function or initializer cannot be expressed, or NULL.  The
   text is owned by the stream. */
extern const char *ecil_unsupported_reason;

/* The length of the longest prefix of text that is at most max bytes long
   and does not end inside a UTF-8 sequence. */
extern size_t ecil_utf8_prefix_length(const char *text, size_t max);

/* Note a reason (the first one wins); the text is copied, cut to
   ECIL_REASON_MAX - 1 bytes at a UTF-8 boundary. */
extern void ecil_note_unsupported(const char *reason);

/* A reason taken aside by ecil_save_reason. */
#define ECIL_REASON_MAX 256

/* The size of a buffer for a diagnostic that names a function or variable
   (at most 200 bytes) and a reason (at most ECIL_REASON_MAX - 1 bytes), with
   room for the fixed text around them. */
#define ECIL_DIAGNOSTIC_MAX (ECIL_REASON_MAX + 400)

typedef struct an_ecil_saved_reason {
  a_boolean     is_set;
  char          text[ECIL_REASON_MAX];
} an_ecil_saved_reason;

/* Take the current reason aside (and clear it) / put it back. */
extern void ecil_save_reason(an_ecil_saved_reason *saved);

extern void ecil_restore_reason(const an_ecil_saved_reason *saved);

/* Note the reason for an unsupported use of the type with G-id gid:
   "vector types are not supported" and the like, else fallback. */
extern void ecil_note_unsupported_type(uint32_t gid, const char *fallback);

/* The G-id table. */
extern uint32_t ecil_new_gid(an_ecil_gid_kind kind);

/* The record of a G-id.  The pointer is valid only until the next G-id is
   assigned (the table moves): look it up again after any call that can
   introduce a type, function or variable. */
extern an_ecil_gid_info *ecil_gid_info(uint32_t gid);

extern uint32_t ecil_num_gids(void);

/*
Structural interning: types are keyed by a sequence of words (kind, child
ids, flags), so equal ECIL types get one G-id however many IL entries map to
them.  ecil_word_lookup returns the G-id or 0.
*/
extern uint32_t ecil_word_lookup(const uint32_t *words, size_t count);

extern void ecil_word_register(const uint32_t *words,
                               size_t         count,
                               uint32_t       gid);

extern void ecil_word_unregister(const uint32_t *words, size_t count);

/*
Per-IL-entry ids.  G-ids live on types, routines, variables, fields; L-ids
on local variables and labels.  With the ecil_id prefix field (CIR builds)
they are stored in the IL entry; in test builds a side table is used.
*/
extern uint32_t ecil_gid_of(void *entry);

extern void ecil_set_gid(void *entry, uint32_t gid);

extern uint32_t ecil_lid_of(void *entry);

extern void ecil_set_lid(void *entry, uint32_t lid);

/* End of a function frame that used num_lids L-ids: ids set so far are
   stale from now on (ecil_lid_of returns 0 for them). */
extern void ecil_end_lid_epoch(uint32_t num_lids);

/* Symbol names must be unique among statics and string literals. */
extern uint32_t ecil_unique_symbol_name(an_ecil_frame *frame,
                                        const char    *base);

extern void ecil_mark_symbol_name(uint32_t sid);

extern a_boolean ecil_symbol_name_used(uint32_t sid);

/* Types and declarations (ecil_type.c). */

extern uint32_t ecil_type_id(an_ecil_frame *frame, a_type_ptr type);

extern uint32_t ecil_intern_int_type(an_ecil_frame *frame,
                                     unsigned int  bits,
                                     a_boolean     is_signed,
                                     unsigned int  size,
                                     unsigned int  alignment);

extern uint32_t ecil_intern_float_type(an_ecil_frame *frame,
                                       unsigned int  format,
                                       unsigned int  size,
                                       unsigned int  alignment);

/* The target's int, double and pointer-sized unsigned types, and the type a
   value of a type has after the integer (and, with an IL type, the default
   argument) promotions. */
extern unsigned int ecil_int_bits(void);

extern uint32_t ecil_target_int_type(an_ecil_frame *frame);

extern uint32_t ecil_target_double_type(an_ecil_frame *frame);

extern uint32_t ecil_target_uintptr_type(an_ecil_frame *frame);

extern uint32_t ecil_promote_gid(an_ecil_frame *frame,
                                 uint32_t      gid,
                                 a_type_ptr    il_type);

/* The linkage (an an_ecil_linkage value) of a function or variable. */
extern unsigned int ecil_linkage_for(a_storage_class     storage_class,
                                     unsigned int        name_linkage,
                                     a_boolean           is_weak);

/* Return TRUE if the type with G-id gid is, or contains, a pointer to a data
   member.  Such a type is not zero-initializable: its null member pointer is
   -1, so it is zeroed by INIT_ZERO or INIT_ZERO_BASE, never ZERO_BYTES (spec
   12.7). */
extern a_boolean ecil_gid_has_data_member_pointer(uint32_t gid);

/* Remember why the function or variable with G-id gid is dropped: the reason
   goes into the unsupported_reason supp blob of its unsupported DECL_FUNC or
   DECL_VAR. */
extern void ecil_remember_drop_reason(uint32_t gid, const char *text);

/* TRUE while the constant of a partly constant aggregate initializer is built
   for VAR_INIT: its dynamic elements are zero (their STATIC_INIT follows). */
extern a_boolean ecil_skip_dynamic_elements;

/* Send DECL_VAR (an introduction or a re-issue) and remember its facts. */
extern void ecil_emit_decl_var(an_ecil_frame *frame,
                               uint32_t      gid,
                               uint32_t      type,
                               unsigned int  linkage,
                               unsigned int  flags,
                               uint32_t      alignment,
                               uint32_t      name_sid);

/* Append the value of an integer constant (64 or 128 bits wide). */
extern void ecil_arg_integer_constant(an_ecil_frame *frame,
                                      a_constant_ptr c,
                                      unsigned int  bits,
                                      a_boolean     is_signed);

/* The Itanium data size (dsize) of a type: its size, except for a non-POD
   class, where tail padding is not counted (spec 11.3). */
extern a_targ_size_t ecil_data_size_of_type(a_type_ptr type);

/* Names of IL kinds for diagnostics (ecil_kind_names.h): NULL if unknown. */
extern const char *ecil_expr_kind_name(int kind);
extern const char *ecil_operator_name(int kind);
extern const char *ecil_constant_kind_name(int kind);
extern const char *ecil_builtin_operation_name(int kind);
extern const char *ecil_statement_kind_name(int kind);

/* Note why a use of a variable of a record type without a body cannot be
   expressed. */
extern void ecil_check_variable_use(uint32_t gid);

/* Re-issue a variable whose definition is dropped (an unsupported
   declaration). */
extern void ecil_emit_dropped_decl_var(an_ecil_frame *frame,
                                       uint32_t      gid,
                                       uint32_t      type,
                                       unsigned int  linkage,
                                       unsigned int  flags,
                                       uint32_t      alignment,
                                       uint32_t      name_sid);

extern uint32_t ecil_void_type(an_ecil_frame *frame);

extern uint32_t ecil_bool_type(an_ecil_frame *frame);

extern void ecil_type_reset(void);

extern uint32_t ecil_ptr_type(an_ecil_frame *frame, uint32_t pointee);

extern uint32_t ecil_func_type_id(an_ecil_frame *frame,
                                  a_type_ptr    routine_type,
                                  a_boolean     promote_params);

extern void ecil_complete_pending_records(an_ecil_frame *frame);

/* Class facts (spec 12.2): the predicates, and the facts a record that needs
   them has them sent first.  A dynamic class has a TY_VPTR field, a
   VIRTUAL_BASE field or a BASE of a dynamic class; its layout facts follow its
   record body, its vtable facts come at the first need. */
extern a_boolean ecil_class_is_dynamic(a_type_ptr type);

extern a_boolean ecil_class_has_own_vptr(a_type_ptr type);

extern void ecil_class_layout_facts(an_ecil_frame *frame, a_type_ptr type);

extern void ecil_class_vtable_facts(an_ecil_frame *frame, a_type_ptr type);

/* The G-id of the typeinfo (DECL_ABI_DATA) of a type, a class or not, with
   everything it needs introduced first (the typeinfo of a pointee or of the
   class of a member pointer, of the bases of a class); 0 after noting why if
   it is not expressible (spec 12.3, 12.8). */
extern uint32_t ecil_typeinfo_id(an_ecil_frame *frame, a_type_ptr type);

/* The G-id of __cxa_throw_bad_array_new_length (no parameters, no return),
   declared on first use. */
extern uint32_t ecil_throw_bad_array_new_length_function(
                                                      an_ecil_frame *frame);

/* A kept record uses the vtable of a dynamic class: send its vtable facts and
   mark it used.  The mark can be taken back with the position of the log taken
   before the body or constant (a dropped body); commit forgets the log. */
extern void ecil_note_vtable_used(an_ecil_frame *frame, a_type_ptr type);

extern size_t ecil_vtable_use_mark(void);

extern void ecil_vtable_use_rollback(size_t mark);

extern void ecil_vtable_use_commit(size_t mark);

/* Decide and send the vtables and class typeinfo of the dynamic classes
   recorded so far: the local classes at the end of their function's region,
   the others at the end of the translation unit (spec 12.8). */
extern void ecil_emit_class_abi_data(an_ecil_frame *frame,
                                     a_boolean     local_classes);

/* The front end completed a class (class_decl.c): the records declared as
   incomplete are looked at again by the next ecil_complete_pending_records. */
extern void ecil_note_class_completed(void);

extern void ecil_forget_local_pending_records(void);

/* The same for the virtual function signatures (CLASS_VFUNCS sig). */
extern void ecil_forget_local_signatures(void);

/* The static invokers of lambdas (spec 11.9): queued when first referenced,
   synthesized into the frame after the function being translated. */
extern void ecil_queue_invoker(a_routine_ptr entry_point);

extern void ecil_drain_invokers(an_ecil_frame *frame);

extern a_boolean ecil_invokers_pending(void);

/* One dynamic initialization of the file scope (the body of F_GLOBALS). */
extern void ecil_stream_static_init(an_ecil_frame *frame,
                                    a_dynamic_init_ptr di);

extern uint32_t ecil_function_id(an_ecil_frame *frame, a_routine_ptr rout);

/* Set by ecil_function_id when it returns 0 because the symbol of the routine
   is already bound to another entity of a different kind or type (a symbol
   clash, not a construct the writer does not support). */
extern a_boolean ecil_symbol_clash;

/* The G-id of the base variant (C2/D2) of a constructor or destructor, else
   the routine's own (ecil_function_id returns the complete variant). */
extern uint32_t ecil_function_base_id(an_ecil_frame *frame,
                                      a_routine_ptr rout);

/* The G-id whose body a routine's definition streams: the base variant of a
   constructor or destructor, else gid itself. */
extern uint32_t ecil_function_body_id(uint32_t gid);

extern uint32_t ecil_array_of(an_ecil_frame *frame, uint32_t elem,
                              uint64_t count);

extern a_boolean ecil_is_plain_float_type(a_type_ptr type);

extern a_targ_alignment ecil_variable_alignment(a_variable_ptr var);

extern void ecil_bind_symbol(uint32_t name_sid, uint32_t gid);

#if BACK_END_TYPE == BACK_END_TYPE_CIR
extern void ecil_cxx_variable_facts(an_ecil_frame *frame,
                                    a_variable_ptr var,
                                    a_boolean      defining,
                                    unsigned int   *linkage,
                                    unsigned int   *flags,
                                    uint32_t       *name_sid);
#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */

extern a_boolean ecil_variable_may_have_multiple_copies(a_variable_ptr var);
extern a_boolean ecil_local_static_may_have_multiple_copies(
                                                       a_variable_ptr var);

#if BACK_END_TYPE == BACK_END_TYPE_CIR
/* TRUE for a routine that has vague linkage (linkonce_odr or weak_odr in a
   COMDAT, spec 11.2), the functions that live in one included. */
extern a_boolean ecil_routine_has_vague_linkage(a_routine_ptr rout);
#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */

extern uint32_t ecil_variable_symbol_gid(an_ecil_frame *frame,
                                         a_variable_ptr var);

extern uint32_t ecil_variable_id(an_ecil_frame *frame, a_variable_ptr var);

extern uint32_t ecil_global_variable_id(an_ecil_frame *frame,
                                        a_variable_ptr var);

extern void ecil_reissue_function(an_ecil_frame *frame,
                                  uint32_t      gid,
                                  a_boolean     defining,
                                  a_boolean     unsupported);

extern a_boolean ecil_routine_is_filtered(a_routine_ptr rout);

/* TRUE if the mangled name of a routine or variable may still change
   (scope_stk.c): it is sent with a nil name and bound at the end. */
extern a_boolean cir_name_may_change(a_source_correspondence *scp);

extern a_boolean cir_routine_name_may_change(a_routine_ptr rout);

/* C++ classes: passed and returned indirectly; has a non-trivial dtor. */
extern a_boolean ecil_class_is_nontrivial_abi(a_type_ptr type);

extern a_boolean ecil_class_has_nontrivial_dtor(a_type_ptr type);

extern const char *ecil_entity_name(a_source_correspondence *scp);

extern uint32_t ecil_intern_cstring(an_ecil_frame *frame, const char *text);

extern const char *ecil_string_text(uint32_t sid);

/* Late names (ecil_type.c): resolve the names that were not final when their
   entities were sent (re-issuing declarations whose linkage changed), then
   write the F_NAMES records. */
extern void ecil_resolve_late_names(an_ecil_frame *frame);

extern void ecil_emit_retractions(an_ecil_frame *frame);

extern void ecil_emit_late_names(an_ecil_frame *frame);

/* Constants, string literals and global variable definitions
   (ecil_const.c). */

/* One entry of a gathered (merged) aggregate. */
typedef struct an_init_entry {
  uint64_t      index;
  uint64_t      count;            /* 1, or the repeat count. */
  a_constant_ptr
                value;            /* The constant (a ck_init_repeat node for
                                     a repeat entry). */
  a_constant_ptr
                *sources;         /* Merged aggregates: all the aggregates
                                     initializing this member, in order; NULL
                                     if there is only value. */
  size_t        num_sources;
  a_boolean     removed;
  a_boolean     is_char;          /* One element of a string constant (value
                                     is the string, index the position). */
} an_init_entry;

/*
One level of an aggregate initializer, with C11 6.7.9 "current object"
handling: designators move the cursor, unnamed bit-fields are skipped, a
union takes its last entry, and initializers that revisit one member are
merged (see ecil_init_level_start).
*/
typedef struct an_init_level {
  a_type_ptr    type;             /* The aggregate type (typerefs skipped). */
  a_boolean     is_union;
  a_boolean     is_array;
  a_boolean     done;             /* A union that has its entry. */
  uint64_t      cur;              /* Index of the next member or element. */
  uint64_t      lead;             /* Records: the entries (the base classes)
                                     before the data members: the index of
                                     the first data member among the
                                     entries (the vptr not counted). */
  uint64_t      vptr_shift;       /* Records: 1 for a class with a vptr of
                                     its own (REC_FIELD 0): the REC_FIELD
                                     index of an entry is its index plus
                                     this. */
  a_field_ptr   cur_field;        /* Records: the member at cur. */
  a_constant_ptr
                next_const;       /* The next constant of the list. */
  /* Gathered mode (the aggregate has designators that revisit a member):
     the entries, merged, in order. */
  an_init_entry *entries;
  size_t        num_entries;
  size_t        entries_capacity;
  size_t        entry_pos;
  /* The members of a record, by index (built by ecil_member_type_gid). */
  a_field_ptr   *members;
  uint64_t      num_members;
  /* The entry last returned by ecil_init_level_next. */
  uint64_t      entry_count;
  a_boolean     entry_is_char;    /* The entry is a character of a string. */
  a_constant_ptr
                *entry_sources;   /* NULL unless merged. */
  size_t        entry_num_sources;
  /* The level belongs to the constant of a variable whose VAR_INIT has the
     constant elements (ECIL_GF_PARTIAL_INIT): the code that builds the
     level's dynamic elements skips the constant ones.  Only the writer's
     STATIC_INIT of that variable sets it, for the variable's own aggregate
     and for the nested aggregates of that constant; every other level (a
     temporary, a closure, a new, the aggregate of a dynamic element) is
     initialized completely. */
  a_boolean     skip_constants;
  /* With exceptions, an element or member that has a non-trivial destructor
     is destroyed if a later one throws: a DEACTIVATABLE eh cleanup begun
     after its initialization, ended before AGG_INIT_END (spec 12.6).  The
     body finds the aggregate through a local that holds its address. */
  uint32_t      addr_lid;         /* That local, or 0 for none. */
  unsigned int  num_partial;      /* Cleanups begun at this level. */
  a_boolean     partial_pending;  /* The entry at partial_index was just
                                     initialized: its cleanup follows. */
  uint64_t      partial_index;
  uint64_t      partial_count;    /* ... and the entries it repeats. */
  a_type_ptr    partial_type;     /* Its type. */
} an_init_level;

/*
Start walking the entries of an aggregate initialized by one constant, or by
several (sources: a later one revisits the same member as an earlier one, as
in "{ .b = 1, .a = 2, .c = 3 }" where b and c are in one anonymous struct).
The aggregate's type must already have been introduced.  After
ecil_init_level_next, the fields entry_sources and entry_num_sources of the
level give the sources of the entry returned (NULL if it is just the value)
and entry_count its repeat count; the level must be ended with
ecil_init_level_end.
*/
extern void ecil_init_level_start(an_init_level *level,
                                  a_constant_ptr *sources,
                                  size_t         num_sources);

extern a_boolean ecil_init_level_next(an_init_level  *level,
                                      uint64_t       *index,
                                      a_constant_ptr *value);

extern void ecil_init_level_end(an_init_level *level);

/* C_INT for byte pos of a string constant as an element of the level's
   array (an entry with entry_is_char). */
extern void ecil_emit_string_char(an_ecil_frame *frame, an_init_level *level,
                                  a_constant_ptr str, uint64_t pos);

/* The record of an integer or floating constant of ECIL type t: C_INT and
   the like if is_initializer, else CONST_INT and the like. */
extern a_boolean ecil_emit_scalar_constant(an_ecil_frame *frame,
                                           a_constant_ptr c,
                                           uint32_t       t,
                                           a_boolean      is_initializer);

/* The G-id an address constant points into (*is_function: a function), 0 if
   not expressible. */
extern uint32_t ecil_address_constant_gid(an_ecil_frame *frame,
                                          a_constant_ptr c,
                                          a_boolean      *is_function);

extern uint32_t ecil_member_type_gid(an_ecil_frame *frame,
                                     an_init_level  *level,
                                     uint64_t       index);

extern a_boolean ecil_emit_constant_sources(an_ecil_frame *frame,
                                            a_constant_ptr constant,
                                            a_constant_ptr *sources,
                                            size_t         num_sources,
                                            uint32_t       target_type);

/* CONST_DATA_MEMBER or CONST_METHOD (C_DATA_MEMBER or C_METHOD with
   as_constant) of a pointer-to-member constant (spec 12.4); FALSE after
   noting why if it is not expressible. */
extern a_boolean ecil_emit_ptr_to_member_constant(an_ecil_frame *frame,
                                                  a_constant_ptr c,
                                                  a_boolean      as_constant);

extern a_boolean ecil_emit_constant_typed(an_ecil_frame *frame,
                                          a_constant_ptr constant,
                                          uint32_t       target_type);

extern uint32_t ecil_string_literal_id(an_ecil_frame *frame,
                                       a_constant_ptr constant);

extern void ecil_define_variable(an_ecil_frame *frame,
                                 a_variable_ptr var,
                                 a_scope_ptr    scope);

/*
Introduce a static local with no declaration statement and queue its
initializer; build the queued initializers.  Constants never nest: building
one only queues the static locals it refers to.
*/
extern uint32_t ecil_queue_variable_definition(an_ecil_frame *frame,
                                               a_variable_ptr var);

extern void ecil_drain_variable_definitions(an_ecil_frame *frame);

extern void ecil_emit_globals(an_ecil_frame *frame);

extern void ecil_const_reset(void);

extern void cir_be_warning(const char *message);

/* The same for something that must exist although nothing refers to it: an
   error. */
extern void cir_be_error(const char *message);

/* A variable that is needed although nothing refers to it (cir_be_error). */
extern a_boolean ecil_variable_is_root(a_variable_ptr var);

/* Function bodies (ecil_code.c). */

extern void ecil_stream_function_region(an_ecil_frame        *frame,
                                        a_memory_region_number region_number);

/*
What the writer has done with a routine's body, by function definition
number.  efs_delayed (the body waits for a name that is not final yet) and
efs_retracted (the entity was made discardable) are set by the C++ hooks.
*/
typedef enum an_ecil_function_state {
  efs_none,       /* Nothing yet. */
  efs_streamed,   /* The body was sent. */
  efs_dropped,    /* The body was dropped as unsupported. */
  efs_retracted,  /* RETRACT was sent for it. */
  efs_delayed     /* Waiting; not handled yet. */
} an_ecil_function_state;

extern an_ecil_function_state ecil_function_state(a_routine_ptr rout);

extern void ecil_set_function_state(a_routine_ptr          rout,
                                    an_ecil_function_state state);

extern a_boolean ecil_function_is_handled(a_routine_ptr rout);

#if CHECKING
extern size_t ecil_code_stack_high;
#endif /* CHECKING */

extern void ecil_code_reset(void);

extern void ecil_check_variable_attributes(a_variable_ptr var);

/*
Internal builtin ids, besides the an_ecil_builtin values, for the builtins
that are translated by a sequence of records (or by a record of their own).
*/
#define ECIL_SPECIAL_C23_VA_START 100U
#define ECIL_SPECIAL_ALLOCA_ALIGN 101U
#define ECIL_SPECIAL_ADD_OVERFLOW_P 102U
#define ECIL_SPECIAL_SUB_OVERFLOW_P 103U
#define ECIL_SPECIAL_MUL_OVERFLOW_P 104U
#define ECIL_SPECIAL_FP_ISNAN 110U
#define ECIL_SPECIAL_FP_ISINF 111U
#define ECIL_SPECIAL_FP_ISFINITE 112U
#define ECIL_SPECIAL_FP_ISNORMAL 113U
#define ECIL_SPECIAL_FP_ISSIGNALING 114U
#define ECIL_SPECIAL_FP_SIGNBIT 115U
#define ECIL_SPECIAL_FP_ISINF_SIGN 116U
#define ECIL_SPECIAL_FP_FPCLASSIFY 117U
#define ECIL_SPECIAL_FP_ISLESS 120U
#define ECIL_SPECIAL_FP_ISGREATER 121U
#define ECIL_SPECIAL_FP_ISLESSEQUAL 122U
#define ECIL_SPECIAL_FP_ISGREATEREQUAL 123U
#define ECIL_SPECIAL_FP_ISLESSGREATER 124U
#define ECIL_SPECIAL_FP_ISUNORDERED 125U
#define ECIL_SPECIAL_CLZG 130U
#define ECIL_SPECIAL_CTZG 131U
#define ECIL_SPECIAL_FP_FIRST 110U
#define ECIL_SPECIAL_FP_LAST 125U

/* What a routine name is, by one hashed lookup. */
typedef struct an_ecil_name_class {
  unsigned int  special_id;       /* __builtin_X: the builtin ECIL has a
                                     record for (0 if none). */
  const char    *library_name;    /* __builtin_X: X, if X is a libc function;
                                     a pointer into the name. */
  a_boolean     is_library_function;
                                  /* The name as written is a libc function. */
} an_ecil_name_class;

extern void ecil_classify_function_name(const char         *name,
                                        an_ecil_name_class *cls);

/* Stream-level helpers. */

extern uint32_t ecil_intern_string(an_ecil_frame *frame,
                                   const char    *text,
                                   size_t        length);

/* The FE-facing stream API. */

extern a_boolean ecil_stream_is_active(void);

extern void ecil_stream_open(void);

/* Capture the target triple; called right after the predefined macros are
   entered (fe_init.c). */
extern void ecil_note_target_triple(void);

extern void ecil_hand_off_region(a_memory_region_number region_number);

extern void ecil_stream_finish(void);

extern void ecil_abort(void);

extern void ecil_stream_finish_back_end(void);

extern void ecil_stream_close(void);

/* TRUE while a function body is written under the rules of cfront 2.1
   (which accept a jump past the initialization of an object with a
   destructor): the scope regions are followed, and a case or goto label with
   a cleanup open is diagnosed. */
extern a_boolean ecil_jump_check_enabled;

/* TRUE for -fwrapv semantics: never send the nsw flag. */
extern a_boolean ecil_wrapv;

/* TRUE for a stream with exceptions (HDR_LANG exceptions = 1): C++ with the
   front end's exceptions enabled.  Functions and calls may throw unless
   marked NOTHROW (spec 12.6). */
extern a_boolean ecil_has_exceptions(void);

extern void ecil_submit_frame(an_ecil_frame *frame);

/*
Output sinks (ecil_sink.c).  A sink is a forward-only consumer of stream
bytes.  A buffer obtained from ecil_sink_acquire is handed to
ecil_sink_submit, which takes ownership; the caller never touches it again.
*/
typedef struct an_ecil_sink an_ecil_sink;

extern an_ecil_sink *ecil_sink_open_file(const char *file_name);

#ifdef EDG_CIR_LIBRARY_LINKED
extern an_ecil_sink *ecil_sink_open_in_process(void);
#endif /* EDG_CIR_LIBRARY_LINKED */

extern char *ecil_sink_acquire(an_ecil_sink *sink, size_t capacity);

extern a_boolean ecil_sink_submit(an_ecil_sink *sink,
                                  char         *buffer,
                                  size_t       length);

extern a_boolean ecil_sink_finish(an_ecil_sink *sink);

extern void ecil_sink_begin_abort(an_ecil_sink *sink);

extern void ecil_sink_abort(an_ecil_sink *sink);

extern void ecil_sink_close(an_ecil_sink *sink);

/* Conditionally close the "edg" namespace. */
END_EDG_NAMESPACE

#endif /* ECIL_WRITER_ENABLED */

#endif /* ifndef ECIL_WRITE_H */
