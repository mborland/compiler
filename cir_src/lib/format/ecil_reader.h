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

#include "ecil_ids.h"
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
  bool            all_uint;  /* array: every element passed operand_u64. */
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

bool next_uint_element(an_array_reader &reader, uint64_t &value);
/*
Fast path for an array that passed all_uint: the next element as an unsigned
value (the acceptance rules of operand_u64).  False at the end.
*/

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
  eos_const    = 32, /* C_* records: declaration sections and bodies. */
  eos_stmt     = 64, /* With eos_body: a statement record (the rest of the
                        body records are expression records). */
  eos_v1       = 128 /* A C++ (ECIL v1, minor 8) record. */
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

/* Brackets and scope regions. */

enum a_bracket_kind : uint8_t {
  bk_block, bk_if, bk_while, bk_do, bk_for, bk_switch, bk_tern, bk_land,
  bk_lor, bk_agg_init, bk_const_agg,
  bk_cleanup,      /* CLEANUP_BEGIN .. CLEANUP_BODY: the cleanup code */
  bk_fullexpr, bk_static_init, bk_meminit,
  bk_array         /* ARRAY_CTOR/DTOR_BEGIN .. END (phase 0: ctor, 1: dtor) */
};  /* a_bracket_kind */

const char *bracket_name(uint8_t kind);
/* A readable name for a bracket kind. */

/*
Scope regions (spec 8.3): the regions a LOCAL or MATERIALIZE_TEMP can belong
to.  The table in ecil_brackets.cpp says which records open and close which
region; a region close ends the lifetime of its locals (and, in C++, is where
their destruction happens).
*/
enum a_region_kind : uint8_t {
  rk_none = 0,
  rk_function,
  rk_block,        /* BLOCK_BEGIN .. BLOCK_END */
  rk_for_init,     /* FOR_BEGIN .. FOR_END */
  rk_if_arm,       /* IF_BEGIN .. IF_ELSE, IF_ELSE .. IF_END */
  rk_loop_body,    /* WHILE_BODY .. WHILE_END, DO_BEGIN .. DO_COND,
                      FOR_BODY .. FOR_END */
  rk_switch_body,  /* SWITCH_BEGIN .. SWITCH_END */
  rk_fullexpr,     /* FULLEXPR_BEGIN .. FULLEXPR_END */
  rk_static_init,  /* STATIC_INIT_BEGIN .. STATIC_INIT_END */
  rk_meminit,      /* MEMINIT_BEGIN .. MEMINIT_END */
  rk_array_body    /* ARRAY_CTOR/DTOR_BEGIN .. END */
};  /* a_region_kind */

enum a_bracket_action : uint8_t {
  eba_none = 0,
  eba_open,      /* The record opens a bracket. */
  eba_advance,   /* The record moves the innermost bracket to its next
                    phase. */
  eba_close      /* The record closes the innermost bracket. */
};  /* a_bracket_action */

/* What a record did to the bracket and scope-region stacks, handed to the
   record handler with the record (all zero for a record that did nothing). */
struct a_scope_event {
  uint8_t   bracket_action;   /* a_bracket_action. */
  uint8_t   bracket_kind;     /* a_bracket_kind of the affected bracket. */
  uint8_t   opened;           /* Scope regions the record opened (0..1). */
  uint8_t   closed;           /* Scope regions it closed (0..2), innermost
                                 first in closed_kind. */
  uint8_t   closed_kind[2];   /* a_region_kind. */
  uint32_t  bracket_depth;    /* Open brackets after the record (function
                                 bodies are not brackets). */

  a_scope_event()
    : bracket_action(eba_none), bracket_kind(0), opened(0), closed(0),
      bracket_depth(0)
    { closed_kind[0] = closed_kind[1] = rk_none; }
};  /* a_scope_event */

/*
The one bracket matcher of the library: BEGIN/ELSE/END nesting, FUNC bodies
and the scope regions, on heap stacks.  The validator and (through
a_scope_event) the emitter share its result.  The effects on the id tables
(locals of a closed region die, a function's L-ids are dropped) are applied
by commit(), after the record handler ran, so a handler still sees the
closing scope's payloads.
*/
class a_bracket_tracker {
public:
  a_bracket_tracker();

  void begin_frame();
  bool check(const a_record &record, uint16_t stream_minor,
             std::string &problem, a_scope_event &event);
  /* Apply the record's bracket rules.  False with a message in problem. */
  void note_local(size_t lid_index);
  /* A value L-id (at this index of the L-id table) belongs to the innermost
     open scope region. */
  void commit(a_id_tables &ids, bool kill_locals);
  /* After the handler: mark the locals of the regions the record closed as
     dead (when kill_locals) and drop a finished function's L-ids. */

  size_t function_depth() const
    { return function_bases_.size(); }
  size_t static_init_depth() const
    { return static_init_depth_; }
  bool static_init_is_global() const
  /* The innermost bracket is a STATIC_INIT opened outside a function: it
     has an L-id space of its own. */
    {
      return !brackets_.empty() && brackets_.back().kind == bk_static_init &&
             (brackets_.back().flags & abf_own_ids) != 0;
    }
  void mark_static_init_exit_only()
  /* The STATIC_INIT just opened may only hold the exit cleanup of a
     variable that has a VAR_INIT. */
    {
      if (!brackets_.empty() && brackets_.back().kind == bk_static_init)
        brackets_.back().flags |= abf_exit_only;
    }
  size_t bracket_depth() const
    { return brackets_.size(); }
  const char *innermost_name() const
    { return brackets_.empty() ? "" : bracket_name(brackets_.back().kind); }

private:
  struct a_bracket {
    uint8_t kind;
    uint8_t phase;
    uint8_t has_else;
    uint8_t case_ok;   /* A case label may sit here (a SWITCH body or BLOCKs
                          nested directly in it). */
    uint8_t flags = 0; /* bk_static_init: abf_*. */
  };  /* a_bracket */
  enum { abf_exit_only = 1,   /* Only the exit cleanup of a variable that has
                                 a VAR_INIT. */
         abf_own_ids = 2 };   /* Opened outside a function: fresh L-id
                                 space. */
  struct a_region {
    uint8_t kind;
    size_t  first;     /* Index into scope_locals_ of its first local. */
  };  /* a_region */
  bool apply_regions(uint8_t opcode, std::string &problem,
                     a_scope_event &event);
  bool check_v1(const a_record &record, std::string &problem);
  /* The structural rules of the C++ (minor 8) records. */
  bool conditional_without_fullexpr() const;

  std::vector<a_bracket>   brackets_;
  std::vector<size_t>      function_bases_;  /* Bracket depth at each
                                                FUNC_BEGIN. */
  std::vector<a_region>    regions_;
  std::vector<size_t>      scope_locals_;
  uint32_t                 switch_depth_;
  uint32_t                 cleanup_code_depth_;  /* Open cleanup code parts. */
  uint32_t                 static_init_depth_;
  uint32_t                 fullexpr_depth_;
  uint32_t                 conditional_depth_;   /* Open TERN/LAND/LOR. */
  bool                     body_started_;        /* Past the ctor prologue. */
  uint32_t                 meminit_count_;
  bool                     meminit_delegation_;
  uint64_t                 meminit_last_idx_;
  size_t                   kill_from_;       /* Lowest region start closed by
                                                the last record. */
  bool                     ended_function_;
};  /* a_bracket_tracker */

/* Reference checking. */

/*
Checks that every id a record refers to was introduced earlier: string ids
(STR), G-ids (TY_*, DECL_*), and L-ids within the current function (PARAM,
LOCAL, MATERIALIZE_TEMP, LABEL; a GOTO may precede its LABEL).  Also that
FIELD/EXTRACT/AGG_SEL/C_SEL name complete records with an index in range, the
declaration rules (kinds, effective names and one G-id per external symbol,
one body per function, CALL names a function) and the scope rule of the
locals.  The facts live in the shared id tables.  It runs inside
a_stream_reader after the section and bracket checks.
*/
class a_ref_checker {
public:
  a_ref_checker(a_id_tables &ids, a_bracket_tracker &tracker);

  void begin_frame();
  bool check(const a_record &record, std::string &problem);
  /* False with a message in problem when the record breaks a rule. */
  bool end_frame(std::string &problem);
  /* The frame is complete: no record body may still await its fields. */

private:
  bool need_sid(const an_operand &op, bool optional, const char *what,
                std::string &problem);
  bool need_type(const an_operand &op, bool optional, std::string &problem);
  bool need_complete(const an_operand &op, std::string &problem,
                     const a_gid_entry *&out);
  bool need_object(const an_operand &op, const char *what,
                   std::string &problem);
  a_gid_entry *introduce(const an_operand &op, a_gid_kind kind,
                         std::string &problem);
  bool declare(const a_record &r, a_gid_kind kind, std::string &problem);
  bool apply_name(uint64_t g, a_gid_entry &entry, uint64_t sid,
                  uint64_t linkage, std::string &problem);
  bool introduce_local(const an_operand &op, a_lid_state state,
                       std::string &problem);
  bool check_selector(uint64_t index, uint64_t repeat, bool is_const,
                      std::string &problem);

  /* The function whose body is being read (a stack: minor-6 streams may
     nest bodies). */
  struct a_fn_state {
    uint32_t  ret_type;   /* Return type G-id. */
    uint32_t  params;     /* PARAM records so far. */
  };  /* a_fn_state */
  bool returns_aggregate() const;
  bool check_v1(const a_record &record, std::string &problem);
  /* The operand rules of the C++ records that need the id tables. */
  bool check_structor(const a_record &record, std::string &problem);
  bool check_comdat(uint64_t decl_flags, uint64_t linkage, a_gid_kind kind,
                    std::string &problem) const;
  /* COMDAT, linkonce_odr and weak_odr describe definitions (spec 11.2). */

  a_id_tables             &ids_;
  a_bracket_tracker       &tracker_;
  uint64_t                 fields_pending_;
  uint32_t                 cur_record_;     /* TY_RECORD_BODY being read. */
  bool                     members_seen_;   /* A non-base field was read. */
  std::vector<a_fn_state>  fns_;
  std::vector<bool>        static_own_ids_;  /* Per open STATIC_INIT. */
  std::vector<uint32_t>    agg_targets_;
  std::vector<uint32_t>    const_targets_;
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
  a_scope_event       scope;     /* Bracket and scope-region effects. */

  a_record_context()
    : frame(nullptr), in_body(false), function_depth(0), offset(0)
    {}
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
  a_id_tables &ids()
  /* The id tables the validator fills (strings, G-ids, L-ids).  They are
     populated only while the reader validates (options.check). */
    { return ids_; }
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

  size_t unit_size(const uint8_t *view, size_t view_size, bool &ok);
  bool process_unit(const uint8_t *data);
  bool process_preamble(const uint8_t *data);
  bool process_frame(const uint8_t *header, const uint8_t *payload);
  bool process_records(const a_frame_info &frame);
  bool check_section(const a_record &record, const a_frame_info &frame,
                     bool &in_body);
  bool check_brackets(const a_record &record, a_scope_event &event);
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
  std::vector<uint8_t>
                     pending_;     /* Partial preamble, header or frame. */
  a_id_tables        ids_;
  a_bracket_tracker  tracker_;
  a_ref_checker      refs_;
};  /* a_stream_reader */

}  /* namespace ecil */

#endif /* ECIL_READER_H */
