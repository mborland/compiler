/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
ecil_reader.h -- the LLVM-free ECIL stream reader of the CIR library.

Layers (all non-recursive, no exceptions, no RTTI):
  - a_cursor and decode_operand/decode_record: zero-copy tag decoding;
  - the opcode schema (find_opcode_info): names, arities and operand rules
    taken from the ECIL_OPCODES table plus operand specifications;
  - a_stream_reader: a chunked frame splitter, a stream validator
    (preamble, version, feature bits, frame kinds and flags, section and
    bracket rules) and a record driver that calls a_stream_handler.

Strings and byte ranges in an_operand point into the caller's buffer and
are valid only during the callback that receives them.
*/

#ifndef ECIL_READER_H
#define ECIL_READER_H 1

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "edg_cir/ecil_format.h"

namespace ecil {

#define ECIL_MAX_ARITY 6

enum class an_error_kind : uint8_t {
  none = 0,
  format,   /* Malformed stream. */
  version   /* Needs a newer reader (or an unsupported major version). */
};  /* an_error_kind */

struct an_error {
  an_error_kind kind;
  std::string   message;
  uint64_t      stream_offset;  /* Byte offset in the stream, best effort. */
  int64_t       frame_index;    /* -1 for the preamble. */

  an_error() : kind(an_error_kind::none), stream_offset(0), frame_index(-1)
    {}
  bool ok() const
    { return kind == an_error_kind::none; }
};  /* an_error */

/* Operand decoding. */

enum class an_operand_kind : uint8_t {
  nil, boolean, uint, sint, f32, f64, fbits, u128, i128, bin, array
};  /* an_operand_kind */

struct an_operand {
  an_operand_kind kind;
  uint8_t         tag;     /* The tag byte as read (for exact re-encoding). */
  uint64_t        u;       /* uint value; boolean 0/1; f32/f64 bit pattern. */
  int64_t         i;       /* sint value. */
  const uint8_t  *data;    /* bin bytes; fbits/u128/i128 bytes; array body. */
  uint32_t        length;  /* bin length; fbits byte count; array body size. */
  uint32_t        count;   /* array element count. */
};  /* an_operand */

struct a_cursor {
  const uint8_t *p;
  const uint8_t *end;

  a_cursor(const uint8_t *begin, const uint8_t *limit) : p(begin), end(limit)
    {}
  size_t remaining() const
    { return static_cast<size_t>(end - p); }
};  /* a_cursor */

/* Iterates the (already validated) elements of an array operand. */
struct an_array_reader {
  const uint8_t *p;
  uint32_t       remaining;
};  /* an_array_reader */

bool decode_operand(a_cursor &cursor, an_operand &operand, an_error &error);
/*
Decode one operand at cursor (scalars, bins and flat arrays).  On failure
set error.message and return false.
*/

an_array_reader array_elements(const an_operand &array);
/* Return a reader positioned at the first element of array. */

bool next_array_element(an_array_reader &reader, an_operand &element);
/* Decode the next element; return false at the end. */

bool operand_u64(const an_operand &operand, uint64_t &value);
/* Return true and the value if operand is an integer in [0, 2^64). */

bool operand_i64(const an_operand &operand, int64_t &value);
/* Return true and the value if operand is an integer in the int64 range. */

/* Opcode schema. */

enum class an_operand_class : uint8_t {
  none,      /* Unused slot. */
  id,        /* uint; any value (0 means "none" in an id space). */
  id_or_nil,
  uinteger,
  uinteger_or_nil,
  integer,   /* any integer width and signedness. */
  boolean,   /* false/true or fixuint 0/1. */
  bin,
  array,     /* array of uint. */
  floating,  /* f32, f64 or fbits. */
  flags,     /* uint, no bits outside mask. */
  enumerator /* uint in [lo, hi]. */
};  /* an_operand_class */

struct an_operand_spec {
  an_operand_class cls;
  uint32_t         lo;    /* enumerator: minimum value. */
  uint32_t         mask;  /* flags: allowed bits; enumerator: maximum. */
};  /* an_operand_spec */

enum an_opcode_section : uint8_t {
  eos_header   = 1,  /* HDR_* records: F_HEADER only. */
  eos_type     = 2,  /* Introductions: header and declaration sections. */
  eos_decl     = 4,  /* Declaration section records. */
  eos_meta     = 8,  /* Allowed in the sections of their own list. */
  eos_body     = 16, /* Function body records (inside FUNC_BEGIN..END). */
  eos_const    = 32  /* C_* records: declaration sections and bodies. */
};  /* an_opcode_section */

struct an_opcode_info {
  const char       *name;    /* Upper-case spelling, e.g. "TY_INT". */
  uint8_t           value;
  uint8_t           arity;
  uint8_t           section;  /* an_opcode_section bits. */
  an_operand_spec   operands[ECIL_MAX_ARITY];
};  /* an_opcode_info */

const an_opcode_info *find_opcode_info(uint8_t opcode);
/* Return the schema entry for opcode, or nullptr if it is unassigned. */

const an_opcode_info *find_opcode_by_name(const std::string &name);
/* Case-insensitive lookup by spelling, or nullptr. */

const char *frame_kind_name(uint8_t kind);
/* "HEADER", "DECLS", ... or nullptr for an extension or unknown kind. */

bool schema_is_consistent(std::string &problem);
/* Self-check: every table arity matches its operand list. */

/* Record decoding. */

struct a_record {
  const uint8_t        *start;
  uint32_t              size;        /* Bytes, including any supp blob. */
  uint8_t               opcode;
  uint8_t               count;       /* Number of operands. */
  an_operand            ops[ECIL_MAX_ARITY];
  const uint8_t        *supp;        /* Supp payload, or nullptr. */
  uint32_t              supp_length;
  uint8_t               supp_tag;
  const an_opcode_info *info;
};  /* a_record */

struct a_decode_context {
  uint16_t stream_minor;   /* Minor version from the preamble. */
  bool     allow_supp;     /* The frame has HAS_SUPP. */
};  /* a_decode_context */

bool decode_record(a_cursor &cursor, const a_decode_context &context,
                   a_record &record, an_error &error);
/*
Decode one record (opcode, its fixed operands and, when the frame allows
it, a trailing supp blob).  The supp payload structure (key value pairs of
scalars) is checked.  Operand values are not range-checked here.
*/

bool check_record_operands(const a_record &record, uint16_t stream_minor,
                           an_error &error);
/*
Check operand kinds, flag masks and enumerator ranges against the schema.
Unknown flag bits or enumerator values report a version error when the
stream's minor is greater than this reader's, else a format error.
*/

bool supp_pairs(const a_record &record, std::vector<std::pair<uint64_t,
                an_operand>> &pairs);
/* Decode the supp payload into pairs (for tools).  False if malformed. */

/* Reference checking. */

struct a_frame_info;

/*
Checks that every id a record refers to was introduced earlier: string ids
(STR), G-ids (TY_*, DECL_*), and L-ids within the current function (PARAM,
LOCAL, MATERIALIZE_TEMP, LABEL; a GOTO may precede its LABEL).  Also that
FIELD/EXTRACT/AGG_SEL/C_SEL name complete records with an index in range,
and that two G-ids do not bind one external symbol name (spec section 7).
It runs inside a_stream_reader after the section and bracket checks.
*/
class a_ref_checker {
public:
  a_ref_checker();

  void begin_frame();
  bool check(const a_record &record, std::string &problem);
  /* False with a message in problem when the record breaks a rule. */

private:
  enum a_kind : uint8_t {
    k_void, k_bool, k_int, k_float, k_ptr, k_array, k_func_type, k_record,
    k_opaque, k_func, k_var
  };  /* a_kind */

  struct a_ent {
    a_kind   kind;
    bool     complete;   /* Record with a body. */
    uint64_t nfields;    /* Record: number of REC_FIELDs. */
    uint64_t element;    /* Array: element gid. */
    bool     has_count;
    uint64_t count;
    bool     is_union;
  };  /* a_ent */

  enum a_lkind : uint8_t { l_value, l_label };

  const a_ent *find(uint64_t g) const;
  bool need_sid(const an_operand &op, bool optional, const char *what,
                std::string &problem);
  bool need_type(const an_operand &op, bool optional, std::string &problem);
  bool need_complete(const an_operand &op, std::string &problem,
                     const a_ent *&out);
  bool need_object(const an_operand &op, const char *what,
                   std::string &problem);
  bool introduce(const an_operand &op, a_ent entity, std::string &problem);
  bool declare(const a_record &r, a_kind kind, std::string &problem);
  bool bind_name(uint64_t g, const an_operand &sid, uint64_t linkage,
                 std::string &problem);
  bool introduce_local(const an_operand &op, a_lkind kind,
                       std::string &problem);
  bool check_selector(uint64_t index, uint64_t repeat, bool is_const,
                      std::string &problem);

  std::unordered_map<uint64_t, std::string>  strings_;
  std::unordered_map<uint64_t, a_ent>        ents_;
  std::unordered_map<std::string, uint64_t>  symbol_owner_;
  uint64_t                                   fields_pending_;
  struct a_function_state {
    std::unordered_map<uint64_t, a_lkind>    lids;
    std::unordered_set<uint64_t>             goto_targets;
  };  /* a_function_state */
  std::vector<a_function_state>              functions_;
  std::vector<uint64_t>                      agg_targets_;
  std::vector<uint64_t>                      const_targets_;
  const char                                *opname_;
};  /* a_ref_checker */

/* Stream reading. */

struct a_preamble {
  uint16_t major;
  uint16_t minor;
  uint32_t features;
};  /* a_preamble */

struct a_frame_info {
  uint8_t        kind;
  uint8_t        flags;
  uint16_t       reserved;
  uint32_t       length;     /* Payload length. */
  uint64_t       offset;     /* Stream offset of the frame header. */
  int64_t        index;      /* Zero-based frame number. */
  const uint8_t *payload;
};  /* a_frame_info */

struct a_record_context {
  const a_frame_info *frame;
  bool                in_body;   /* After the first FUNC_BEGIN of a region. */
  uint32_t            function_depth;
  uint64_t            offset;    /* Stream offset of the record. */
};  /* a_record_context */

/*
Client of the reader.  Each callback returns false to stop (the client
then holds its own error); the reader returns false from consume and
leaves error() empty.
*/
class a_stream_handler {
public:
  virtual ~a_stream_handler() {}
  virtual bool on_preamble(const a_preamble &preamble) = 0;
  virtual bool on_frame_begin(const a_frame_info &frame) = 0;
  virtual bool on_record(const a_record &record,
                         const a_record_context &context) = 0;
  virtual bool on_frame_end(const a_frame_info &frame) = 0;
  virtual bool on_extension_frame(const a_frame_info &frame) = 0;
};  /* a_stream_handler */

struct a_reader_options {
  bool check;          /* Validate sections, brackets, operands, features. */
  size_t max_frame;    /* Reject payloads larger than this. */

  a_reader_options() : check(true), max_frame(size_t(1) << 30) {}
};  /* a_reader_options */

class a_stream_reader {
public:
  a_stream_reader(a_stream_handler &handler,
                  const a_reader_options &options = a_reader_options());

  bool consume(const void *bytes, size_t length);
  /*
  Feed the next bytes.  Whole frames inside the chunk are decoded in
  place; a partial trailing frame is buffered.  Return false on a stream
  error (see error()) or when a handler callback stopped.
  */

  bool finish();
  /* Check that F_END or F_ABORT was seen.  Return false otherwise. */

  const an_error &error() const
    { return error_; }
  bool stopped_by_handler() const
    { return stopped_; }
  bool complete() const
    { return state_ == state_done; }
  bool aborted() const
    { return aborted_; }
  uint64_t offset() const
    { return offset_; }
  int64_t frames_seen() const
    { return frame_count_; }
  const a_preamble &preamble() const
    { return preamble_; }
  uint32_t end_frame_count() const
    { return end_n_frames_; }
  uint32_t end_status() const
    { return end_status_; }

private:
  enum a_state {
    state_preamble,
    state_frame_header,
    state_done
  };  /* a_state */

  struct a_bracket {
    uint8_t kind;
    uint8_t phase;
    uint8_t has_else;
  };  /* a_bracket */

  size_t unit_size(const uint8_t *view, size_t view_size, bool &ok);
  bool process_unit(const uint8_t *data);
  bool process_preamble(const uint8_t *data);
  bool process_frame(const uint8_t *header, const uint8_t *payload);
  bool process_records(const a_frame_info &frame);
  bool check_section(const a_record &record, const a_frame_info &frame,
                     bool &in_body);
  bool check_brackets(const a_record &record, const a_frame_info &frame);
  bool check_refs(const a_record &record);
  bool fail(an_error_kind kind, const std::string &message, uint64_t offset);

  a_stream_handler  &handler_;
  a_reader_options   options_;
  a_state            state_;
  a_preamble         preamble_;
  an_error           error_;
  bool               stopped_;
  bool               aborted_;
  bool               seen_end_;
  uint64_t           offset_;      /* Bytes consumed so far. */
  int64_t            frame_count_;
  uint32_t           end_n_frames_;
  uint32_t           end_status_;
  uint32_t           switch_depth_;
  std::vector<uint8_t>
                     pending_;     /* Partial preamble, header or frame. */
  std::vector<a_bracket>
                     brackets_;
  std::vector<size_t>
                     function_bases_;  /* Bracket depth at each FUNC_BEGIN. */
  a_ref_checker      refs_;
};  /* a_stream_reader */

}  /* namespace ecil */

#endif /* ECIL_READER_H */
