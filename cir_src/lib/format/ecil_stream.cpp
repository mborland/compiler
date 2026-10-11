/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
ecil_stream.cpp -- chunked frame splitter, stream validator and record
driver (a_stream_reader).  Bracket tracking uses a heap vector, never the
call stack.
*/

#include <cstdio>
#include <cstring>

#include "ecil_reader.h"

namespace ecil {

namespace {

uint32_t le32(const uint8_t *p)
/*
Read a little-endian u32.
*/
{
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}  /* le32 */

uint16_t le16(const uint8_t *p)
/*
Read a little-endian u16.
*/
{
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}  /* le16 */

}  /* anonymous namespace */

a_stream_reader::a_stream_reader(a_stream_handler &handler,
                                 const a_reader_options &options)
/*
Create a reader that reports to handler.
*/
  : handler_(handler), options_(options), state_(state_preamble),
    stopped_(false), aborted_(false), seen_end_(false), offset_(0),
    frame_count_(0), end_n_frames_(0), end_status_(0), refs_(ids_, tracker_)
{
  preamble_.major = 0;
  preamble_.minor = 0;
  preamble_.features = 0;
}  /* a_stream_reader::a_stream_reader */

bool a_stream_reader::fail(an_error_kind kind, const std::string &message,
                           uint64_t offset)
/*
Record the first error and return false.
*/
{
  if (error_.ok()) {
    error_.kind = kind;
    error_.message = message;
    error_.stream_offset = offset;
    error_.frame_index = state_ == state_preamble ? -1 : frame_count_;
  }
  return false;
}  /* a_stream_reader::fail */

bool a_stream_reader::process_preamble(const uint8_t *data)
/*
Validate the 16-byte preamble and report it.
*/
{
  static const uint8_t magic[8] = {ECIL_MAGIC_0, ECIL_MAGIC_1, ECIL_MAGIC_2,
                                   ECIL_MAGIC_3, ECIL_MAGIC_4, ECIL_MAGIC_5,
                                   ECIL_MAGIC_6, ECIL_MAGIC_7};
  bool result = true;

  if (std::memcmp(data, magic, sizeof magic) != 0) {
    result = fail(an_error_kind::format,
                  "bad magic: not an ECIL stream (or damaged by text-mode "
                  "transfer)", offset_);
  } else {
    preamble_.major = le16(data + 8);
    preamble_.minor = le16(data + 10);
    preamble_.features = le32(data + 12);
    if (preamble_.major != ECIL_VERSION_MAJOR) {
      char buffer[128];
      std::snprintf(buffer, sizeof buffer, "ECIL major version %u is not "
                    "supported (reader is %d.%d)", preamble_.major,
                    ECIL_VERSION_MAJOR, ECIL_VERSION_MINOR);
      result = fail(an_error_kind::version, buffer, offset_ + 8);
    } else if (preamble_.features & ECIL_FEATURE_REQUIRED_MASK) {
      char buffer[128];
      std::snprintf(buffer, sizeof buffer, "required feature bits 0x%08X are "
                    "not supported by this reader",
                    preamble_.features & ECIL_FEATURE_REQUIRED_MASK);
      result = fail(an_error_kind::version, buffer, offset_ + 12);
    }
  }
  offset_ += ECIL_PREAMBLE_SIZE;
  state_ = state_frame_header;
  if (result && !handler_.on_preamble(preamble_)) {
    stopped_ = true;
    result = false;
  }
  return result;
}  /* a_stream_reader::process_preamble */

bool a_stream_reader::check_section(const a_record &record,
                                    const a_frame_info &frame, bool &in_body)
/*
Check that record may appear in this frame kind and section, and that the
feature bits it needs are set.
*/
{
  const an_opcode_info &info = *record.info;
  uint8_t op = record.opcode;
  uint8_t sec = info.section;
  bool result = true;
  bool decl_ok = (sec & (eos_type | eos_decl | eos_const)) != 0 ||
                 op == eop_str || op == eop_loc || op == eop_ext ||
                 op == eop_name_bind || op == eop_retract ||
                 op == eop_hdr_type_role;
  const char *problem = nullptr;

  if (op == eop_loc && !(preamble_.features & ECIL_FEATURE_LOCATIONS))
    problem = "LOC record without the LOCATIONS feature bit";
  else if (op == eop_name_bind && !(preamble_.features &
                                    ECIL_FEATURE_LATE_NAMES))
    problem = "NAME_BIND record without the LATE_NAMES feature bit";
  else if (op == eop_refs && !(preamble_.features & ECIL_FEATURE_REFS))
    problem = "REFS record without the REFS feature bit";
  else {
    switch (frame.kind) {
    case efk_header:
      if (!((sec & (eos_header | eos_type)) != 0 || op == eop_str ||
            op == eop_loc || op == eop_ext || op == eop_hdr_type_role))
        problem = "record not allowed in F_HEADER";
      break;
    case efk_decls:
      if (!decl_ok || (sec & eos_header))
        problem = "record not allowed in a declaration frame";
      break;
    case efk_globals:
      /* A declaration section, then (C++) a body section of STATIC_INIT
         groups that starts at the first STATIC_INIT_BEGIN. */
      if (!in_body) {
        if (op == eop_static_init_begin)
          in_body = true;
        else if (!decl_ok || (sec & eos_header))
          problem = (sec & eos_body)
                      ? "body record before STATIC_INIT_BEGIN"
                      : "record not allowed in a declaration frame";
      }
      if (in_body && problem == nullptr) {
        bool body_ok = (sec & (eos_body | eos_const)) != 0 ||
                       op == eop_loc || op == eop_ext;
        if (!body_ok)
          problem = "declaration record after STATIC_INIT_BEGIN";
        else if ((sec & eos_body) && op != eop_static_init_begin &&
                 tracker_.static_init_depth() == 0)
          problem = "body record outside STATIC_INIT_BEGIN..STATIC_INIT_END";
      }
      break;
    case efk_region:
      if (!in_body) {
        if (op == eop_func_begin)
          in_body = true;
        else if (!decl_ok || (sec & eos_header))
          problem = (sec & eos_body)
                      ? "body record before FUNC_BEGIN"
                      : "record not allowed in the declaration section";
      }
      if (in_body && problem == nullptr) {
        bool body_ok = (sec & (eos_body | eos_const)) != 0 ||
                       op == eop_loc || op == eop_ext || op == eop_refs;
        if (!body_ok)
          problem = "declaration record after FUNC_BEGIN";
        else if (op == eop_refs && tracker_.function_depth() != 0)
          problem = "REFS inside a function body";
        else if ((sec & eos_body) && op != eop_func_begin &&
                 tracker_.function_depth() == 0)
          problem = "body record outside FUNC_BEGIN..FUNC_END";
      }
      break;
    case efk_names:
      if (!(op == eop_name_bind || op == eop_str || op == eop_loc ||
            op == eop_ext))
        problem = "record not allowed in F_NAMES";
      break;
    case efk_end:
      if (op != eop_end)
        problem = "only END is allowed in F_END";
      break;
    default:
      break;
    }
    if (problem == nullptr && op == eop_end && frame.kind != efk_end)
      problem = "END record outside F_END";
    if (problem == nullptr && op == eop_refs && frame.kind != efk_region)
      problem = "REFS record outside F_REGION";
  }
  if (problem != nullptr)
    result = fail(an_error_kind::format,
                  std::string(info.name) + ": " + problem, offset_);
  return result;
}  /* a_stream_reader::check_section */

bool a_stream_reader::check_brackets(const a_record &record,
                                     a_scope_event &event)
/*
Track FUNC_BEGIN/FUNC_END, the BEGIN/ELSE/END brackets and the scope regions
(a_bracket_tracker), and reject mismatches.
*/
{
  std::string problem;
  bool result = tracker_.check(record, preamble_.minor, problem, event);

  if (!result)
    result = fail(an_error_kind::format, problem, offset_);
  return result;
}  /* a_stream_reader::check_brackets */

bool a_stream_reader::check_refs(const a_record &record)
/*
Check that every id the record uses was introduced (see a_ref_checker).
*/
{
  std::string problem;
  bool result = refs_.check(record, problem);

  if (result && record.opcode == eop_hdr_lang) {
    /* A C++ stream with exceptions needs a later ECIL (M4); v1 is
       exceptions-off. */
    uint64_t language = 0;
    uint64_t lang_flags = 0;
    operand_u64(record.ops[0], language);
    operand_u64(record.ops[2], lang_flags);
    if (language == elang_cxx && (lang_flags & ECIL_LANG_EXCEPTIONS))
      result = fail(an_error_kind::version,
                    "HDR_LANG: C++ with exceptions needs a newer ECIL (v1 "
                    "is exceptions-off)", offset_);
  } else if (!result) {
    result = fail(an_error_kind::format,
                  std::string(record.info->name) + ": " + problem, offset_);
  }
  return result;
}  /* a_stream_reader::check_refs */

bool a_stream_reader::process_records(const a_frame_info &frame)
/*
Decode and check every record of the frame and pass it to the handler.
*/
{
  bool result = true;
  bool in_body = false;
  uint32_t end_records = 0;
  a_cursor cursor(frame.payload, frame.payload + frame.length);
  a_decode_context context;
  a_scope_event event;
  std::string frame_problem;

  context.stream_minor = preamble_.minor;
  context.allow_supp = (frame.flags & ECIL_FRAME_FLAG_HAS_SUPP) != 0;
  tracker_.begin_frame();
  refs_.begin_frame();
  while (result && cursor.remaining() > 0) {
    a_record record;
    an_error decode_error;
    const uint8_t *record_start = cursor.p;
    uint64_t record_offset = frame.offset + ECIL_FRAME_HEADER_SIZE +
                             static_cast<uint64_t>(record_start -
                                                   frame.payload);

    if (!decode_record(cursor, context, record, decode_error)) {
      result = fail(decode_error.kind, decode_error.message, record_offset);
    } else {
      offset_ = record_offset;  /* For messages from the checks. */
      event = a_scope_event();
      if (options_.check) {
        an_error operand_error;
        if (!check_record_operands(record, preamble_.minor, operand_error))
          result = fail(operand_error.kind, operand_error.message,
                        record_offset);
        else
          result = check_section(record, frame, in_body) &&
                   check_brackets(record, event) && check_refs(record);
      }
      if (result) {
        a_record_context rc;
        rc.frame = &frame;
        rc.in_body = in_body;
        rc.function_depth = static_cast<uint32_t>(tracker_.function_depth());
        rc.offset = record_offset;
        rc.scope = event;
        if (record.opcode == eop_end) {
          ++end_records;
          uint64_t v = 0;
          if (operand_u64(record.ops[0], v)) {
            end_n_frames_ = static_cast<uint32_t>(v);
            if (options_.check && static_cast<int64_t>(v) != frame.index)
              result = fail(an_error_kind::format,
                            "END n_frames does not match the number of "
                            "preceding frames", record_offset);
          }
          if (operand_u64(record.ops[1], v))
            end_status_ = static_cast<uint32_t>(v);
        }
        if (!handler_.on_record(record, rc)) {
          stopped_ = true;
          result = false;
        } else if (options_.check) {
          /* After the handler: the closed scopes' locals die and a finished
             function's L-ids go, so the handler saw them intact. */
          tracker_.commit(ids_, preamble_.minor >= 7);
        }
      }
    }
  }
  if (result && options_.check) {
    if (tracker_.function_depth() != 0)
      result = fail(an_error_kind::format,
                    "frame ends inside a function body", frame.offset);
    else if (tracker_.bracket_depth() != 0)
      result = fail(an_error_kind::format,
                    std::string("frame ends with an open ") +
                    tracker_.innermost_name() + std::string(" bracket"),
                    frame.offset);
    else if (!refs_.end_frame(frame_problem))
      result = fail(an_error_kind::format, frame_problem, frame.offset);
    else if (frame.kind == efk_end && end_records != 1)
      result = fail(an_error_kind::format,
                    "F_END must contain exactly one END record",
                    frame.offset);
  }
  return result;
}  /* a_stream_reader::process_records */

bool a_stream_reader::process_frame(const uint8_t *header,
                                    const uint8_t *payload)
/*
Validate a frame header, then decode its payload.
*/
{
  bool result = true;
  a_frame_info frame;

  frame.length = le32(header);
  frame.kind = header[4];
  frame.flags = header[5];
  frame.reserved = le16(header + 6);
  frame.offset = offset_;
  frame.index = frame_count_;
  frame.payload = payload;
  if (options_.check) {
    if (frame.reserved != 0)
      result = fail(an_error_kind::format, "frame reserved field is not 0",
                    offset_ + 6);
    else if (frame.flags & ~ECIL_FRAME_FLAG_HAS_SUPP)
      result = fail(an_error_kind::format, "unknown frame flag bits",
                    offset_ + 5);
    else if ((frame.flags & ECIL_FRAME_FLAG_HAS_SUPP) &&
             !(preamble_.features & ECIL_FEATURE_SUPP))
      result = fail(an_error_kind::format,
                    "HAS_SUPP frame without the SUPP feature bit",
                    offset_ + 5);
    else if (frame_count_ == 0 && frame.kind != efk_header)
      result = fail(an_error_kind::format, "the first frame must be F_HEADER",
                    offset_ + 4);
    else if (frame_count_ > 0 && frame.kind == efk_header)
      result = fail(an_error_kind::format, "duplicate F_HEADER", offset_ + 4);
    else if (frame.kind < efk_first_ext && frame_kind_name(frame.kind) ==
                                           nullptr) {
      char buffer[128];
      std::snprintf(buffer, sizeof buffer, "unknown frame kind 0x%02X%s",
                    frame.kind, preamble_.minor > ECIL_VERSION_MINOR
                      ? " (stream needs a newer ECIL minor)" : "");
      result = fail(preamble_.minor > ECIL_VERSION_MINOR
                      ? an_error_kind::version : an_error_kind::format,
                    buffer, offset_ + 4);
    }
  }
  if (result) {
    if (frame.kind >= efk_first_ext) {
      if (!handler_.on_extension_frame(frame)) {
        stopped_ = true;
        result = false;
      }
    } else if (!handler_.on_frame_begin(frame)) {
      stopped_ = true;
      result = false;
    } else {
      if (frame.kind != efk_abort)
        result = process_records(frame);
      if (result && !handler_.on_frame_end(frame)) {
        stopped_ = true;
        result = false;
      }
      if (result && frame.kind == efk_end) {
        seen_end_ = true;
        state_ = state_done;
      }
      if (result && frame.kind == efk_abort) {
        aborted_ = true;
        state_ = state_done;
      }
    }
  }
  offset_ = frame.offset + ECIL_FRAME_HEADER_SIZE + frame.length;
  ++frame_count_;
  return result;
}  /* a_stream_reader::process_frame */

size_t a_stream_reader::unit_size(const uint8_t *view, size_t view_size,
                                  bool &ok)
/*
Return the size of the current unit (preamble or frame) given the bytes
visible so far; before a frame header is complete, return the header size.
Set ok to false if the declared payload exceeds the reader limit.
*/
{
  size_t result = ECIL_PREAMBLE_SIZE;

  if (state_ != state_preamble) {
    result = ECIL_FRAME_HEADER_SIZE;
    if (view_size >= ECIL_FRAME_HEADER_SIZE) {
      uint32_t payload_length = le32(view);
      if (payload_length > options_.max_frame)
        ok = fail(an_error_kind::format,
                  "frame payload length exceeds the reader limit", offset_);
      else
        result = ECIL_FRAME_HEADER_SIZE + payload_length;
    }
  }
  return result;
}  /* a_stream_reader::unit_size */

bool a_stream_reader::process_unit(const uint8_t *data)
/*
Process one complete preamble or frame starting at data.
*/
{
  return state_ == state_preamble
           ? process_preamble(data)
           : process_frame(data, data + ECIL_FRAME_HEADER_SIZE);
}  /* a_stream_reader::process_unit */

bool a_stream_reader::consume(const void *bytes, size_t length)
/*
Feed the next bytes.  Whole frames inside the chunk are decoded in place; a
partial trailing frame is buffered.  Return false on a stream error or when
a handler callback stopped.
*/
{
  const uint8_t *p = static_cast<const uint8_t *>(bytes);
  size_t left = length;
  bool ok = error_.ok() && !stopped_;

  while (ok && left > 0 && state_ != state_done) {
    if (!pending_.empty()) {
      size_t need = unit_size(pending_.data(), pending_.size(), ok);
      if (ok) {
        size_t take_now = need > pending_.size() ? need - pending_.size() : 0;
        if (take_now > left)
          take_now = left;
        pending_.insert(pending_.end(), p, p + take_now);
        p += take_now;
        left -= take_now;
        need = unit_size(pending_.data(), pending_.size(), ok);
        if (ok && pending_.size() >= need) {
          ok = process_unit(pending_.data());
          pending_.clear();
        }
      }
    } else {
      size_t need = unit_size(p, left, ok);
      if (ok) {
        if (left >= need) {
          ok = process_unit(p);
          p += need;
          left -= need;
        } else {
          pending_.assign(p, p + left);
          p += left;
          left = 0;
        }
      }
    }
  }
  if (ok && left > 0 && state_ == state_done)
    ok = fail(an_error_kind::format, "data after the end of the stream",
              offset_);
  return ok;
}  /* a_stream_reader::consume */

bool a_stream_reader::finish()
/*
Check that F_END or F_ABORT was seen.
*/
{
  bool result = error_.ok() && !stopped_;

  if (result && state_ != state_done) {
    result = fail(an_error_kind::format,
                  pending_.empty() && state_ != state_preamble
                    ? "stream ended before F_END"
                    : "truncated stream (partial preamble or frame)",
                  offset_);
  }
  return result;
}  /* a_stream_reader::finish */

}  /* namespace ecil */
