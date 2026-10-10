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

/* An entry of the string table (a copy of the interned text). */
typedef struct an_ecil_string {
  char          *text;
  size_t        length;
  uint32_t      hash;
  a_boolean     symbol_used;      /* Taken as a symbol name. */
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

static const char *ecil_target_triple(void)
/*
The target triple, derived from the front end's target configuration: the
architecture from the targ_supports_* flags, the operating system from the
host the front end was built for (the front end targets its host).  The CIR
library supports x86-64 Linux only and rejects any other triple; a front end
for another target still writes a valid stream, so the writer-only test loop
works there, but its output is not authoritative.
*/
{
  const char  *arch;
  const char  *os;
  static char triple[96];

  if (target_is_arm_based()) {
    arch = target_is_64_bits() ? "aarch64" : "armv7";
  } else if (target_is_riscv_based()) {
    arch = target_is_64_bits() ? "riscv64" : "riscv32";
  } else {
    arch = target_is_64_bits() ? "x86_64" : "i686";
  }  /* if */
#if defined(__linux__)
  os = "unknown-linux-gnu";
#elif defined(__APPLE__) && defined(__MACH__)
  os = "apple-macosx";
#elif defined(_WIN32)
  os = "pc-windows-msvc";
#else
  os = "unknown-unknown";
#endif
  (void)snprintf(triple, sizeof(triple), "%s-%s", arch, os);
  return triple;
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


void ecil_put_uint(an_ecil_buffer *buf, uint64_t value)
/*
Append an unsigned integer in its shortest encoding.
*/
{
  if (value <= ECIL_TAG_FIXUINT_MAX) {
    ecil_buf_put_byte(buf, (unsigned int)value);
  } else if (value <= 0xFF) {
    ecil_buf_put_byte(buf, ECIL_TAG_U8);
    ecil_buf_put_le(buf, value, 1);
  } else if (value <= 0xFFFF) {
    ecil_buf_put_byte(buf, ECIL_TAG_U16);
    ecil_buf_put_le(buf, value, 2);
  } else if (value <= 0xFFFFFFFFu) {
    ecil_buf_put_byte(buf, ECIL_TAG_U32);
    ecil_buf_put_le(buf, value, 4);
  } else {
    ecil_buf_put_byte(buf, ECIL_TAG_U64);
    ecil_buf_put_le(buf, value, 8);
  }  /* if */
}  /* ecil_put_uint */


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
    ecil_buf_put_byte(buf, ECIL_TAG_I8);
    ecil_buf_put_le(buf, (uint64_t)value, 1);
  } else if (value >= -32768) {
    ecil_buf_put_byte(buf, ECIL_TAG_I16);
    ecil_buf_put_le(buf, (uint64_t)value, 2);
  } else if (value >= -2147483647 - 1) {
    ecil_buf_put_byte(buf, ECIL_TAG_I32);
    ecil_buf_put_le(buf, (uint64_t)value, 4);
  } else {
    ecil_buf_put_byte(buf, ECIL_TAG_I64);
    ecil_buf_put_le(buf, (uint64_t)value, 8);
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
}  /* ecil_frame_init */


void ecil_frame_reset(an_ecil_frame *frame, unsigned int kind)
/*
Empty a frame for reuse, keeping its storage.
*/
{
  frame->decl.size = 0;
  frame->body.size = 0;
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

  mark.decl_size = frame->decl.size;
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


/* Record helpers. */

void ecil_rec_begin(an_ecil_frame *frame, unsigned int opcode)
/*
Start a record: write its opcode byte.
*/
{
  LOCAL_UNUSED int  arity = ecil_opcode_arity(opcode);

  check_assertion(frame->pending_operands == -1);
  check_assertion(arity >= 0);
  ecil_buf_put_byte(frame->current, opcode);
#if CHECKING
  frame->pending_operands = arity;
#else /* !CHECKING */
  (void)arity;
#endif /* CHECKING */
}  /* ecil_rec_begin */


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


void ecil_arg_uint(an_ecil_frame *frame, uint64_t value)
/*
Append an unsigned operand.
*/
{
  ecil_note_operand(frame);
  ecil_put_uint(frame->current, value);
}  /* ecil_arg_uint */


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


void ecil_rec_end(an_ecil_frame *frame)
/*
Finish a record.  In CHECKING builds, verify that it received exactly the
operands its opcode declares.
*/
{
  /* A record abandoned half-built because a construct is unsupported is
     discarded with its body or constant, so it need not be complete. */
  check_assertion(frame->pending_operands == 0 ||
                  frame->pending_operands == -1 ||
                  ecil_unsupported_reason != NULL);
  frame->pending_operands = -1;
}  /* ecil_rec_end */


void ecil_rec0(an_ecil_frame *frame, unsigned int opcode)
/*
Write a record with no operands.
*/
{
  ecil_rec_begin(frame, opcode);
  ecil_rec_end(frame);
}  /* ecil_rec0 */


void ecil_rec1(an_ecil_frame *frame, unsigned int opcode, uint64_t a)
/*
Write a record with one unsigned operand.
*/
{
  ecil_rec_begin(frame, opcode);
  ecil_arg_uint(frame, a);
  ecil_rec_end(frame);
}  /* ecil_rec1 */


void ecil_rec2(an_ecil_frame *frame,
               unsigned int  opcode,
               uint64_t      a,
               uint64_t      b)
/*
Write a record with two unsigned operands.
*/
{
  ecil_rec_begin(frame, opcode);
  ecil_arg_uint(frame, a);
  ecil_arg_uint(frame, b);
  ecil_rec_end(frame);
}  /* ecil_rec2 */


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
    char  *copy = (char *)malloc(length == 0 ? 1 : length);

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
      stream->strings[stream->num_strings].text = copy;
      stream->strings[stream->num_strings].length = length;
      stream->strings[stream->num_strings].hash = hash;
      stream->strings[stream->num_strings].symbol_used = FALSE;
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


/* Unsupported-construct reporting. */

const char *ecil_unsupported_reason;

void ecil_note_unsupported(const char *reason)
/*
Record why the current function or initializer cannot be expressed.  The
first reason wins; the caller of the traversal checks
ecil_unsupported_reason and discards its partial output.
*/
{
  if (ecil_unsupported_reason == NULL) ecil_unsupported_reason = reason;
}  /* ecil_note_unsupported */


/* The G-id table. */

uint32_t ecil_new_gid(an_ecil_gid_kind kind)
/*
Assign the next G-id (dense from 1) and record what it names.
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
Hash a word sequence (FNV-1a over the words).
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
Register a key for a G-id.  The key must not be registered yet.
*/
{
  an_ecil_stream  *stream = &ecil_stream;
  a_boolean       ok = TRUE;

  if ((stream->num_word_entries + 1) * 2 > stream->num_word_slots) {
    ok = ecil_word_table_grow(stream);
  }  /* if */
  if (ok && stream->num_word_entries == stream->word_entries_capacity) {
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
  if (ok && stream->word_arena_size + count > stream->word_arena_capacity) {
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
  if (!ok) {
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
Return the frame-local id of a local variable or label, or 0.
*/
{
#if ECIL_ID_IN_PREFIX
  return il_entry_prefix_of(entry).ecil_id;
#else /* !ECIL_ID_IN_PREFIX */
  uint32_t  *slot = ecil_id_map_slot(&ecil_stream.lid_map, entry, FALSE);

  return slot == NULL ? 0 : *slot;
#endif /* ECIL_ID_IN_PREFIX */
}  /* ecil_lid_of */


void ecil_set_lid(void *entry, uint32_t lid)
/*
Record the frame-local id of a local variable or label.
*/
{
#if ECIL_ID_IN_PREFIX
  il_entry_prefix_of(entry).ecil_id = lid;
#else /* !ECIL_ID_IN_PREFIX */
  uint32_t  *slot = ecil_id_map_slot(&ecil_stream.lid_map, entry, TRUE);

  if (slot != NULL) *slot = lid;
#endif /* ECIL_ID_IN_PREFIX */
}  /* ecil_set_lid */


void ecil_forget_lids(void)
/*
End of a function frame: forget the local ids.  (With the prefix id the
entries are freed with their region, so nothing is needed.)
*/
{
#if !ECIL_ID_IN_PREFIX
  an_ecil_id_map  *map = &ecil_stream.lid_map;

  if (map->capacity != 0) {
    memset(map->keys, 0, map->capacity * sizeof(void *));
    memset(map->values, 0, map->capacity * sizeof(uint32_t));
    map->count = 0;
  }  /* if */
#endif /* !ECIL_ID_IN_PREFIX */
}  /* ecil_forget_lids */


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
base itself, or base.1, base.2, ... in order of first use.
*/
{
  char      name[1024];
  uint32_t  sid = ecil_intern_cstring(frame, base);
  unsigned  suffix = 0;

  while (sid != 0 && ecil_symbol_name_used(sid)) {
    suffix++;
    (void)snprintf(name, sizeof(name), "%.1000s.%u", base, suffix);
    sid = ecil_intern_cstring(frame, name);
  }  /* while */
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


static a_boolean ecil_target_is_supported(void)
/*
Return TRUE if the FE target configuration is x86-64 SysV (ILP32 int, LP64
long and pointers, little endian, 8-bit chars): the only target the CIR
library supports.
*/
{
  return targ_sizeof_pointer == 8 && targ_sizeof_int == 4 &&
         targ_sizeof_long == 8 && targ_char_bit == 8 && targ_little_endian;
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
  unsigned int  target_flags = ECIL_TARGET_CHEAP_UNALIGNED_BF;
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
    if (exceptions_enabled) lang_flags |= ECIL_LANG_EXCEPTIONS;
    if (rtti_enabled) lang_flags |= ECIL_LANG_RTTI;
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
  uint32_t  i;

  for (i = 0; i < stream->num_strings; i++) free(stream->strings[i].text);
  free(stream->strings);
  free(stream->string_slots);
  free(stream->gids);
  free(stream->word_arena);
  free(stream->word_entries);
  free(stream->word_slots);
  free(stream->gid_map.keys);
  free(stream->gid_map.values);
  free(stream->lid_map.keys);
  free(stream->lid_map.values);
  memset(stream, 0, sizeof(*stream));
}  /* ecil_release_stream_tables */


void ecil_stream_open(void)
/*
Open the stream: choose the sink (the --ecil_output file or the in-process
library session), submit the preamble and F_HEADER.  Called where the IL
file would be started.  If the stream cannot be opened an error is
reported and later calls do nothing.
*/
{
  an_ecil_stream  *stream = &ecil_stream;
  an_ecil_frame   frame;
  char            *preamble;

  if (stream->state == ess_closed) {
    memset(stream, 0, sizeof(*stream));
    stream->state = ess_failed;
    if (!ecil_target_is_supported()) {
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
          /* Features: none yet (LOCATIONS is off in M1). */
          preamble[12] = 0;
          preamble[13] = 0;
          preamble[14] = 0;
          preamble[15] = 0;
          if (!ecil_sink_submit(stream->sink, preamble,
                                ECIL_PREAMBLE_SIZE)) {
            stream->state = ess_failed;
          } else {
            ecil_frame_init(&frame, efk_header);
            ecil_write_header(&frame);
            ecil_submit_frame(&frame);
            ecil_frame_free(&frame);
          }  /* if */
        }  /* if */
      }  /* if */
    }  /* if */
  }  /* if */
}  /* ecil_stream_open */


void ecil_stream_globals(void)
/*
Submit F_GLOBALS: the definitions of the file-scope variables and the final
re-issued declarations.  Called once, when the file-scope region is handed
off.
*/
{
  ecil_hand_off_region(FILE_SCOPE_REGION_NUMBER);
}  /* ecil_stream_globals */


void ecil_hand_off_region(a_memory_region_number region_number)
/*
Hand a completed memory region to the stream.  A function region becomes an
F_REGION frame; the file-scope region becomes F_GLOBALS.  Does nothing once
the stream has failed, been aborted or ended.
*/
{
  if (ecil_stream.state == ess_open) {
    an_ecil_frame frame;

    if (region_number == FILE_SCOPE_REGION_NUMBER) {
      ecil_frame_init(&frame, efk_globals);
      ecil_emit_globals(&frame);
    } else {
      ecil_frame_init(&frame, efk_region);
      ecil_stream_function_region(&frame, region_number);
    }  /* if */
    if (frame.decl.size != 0 || frame.body.size != 0 ||
        frame.kind == efk_globals) {
      ecil_submit_frame(&frame);
    }  /* if */
    ecil_frame_free(&frame);
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
