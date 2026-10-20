/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*

ecil_write.c -- The ECIL stream writer: tag encoders, the frame builder, and
                the stream state machine (header, globals, end, abort).

The stream is forward-only and holds no pointer values.  Ids are dense
integers assigned in order of first use, strings are interned by content in
insertion order, so the output is deterministic (spec rule 7).

Function regions are handed off one at a time (ecil_hand_off_region, from
the memory-region hooks) and become F_REGION frames; the file-scope region
becomes F_GLOBALS; the stream ends with F_END, or F_ABORT after an error.
The translation of function bodies, types and constants is in ecil_code.c,
ecil_type.c and ecil_const.c.

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
#include "cir_be.h"

#if ECIL_WRITER_ENABLED

/* Conditionally open the "edg" namespace. */
BEGIN_EDG_NAMESPACE

/* The states of the stream. */
enum an_ecil_stream_state {
  ess_closed,     /* No stream, or the stream was closed. */
  ess_open,       /* The preamble and F_HEADER have been submitted. */
  ess_ended,      /* F_END has been submitted. */
  ess_aborted,    /* F_ABORT has been submitted. */
  ess_failed      /* The stream could not be opened or broke. */
};

#if CHECKING
/* Below this many G-ids the table moves at every new one (see
   ecil_new_gid). */
#define ECIL_GID_MOVE_LIMIT 160u

/* A table a move leaves is freed (AddressSanitizer reports a stale use).
   Under ECIL_GID_STRESS the whole table is filled with this byte and kept
   (the last ECIL_RETIRED_TABLES of them) and verified, so that a write
   through a stale pointer changes the pattern, which
   ecil_check_retired_tables notices.  The pattern alternates between this
   byte and its complement from one entry to the next, so that a write that
   clears a flag (a no-op on a poison bit that is already clear) is seen on
   half of the entries and a write that sets one on the other half. */
#define ECIL_POISON_BYTE 0xA5
#define ECIL_RETIRED_TABLES 32

typedef struct an_ecil_retired_table {
  unsigned char *table;
  size_t        bytes;
} an_ecil_retired_table;
#endif /* CHECKING */

/* An entry of the string table (a copy of the interned text). */
typedef struct an_ecil_string {
  char          *text;
  size_t        length;
  uint32_t      hash;
  a_boolean     symbol_used;      /* Taken as a symbol name. */
  uint32_t      next_suffix;      /* As a base name: the first suffix that
                                     may still be free (0: none tried). */
} an_ecil_string;

/* A word-sequence key of the structural interning table. */
typedef struct an_ecil_word_entry {
  uint32_t      hash;
  size_t        offset;           /* Into the word arena. */
  size_t        count;
  uint32_t      gid;
} an_ecil_word_entry;

/* A pointer-keyed id table (test builds without the prefix id field). */
typedef struct an_ecil_id_map {
  void          **keys;
  uint32_t      *values;
  size_t        capacity;         /* A power of two, or 0. */
  size_t        count;
} an_ecil_id_map;

/*
The writer state.  There is one stream per compilation.
*/
typedef struct an_ecil_stream {
  an_ecil_sink  *sink;
  enum an_ecil_stream_state
                state;
  unsigned long num_frames;       /* Frames submitted so far. */
  an_ecil_string
                *strings;         /* strings[sid - 1] */
  uint32_t      num_strings;
  uint32_t      strings_capacity;
  uint32_t      *string_slots;    /* Open-addressing table of sids. */
  uint32_t      num_string_slots; /* A power of two, or 0. */
  an_ecil_gid_info
                *gids;            /* gids[gid]; entry 0 is unused. */
  uint32_t      num_gids;         /* The last G-id assigned. */
  uint32_t      gids_capacity;
  uint32_t      *word_arena;
  size_t        word_arena_size;
  size_t        word_arena_capacity;
  an_ecil_word_entry
                *word_entries;
  size_t        num_word_entries;
  size_t        word_entries_capacity;
  size_t        *word_slots;      /* Open addressing; index + 1. */
  size_t        num_word_slots;   /* A power of two, or 0. */
  a_boolean     header_sent;      /* F_HEADER has been submitted. */
  uint32_t      lid_base;         /* L-ids assigned before the current
                                     function (see ecil_lid_of). */
  an_ecil_frame work_frame;       /* The frame of region hand-offs, reused. */
  a_boolean     work_frame_ready;
#if CHECKING
  uint32_t      gids_introduced;  /* The highest G-id introduced by a record
                                     written so far. */
  uint32_t      sids_introduced;
  uint32_t      lids_introduced;  /* In the current function. */
  a_boolean     gid_stress;       /* Move the G-id table at every new G-id
                                     (finds stale an_ecil_gid_info
                                     pointers). */
  an_ecil_retired_table
                retired[ECIL_RETIRED_TABLES];
                                  /* The last tables the G-id table moved
                                     from, kept filled with the poison
                                     pattern. */
  unsigned int  num_retired;
#endif /* CHECKING */
  an_ecil_id_map
                gid_map;          /* Test builds only. */
  an_ecil_id_map
                lid_map;          /* Test builds only. */
} an_ecil_stream;

STATIC_THREAD an_ecil_stream
                ecil_stream;

/*
The producer name written to HDR_PRODUCER.
*/
#define ECIL_PRODUCER_NAME \
  "EDG C/C++ front end " VERSION_NUMBER " (ECIL writer)"

STATIC_THREAD a_boolean
                ecil_triple_guarded;  /* Look macros up only once the file
                                         scope exists (see below). */

static a_boolean ecil_predefined_macro_is_defined(const char *name)
/*
Return TRUE if the preprocessor has a macro of that name: the predefined
macros are the front end's own description of the target (they come from
the target configuration's auxiliary information).  When the triple is
derived late, as a fallback (an error during start-up), the file scope of the
translation unit is created right after the predefined macros, and before
that the symbol table cannot be asked.
*/
{
  a_symbol_locator  locator;
  a_boolean         defined = FALSE;

  if (!ecil_triple_guarded ||
      (curr_translation_unit != NULL &&
       curr_translation_unit->primary_scope != NULL)) {
    clear_locator(&locator, &null_source_position);
    defined = find_macro_symbol_by_name(name, (sizeof_t)strlen(name),
                                        &locator) != NULL;
  }  /* if */
  return defined;
}  /* ecil_predefined_macro_is_defined */


/* The target triple, captured when the predefined macros were entered (see
   ecil_note_target_triple), or empty. */
STATIC_THREAD char
                ecil_triple_text[96];

void ecil_note_target_triple(void)
/*
Record the target triple, derived from the front end's target configuration:
the architecture from the targ_supports_* flags, the operating system from the
target's predefined macros (not from the host the front end was built on, so
a cross-configured front end is labelled correctly).  It is called right
after the predefined macros are entered, so that a source-level #undef or
#define of a system macro cannot change it.  The CIR library supports x86-64
Linux only and rejects any other triple, including the "unknown" one written
when the target defines none of the known system macros; a front end for
another target still writes a valid stream, so the writer-only test loop
works there, but its output is not authoritative.
*/
{
  const char  *arch;
  const char  *os;

  if (target_is_arm_based()) {
    arch = target_is_64_bits() ? "aarch64" : "armv7";
  } else if (target_is_riscv_based()) {
    arch = target_is_64_bits() ? "riscv64" : "riscv32";
  } else {
    arch = target_is_64_bits() ? "x86_64" : "i686";
  }  /* if */
  if (ecil_predefined_macro_is_defined("__linux__")) {
    os = "unknown-linux-gnu";
  } else if (ecil_predefined_macro_is_defined("__APPLE__")) {
    os = "apple-macosx";
  } else if (ecil_predefined_macro_is_defined("_WIN32")) {
    os = "pc-windows-msvc";
  } else {
    os = "unknown-unknown";
  }  /* if */
  (void)snprintf(ecil_triple_text, sizeof(ecil_triple_text), "%s-%s", arch,
                 os);
}  /* ecil_note_target_triple */


static const char *ecil_target_triple(void)
/*
The target triple of the stream: the one captured when the predefined macros
were entered.  If that did not happen (an error during start-up, a test
driver) it is derived now.
*/
{
  if (ecil_triple_text[0] == '\0') {
    ecil_triple_guarded = TRUE;
    ecil_note_target_triple();
    ecil_triple_guarded = FALSE;
  }  /* if */
  return ecil_triple_text;
}  /* ecil_target_triple */

/*
TRUE if signed overflow wraps (-fwrapv semantics): no operation is then sent
with the nsw flag and HDR_LANG carries the wrapv flag.  The front end has no
option for it yet (a cmd_line.c option would set this); the default is the
C rule, signed overflow is undefined.  CHECKING builds also honor the
environment variable EDG_CIR_WRAPV, for experiments.
*/
#ifndef ECIL_DEFAULT_WRAPV
#define ECIL_DEFAULT_WRAPV FALSE
#endif /* ifndef ECIL_DEFAULT_WRAPV */
a_boolean ecil_wrapv = ECIL_DEFAULT_WRAPV;


a_boolean ecil_has_exceptions(void)
/*
Return TRUE if the stream is a C++ stream with exceptions (spec 12.6): the
front end has them enabled (--no_exceptions turns them off).
*/
{
  return !C_mode() && exceptions_enabled;
}  /* ecil_has_exceptions */


/* Buffer and tag encoders. */

void ecil_buf_init(an_ecil_buffer *buf)
/*
Initialize an empty buffer.
*/
{
  buf->data = NULL;
  buf->size = 0;
  buf->capacity = 0;
  buf->failed = FALSE;
}  /* ecil_buf_init */


void ecil_buf_free(an_ecil_buffer *buf)
/*
Release the storage of a buffer.
*/
{
  free(buf->data);
  ecil_buf_init(buf);
}  /* ecil_buf_free */


static void ecil_buf_reserve(an_ecil_buffer *buf, size_t extra)
/*
Make room for extra more bytes.  On failure the buffer is marked failed.
*/
{
  if (!buf->failed && extra > buf->capacity - buf->size) {
    size_t  new_capacity = (buf->capacity == 0) ? 256 : buf->capacity;
    char    *new_data;

    while (new_capacity - buf->size < extra) {
      if (new_capacity > ((size_t)-1) / 2) {
        new_capacity = buf->size + extra;
        break;
      }  /* if */
      new_capacity *= 2;
    }  /* while */
    new_data = (char *)realloc(buf->data, new_capacity);
    if (new_data == NULL) {
      buf->failed = TRUE;
    } else {
      buf->data = new_data;
      buf->capacity = new_capacity;
    }  /* if */
  }  /* if */
}  /* ecil_buf_reserve */


void ecil_buf_put_bytes(an_ecil_buffer *buf,
                        const void     *bytes,
                        size_t         length)
/*
Append raw bytes.
*/
{
  ecil_buf_reserve(buf, length);
  if (!buf->failed && length != 0) {
    memcpy(buf->data + buf->size, bytes, length);
    buf->size += length;
  }  /* if */
}  /* ecil_buf_put_bytes */


static void ecil_buf_put_byte(an_ecil_buffer *buf, unsigned int byte)
/*
Append one byte.
*/
{
  ecil_buf_reserve(buf, 1);
  if (!buf->failed) {
    buf->data[buf->size] = (char)(unsigned char)byte;
    buf->size++;
  }  /* if */
}  /* ecil_buf_put_byte */


static void ecil_buf_put_le(an_ecil_buffer *buf,
                            uint64_t       value,
                            unsigned int   num_bytes)
/*
Append the low num_bytes bytes of value, least significant first.
*/
{
  unsigned int  i;

  for (i = 0; i < num_bytes; i++) {
    ecil_buf_put_byte(buf, (unsigned int)((value >> (8 * i)) & 0xFF));
  }  /* for */
}  /* ecil_buf_put_le */


static char *ecil_buf_room(an_ecil_buffer *buf, size_t length)
/*
Make room for length more bytes and return the address to write them at, or
NULL if the buffer failed.  The caller advances buf->size.
*/
{
  ecil_buf_reserve(buf, length);
  return buf->failed ? NULL : buf->data + buf->size;
}  /* ecil_buf_room */


static void ecil_buf_put_tagged(an_ecil_buffer *buf,
                                unsigned int   tag,
                                uint64_t       value,
                                unsigned int   num_bytes)
/*
Append a tag byte and the low num_bytes bytes of value (at most 8), with one
check for room.
*/
{
  char          *p = ecil_buf_room(buf, 9);
  unsigned int  i;

  if (p != NULL) {
    p[0] = (char)(unsigned char)tag;
    for (i = 0; i < num_bytes; i++) {
      p[1 + i] = (char)(unsigned char)((value >> (8 * i)) & 0xFF);
    }  /* for */
    buf->size += 1 + num_bytes;
  }  /* if */
}  /* ecil_buf_put_tagged */


void ecil_put_uint_slow(an_ecil_buffer *buf, uint64_t value)
/*
Append an unsigned integer in its shortest encoding (the general path of the
inline ecil_put_uint: the buffer may have to grow or may have failed).
*/
{
  if (value <= ECIL_TAG_FIXUINT_MAX) {
    ecil_buf_put_byte(buf, (unsigned int)value);
  } else if (value <= 0xFF) {
    ecil_buf_put_tagged(buf, ECIL_TAG_U8, value, 1);
  } else if (value <= 0xFFFF) {
    ecil_buf_put_tagged(buf, ECIL_TAG_U16, value, 2);
  } else if (value <= 0xFFFFFFFFu) {
    ecil_buf_put_tagged(buf, ECIL_TAG_U32, value, 4);
  } else {
    ecil_buf_put_tagged(buf, ECIL_TAG_U64, value, 8);
  }  /* if */
}  /* ecil_put_uint_slow */


void ecil_put_int(an_ecil_buffer *buf, int64_t value)
/*
Append a signed integer in its shortest encoding.  Non-negative values use
the unsigned encodings (readers accept any width).
*/
{
  if (value >= 0) {
    ecil_put_uint(buf, (uint64_t)value);
  } else if (value >= -32) {
    ecil_buf_put_byte(buf, (unsigned int)(256 + value));
  } else if (value >= -128) {
    ecil_buf_put_tagged(buf, ECIL_TAG_I8, (uint64_t)value, 1);
  } else if (value >= -32768) {
    ecil_buf_put_tagged(buf, ECIL_TAG_I16, (uint64_t)value, 2);
  } else if (value >= -2147483647 - 1) {
    ecil_buf_put_tagged(buf, ECIL_TAG_I32, (uint64_t)value, 4);
  } else {
    ecil_buf_put_tagged(buf, ECIL_TAG_I64, (uint64_t)value, 8);
  }  /* if */
}  /* ecil_put_int */


void ecil_put_nil(an_ecil_buffer *buf)
/*
Append nil.
*/
{
  ecil_buf_put_byte(buf, ECIL_TAG_NIL);
}  /* ecil_put_nil */


void ecil_put_bool(an_ecil_buffer *buf, a_boolean value)
/*
Append false or true.
*/
{
  ecil_buf_put_byte(buf, value ? ECIL_TAG_TRUE : ECIL_TAG_FALSE);
}  /* ecil_put_bool */


void ecil_put_bin(an_ecil_buffer *buf, const void *bytes, size_t length)
/*
Append a byte string (fixbin, bin8, bin16 or bin32).
*/
{
  if (length < 32) {
    ecil_buf_put_byte(buf, ECIL_TAG_FIXBIN + (unsigned int)length);
  } else if (length <= 0xFF) {
    ecil_buf_put_byte(buf, ECIL_TAG_BIN8);
    ecil_buf_put_le(buf, length, 1);
  } else if (length <= 0xFFFF) {
    ecil_buf_put_byte(buf, ECIL_TAG_BIN16);
    ecil_buf_put_le(buf, length, 2);
  } else {
    ecil_buf_put_byte(buf, ECIL_TAG_BIN32);
    ecil_buf_put_le(buf, length, 4);
  }  /* if */
  ecil_buf_put_bytes(buf, bytes, length);
}  /* ecil_put_bin */


void ecil_put_f32_bits(an_ecil_buffer *buf, uint32_t bits)
/*
Append an IEEE single, given as its bit pattern.
*/
{
  ecil_buf_put_byte(buf, ECIL_TAG_F32);
  ecil_buf_put_le(buf, bits, 4);
}  /* ecil_put_f32_bits */


void ecil_put_f64_bits(an_ecil_buffer *buf, uint64_t bits)
/*
Append an IEEE double, given as its bit pattern.
*/
{
  ecil_buf_put_byte(buf, ECIL_TAG_F64);
  ecil_buf_put_le(buf, bits, 8);
}  /* ecil_put_f64_bits */


void ecil_put_fbits(an_ecil_buffer      *buf,
                    const unsigned char *little_endian_bytes,
                    unsigned int        length)
/*
Append a target float bit pattern of length bytes (x87 80-bit: 10).
*/
{
  ecil_buf_put_byte(buf, ECIL_TAG_FBITS);
  ecil_buf_put_byte(buf, length);
  ecil_buf_put_bytes(buf, little_endian_bytes, length);
}  /* ecil_put_fbits */


void ecil_put_u128(an_ecil_buffer *buf, uint64_t low, uint64_t high)
/*
Append a 128-bit unsigned integer.
*/
{
  ecil_buf_put_byte(buf, ECIL_TAG_U128);
  ecil_buf_put_le(buf, low, 8);
  ecil_buf_put_le(buf, high, 8);
}  /* ecil_put_u128 */


void ecil_put_i128(an_ecil_buffer *buf, uint64_t low, uint64_t high)
/*
Append a 128-bit signed integer, given as two's complement halves.
*/
{
  ecil_buf_put_byte(buf, ECIL_TAG_I128);
  ecil_buf_put_le(buf, low, 8);
  ecil_buf_put_le(buf, high, 8);
}  /* ecil_put_i128 */


void ecil_put_uint_array(an_ecil_buffer *buf,
                         const uint32_t *elements,
                         size_t         count)
/*
Append a flat array of unsigned integers (ids).
*/
{
  size_t  i;

  if (count < 32) {
    ecil_buf_put_byte(buf, ECIL_TAG_FIXARRAY + (unsigned int)count);
  } else if (count <= 0xFFFF) {
    ecil_buf_put_byte(buf, ECIL_TAG_ARR16);
    ecil_buf_put_le(buf, count, 2);
  } else {
    ecil_buf_put_byte(buf, ECIL_TAG_ARR32);
    ecil_buf_put_le(buf, count, 4);
  }  /* if */
  for (i = 0; i < count; i++) {
    ecil_put_uint(buf, elements[i]);
  }  /* for */
}  /* ecil_put_uint_array */


int ecil_opcode_arity(unsigned int opcode)
/*
Return the operand count of an opcode, or -1 if it is not assigned.
*/
{
  int   arity = -1;

#define ECIL_ARITY_CASE(name, value, count) \
  case value: arity = count; break;
  switch (opcode) {
    ECIL_OPCODES(ECIL_ARITY_CASE)
    default:
      break;
  }  /* switch */
#undef ECIL_ARITY_CASE
  return arity;
}  /* ecil_opcode_arity */


/* Frame builder. */

void ecil_frame_init(an_ecil_frame *frame, unsigned int kind)
/*
Initialize a frame of the given kind, with the declaration section current.
*/
{
  ecil_buf_init(&frame->decl);
  ecil_buf_init(&frame->body);
  frame->current = &frame->decl;
  frame->kind = kind;
  frame->flags = 0;
  frame->pending_operands = -1;
#if CHECKING
  frame->intro_kind = 0;
#endif /* CHECKING */
}  /* ecil_frame_init */


void ecil_frame_reset(an_ecil_frame *frame, unsigned int kind)
/*
Empty a frame for reuse, keeping its storage unless a section grew beyond
ECIL_FRAME_KEEP_BYTES (one huge function must not pin its memory for the rest
of the compilation).
*/
{
  if (frame->decl.capacity > ECIL_FRAME_KEEP_BYTES) {
    ecil_buf_free(&frame->decl);
  }  /* if */
  if (frame->body.capacity > ECIL_FRAME_KEEP_BYTES) {
    ecil_buf_free(&frame->body);
  }  /* if */
  frame->decl.size = 0;
  frame->decl.failed = FALSE;
#if CHECKING
  frame->intro_kind = eik_none;
#endif /* CHECKING */
  frame->body.size = 0;
  frame->body.failed = FALSE;
  frame->current = &frame->decl;
  frame->kind = kind;
  frame->flags = 0;
  frame->pending_operands = -1;
}  /* ecil_frame_reset */


void ecil_frame_free(an_ecil_frame *frame)
/*
Release the storage of a frame.
*/
{
  ecil_buf_free(&frame->decl);
  ecil_buf_free(&frame->body);
  frame->current = &frame->decl;
}  /* ecil_frame_free */


void ecil_frame_begin_body(an_ecil_frame *frame)
/*
Make the body section current.  Records until the next frame reset go there.
*/
{
  check_assertion(frame->pending_operands == -1);
  frame->current = &frame->body;
}  /* ecil_frame_begin_body */


an_ecil_buffer *ecil_frame_enter_decl(an_ecil_frame *frame)
/*
Make the declaration section current; return the section that was current.
*/
{
  an_ecil_buffer  *saved = frame->current;

  check_assertion(frame->pending_operands == -1);
  frame->current = &frame->decl;
  return saved;
}  /* ecil_frame_enter_decl */


void ecil_frame_leave_decl(an_ecil_frame *frame, an_ecil_buffer *saved)
/*
Return to the section that was current before ecil_frame_enter_decl.
*/
{
  frame->current = saved;
}  /* ecil_frame_leave_decl */


an_ecil_mark ecil_frame_mark(const an_ecil_frame *frame)
/*
Return the current position of a frame.
*/
{
  an_ecil_mark  mark;

  mark.body_size = frame->body.size;
  mark.in_body = (frame->current == &frame->body);
  return mark;
}  /* ecil_frame_mark */


void ecil_frame_truncate(an_ecil_frame *frame, an_ecil_mark mark)
/*
Abandon the body records written after mark.  The declaration section is
kept whole (spec 8.11): introductions made there while the body was being
translated (types, strings, declarations) are already known to the id and
string tables, so dropping them would leave later references dangling.
*/
{
  check_assertion(frame->pending_operands == -1);
  frame->body.size = mark.body_size;
  frame->current = mark.in_body ? &frame->body : &frame->decl;
}  /* ecil_frame_truncate */


#if CHECKING

/* Per-opcode tables for the record checks: the operand count and what the
   first operand introduces.  Built on first use from ecil_opcode_arity. */
signed char     ecil_arity_table[256];
unsigned char   ecil_intro_table[256];
a_boolean       ecil_tables_ready;

void ecil_build_check_tables(void)
/*
Build the per-opcode tables of the record checks.
*/
{
  unsigned int  opcode;

  for (opcode = 0; opcode < 256; opcode++) {
    ecil_arity_table[opcode] = (signed char)ecil_opcode_arity(opcode);
    ecil_intro_table[opcode] = (unsigned char)eik_none;
  }  /* for */
  ecil_intro_table[eop_str] = (unsigned char)eik_sid;
  ecil_intro_table[eop_ty_void] = (unsigned char)eik_gid;
  ecil_intro_table[eop_ty_bool] = (unsigned char)eik_gid;
  ecil_intro_table[eop_ty_int] = (unsigned char)eik_gid;
  ecil_intro_table[eop_ty_float] = (unsigned char)eik_gid;
  ecil_intro_table[eop_ty_ptr] = (unsigned char)eik_gid;
  ecil_intro_table[eop_ty_array] = (unsigned char)eik_gid;
  ecil_intro_table[eop_ty_func] = (unsigned char)eik_gid;
  ecil_intro_table[eop_ty_record_decl] = (unsigned char)eik_gid;
  ecil_intro_table[eop_ty_opaque] = (unsigned char)eik_gid;
  ecil_intro_table[eop_ty_vptr] = (unsigned char)eik_gid;
  ecil_intro_table[eop_ty_data_member] = (unsigned char)eik_gid;
  ecil_intro_table[eop_ty_method] = (unsigned char)eik_gid;
  ecil_intro_table[eop_decl_abi_data] = (unsigned char)eik_gid_or_reissue;
  ecil_intro_table[eop_decl_func] = (unsigned char)eik_gid_or_reissue;
  ecil_intro_table[eop_decl_var] = (unsigned char)eik_gid_or_reissue;
  ecil_intro_table[eop_ty_record_body] = (unsigned char)eik_gid_existing;
  ecil_intro_table[eop_local] = (unsigned char)eik_lid;
  ecil_intro_table[eop_param] = (unsigned char)eik_lid;
  ecil_intro_table[eop_materialize_temp] = (unsigned char)eik_lid;
  ecil_intro_table[eop_label] = (unsigned char)eik_lid;
  ecil_intro_table[eop_func_begin] = (unsigned char)eik_function;
  ecil_tables_ready = TRUE;
}  /* ecil_build_check_tables */


void ecil_check_introduction(unsigned int kind, uint64_t id)
/*
Account for the id a record introduces (see ecil_check_all_introduced).  A
new G-id or sid must be the highest introduced so far plus 1 (spec 7).
*/
{
  an_ecil_stream  *stream = &ecil_stream;

  switch (kind) {
    case eik_sid:
      check_assertion(id == (uint64_t)stream->sids_introduced + 1);
      stream->sids_introduced++;
      break;
    case eik_gid:
      check_assertion(id == (uint64_t)stream->gids_introduced + 1);
      stream->gids_introduced++;
      break;
    case eik_gid_or_reissue:
      if (id == (uint64_t)stream->gids_introduced + 1) {
        stream->gids_introduced++;
      } else {
        check_assertion(id != 0 && id <= stream->gids_introduced);
      }  /* if */
      break;
    case eik_gid_existing:
      check_assertion(id != 0 && id <= stream->gids_introduced);
      break;
    case eik_lid:
      stream->lids_introduced++;
      break;
    case eik_function:
      stream->lids_introduced = 0;
      break;
    default:
      break;
  }  /* switch */
}  /* ecil_check_introduction */


void ecil_check_begin_lids(void)
/*
A STATIC_INIT of the file scope starts a fresh L-id space (spec 11.7).
*/
{
  ecil_stream.lids_introduced = 0;
}  /* ecil_check_begin_lids */


void ecil_check_function_lids(uint32_t num_lids)
/*
At FUNC_END: the function allocated num_lids L-ids and every one of them was
introduced (a forward goto's label counts when its LABEL was written).
*/
{
  check_assertion(ecil_stream.lids_introduced == num_lids);
}  /* ecil_check_function_lids */


void ecil_check_all_introduced(void)
/*
At F_END: every G-id and sid the writer allocated was introduced.
*/
{
  check_assertion(ecil_stream.gids_introduced == ecil_stream.num_gids);
  check_assertion(ecil_stream.sids_introduced == ecil_stream.num_strings);
}  /* ecil_check_all_introduced */

#endif /* CHECKING */


/* Record helpers. */

/* Open scope regions and their cleanups (the jump check). */

a_boolean ecil_jump_check_enabled = FALSE;

typedef struct a_region_entry {
  unsigned int  cleanups;         /* CLEANUP_BEGIN records in the region. */
  a_boolean     is_switch;        /* The region is a switch's body. */
} a_region_entry;

STATIC_THREAD a_region_entry
                *region_stack;
STATIC_THREAD size_t
                region_stack_size;
STATIC_THREAD size_t
                region_stack_capacity;

static void ecil_push_region(a_boolean is_switch)
/*
Open a scope region.  If memory runs out the check is turned off for the rest
of the function (the check only diagnoses; it never changes the stream).
*/
{
  if (region_stack_size == region_stack_capacity) {
    size_t          new_capacity = region_stack_capacity == 0 ? 64 :
                                   region_stack_capacity * 2;
    a_region_entry  *new_stack = (a_region_entry *)realloc(region_stack,
                                  new_capacity * sizeof(a_region_entry));

    if (new_stack == NULL) {
      ecil_jump_check_enabled = FALSE;
    } else {
      region_stack = new_stack;
      region_stack_capacity = new_capacity;
    }  /* if */
  }  /* if */
  if (ecil_jump_check_enabled) {
    region_stack[region_stack_size].cleanups = 0;
    region_stack[region_stack_size].is_switch = is_switch;
    region_stack_size++;
  }  /* if */
}  /* ecil_push_region */


static void ecil_pop_region(void)
/*
Close the innermost scope region.
*/
{
  if (region_stack_size > 0) region_stack_size--;
}  /* ecil_pop_region */


static unsigned int ecil_open_cleanups(a_boolean down_to_switch)
/*
The number of cleanups begun in the open regions: those from the innermost
switch's body inwards, or all of them.
*/
{
  unsigned int  count = 0;
  size_t        i = region_stack_size;

  while (i > 0) {
    i--;
    count += region_stack[i].cleanups;
    if (down_to_switch && region_stack[i].is_switch) break;
  }  /* while */
  return count;
}  /* ecil_open_cleanups */


static void ecil_track_regions(unsigned int opcode)
/*
Follow the scope regions that the body records open and close.  A case or
default label is reached by a jump from the head of its switch, and a label
may be reached by a jump from outside the cleanups that are open at it: under
the rules of cfront 2.1 the front end accepts that jump (past the
initialization of an object with a destructor), but the cleanup would run for
an object that was not constructed, so it is diagnosed.
*/
{
  switch (opcode) {
    case eop_func_begin:
      region_stack_size = 0;
      ecil_push_region(FALSE);
      break;
    case eop_static_init_begin:
      /* The initialization of a static local is a region inside the body. */
      ecil_push_region(FALSE);
      break;
    case eop_block_begin:
    case eop_if_begin:
    case eop_while_body:
    case eop_do_begin:
    case eop_for_begin:
    case eop_for_body:
    case eop_fullexpr_begin:
    case eop_meminit_begin:
    case eop_array_ctor_begin:
      ecil_push_region(FALSE);
      break;
    case eop_switch_begin:
      ecil_push_region(TRUE);
      break;
    case eop_if_else:
      ecil_pop_region();
      ecil_push_region(FALSE);
      break;
    case eop_block_end:
    case eop_if_end:
    case eop_while_end:
    case eop_do_cond:
    case eop_switch_end:
    case eop_fullexpr_end:
    case eop_meminit_end:
    case eop_array_ctor_end:
    case eop_static_init_end:
      ecil_pop_region();
      break;
    case eop_for_end:
      ecil_pop_region();                /* The body, then the init scope. */
      ecil_pop_region();
      break;
    case eop_cleanup_begin:
      if (region_stack_size > 0) {
        region_stack[region_stack_size - 1].cleanups++;
      }  /* if */
      break;
    case eop_case_value:
    case eop_case_range:
    case eop_case_default:
      if (ecil_open_cleanups(TRUE) != 0) {
        ecil_note_unsupported("a case label after a declaration of an object "
                              "with a destructor (jumping past it is "
                              "accepted in cfront mode)");
      }  /* if */
      break;
    case eop_label:
      if (ecil_open_cleanups(FALSE) != 0) {
        ecil_note_unsupported("a label after a declaration of an object with "
                              "a destructor (a jump past it is accepted in "
                              "cfront mode)");
      }  /* if */
      break;
    default:
      break;
  }  /* switch */
}  /* ecil_track_regions */


void ecil_rec_begin_slow(an_ecil_frame *frame, unsigned int opcode)
/*
Start a record: write its opcode byte.  (ecil_rec_begin is inline and comes
here when a table is not built yet, the buffer must grow or the scope regions
are followed.)
*/
{
  check_assertion(frame->pending_operands == -1);
#if CHECKING
  {
    int  arity;

    if (!ecil_tables_ready) ecil_build_check_tables();
    arity = ecil_arity_table[opcode & 0xFF];
    check_assertion(arity >= 0 && opcode < 256);
    frame->pending_operands = arity;
    frame->intro_kind = ecil_intro_table[opcode];
  }
#endif /* CHECKING */
  if (ecil_jump_check_enabled && frame->current == &frame->body) {
    ecil_track_regions(opcode);
  }  /* if */
  ecil_buf_put_byte(frame->current, opcode);
}  /* ecil_rec_begin_slow */


#if CHECKING
static void ecil_note_operand(an_ecil_frame *frame)
/*
Account for one operand of the open record.
*/
{
  check_assertion(frame->pending_operands > 0);
  frame->pending_operands--;
}  /* ecil_note_operand */
#else /* !CHECKING */
#define ecil_note_operand(frame) ((void)0)
#endif /* CHECKING */


#if CHECKING
void ecil_arg_uint_slow(an_ecil_frame *frame, uint64_t value)
/*
Append an unsigned operand (the general path of the inline ecil_arg_uint: the
operand introduces an id, or the record has no operand left).
*/
{
#if CHECKING
  if (frame->intro_kind != eik_none) {
    ecil_check_introduction(frame->intro_kind, value);
    frame->intro_kind = eik_none;
  }  /* if */
#endif /* CHECKING */
  ecil_note_operand(frame);
  ecil_put_uint(frame->current, value);
}  /* ecil_arg_uint_slow */
#endif /* CHECKING */


void ecil_arg_int(an_ecil_frame *frame, int64_t value)
/*
Append a signed operand.
*/
{
  ecil_note_operand(frame);
  ecil_put_int(frame->current, value);
}  /* ecil_arg_int */


void ecil_arg_nil(an_ecil_frame *frame)
/*
Append a nil operand.
*/
{
  ecil_note_operand(frame);
  ecil_put_nil(frame->current);
}  /* ecil_arg_nil */


void ecil_arg_bool(an_ecil_frame *frame, a_boolean value)
/*
Append a boolean operand.
*/
{
  ecil_note_operand(frame);
  ecil_put_bool(frame->current, value);
}  /* ecil_arg_bool */


void ecil_arg_bin(an_ecil_frame *frame, const void *bytes, size_t length)
/*
Append a byte-string operand.
*/
{
  ecil_note_operand(frame);
  ecil_put_bin(frame->current, bytes, length);
}  /* ecil_arg_bin */


void ecil_arg_array(an_ecil_frame  *frame,
                    const uint32_t *elements,
                    size_t         count)
/*
Append a flat id-array operand.
*/
{
  ecil_note_operand(frame);
  ecil_put_uint_array(frame->current, elements, count);
}  /* ecil_arg_array */


void ecil_arg_f32_bits(an_ecil_frame *frame, uint32_t bits)
/*
Append an IEEE single operand.
*/
{
  ecil_note_operand(frame);
  ecil_put_f32_bits(frame->current, bits);
}  /* ecil_arg_f32_bits */


void ecil_arg_f64_bits(an_ecil_frame *frame, uint64_t bits)
/*
Append an IEEE double operand.
*/
{
  ecil_note_operand(frame);
  ecil_put_f64_bits(frame->current, bits);
}  /* ecil_arg_f64_bits */


void ecil_arg_fbits(an_ecil_frame       *frame,
                    const unsigned char *little_endian_bytes,
                    unsigned int        length)
/*
Append a target-format float operand (FBITS).
*/
{
  ecil_note_operand(frame);
  ecil_put_fbits(frame->current, little_endian_bytes, length);
}  /* ecil_arg_fbits */


void ecil_arg_int128(an_ecil_frame *frame,
                     uint64_t      low,
                     uint64_t      high,
                     a_boolean     is_signed)
/*
Append a 128-bit integer operand.
*/
{
  ecil_note_operand(frame);
  if (is_signed) {
    ecil_put_i128(frame->current, low, high);
  } else {
    ecil_put_u128(frame->current, low, high);
  }  /* if */
}  /* ecil_arg_int128 */


void ecil_supp_begin(an_ecil_supp *supp)
/*
Start a supp blob (spec 6).
*/
{
  ecil_buf_init(&supp->payload);
  supp->num_pairs = 0;
  supp->last_key = 0;
}  /* ecil_supp_begin */


static void ecil_supp_key(an_ecil_supp *supp, an_ecil_supp_key key)
/*
Write the key of the next pair.  The keys of a blob increase, so that none
repeats.
*/
{
  check_assertion((unsigned int)key > supp->last_key);
  supp->last_key = (unsigned int)key;
  supp->num_pairs++;
  ecil_put_uint(&supp->payload, (uint64_t)key);
}  /* ecil_supp_key */


void ecil_supp_true(an_ecil_supp *supp, an_ecil_supp_key key)
/*
Add a flag-like pair: the key with the value true.
*/
{
  ecil_supp_key(supp, key);
  ecil_put_bool(&supp->payload, TRUE);
}  /* ecil_supp_true */


void ecil_supp_uint(an_ecil_supp *supp, an_ecil_supp_key key, uint64_t value)
/*
Add a pair with an unsigned integer value.
*/
{
  ecil_supp_key(supp, key);
  ecil_put_uint(&supp->payload, value);
}  /* ecil_supp_uint */


static size_t ecil_sanitize_utf8(char *to, const char *from, size_t max);

void ecil_supp_text(an_ecil_supp *supp, an_ecil_supp_key key, const char *text)
/*
Add a pair whose value is the text, as a bin of at most 255 bytes (UTF-8, cut
at a character boundary).
*/
{
  char    clean[256];
  size_t  length = ecil_sanitize_utf8(clean, text, 255);

  ecil_supp_key(supp, key);
  ecil_put_bin(&supp->payload, clean, length);
}  /* ecil_supp_text */


void ecil_rec_supp_end(an_ecil_frame *frame, an_ecil_supp *supp)
/*
Append the blob to the record just written: the tag with the length of the
payload (supp8 if it fits in one byte, else supp16), then the pairs.
*/
{
  size_t  payload = supp->payload.size;

  check_assertion(frame->pending_operands == 0 ||
                  frame->pending_operands == -1);
  if (supp->payload.failed) {
    frame->current->failed = TRUE;
  } else if (supp->num_pairs != 0) {
    frame->flags |= ECIL_FRAME_FLAG_HAS_SUPP;
    if (payload <= 0xFF) {
      ecil_buf_put_byte(frame->current, ECIL_TAG_SUPP8);
      ecil_buf_put_le(frame->current, payload, 1);
    } else {
      ecil_buf_put_byte(frame->current, ECIL_TAG_SUPP16);
      ecil_buf_put_le(frame->current, payload, 2);
    }  /* if */
    ecil_buf_put_bytes(frame->current, supp->payload.data, payload);
  }  /* if */
  ecil_buf_free(&supp->payload);
  supp->num_pairs = 0;
  supp->last_key = 0;
}  /* ecil_rec_supp_end */


/* Strings and types. */

static uint32_t ecil_hash_bytes(const char *text, size_t length)
/*
FNV-1a over the content; used only to index the string table, never to
order output.
*/
{
  uint32_t  hash = 2166136261u;
  size_t    i;

  for (i = 0; i < length; i++) {
    hash ^= (unsigned char)text[i];
    hash *= 16777619u;
  }  /* for */
  return hash;
}  /* ecil_hash_bytes */


static a_boolean ecil_string_table_grow(an_ecil_stream *stream)
/*
Double the open-addressing table and reinsert every sid.  Return FALSE on
allocation failure.
*/
{
  uint32_t  new_count = (stream->num_string_slots == 0) ?
                                  64 : stream->num_string_slots * 2;
  uint32_t  *new_slots = (uint32_t *)calloc(new_count, sizeof(uint32_t));
  uint32_t  sid;

  if (new_slots != NULL) {
    for (sid = 1; sid <= stream->num_strings; sid++) {
      uint32_t  slot = stream->strings[sid - 1].hash & (new_count - 1);

      while (new_slots[slot] != 0) slot = (slot + 1) & (new_count - 1);
      new_slots[slot] = sid;
    }  /* for */
    free(stream->string_slots);
    stream->string_slots = new_slots;
    stream->num_string_slots = new_count;
  }  /* if */
  return new_slots != NULL;
}  /* ecil_string_table_grow */


uint32_t ecil_intern_string(an_ecil_frame *frame,
                            const char    *text,
                            size_t        length)
/*
Return the sid of the string, introducing it with a STR record in the
declaration section of frame if it is new.  Sids are assigned in order of
first use.
*/
{
  an_ecil_stream  *stream = &ecil_stream;
  uint32_t        hash = ecil_hash_bytes(text, length);
  uint32_t        sid = 0;
  uint32_t        slot;

  if (stream->num_string_slots != 0) {
    slot = hash & (stream->num_string_slots - 1);
    while (stream->string_slots[slot] != 0) {
      an_ecil_string  *s = &stream->strings[stream->string_slots[slot] - 1];

      if (s->hash == hash && s->length == length &&
          (length == 0 || memcmp(s->text, text, length) == 0)) {
        sid = stream->string_slots[slot];
        break;
      }  /* if */
      slot = (slot + 1) & (stream->num_string_slots - 1);
    }  /* while */
  }  /* if */
  if (sid == 0) {
    char  *copy = (char *)malloc(length + 1);  /* NUL-terminated. */

    if (copy != NULL &&
        stream->num_strings == stream->strings_capacity) {
      uint32_t        new_capacity = (stream->strings_capacity == 0) ?
                                     64 : stream->strings_capacity * 2;
      an_ecil_string  *new_strings = (an_ecil_string *)realloc(
                                    stream->strings,
                                    new_capacity * sizeof(an_ecil_string));

      if (new_strings == NULL) {
        free(copy);
        copy = NULL;
      } else {
        stream->strings = new_strings;
        stream->strings_capacity = new_capacity;
      }  /* if */
    }  /* if */
    if (copy != NULL &&
        (stream->num_strings + 1) * 2 > stream->num_string_slots &&
        !ecil_string_table_grow(stream)) {
      free(copy);
      copy = NULL;
    }  /* if */
    if (copy == NULL) {
      frame->decl.failed = TRUE;
    } else {
      if (length != 0) memcpy(copy, text, length);
      copy[length] = '\0';
      stream->strings[stream->num_strings].text = copy;
      stream->strings[stream->num_strings].length = length;
      stream->strings[stream->num_strings].hash = hash;
      stream->strings[stream->num_strings].symbol_used = FALSE;
      stream->strings[stream->num_strings].next_suffix = 0;
      stream->num_strings++;
      sid = stream->num_strings;
      slot = hash & (stream->num_string_slots - 1);
      while (stream->string_slots[slot] != 0) {
        slot = (slot + 1) & (stream->num_string_slots - 1);
      }  /* while */
      stream->string_slots[slot] = sid;
      {
        an_ecil_buffer  *saved = frame->current;

        frame->current = &frame->decl;
        ecil_rec_begin(frame, eop_str);
        ecil_arg_uint(frame, sid);
        ecil_arg_bin(frame, text, length);
        ecil_rec_end(frame);
        frame->current = saved;
      }
    }  /* if */
  }  /* if */
  return sid;
}  /* ecil_intern_string */


uint32_t ecil_intern_cstring(an_ecil_frame *frame, const char *text)
/*
Intern a NUL-terminated string.
*/
{
  return ecil_intern_string(frame, text, strlen(text));
}  /* ecil_intern_cstring */


const char *ecil_string_text(uint32_t sid)
/*
Return the text of an interned string (NULL for sid 0 or an unknown sid).
The pointer is valid until the next string is interned.
*/
{
  an_ecil_stream  *stream = &ecil_stream;

  return (sid != 0 && sid <= stream->num_strings) ?
                               stream->strings[sid - 1].text : NULL;
}  /* ecil_string_text */


/* Unsupported-construct reporting. */

const char *ecil_unsupported_reason;

/* The text of the first reason, owned by the stream (callers pass literals
   and temporary buffers). */
static char     ecil_reason_text[ECIL_REASON_MAX];

size_t ecil_utf8_prefix_length(const char *text, size_t max)
/*
Return the length of the longest prefix of text that is at most max bytes
long and does not end inside a UTF-8 sequence: a cut that would split one
backs up to the start of the sequence.
*/
{
  size_t  length = strlen(text);

  if (length > max) {
    length = max;
    while (length > 0 && (((unsigned char)text[length]) & 0xC0) == 0x80) {
      length--;
    }  /* while */
  }  /* if */
  return length;
}  /* ecil_utf8_prefix_length */


static size_t ecil_utf8_sequence_length(const unsigned char *p)
/*
Return the length of the valid UTF-8 sequence that starts at p (which is
NUL-terminated), or 0 if the bytes do not form one (overlong forms, the
surrogates and values above U+10FFFF are not valid).
*/
{
  size_t  length = 0;

  if (p[0] < 0x80) {
    length = 1;
  } else if (p[0] >= 0xC2 && p[0] <= 0xDF) {
    if ((p[1] & 0xC0) == 0x80) length = 2;
  } else if (p[0] >= 0xE0 && p[0] <= 0xEF) {
    if ((p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 &&
        !(p[0] == 0xE0 && p[1] < 0xA0) && !(p[0] == 0xED && p[1] >= 0xA0)) {
      length = 3;
    }  /* if */
  } else if (p[0] >= 0xF0 && p[0] <= 0xF4) {
    if ((p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 &&
        (p[3] & 0xC0) == 0x80 && !(p[0] == 0xF0 && p[1] < 0x90) &&
        !(p[0] == 0xF4 && p[1] >= 0x90)) {
      length = 4;
    }  /* if */
  }  /* if */
  return length;
}  /* ecil_utf8_sequence_length */


static size_t ecil_sanitize_utf8(char *to, const char *from, size_t max)
/*
Copy at most max bytes of the NUL-terminated text from into to (which has
room for max + 1), replacing each byte that is not part of a valid UTF-8
sequence with '?', and cutting at a sequence boundary.  The reason strings are
optional information: an invalid byte (in a source identifier) must not make
the stream invalid.  Return the length.
*/
{
  size_t                i = 0;
  size_t                n = 0;
  const unsigned char   *p = (const unsigned char *)from;

  while (p[i] != 0 && n < max) {
    size_t  len = ecil_utf8_sequence_length(p + i);

    if (len == 0) {
      to[n++] = '?';
      i++;
    } else if (n + len > max) {
      break;
    } else {
      memcpy(to + n, p + i, len);
      n += len;
      i += len;
    }  /* if */
  }  /* while */
  to[n] = '\0';
  return n;
}  /* ecil_sanitize_utf8 */


static void ecil_copy_reason(char *to, const char *from)
/*
Copy a reason into a buffer of ECIL_REASON_MAX bytes, cut at a UTF-8
boundary if it is too long, and with invalid UTF-8 replaced.
*/
{
  (void)ecil_sanitize_utf8(to, from, ECIL_REASON_MAX - 1);
}  /* ecil_copy_reason */


void ecil_note_unsupported(const char *reason)
/*
Record why the current function or initializer cannot be expressed.  The
first reason wins; the caller of the traversal checks
ecil_unsupported_reason and discards its partial output.  The text is copied,
so the argument need not outlive the call.
*/
{
  if (ecil_unsupported_reason == NULL) {
    ecil_copy_reason(ecil_reason_text, reason);
    ecil_unsupported_reason = ecil_reason_text;
  }  /* if */
}  /* ecil_note_unsupported */


void ecil_save_reason(an_ecil_saved_reason *saved)
/*
Take the current reason (if any) aside and clear it, so that the caller can
run code that may note and discard reasons of its own.
*/
{
  saved->is_set = (ecil_unsupported_reason != NULL);
  if (saved->is_set) {
    ecil_copy_reason(saved->text, ecil_unsupported_reason);
  } else {
    saved->text[0] = '\0';
  }  /* if */
  ecil_unsupported_reason = NULL;
}  /* ecil_save_reason */


void ecil_restore_reason(const an_ecil_saved_reason *saved)
/*
Put back a reason taken aside by ecil_save_reason.  It replaces any reason
noted since, because the first reason wins.  If none was saved, a reason noted
since stays.
*/
{
  if (saved->is_set) {
    ecil_unsupported_reason = NULL;
    ecil_note_unsupported(saved->text);
  }  /* if */
}  /* ecil_restore_reason */


/* The G-id table. */

#if CHECKING

static unsigned char ecil_poison_byte(size_t offset)
/*
Return the poison byte for an offset into a retired G-id table: the pattern
alternates from one an_ecil_gid_info entry to the next.
*/
{
  return ((offset / sizeof(an_ecil_gid_info)) & 1) ?
                    (unsigned char)~ECIL_POISON_BYTE :
                    (unsigned char)ECIL_POISON_BYTE;
}  /* ecil_poison_byte */


static void ecil_poison_table(unsigned char *table, size_t bytes)
/*
Fill a table that was just retired with the poison pattern.
*/
{
  size_t  j;

  for (j = 0; j < bytes; j++) {
    table[j] = ecil_poison_byte(j);
  }  /* for */
}  /* ecil_poison_table */


static void ecil_check_retired_tables(an_ecil_stream *stream,
                                      unsigned int   first,
                                      unsigned int   end)
/*
Assert that the retired G-id tables first to end-1 still hold the poison
pattern: nothing wrote through a stale an_ecil_gid_info pointer.
*/
{
  unsigned int  i;
  size_t        j;

  for (i = first; i < end; i++) {
    for (j = 0; j < stream->retired[i].bytes; j++) {
      check_assertion(stream->retired[i].table[j] == ecil_poison_byte(j));
    }  /* for */
  }  /* for */
}  /* ecil_check_retired_tables */

#endif /* CHECKING */


uint32_t ecil_new_gid(an_ecil_gid_kind kind)
/*
Assign the next G-id (dense from 1) and record what it names.  The table may
move: an an_ecil_gid_info pointer must not be held across a call that can
assign a G-id.  CHECKING builds move the table at every new G-id while
there are few (and at every one if ECIL_GID_STRESS is set in the
environment), so that a stale pointer is a use-after-free that
AddressSanitizer reports in every small test.  Only under ECIL_GID_STRESS are
the old tables kept and verified (a stale write is invisible otherwise).
*/
{
  an_ecil_stream  *stream = &ecil_stream;
  uint32_t        gid = 0;

  if (stream->gids == NULL || stream->num_gids + 1 >= stream->gids_capacity) {
    uint32_t          new_capacity = (stream->gids_capacity == 0) ?
                                     256 : stream->gids_capacity * 2;
    an_ecil_gid_info  *new_gids = (an_ecil_gid_info *)realloc(
                                    stream->gids,
                                    new_capacity * sizeof(an_ecil_gid_info));

    if (new_gids != NULL) {
      if (stream->gids == NULL) {
        memset(&new_gids[0], 0, sizeof(an_ecil_gid_info));
      }  /* if */
      stream->gids = new_gids;
      stream->gids_capacity = new_capacity;
    }  /* if */
  }  /* if */
#if CHECKING
  if ((stream->gid_stress || stream->num_gids < ECIL_GID_MOVE_LIMIT) &&
      stream->gids != NULL) {
    size_t            bytes = stream->gids_capacity * sizeof(an_ecil_gid_info);
    an_ecil_gid_info  *moved = (an_ecil_gid_info *)malloc(bytes);

    if (moved != NULL) {
      size_t  used = ((size_t)stream->num_gids + 1) * sizeof(an_ecil_gid_info);

      memcpy(moved, stream->gids, used);
      if (!stream->gid_stress) {
        /* Plain CHECKING: free the old table (AddressSanitizer reports a
           later use of it).  O(1) amortized per G-id: the move window is
           short, and nothing is scanned or poisoned (that costs 1M
           instructions per small test under cachegrind). */
        free(stream->gids);
      } else {
        /* ECIL_GID_STRESS: keep the old tables poisoned and verify them
           (a stale write is a lost update that no ordinary run sees). */
        if (stream->num_retired != 0) {
          ecil_check_retired_tables(stream, stream->num_retired - 1,
                                    stream->num_retired);
        }  /* if */
        ecil_poison_table((unsigned char *)stream->gids, bytes);
        if (stream->num_retired == ECIL_RETIRED_TABLES) {
          ecil_check_retired_tables(stream, 0, 1);
          free(stream->retired[0].table);
          memmove(&stream->retired[0], &stream->retired[1],
                  (ECIL_RETIRED_TABLES - 1) * sizeof(an_ecil_retired_table));
          stream->num_retired--;
        }  /* if */
        stream->retired[stream->num_retired].table =
                                              (unsigned char *)stream->gids;
        stream->retired[stream->num_retired].bytes = bytes;
        stream->num_retired++;
      }  /* if */
      stream->gids = moved;
    }  /* if */
  }  /* if */
#endif /* CHECKING */
  if (stream->gids != NULL && stream->num_gids + 1 < stream->gids_capacity) {
    gid = ++stream->num_gids;
    memset(&stream->gids[gid], 0, sizeof(an_ecil_gid_info));
    stream->gids[gid].kind = kind;
  } else {
    ecil_note_unsupported("out of memory in the ECIL writer");
  }  /* if */
  return gid;
}  /* ecil_new_gid */


an_ecil_gid_info *ecil_gid_info(uint32_t gid)
/*
Return the record kept for a G-id.
*/
{
  check_assertion(gid != 0 && gid <= ecil_stream.num_gids);
  return &ecil_stream.gids[gid];
}  /* ecil_gid_info */


uint32_t ecil_num_gids(void)
/*
Return the number of G-ids assigned so far.
*/
{
  return ecil_stream.num_gids;
}  /* ecil_num_gids */


/* Structural interning of types. */

static uint32_t ecil_hash_words(const uint32_t *words, size_t count)
/*
Hash a word sequence (FNV-1a over the words).  A mixer with better low bits
was tried and measured no faster on the corpus and the generated TUs (the
tables hold some hundreds of keys, so collisions are rare).
*/
{
  uint32_t  hash = 2166136261u;
  size_t    i;

  for (i = 0; i < count; i++) {
    hash ^= words[i];
    hash *= 16777619u;
  }  /* for */
  return hash;
}  /* ecil_hash_words */


static size_t ecil_word_find_slot(const an_ecil_stream *stream,
                                  const uint32_t       *words,
                                  size_t               count,
                                  uint32_t             hash)
/*
Return the slot at which the key is, or the empty slot where it belongs.
The table must be non-empty.
*/
{
  size_t  slot = hash & (stream->num_word_slots - 1);

  while (stream->word_slots[slot] != 0) {
    const an_ecil_word_entry  *e =
                         &stream->word_entries[stream->word_slots[slot] - 1];

    if (e->hash == hash && e->count == count &&
        memcmp(stream->word_arena + e->offset, words,
               count * sizeof(uint32_t)) == 0) {
      break;
    }  /* if */
    slot = (slot + 1) & (stream->num_word_slots - 1);
  }  /* while */
  return slot;
}  /* ecil_word_find_slot */


uint32_t ecil_word_lookup(const uint32_t *words, size_t count)
/*
Return the G-id registered for the key, or 0.
*/
{
  an_ecil_stream  *stream = &ecil_stream;
  uint32_t        gid = 0;

  if (stream->num_word_slots != 0) {
    uint32_t  hash = ecil_hash_words(words, count);
    size_t    slot = ecil_word_find_slot(stream, words, count, hash);

    if (stream->word_slots[slot] != 0) {
      gid = stream->word_entries[stream->word_slots[slot] - 1].gid;
    }  /* if */
  }  /* if */
  return gid;
}  /* ecil_word_lookup */


void ecil_word_unregister(const uint32_t *words, size_t count)
/*
Forget the G-id registered for the key (it reads as 0 afterwards).  Nothing
happens if the key is not registered.
*/
{
  an_ecil_stream  *stream = &ecil_stream;

  if (stream->num_word_slots != 0) {
    uint32_t  hash = ecil_hash_words(words, count);
    size_t    slot = ecil_word_find_slot(stream, words, count, hash);

    if (stream->word_slots[slot] != 0) {
      stream->word_entries[stream->word_slots[slot] - 1].gid = 0;
    }  /* if */
  }  /* if */
}  /* ecil_word_unregister */


static a_boolean ecil_word_table_grow(an_ecil_stream *stream)
/*
Double the slot table and reinsert every entry; return FALSE on failure.
*/
{
  size_t  new_count = (stream->num_word_slots == 0) ?
                                  256 : stream->num_word_slots * 2;
  size_t  *new_slots = (size_t *)calloc(new_count, sizeof(size_t));
  size_t  i;

  if (new_slots != NULL) {
    for (i = 0; i < stream->num_word_entries; i++) {
      size_t  slot = stream->word_entries[i].hash & (new_count - 1);

      while (new_slots[slot] != 0) slot = (slot + 1) & (new_count - 1);
      new_slots[slot] = i + 1;
    }  /* for */
    free(stream->word_slots);
    stream->word_slots = new_slots;
    stream->num_word_slots = new_count;
  }  /* if */
  return new_slots != NULL;
}  /* ecil_word_table_grow */


void ecil_word_register(const uint32_t *words, size_t count, uint32_t gid)
/*
Register a key for a G-id.  If the key has an entry (live, or left with
gid 0 by ecil_word_unregister) that entry gets the G-id, so that there is
never more than one entry per key (the table is reinserted in entry order
when it grows).
*/
{
  an_ecil_stream  *stream = &ecil_stream;
  a_boolean       ok = TRUE;
  size_t          existing_slot = 0;
  a_boolean       found = FALSE;

  if (stream->num_word_slots != 0) {
    existing_slot = ecil_word_find_slot(stream, words, count,
                                        ecil_hash_words(words, count));
    if (stream->word_slots[existing_slot] != 0) {
      stream->word_entries[stream->word_slots[existing_slot] - 1].gid = gid;
      found = TRUE;
    }  /* if */
  }  /* if */
  if (found) {
    /* Done. */
  } else if ((stream->num_word_entries + 1) * 2 > stream->num_word_slots) {
    ok = ecil_word_table_grow(stream);
  }  /* if */
  if (!found && ok &&
      stream->num_word_entries == stream->word_entries_capacity) {
    size_t              new_capacity = (stream->word_entries_capacity == 0) ?
                                  128 : stream->word_entries_capacity * 2;
    an_ecil_word_entry  *new_entries = (an_ecil_word_entry *)realloc(
                                  stream->word_entries,
                                  new_capacity * sizeof(an_ecil_word_entry));

    if (new_entries == NULL) {
      ok = FALSE;
    } else {
      stream->word_entries = new_entries;
      stream->word_entries_capacity = new_capacity;
    }  /* if */
  }  /* if */
  if (!found && ok &&
      stream->word_arena_size + count > stream->word_arena_capacity) {
    size_t    new_capacity = (stream->word_arena_capacity == 0) ?
                             1024 : stream->word_arena_capacity * 2;
    uint32_t  *new_arena;

    while (new_capacity < stream->word_arena_size + count) new_capacity *= 2;
    new_arena = (uint32_t *)realloc(stream->word_arena,
                                    new_capacity * sizeof(uint32_t));
    if (new_arena == NULL) {
      ok = FALSE;
    } else {
      stream->word_arena = new_arena;
      stream->word_arena_capacity = new_capacity;
    }  /* if */
  }  /* if */
  if (found) {
    /* The existing entry was updated above. */
  } else if (!ok) {
    ecil_note_unsupported("out of memory in the ECIL writer");
  } else {
    uint32_t            hash = ecil_hash_words(words, count);
    size_t              slot = ecil_word_find_slot(stream, words, count,
                                                   hash);
    an_ecil_word_entry  *e = &stream->word_entries[stream->num_word_entries];

    memcpy(stream->word_arena + stream->word_arena_size, words,
           count * sizeof(uint32_t));
    e->hash = hash;
    e->offset = stream->word_arena_size;
    e->count = count;
    e->gid = gid;
    stream->word_arena_size += count;
    stream->num_word_entries++;
    stream->word_slots[slot] = stream->num_word_entries;
  }  /* if */
}  /* ecil_word_register */


/* Per-entry ids. */

#if !ECIL_ID_IN_PREFIX

static size_t ecil_pointer_hash(void *key)
/*
Hash an IL address for the test-build id tables.  No output depends on it.
*/
{
  return (size_t)(((uintptr_t)key >> 4) * 11400714819323198485ull);
}  /* ecil_pointer_hash */


static uint32_t *ecil_id_map_slot(an_ecil_id_map *map,
                                  void           *key,
                                  a_boolean      create)
/*
Return the address of the id stored for key, or NULL if there is none and
create is FALSE.  (Test builds only.)
*/
{
  uint32_t  *result = NULL;
  size_t    slot;

  if (map->capacity != 0) {
    slot = ecil_pointer_hash(key) & (map->capacity - 1);
    while (map->keys[slot] != NULL && map->keys[slot] != key) {
      slot = (slot + 1) & (map->capacity - 1);
    }  /* while */
    if (map->keys[slot] == key) result = &map->values[slot];
  }  /* if */
  if (result == NULL && create) {
    if ((map->count + 1) * 2 > map->capacity) {
      size_t    new_capacity = (map->capacity == 0) ? 1024 : map->capacity * 2;
      void      **new_keys = (void **)calloc(new_capacity, sizeof(void *));
      uint32_t  *new_values = (uint32_t *)calloc(new_capacity,
                                                 sizeof(uint32_t));
      size_t    i;

      if (new_keys != NULL && new_values != NULL) {
        for (i = 0; i < map->capacity; i++) {
          if (map->keys[i] != NULL) {
            slot = ecil_pointer_hash(map->keys[i]) & (new_capacity - 1);
            while (new_keys[slot] != NULL) {
              slot = (slot + 1) & (new_capacity - 1);
            }  /* while */
            new_keys[slot] = map->keys[i];
            new_values[slot] = map->values[i];
          }  /* if */
        }  /* for */
        free(map->keys);
        free(map->values);
        map->keys = new_keys;
        map->values = new_values;
        map->capacity = new_capacity;
      } else {
        free(new_keys);
        free(new_values);
      }  /* if */
    }  /* if */
    if (map->capacity != 0) {
      slot = ecil_pointer_hash(key) & (map->capacity - 1);
      while (map->keys[slot] != NULL && map->keys[slot] != key) {
        slot = (slot + 1) & (map->capacity - 1);
      }  /* while */
      if (map->keys[slot] == NULL) {
        map->keys[slot] = key;
        map->values[slot] = 0;
        map->count++;
      }  /* if */
      result = &map->values[slot];
    }  /* if */
  }  /* if */
  return result;
}  /* ecil_id_map_slot */

#endif /* !ECIL_ID_IN_PREFIX */


uint32_t ecil_gid_of(void *entry)
/*
Return the G-id of an IL entry (type, routine, variable, field), or 0.
*/
{
#if ECIL_ID_IN_PREFIX
  return il_entry_prefix_of(entry).ecil_id;
#else /* !ECIL_ID_IN_PREFIX */
  uint32_t  *slot = ecil_id_map_slot(&ecil_stream.gid_map, entry, FALSE);

  return slot == NULL ? 0 : *slot;
#endif /* ECIL_ID_IN_PREFIX */
}  /* ecil_gid_of */


void ecil_set_gid(void *entry, uint32_t gid)
/*
Record the G-id of an IL entry.
*/
{
#if ECIL_ID_IN_PREFIX
  il_entry_prefix_of(entry).ecil_id = gid;
#else /* !ECIL_ID_IN_PREFIX */
  uint32_t  *slot = ecil_id_map_slot(&ecil_stream.gid_map, entry, TRUE);

  if (slot != NULL) *slot = gid;
#endif /* ECIL_ID_IN_PREFIX */
}  /* ecil_set_gid */


uint32_t ecil_lid_of(void *entry)
/*
Return the frame-local id of a local variable or label in the current function
frame, or 0.  An entry keeps the id it was given by ecil_set_lid with the
base of its function added (the number of L-ids all earlier functions used),
so an id left by an earlier function (stale) is not above the current base
and reads as 0.
*/
{
  uint32_t  stored;

#if ECIL_ID_IN_PREFIX
  stored = il_entry_prefix_of(entry).ecil_id;
#else /* !ECIL_ID_IN_PREFIX */
  uint32_t  *slot = ecil_id_map_slot(&ecil_stream.lid_map, entry, FALSE);

  stored = (slot == NULL) ? 0 : *slot;
#endif /* ECIL_ID_IN_PREFIX */
  return stored > ecil_stream.lid_base ? stored - ecil_stream.lid_base : 0;
}  /* ecil_lid_of */


void ecil_set_lid(void *entry, uint32_t lid)
/*
Record the frame-local id (1 or more) of a local variable or label of the
current function.
*/
{
  uint32_t  stored = ecil_stream.lid_base + lid;

#if ECIL_ID_IN_PREFIX
  il_entry_prefix_of(entry).ecil_id = stored;
#else /* !ECIL_ID_IN_PREFIX */
  uint32_t  *slot = ecil_id_map_slot(&ecil_stream.lid_map, entry, TRUE);

  if (slot != NULL) *slot = stored;
#endif /* ECIL_ID_IN_PREFIX */
}  /* ecil_set_lid */


void ecil_end_lid_epoch(uint32_t num_lids)
/*
End of a function frame, which used num_lids L-ids: every id set so far
becomes stale (ecil_lid_of returns 0 for it).  Nothing is cleared.
*/
{
  check_assertion(num_lids <= 0xFFFFFFFFu - ecil_stream.lid_base - 1);
  ecil_stream.lid_base += num_lids;
}  /* ecil_end_lid_epoch */


/* Unique symbol names. */

static an_ecil_string *ecil_string_of(uint32_t sid)
/*
Return the string-table entry of a sid.
*/
{
  check_assertion(sid != 0 && sid <= ecil_stream.num_strings);
  return &ecil_stream.strings[sid - 1];
}  /* ecil_string_of */


void ecil_mark_symbol_name(uint32_t sid)
/*
Note that the string is a symbol name in use.
*/
{
  ecil_string_of(sid)->symbol_used = TRUE;
}  /* ecil_mark_symbol_name */


a_boolean ecil_symbol_name_used(uint32_t sid)
/*
Return TRUE if the string has been taken as a symbol name.
*/
{
  return ecil_string_of(sid)->symbol_used;
}  /* ecil_symbol_name_used */


uint32_t ecil_unique_symbol_name(an_ecil_frame *frame, const char *base)
/*
Return the sid of a name that no other static or string literal has: the
base itself, or base.1, base.2, ... in order of first use.  Names are never
released, so every suffix below the one a base last got is taken: the base
keeps the first suffix still worth trying (its probing is not quadratic).
*/
{
  char      name[1024];
  uint32_t  base_sid = ecil_intern_cstring(frame, base);
  uint32_t  sid = base_sid;

  if (sid != 0 && ecil_symbol_name_used(sid)) {
    unsigned  suffix = ecil_string_of(base_sid)->next_suffix;

    if (suffix == 0) suffix = 1;
    do {
      (void)snprintf(name, sizeof(name), "%.1000s.%u", base, suffix);
      sid = ecil_intern_cstring(frame, name);
      suffix++;
    } while (sid != 0 && ecil_symbol_name_used(sid));
    if (sid != 0) ecil_string_of(base_sid)->next_suffix = suffix;
  }  /* if */
  if (sid != 0) ecil_mark_symbol_name(sid);
  return sid;
}  /* ecil_unique_symbol_name */


/* The stream. */

a_boolean ecil_stream_is_active(void)
/*
Return TRUE if frames can still be submitted.
*/
{
  return ecil_stream.state == ess_open;
}  /* ecil_stream_is_active */


static void ecil_fail_stream(const char *message)
/*
Report a writer failure, mark the stream broken and tell the sink.
*/
{
  an_ecil_stream  *stream = &ecil_stream;

  if (stream->state != ess_failed) {
    stream->state = ess_failed;
    if (stream->sink != NULL) ecil_sink_abort(stream->sink);
    cir_be_report_status(message, -1);
  }  /* if */
}  /* ecil_fail_stream */


void ecil_submit_frame(an_ecil_frame *frame)
/*
Assemble the frame (header, declaration section, body section) in a buffer
obtained from the sink and submit it.  The frame is left empty.
*/
{
  an_ecil_stream  *stream = &ecil_stream;
  size_t          payload = frame->decl.size + frame->body.size;
  char            *buffer;

  if (stream->state != ess_open &&
      !(stream->state == ess_aborted && frame->kind == efk_abort)) {
    /* Nothing more is sent after F_END, F_ABORT or a failure. */
  } else if (frame->decl.failed || frame->body.failed ||
             payload > 0xFFFFFFFFu) {
    ecil_fail_stream("out of memory building an ECIL frame");
  } else {
    buffer = ecil_sink_acquire(stream->sink,
                               (size_t)ECIL_FRAME_HEADER_SIZE + payload);
    if (buffer == NULL) {
      ecil_fail_stream("out of memory acquiring an ECIL buffer");
    } else {
      buffer[0] = (char)(payload & 0xFF);
      buffer[1] = (char)((payload >> 8) & 0xFF);
      buffer[2] = (char)((payload >> 16) & 0xFF);
      buffer[3] = (char)((payload >> 24) & 0xFF);
      buffer[4] = (char)frame->kind;
      buffer[5] = (char)frame->flags;
      buffer[6] = 0;
      buffer[7] = 0;
      if (frame->decl.size != 0) {
        memcpy(buffer + ECIL_FRAME_HEADER_SIZE, frame->decl.data,
               frame->decl.size);
      }  /* if */
      if (frame->body.size != 0) {
        memcpy(buffer + ECIL_FRAME_HEADER_SIZE + frame->decl.size,
               frame->body.data, frame->body.size);
      }  /* if */
      stream->num_frames++;
      if (!ecil_sink_submit(stream->sink, buffer,
                            (size_t)ECIL_FRAME_HEADER_SIZE + payload) &&
          stream->state == ess_open) {
        /* The sink reported the reason; later calls are no-ops. */
        stream->state = ess_failed;
      }  /* if */
    }  /* if */
  }  /* if */
  ecil_frame_reset(frame, frame->kind);
}  /* ecil_submit_frame */


static a_boolean ecil_target_is_supported(a_boolean in_process)
/*
Return TRUE if the FE target configuration is one the writer can describe:
ILP32 int, LP64 long and pointers, little endian, 8-bit chars.  The in-process
library accepts only x86-64 SysV, so there the architecture is checked too
(the x86 family in 64-bit mode); a stream written to a file for another
architecture (the writer-only test loop on an AArch64 Mac) is valid, and the
library rejects its target triple if it is ever consumed.
*/
{
  return targ_sizeof_pointer == 8 && targ_sizeof_int == 4 &&
         targ_sizeof_long == 8 && targ_char_bit == 8 && targ_little_endian &&
         (!in_process || (target_is_x86_based() && target_is_64_bits()));
}  /* ecil_target_is_supported */


static void ecil_write_header(an_ecil_frame *frame)
/*
Write the F_HEADER records: strings and the integer types the type roles
need, then HDR_TARGET, HDR_LANG, HDR_PRODUCER and HDR_TYPE_ROLE.
*/
{
  uint32_t      triple_sid;
  uint32_t      abi_sid;
  uint32_t      producer_sid;
  uint32_t      source_sid = 0;
  uint32_t      int_type;
  uint32_t      size_type;
  uint32_t      ptrdiff_type;
  unsigned int  target_flags = 0;
  unsigned int  lang_flags = 0;
  unsigned int  ptr_bits = (unsigned int)(targ_sizeof_pointer *
                                          targ_char_bit);

  triple_sid = ecil_intern_string(frame, ecil_target_triple(),
                                  strlen(ecil_target_triple()));
  abi_sid = ecil_intern_string(frame, "", 0);
  producer_sid = ecil_intern_string(frame, ECIL_PRODUCER_NAME,
                                    strlen(ECIL_PRODUCER_NAME));
  if (primary_source_file_name != NULL) {
    source_sid = ecil_intern_string(frame, primary_source_file_name,
                                    strlen(primary_source_file_name));
  }  /* if */
  /* The type roles: int, size_t, ptrdiff_t. */
  int_type = ecil_intern_int_type(frame,
                           (unsigned int)(targ_sizeof_int * targ_char_bit),
                           TRUE, (unsigned int)targ_sizeof_int,
                           (unsigned int)targ_alignof_int);
  size_type = ecil_intern_int_type(frame, ptr_bits, FALSE,
                            (unsigned int)targ_sizeof_pointer,
                            (unsigned int)targ_sizeof_pointer);
  ptrdiff_type = ecil_intern_int_type(frame, ptr_bits, TRUE,
                               (unsigned int)targ_sizeof_pointer,
                               (unsigned int)targ_sizeof_pointer);
  if (targ_has_signed_chars) target_flags |= ECIL_TARGET_CHAR_SIGNED;
  /* Unaligned bit-field access is cheap on x86 and AArch64 (clang's
     hasCheapUnalignedBitFieldAccess); not assumed elsewhere. */
  if (target_is_x86_based() || (target_is_arm_based() &&
                                target_is_64_bits())) {
    target_flags |= ECIL_TARGET_CHEAP_UNALIGNED_BF;
  }  /* if */
  ecil_rec_begin(frame, eop_hdr_target);
  ecil_arg_uint(frame, triple_sid);
  ecil_arg_uint(frame, abi_sid);
  ecil_arg_nil(frame);
  ecil_arg_uint(frame, ptr_bits);
  ecil_arg_uint(frame, ptr_bits);
  ecil_arg_uint(frame, target_flags);
  ecil_rec_end(frame);
#if CHECKING
  if (getenv("EDG_CIR_WRAPV") != NULL) ecil_wrapv = TRUE;
#endif /* CHECKING */
  if (ecil_wrapv) lang_flags |= ECIL_LANG_WRAPV;
  if (!C_mode()) {
    /* A C++ stream has exceptions when the front end does (spec 12.6): calls
       may throw unless nothrow, and the eh and all cleanups are live. */
    if (ecil_has_exceptions()) lang_flags |= ECIL_LANG_EXCEPTIONS;
    if (rtti_enabled) lang_flags |= ECIL_LANG_RTTI;
    /* The front end always has thread-safe initialization of function-local
       statics (__cpp_threadsafe_static_init). */
    lang_flags |= ECIL_LANG_THREADSAFE_STATICS;
  }  /* if */
  ecil_rec_begin(frame, eop_hdr_lang);
  ecil_arg_uint(frame, C_mode() ? (unsigned int)elang_c :
                                  (unsigned int)elang_cxx);
  ecil_arg_uint(frame, (uint64_t)std_version);
  ecil_arg_uint(frame, lang_flags);
  ecil_rec_end(frame);
  ecil_rec_begin(frame, eop_hdr_producer);
  ecil_arg_uint(frame, producer_sid);
  if (source_sid != 0) {
    ecil_arg_uint(frame, source_sid);
  } else {
    ecil_arg_nil(frame);
  }  /* if */
  ecil_rec_end(frame);
  ecil_rec2(frame, eop_hdr_type_role, etr_int, int_type);
  ecil_rec2(frame, eop_hdr_type_role, etr_size_t, size_type);
  ecil_rec2(frame, eop_hdr_type_role, etr_ptrdiff_t, ptrdiff_type);
}  /* ecil_write_header */


static void ecil_release_stream_tables(an_ecil_stream *stream)
/*
Free the tables of the stream.
*/
{
  free(region_stack);
  region_stack = NULL;
  region_stack_size = region_stack_capacity = 0;
  ecil_jump_check_enabled = FALSE;
  uint32_t  i;

  for (i = 0; i < stream->num_strings; i++) free(stream->strings[i].text);
  free(stream->strings);
  free(stream->string_slots);
#if CHECKING
  ecil_check_retired_tables(stream, 0, stream->num_retired);
  for (i = 0; i < stream->num_retired; i++) free(stream->retired[i].table);
#endif /* CHECKING */
  free(stream->gids);
  free(stream->word_arena);
  free(stream->word_entries);
  free(stream->word_slots);
  if (stream->work_frame_ready) ecil_frame_free(&stream->work_frame);
  free(stream->gid_map.keys);
  free(stream->gid_map.values);
  free(stream->lid_map.keys);
  free(stream->lid_map.values);
  memset(stream, 0, sizeof(*stream));
}  /* ecil_release_stream_tables */


void ecil_stream_open(void)
/*
Open the stream: choose the sink (the --ecil_output file or the in-process
library session) and submit the preamble.  F_HEADER follows with the first
hand-off (or the end of the stream), when the predefined macros that name the
target's operating system exist.  Called where the IL file would be started.
If the stream cannot be opened an error is reported and later calls do
nothing.
*/
{
  an_ecil_stream  *stream = &ecil_stream;
  char            *preamble;

  if (stream->state == ess_closed) {
    memset(stream, 0, sizeof(*stream));
    stream->state = ess_failed;
#if CHECKING
    stream->gid_stress = (getenv("ECIL_GID_STRESS") != NULL);
#endif /* CHECKING */
    if (!ecil_target_is_supported(ecil_output_file_name == NULL)) {
      cir_be_report_status("the CIR back end supports only x86-64 SysV "
                           "targets", -1);
    } else {
      if (ecil_output_file_name != NULL) {
        stream->sink = ecil_sink_open_file(ecil_output_file_name);
      } else {
#ifdef EDG_CIR_LIBRARY_LINKED
        stream->sink = ecil_sink_open_in_process();
#else /* !defined(EDG_CIR_LIBRARY_LINKED) */
        cir_be_report_status("this front end was built without the CIR "
                             "library; use --ecil_output=<file>", -1);
#endif /* defined(EDG_CIR_LIBRARY_LINKED) */
      }  /* if */
      if (stream->sink != NULL) {
        stream->state = ess_open;
        preamble = ecil_sink_acquire(stream->sink, ECIL_PREAMBLE_SIZE);
        if (preamble == NULL) {
          ecil_fail_stream("out of memory");
        } else {
          preamble[0] = (char)ECIL_MAGIC_0;
          preamble[1] = (char)ECIL_MAGIC_1;
          preamble[2] = (char)ECIL_MAGIC_2;
          preamble[3] = (char)ECIL_MAGIC_3;
          preamble[4] = (char)ECIL_MAGIC_4;
          preamble[5] = (char)ECIL_MAGIC_5;
          preamble[6] = (char)ECIL_MAGIC_6;
          preamble[7] = (char)ECIL_MAGIC_7;
          preamble[8] = (char)(ECIL_VERSION_MAJOR & 0xFF);
          preamble[9] = (char)((ECIL_VERSION_MAJOR >> 8) & 0xFF);
          preamble[10] = (char)(ECIL_VERSION_MINOR & 0xFF);
          preamble[11] = (char)((ECIL_VERSION_MINOR >> 8) & 0xFF);
          /* Features: SUPP (ECIL 1.1: a dropped declaration carries its
             reason; the preamble goes out before any frame, so the bit says
             that frames may carry a blob); INLINE_HINTS (1.3: DECL_FUNC
             carries INLINE_HINT exactly, where ECIL_INLINE_HINTS);
             LATE_NAMES for C++ (NAME_BIND in F_NAMES); LOCATIONS is off. */
          preamble[12] = (char)(ECIL_FEATURE_SUPP |
                                (ECIL_INLINE_HINTS ?
                                        ECIL_FEATURE_INLINE_HINTS : 0) |
                                (C_mode() ? 0 : ECIL_FEATURE_LATE_NAMES));
          preamble[13] = 0;
          preamble[14] = 0;
          preamble[15] = 0;
          if (!ecil_sink_submit(stream->sink, preamble,
                                ECIL_PREAMBLE_SIZE)) {
            stream->state = ess_failed;
          }  /* if */
        }  /* if */
      }  /* if */
    }  /* if */
  }  /* if */
}  /* ecil_stream_open */


static void ecil_send_header(void)
/*
Submit F_HEADER if the stream is open and it has not been sent yet.
*/
{
  an_ecil_stream  *stream = &ecil_stream;
  an_ecil_frame   frame;

  if (stream->state == ess_open && !stream->header_sent) {
    stream->header_sent = TRUE;
    ecil_frame_init(&frame, efk_header);
    ecil_write_header(&frame);
    ecil_submit_frame(&frame);
    ecil_frame_free(&frame);
  }  /* if */
}  /* ecil_send_header */


void ecil_hand_off_region(a_memory_region_number region_number)
/*
Hand a completed memory region to the stream.  A function region becomes an
F_REGION frame; the file-scope region becomes F_GLOBALS.  Does nothing once
the stream has failed, been aborted or ended.  The frame is reused from one
hand-off to the next (its storage is released only when it grew large).
*/
{
  an_ecil_stream  *stream = &ecil_stream;

  ecil_send_header();
  if (stream->state == ess_open) {
    an_ecil_frame *frame = &stream->work_frame;

    if (!stream->work_frame_ready) {
      ecil_frame_init(frame, efk_region);
      stream->work_frame_ready = TRUE;
    }  /* if */
    if (region_number == FILE_SCOPE_REGION_NUMBER) {
      ecil_frame_reset(frame, efk_globals);
      ecil_emit_globals(frame);
    } else {
      ecil_frame_reset(frame, efk_region);
      ecil_stream_function_region(frame, region_number);
    }  /* if */
    if (frame->decl.size != 0 || frame->body.size != 0 ||
        frame->kind == efk_globals) {
      ecil_submit_frame(frame);
    }  /* if */
    if (region_number == FILE_SCOPE_REGION_NUMBER && !C_mode() &&
        stream->state == ess_open && ecil_invokers_pending()) {
      /* Static invokers of lambdas that only the file scope refers to (a
         constant function pointer): their bodies follow in a region frame
         (spec 11.9). */
      ecil_frame_reset(frame, efk_region);
      ecil_drain_invokers(frame);
      ecil_submit_frame(frame);
    }  /* if */
    if (region_number == FILE_SCOPE_REGION_NUMBER && !C_mode() &&
        stream->state == ess_open) {
      /* The names that were not final when their entities were sent, bound
         now that the file scope is complete (spec 7); F_NAMES is always
         present for C++, possibly empty. */
      ecil_frame_reset(frame, efk_names);
      ecil_emit_late_names(frame);
      ecil_submit_frame(frame);
    }  /* if */
  }  /* if */
}  /* ecil_hand_off_region */


void ecil_stream_finish(void)
/*
End the stream: F_END if the compilation is still clean, otherwise F_ABORT.
Does nothing if the stream is not open (and is idempotent).
*/
{
  an_ecil_stream  *stream = &ecil_stream;
  an_ecil_frame   frame;

#if CHECKING
  if (getenv("ECIL_STATS") != NULL) {
    /* How many function regions are still resident: with streaming and no
       --ecil_keep_regions there should be none (or only those kept for
       later use). */
    a_memory_region_number  n;
    unsigned long           live = 0;
    unsigned long           total = 0;

    for (n = FILE_SCOPE_REGION_NUMBER + 1; n <= highest_used_region_number;
         n++) {
      total++;
      if (mem_region_table[n] != NULL) live++;
    }  /* for */
    fprintf(stderr, "ecil: function regions %lu, resident at finish %lu\n",
            total, live);
  }  /* if */
#endif /* CHECKING */
  if (stream->state == ess_open) {
    if (is_at_least_one_error()) {
      ecil_abort();
    } else {
      ecil_send_header();
#if CHECKING
      ecil_check_all_introduced();
#endif /* CHECKING */
      ecil_frame_init(&frame, efk_end);
      ecil_rec2(&frame, eop_end, stream->num_frames, 0);
      ecil_submit_frame(&frame);
      ecil_frame_free(&frame);
      if (stream->state == ess_open) stream->state = ess_ended;
    }  /* if */
  }  /* if */
}  /* ecil_stream_finish */


void ecil_abort(void)
/*
Invalidate the stream: submit F_ABORT.  Idempotent, and a no-op once F_END
has been submitted or if no stream is open.  It never calls into the
library except through the sink's submit, which is not re-entered because
library diagnostics are only reported after an API call has returned.
*/
{
  an_ecil_stream  *stream = &ecil_stream;
  an_ecil_frame   frame;

  if (stream->state == ess_open) {
    ecil_send_header();
    stream->state = ess_aborted;
    if (stream->sink != NULL) ecil_sink_begin_abort(stream->sink);
    ecil_frame_init(&frame, efk_abort);
    ecil_submit_frame(&frame);
    ecil_frame_free(&frame);
    ecil_sink_abort(stream->sink);
  }  /* if */
}  /* ecil_abort */


void ecil_stream_finish_back_end(void)
/*
Called from back_end(): complete the output.  For the in-process sink this
runs the CIR pipeline and writes the object or assembly; for a file sink it
flushes and closes the file.
*/
{
  an_ecil_stream  *stream = &ecil_stream;

  if (stream->state == ess_ended && stream->sink != NULL) {
    if (!ecil_sink_finish(stream->sink)) stream->state = ess_failed;
  }  /* if */
}  /* ecil_stream_finish_back_end */


void ecil_stream_close(void)
/*
Release the sink and the tables.  Safe to call at any time and more than
once.
*/
{
  an_ecil_stream  *stream = &ecil_stream;

  if (stream->sink != NULL) {
    ecil_sink_close(stream->sink);
    stream->sink = NULL;
  }  /* if */
  ecil_release_stream_tables(stream);
  ecil_type_reset();
  ecil_const_reset();
  ecil_code_reset();
  stream->state = ess_closed;
}  /* ecil_stream_close */


/* Conditionally close the "edg" namespace. */
END_EDG_NAMESPACE

#endif /* ECIL_WRITER_ENABLED */
