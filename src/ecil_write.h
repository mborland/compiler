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
} an_ecil_frame;

/*
A position in a frame, taken before a sequence of records so that the
sequence can be abandoned (spec rule F4: the stacks are empty at every
frame end, so a writer that gives up on a body truncates to a mark).
*/
typedef struct an_ecil_mark {
  size_t        decl_size;
  size_t        body_size;
  a_boolean     in_body;
} an_ecil_mark;

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
  egk_var         /* A variable (DECL_VAR). */
} an_ecil_gid_kind;

#define ECIL_GF_OPAQUE      0x01u   /* An opaque scalar (TY_OPAQUE). */
#define ECIL_GF_ABI_OPAQUE  0x02u   /* Contains opaque storage by value. */
#define ECIL_GF_COMPLETE    0x04u   /* A record whose body was emitted. */
#define ECIL_GF_DEFINED     0x08u   /* egk_func: a body was streamed. */
#define ECIL_GF_VARARGS     0x10u   /* egk_func_type: variadic. */
#define ECIL_GF_NO_PROTO    0x20u   /* egk_func_type: unprototyped. */
#define ECIL_GF_VECTOR      0x40u   /* An opaque GNU vector type. */
#define ECIL_GF_COMPLEX     0x80u   /* An opaque complex/imaginary type. */

/* Everything the writer remembers about a G-id. */
typedef struct an_ecil_gid_info {
  an_ecil_gid_kind
                kind;
  unsigned int  flags;            /* ECIL_GF_*. */
  unsigned int  bits;             /* Integers: width. */
  a_boolean     is_signed;
  uint32_t      size;
  uint32_t      align;
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
  uint32_t      decl_align;
  a_boolean     decl_emitted;
} an_ecil_gid_info;

/* Why the current function or initializer cannot be expressed, or NULL. */
extern const char *ecil_unsupported_reason;

extern void ecil_note_unsupported(const char *reason);

/* Note the reason for an unsupported use of the type with G-id gid:
   "vector types are not supported" and the like, else fallback. */
extern void ecil_note_unsupported_type(uint32_t gid, const char *fallback);

/* The G-id table. */
extern uint32_t ecil_new_gid(an_ecil_gid_kind kind);

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

/*
Per-IL-entry ids.  G-ids live on types, routines, variables, fields; L-ids
on local variables and labels.  With the ecil_id prefix field (CIR builds)
they are stored in the IL entry; in test builds a side table is used.
*/
extern uint32_t ecil_gid_of(void *entry);

extern void ecil_set_gid(void *entry, uint32_t gid);

extern uint32_t ecil_lid_of(void *entry);

extern void ecil_set_lid(void *entry, uint32_t lid);

extern void ecil_forget_lids(void);

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

extern uint32_t ecil_void_type(an_ecil_frame *frame);

extern uint32_t ecil_bool_type(an_ecil_frame *frame);

extern void ecil_type_reset(void);

extern uint32_t ecil_ptr_type(an_ecil_frame *frame, uint32_t pointee);

extern uint32_t ecil_func_type_id(an_ecil_frame *frame,
                                  a_type_ptr    routine_type,
                                  a_boolean     promote_params);

extern void ecil_complete_pending_records(an_ecil_frame *frame);

extern uint32_t ecil_function_id(an_ecil_frame *frame, a_routine_ptr rout);

extern void ecil_bind_symbol(uint32_t name_sid, uint32_t gid);

extern uint32_t ecil_variable_symbol_gid(an_ecil_frame *frame,
                                         a_variable_ptr var);

extern uint32_t ecil_variable_id(an_ecil_frame *frame, a_variable_ptr var);

extern void ecil_reissue_function(an_ecil_frame *frame,
                                  uint32_t      gid,
                                  a_boolean     defining,
                                  a_boolean     unsupported);

extern a_boolean ecil_routine_is_filtered(a_routine_ptr rout);

extern const char *ecil_entity_name(a_source_correspondence *scp);

extern uint32_t ecil_intern_cstring(an_ecil_frame *frame, const char *text);

/* Constants, string literals and global variable definitions
   (ecil_const.c). */

/*
One level of an aggregate initializer, with C11 6.7.9 "current object"
handling: designators move the cursor, unnamed bit-fields are skipped, a
union takes one entry.
*/
typedef struct an_init_level {
  a_type_ptr    type;             /* The aggregate type (typerefs skipped). */
  a_boolean     is_union;
  a_boolean     is_array;
  a_boolean     done;             /* A union that has its entry. */
  uint64_t      cur;              /* Index of the next member or element. */
  a_field_ptr   cur_field;        /* Records: the member at cur. */
  a_constant_ptr
                next_const;       /* The next constant of the list. */
} an_init_level;

extern void ecil_init_level_start(an_init_level *level,
                                  a_constant_ptr aggregate);

extern a_boolean ecil_init_level_next(an_init_level  *level,
                                      uint64_t       *index,
                                      a_constant_ptr *value);

extern a_boolean ecil_emit_float_constant(an_ecil_frame *frame,
                                           a_constant_ptr c);

extern a_boolean ecil_emit_constant(an_ecil_frame *frame,
                                    a_constant_ptr constant);

extern uint32_t ecil_string_literal_id(an_ecil_frame *frame,
                                       a_constant_ptr constant);

extern void ecil_define_variable(an_ecil_frame *frame,
                                 a_variable_ptr var,
                                 a_scope_ptr    scope);

extern void ecil_emit_globals(an_ecil_frame *frame);

extern void ecil_const_reset(void);

extern void cir_be_warning(const char *message);

/* Function bodies (ecil_code.c). */

extern void ecil_stream_function_region(an_ecil_frame        *frame,
                                        a_memory_region_number region_number);

extern a_boolean ecil_function_is_streamed(a_routine_ptr rout);

#if CHECKING
extern size_t ecil_code_stack_high;
#endif /* CHECKING */

extern void ecil_code_reset(void);

extern void ecil_check_variable_attributes(a_variable_ptr var);

extern const char *ecil_library_builtin_name(const char *name);

/* Stream-level helpers. */

extern uint32_t ecil_intern_string(an_ecil_frame *frame,
                                   const char    *text,
                                   size_t        length);

/* The FE-facing stream API. */

extern a_boolean ecil_stream_is_active(void);

extern void ecil_stream_open(void);

extern void ecil_hand_off_region(a_memory_region_number region_number);

extern void ecil_stream_globals(void);

extern void ecil_stream_finish(void);

extern void ecil_abort(void);

extern void ecil_stream_finish_back_end(void);

extern void ecil_stream_close(void);

/* TRUE for -fwrapv semantics: never send the nsw flag. */
extern a_boolean ecil_wrapv;

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
