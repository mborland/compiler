/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
ecil_text.cpp -- dump (binary to text) and assemble (text to binary).
*/

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ecil_abi_decode.h"
#include "ecil_text.h"

namespace ecil {

namespace {

/* Dump. */

void append_hex(std::string &out, uint64_t value, int digits)
/*
Append value as 0x followed by exactly "digits" lowercase hex digits.
*/
{
  static const char hex[] = "0123456789abcdef";

  out += "0x";
  for (int k = digits - 1; k >= 0; --k)
    out += hex[(value >> (4 * k)) & 0xF];
}  /* append_hex */

void append_hex_bytes_msb(std::string &out, const uint8_t *data, uint32_t n)
/*
Append 0x and the n little-endian bytes as a big-endian hex number.
*/
{
  static const char hex[] = "0123456789abcdef";

  out += "0x";
  for (uint32_t k = n; k > 0; --k) {
    out += hex[data[k - 1] >> 4];
    out += hex[data[k - 1] & 0xF];
  }
}  /* append_hex_bytes_msb */

void append_string(std::string &out, const uint8_t *data, uint32_t n)
/*
Append a quoted string with \" \\ and \xHH escapes.
*/
{
  static const char hex[] = "0123456789abcdef";

  out += '"';
  for (uint32_t k = 0; k < n; ++k) {
    uint8_t c = data[k];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c >= 0x20 && c <= 0x7E) {
      out += static_cast<char>(c);
    } else {
      out += "\\x";
      out += hex[c >> 4];
      out += hex[c & 0xF];
    }
  }
  out += '"';
}  /* append_string */

uint8_t canonical_unsigned_tag(uint64_t v)
/*
Return the shortest unsigned tag for v.
*/
{
  uint8_t tag = ECIL_TAG_U64;

  if (v <= 0x7F)
    tag = static_cast<uint8_t>(v);
  else if (v <= 0xFF)
    tag = ECIL_TAG_U8;
  else if (v <= 0xFFFF)
    tag = ECIL_TAG_U16;
  else if (v <= 0xFFFFFFFFu)
    tag = ECIL_TAG_U32;
  return tag;
}  /* canonical_unsigned_tag */

uint8_t canonical_negative_tag(int64_t v)
/*
Return the shortest tag for a negative integer v.
*/
{
  uint8_t tag = ECIL_TAG_I64;

  if (v >= -32)
    tag = static_cast<uint8_t>(v);
  else if (v >= -128)
    tag = ECIL_TAG_I8;
  else if (v >= -32768)
    tag = ECIL_TAG_I16;
  else if (v >= -2147483647 - 1)
    tag = ECIL_TAG_I32;
  return tag;
}  /* canonical_negative_tag */

const char *int_prefix(uint8_t tag)
/*
Return the spelling prefix of an explicit integer tag.
*/
{
  const char *result = "?";

  switch (tag) {
  case ECIL_TAG_U8: result = "u8"; break;
  case ECIL_TAG_U16: result = "u16"; break;
  case ECIL_TAG_U32: result = "u32"; break;
  case ECIL_TAG_U64: result = "u64"; break;
  case ECIL_TAG_I8: result = "i8"; break;
  case ECIL_TAG_I16: result = "i16"; break;
  case ECIL_TAG_I32: result = "i32"; break;
  case ECIL_TAG_I64: result = "i64"; break;
  default: break;
  }
  return result;
}  /* int_prefix */

void append_operand(std::string &out, const an_operand &op)
/*
Append the canonical text of one operand.
*/
{
  char buffer[48];

  switch (op.kind) {
  case an_operand_kind::nil:
    out += "nil";
    break;
  case an_operand_kind::boolean:
    out += op.u ? "true" : "false";
    break;
  case an_operand_kind::uint:
    if (op.tag != canonical_unsigned_tag(op.u)) {
      out += int_prefix(op.tag);
      out += ':';
    }
    std::snprintf(buffer, sizeof buffer, "%llu",
                  static_cast<unsigned long long>(op.u));
    out += buffer;
    break;
  case an_operand_kind::sint:
    if (op.i >= 0 || op.tag != canonical_negative_tag(op.i)) {
      out += int_prefix(op.tag);
      out += ':';
    }
    std::snprintf(buffer, sizeof buffer, "%lld",
                  static_cast<long long>(op.i));
    out += buffer;
    break;
  case an_operand_kind::f32:
    out += "f32:";
    append_hex(out, op.u, 8);
    break;
  case an_operand_kind::f64:
    out += "f64:";
    append_hex(out, op.u, 16);
    break;
  case an_operand_kind::fbits:
    std::snprintf(buffer, sizeof buffer, "fbits:%u:", op.length);
    out += buffer;
    append_hex_bytes_msb(out, op.data, op.length);
    break;
  case an_operand_kind::u128:
  case an_operand_kind::i128:
    out += op.kind == an_operand_kind::u128 ? "u128:" : "i128:";
    append_hex_bytes_msb(out, op.data, 16);
    break;
  case an_operand_kind::bin: {
    uint8_t canonical = op.length <= 31
                          ? static_cast<uint8_t>(ECIL_TAG_FIXBIN + op.length)
                          : op.length <= 0xFF ? ECIL_TAG_BIN8
                          : op.length <= 0xFFFF ? ECIL_TAG_BIN16
                                                : ECIL_TAG_BIN32;
    if (op.tag != canonical) {
      out += op.tag == ECIL_TAG_BIN8 ? "bin8:"
             : op.tag == ECIL_TAG_BIN16 ? "bin16:" : "bin32:";
    }
    append_string(out, op.data, op.length);
    break;
  }
  case an_operand_kind::array: {
    uint8_t canonical = op.count <= 31
                          ? static_cast<uint8_t>(ECIL_TAG_FIXARRAY + op.count)
                          : op.count <= 0xFFFF ? ECIL_TAG_ARR16
                                               : ECIL_TAG_ARR32;
    an_array_reader reader = array_elements(op);
    an_operand element;
    bool first = true;

    if (op.tag != canonical)
      out += op.tag == ECIL_TAG_ARR16 ? "arr16" : "arr32";
    out += '[';
    while (next_array_element(reader, element)) {
      if (!first)
        out += ", ";
      first = false;
      append_operand(out, element);
    }
    out += ']';
    break;
  }
  }
}  /* append_operand */

class a_dump_handler : public a_stream_handler {
public:
  a_dump_handler(std::string &out, const a_dump_options &options)
    : out_(out), options_(options) {}

  bool on_preamble(const a_preamble &preamble) override;
  bool on_frame_begin(const a_frame_info &frame) override;
  bool on_record(const a_record &record,
                 const a_record_context &context) override;
  bool on_frame_end(const a_frame_info &frame) override;
  bool on_extension_frame(const a_frame_info &frame) override;

private:
  void frame_line(const a_frame_info &frame, bool raw);

  std::string           &out_;
  const a_dump_options  &options_;
};  /* a_dump_handler */

bool a_dump_handler::on_preamble(const a_preamble &preamble)
/*
Write the "ecil" line.
*/
{
  char buffer[64];

  std::snprintf(buffer, sizeof buffer, "ecil %u.%u features=", preamble.major,
                preamble.minor);
  out_ += buffer;
  append_hex(out_, preamble.features, 8);
  out_ += '\n';
  return true;
}  /* a_dump_handler::on_preamble */

void a_dump_handler::frame_line(const a_frame_info &frame, bool raw)
/*
Write the "frame" line (with the payload bytes when raw).
*/
{
  const char *name = frame_kind_name(frame.kind);
  char buffer[32];

  out_ += "frame ";
  if (name != nullptr) {
    out_ += name;
  } else {
    std::snprintf(buffer, sizeof buffer, "0x%02x", frame.kind);
    out_ += buffer;
  }
  if (frame.flags & ECIL_FRAME_FLAG_HAS_SUPP)
    out_ += " supp";
  if (frame.flags & ~ECIL_FRAME_FLAG_HAS_SUPP) {
    out_ += " flags=";
    append_hex(out_, frame.flags & ~ECIL_FRAME_FLAG_HAS_SUPP, 2);
  }
  if (frame.reserved != 0) {
    std::snprintf(buffer, sizeof buffer, " reserved=%u", frame.reserved);
    out_ += buffer;
  }
  if (raw) {
    static const char hex[] = "0123456789abcdef";
    out_ += " bytes=";
    for (uint32_t k = 0; k < frame.length; ++k) {
      out_ += hex[frame.payload[k] >> 4];
      out_ += hex[frame.payload[k] & 0xF];
    }
  }
  out_ += '\n';
}  /* a_dump_handler::frame_line */

bool a_dump_handler::on_frame_begin(const a_frame_info &frame)
/*
Write the frame line; abort frames show their payload bytes.
*/
{
  frame_line(frame, frame.kind == efk_abort && frame.length > 0);
  return true;
}  /* a_dump_handler::on_frame_begin */

bool a_dump_handler::on_extension_frame(const a_frame_info &frame)
/*
Write an extension frame with its raw payload.
*/
{
  frame_line(frame, true);
  return true;
}  /* a_dump_handler::on_extension_frame */

bool a_dump_handler::on_frame_end(const a_frame_info &)
/*
Nothing to write.
*/
{
  return true;
}  /* a_dump_handler::on_frame_end */

bool a_dump_handler::on_record(const a_record &record,
                               const a_record_context &context)
/*
Write one record line.
*/
{
  if (options_.show_offsets) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "  ; @%llu\n",
                  static_cast<unsigned long long>(context.offset));
    out_ += buffer;
  }
  out_ += "  ";
  out_ += record.info->name;
  for (uint8_t k = 0; k < record.count; ++k) {
    out_ += ' ';
    append_operand(out_, record.ops[k]);
  }
  if (record.supp != nullptr) {
    uint8_t canonical = record.supp_length <= 0xFF ? ECIL_TAG_SUPP8
                        : record.supp_length <= 0xFFFF ? ECIL_TAG_SUPP16
                                                       : ECIL_TAG_SUPP32;
    bool first = true;

    out_ += " supp";
    if (record.supp_tag != canonical)
      out_ += record.supp_tag == ECIL_TAG_SUPP16 ? "16" : "32";
    out_ += '{';
    /* Keys keep their own tags: re-decode to print them exactly. */
    a_cursor cursor(record.supp, record.supp + record.supp_length);
    an_error ignored;
    an_operand key;
    an_operand value;
    while (cursor.remaining() > 0 &&
           decode_operand(cursor, key, ignored) &&
           decode_operand(cursor, value, ignored)) {
      if (!first)
        out_ += ", ";
      first = false;
      append_operand(out_, key);
      out_ += ": ";
      append_operand(out_, value);
    }
    out_ += '}';
  }
  out_ += '\n';
  return true;
}  /* a_dump_handler::on_record */

/* Assemble. */

class an_assembler {
public:
  bool run(const std::string &text, std::vector<uint8_t> &out,
           std::string &error);

private:
  bool line(const std::string &text);
  bool directive_frame(const std::string &rest);
  bool directive_ecil(const std::string &rest);
  bool directive_hex(const std::string &rest, std::vector<uint8_t> &target);
  bool record(const std::string &text, bool check_arity);
  void end_frame();
  bool fail(const std::string &message);

  bool parse_operand(const std::string &s, size_t &pos,
                     std::vector<uint8_t> &out, bool top_level);
  bool parse_number(const std::string &s, size_t &pos, bool allow_negative,
                    bool &negative, uint64_t &magnitude);
  bool parse_string(const std::string &s, size_t &pos,
                    std::vector<uint8_t> &bytes);
  bool parse_hex_value(const std::string &s, size_t &pos, uint8_t *bytes,
                       size_t n);
  bool expect_colon(const std::string &s, size_t &pos,
                    const std::string &word);
  bool parse_array_elements(const std::string &s, size_t &pos,
                            std::vector<uint8_t> &body, uint32_t &count);
  bool parse_supp(const std::string &s, size_t &pos,
                  std::vector<uint8_t> &out, uint8_t explicit_tag);
  void skip_spaces(const std::string &s, size_t &pos);

  std::vector<uint8_t>  stream_;
  std::vector<uint8_t>  payload_;
  bool                  in_frame_ = false;
  uint8_t               frame_kind_ = 0;
  uint8_t               frame_flags_ = 0;
  uint16_t              frame_reserved_ = 0;
  bool                  frame_len_override_ = false;
  uint32_t              frame_len_ = 0;
  int                   line_no_ = 0;
  size_t                frames_written_ = 0;
  std::string           error_;
};  /* an_assembler */

bool an_assembler::fail(const std::string &message)
/*
Record an error with the current line number.
*/
{
  if (error_.empty()) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "line %d: ", line_no_);
    error_ = buffer + message;
  }
  return false;
}  /* an_assembler::fail */

void an_assembler::skip_spaces(const std::string &s, size_t &pos)
/*
Skip blanks.
*/
{
  while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t'))
    ++pos;
}  /* an_assembler::skip_spaces */

void put_u16(std::vector<uint8_t> &v, uint16_t x)
/*
Append a little-endian u16.
*/
{
  v.push_back(static_cast<uint8_t>(x));
  v.push_back(static_cast<uint8_t>(x >> 8));
}  /* put_u16 */

void put_u32(std::vector<uint8_t> &v, uint32_t x)
/*
Append a little-endian u32.
*/
{
  put_u16(v, static_cast<uint16_t>(x));
  put_u16(v, static_cast<uint16_t>(x >> 16));
}  /* put_u32 */

void put_le(std::vector<uint8_t> &v, uint64_t x, int n)
/*
Append n little-endian bytes of x.
*/
{
  for (int k = 0; k < n; ++k)
    v.push_back(static_cast<uint8_t>(x >> (8 * k)));
}  /* put_le */

bool an_assembler::parse_number(const std::string &s, size_t &pos,
                                bool allow_negative, bool &negative,
                                uint64_t &magnitude)
/*
Parse [-]decimal or [-]0xHEX into a sign and a 64-bit magnitude.
*/
{
  bool result = true;
  int base = 10;
  size_t digits = 0;

  negative = false;
  magnitude = 0;
  if (pos < s.size() && s[pos] == '-') {
    if (!allow_negative)
      result = fail("negative number not allowed here");
    negative = true;
    ++pos;
  }
  if (pos + 1 < s.size() && s[pos] == '0' && (s[pos + 1] == 'x' ||
                                              s[pos + 1] == 'X')) {
    base = 16;
    pos += 2;
  }
  while (result && pos < s.size() &&
         std::isxdigit(static_cast<unsigned char>(s[pos]))) {
    int d = std::isdigit(static_cast<unsigned char>(s[pos]))
              ? s[pos] - '0' : (std::tolower(s[pos]) - 'a' + 10);
    if (d >= base)
      break;
    uint64_t next = magnitude * static_cast<uint64_t>(base) +
                    static_cast<uint64_t>(d);
    if (magnitude != 0 && next / static_cast<uint64_t>(base) != magnitude)
      result = fail("number out of range");
    magnitude = next;
    ++pos;
    ++digits;
  }
  if (result && digits == 0)
    result = fail("expected a number");
  return result;
}  /* an_assembler::parse_number */

bool an_assembler::parse_string(const std::string &s, size_t &pos,
                                std::vector<uint8_t> &bytes)
/*
Parse a quoted string with escapes into bytes.
*/
{
  bool result = true;
  bool closed = false;

  bytes.clear();
  if (pos >= s.size() || s[pos] != '"')
    result = fail("expected a string");
  else
    ++pos;
  while (result && !closed && pos < s.size()) {
    char c = s[pos++];
    if (c == '"') {
      closed = true;
    } else if (c == '\\') {
      if (pos >= s.size()) {
        result = fail("bad escape");
      } else {
        char e = s[pos++];
        if (e == 'x' && pos + 1 < s.size() + 0 &&
            std::isxdigit(static_cast<unsigned char>(s[pos])) &&
            std::isxdigit(static_cast<unsigned char>(s[pos + 1]))) {
          auto hexval = [](char h) {
            return std::isdigit(static_cast<unsigned char>(h))
                     ? h - '0' : std::tolower(h) - 'a' + 10;
          };
          bytes.push_back(static_cast<uint8_t>(hexval(s[pos]) * 16 +
                                               hexval(s[pos + 1])));
          pos += 2;
        } else if (e == '"' || e == '\\') {
          bytes.push_back(static_cast<uint8_t>(e));
        } else if (e == 'n') {
          bytes.push_back('\n');
        } else if (e == 't') {
          bytes.push_back('\t');
        } else if (e == 'r') {
          bytes.push_back('\r');
        } else {
          result = fail("bad escape");
        }
      }
    } else {
      bytes.push_back(static_cast<uint8_t>(c));
    }
  }
  if (result && !closed)
    result = fail("unterminated string");
  return result;
}  /* an_assembler::parse_string */

bool an_assembler::parse_hex_value(const std::string &s, size_t &pos,
                                   uint8_t *bytes, size_t n)
/*
Parse 0xHEX (at most 2n digits) into n little-endian bytes.
*/
{
  bool result = true;
  size_t start;
  size_t count = 0;

  std::memset(bytes, 0, n);
  if (!(pos + 1 < s.size() && s[pos] == '0' && s[pos + 1] == 'x'))
    result = fail("expected 0xHEX");
  else
    pos += 2;
  start = pos;
  while (result && pos < s.size() &&
         std::isxdigit(static_cast<unsigned char>(s[pos])))
    ++pos;
  count = pos - start;
  if (!result) {
    /* The failure is already recorded. */
  } else if (count == 0 || count > 2 * n) {
    result = fail("hex value has the wrong number of digits");
  } else {
    for (size_t k = 0; k < count; ++k) {
      char h = s[pos - 1 - k];
      int d = std::isdigit(static_cast<unsigned char>(h))
                ? h - '0' : std::tolower(h) - 'a' + 10;
      bytes[k / 2] = static_cast<uint8_t>(bytes[k / 2] | (d << (4 * (k % 2))));
    }
  }
  return result;
}  /* an_assembler::parse_hex_value */

bool an_assembler::expect_colon(const std::string &s, size_t &pos,
                                const std::string &word)
/*
Consume the ':' that follows a typed-operand word.
*/
{
  bool result = true;

  if (pos >= s.size() || s[pos] != ':')
    result = fail("expected : after " + word);
  else
    ++pos;
  return result;
}  /* an_assembler::expect_colon */

bool an_assembler::parse_array_elements(const std::string &s, size_t &pos,
                                        std::vector<uint8_t> &body,
                                        uint32_t &count)
/*
Parse the elements of an array after its '[' up to and including the ']'.
*/
{
  bool result = true;
  bool closed = false;

  count = 0;
  skip_spaces(s, pos);
  if (pos < s.size() && s[pos] == ']') {
    ++pos;
    closed = true;
  }
  while (result && !closed) {
    result = parse_operand(s, pos, body, false);
    if (result) {
      ++count;
      skip_spaces(s, pos);
      if (pos < s.size() && s[pos] == ',') {
        ++pos;
      } else if (pos < s.size() && s[pos] == ']') {
        ++pos;
        closed = true;
      } else {
        result = fail("expected , or ] in array");
      }
    }
  }
  return result;
}  /* an_assembler::parse_array_elements */

bool an_assembler::parse_operand(const std::string &s, size_t &pos,
                                 std::vector<uint8_t> &out, bool top_level)
/*
Parse one operand at pos and append its encoding.  Arrays and strings are
only accepted when top_level (array elements are scalars).
*/
{
  bool result = true;

  skip_spaces(s, pos);
  if (pos >= s.size()) {
    result = fail("missing operand");
  } else if (s[pos] == '"') {
    std::vector<uint8_t> bytes;
    result = top_level ? parse_string(s, pos, bytes)
                       : fail("strings are not allowed in arrays");
    if (result) {
      size_t n = bytes.size();
      if (n <= 31) {
        out.push_back(static_cast<uint8_t>(ECIL_TAG_FIXBIN + n));
      } else if (n <= 0xFF) {
        out.push_back(ECIL_TAG_BIN8);
        out.push_back(static_cast<uint8_t>(n));
      } else if (n <= 0xFFFF) {
        out.push_back(ECIL_TAG_BIN16);
        put_u16(out, static_cast<uint16_t>(n));
      } else {
        out.push_back(ECIL_TAG_BIN32);
        put_u32(out, static_cast<uint32_t>(n));
      }
      out.insert(out.end(), bytes.begin(), bytes.end());
    }
  } else if (s[pos] == '[') {
    std::vector<uint8_t> body;
    uint32_t count = 0;

    if (!top_level) {
      result = fail("arrays are not allowed in arrays");
    } else {
      ++pos;
      result = parse_array_elements(s, pos, body, count);
    }
    if (result) {
      if (count <= 31) {
        out.push_back(static_cast<uint8_t>(ECIL_TAG_FIXARRAY + count));
      } else if (count <= 0xFFFF) {
        out.push_back(ECIL_TAG_ARR16);
        put_u16(out, static_cast<uint16_t>(count));
      } else {
        out.push_back(ECIL_TAG_ARR32);
        put_u32(out, count);
      }
      out.insert(out.end(), body.begin(), body.end());
    }
  } else if (std::isdigit(static_cast<unsigned char>(s[pos])) ||
             s[pos] == '-') {
    bool negative;
    uint64_t mag;
    result = parse_number(s, pos, true, negative, mag);
    if (result) {
      if (!negative) {
        uint8_t tag = canonical_unsigned_tag(mag);
        out.push_back(tag);
        if (tag >= ECIL_TAG_U8 && tag <= ECIL_TAG_U64)
          put_le(out, mag, 1 << (tag - ECIL_TAG_U8));
      } else if (mag > (uint64_t(1) << 63)) {
        result = fail("negative number out of range");
      } else {
        int64_t v = mag == (uint64_t(1) << 63)
                      ? INT64_MIN : -static_cast<int64_t>(mag);
        uint8_t tag = canonical_negative_tag(v);
        out.push_back(tag);
        if (tag >= ECIL_TAG_I8 && tag <= ECIL_TAG_I64)
          put_le(out, static_cast<uint64_t>(v), 1 << (tag - ECIL_TAG_I8));
      }
    }
  } else {
    /* A word, then a typed payload. */
    size_t start = pos;
    while (pos < s.size() &&
           (std::isalnum(static_cast<unsigned char>(s[pos])) ||
            s[pos] == '_'))
      ++pos;
    std::string word = s.substr(start, pos - start);
    if (word == "auto") {
      /* The number of frames written so far (for END n_frames). */
      uint64_t n = frames_written_;
      uint8_t tag = canonical_unsigned_tag(n);
      out.push_back(tag);
      if (tag >= ECIL_TAG_U8 && tag <= ECIL_TAG_U64)
        put_le(out, n, 1 << (tag - ECIL_TAG_U8));
    } else if (word == "nil") {
      out.push_back(ECIL_TAG_NIL);
    } else if (word == "poison") {
      out.push_back(ECIL_TAG_POISON);
    } else if (word == "true") {
      out.push_back(ECIL_TAG_TRUE);
    } else if (word == "false") {
      out.push_back(ECIL_TAG_FALSE);
    } else if (word == "u8" || word == "u16" || word == "u32" ||
               word == "u64" || word == "i8" || word == "i16" ||
               word == "i32" || word == "i64") {
      bool is_signed = word[0] == 'i';
      int bits = std::atoi(word.c_str() + 1);
      bool negative;
      uint64_t mag;

      result = expect_colon(s, pos, word) &&
               parse_number(s, pos, is_signed, negative, mag);
      if (result) {
        uint64_t limit = is_signed ? (uint64_t(1) << (bits - 1))
                                   : (bits == 64 ? ~uint64_t(0)
                                                 : (uint64_t(1) << bits) - 1);
        uint64_t raw;
        if (is_signed ? (negative ? mag > limit : mag >= limit)
                      : mag > limit) {
          result = fail("value out of range for " + word);
        } else {
          raw = negative ? (~mag + 1) : mag;
          out.push_back(static_cast<uint8_t>((is_signed ? ECIL_TAG_I8
                                                        : ECIL_TAG_U8) +
                                             (bits == 8 ? 0 : bits == 16 ? 1
                                              : bits == 32 ? 2 : 3)));
          put_le(out, raw, bits / 8);
        }
      }
    } else if (word == "u128" || word == "i128") {
      uint8_t bytes[16];
      result = expect_colon(s, pos, word);
      if (result) {
        if (word == "i128" && pos < s.size() && s[pos] == '-') {
          bool negative;
          uint64_t mag;
          result = parse_number(s, pos, true, negative, mag);
          uint64_t low = ~mag + 1;
          std::memset(bytes, 0xFF, 16);
          for (int k = 0; k < 8; ++k)
            bytes[k] = static_cast<uint8_t>(low >> (8 * k));
        } else {
          result = parse_hex_value(s, pos, bytes, 16);
        }
      }
      if (result) {
        out.push_back(word == "u128" ? ECIL_TAG_U128 : ECIL_TAG_I128);
        out.insert(out.end(), bytes, bytes + 16);
      }
    } else if (word == "f32" || word == "f64") {
      uint8_t bytes[8];
      size_t n = word == "f32" ? 4 : 8;
      result = expect_colon(s, pos, word) &&
               parse_hex_value(s, pos, bytes, n);
      if (result) {
        out.push_back(n == 4 ? ECIL_TAG_F32 : ECIL_TAG_F64);
        out.insert(out.end(), bytes, bytes + n);
      }
    } else if (word == "fbits") {
      bool negative;
      uint64_t n = 0;
      uint8_t bytes[16];
      result = expect_colon(s, pos, word) &&
               parse_number(s, pos, false, negative, n);
      if (result && (n < 1 || n > 16))
        result = fail("fbits size must be 1..16");
      if (result && (pos >= s.size() || s[pos] != ':'))
        result = fail("expected : after the fbits size");
      if (result) {
        ++pos;
        result = parse_hex_value(s, pos, bytes, static_cast<size_t>(n));
      }
      if (result) {
        out.push_back(ECIL_TAG_FBITS);
        out.push_back(static_cast<uint8_t>(n));
        out.insert(out.end(), bytes, bytes + n);
      }
    } else if (word == "bin8" || word == "bin16" || word == "bin32") {
      std::vector<uint8_t> bytes;
      if (!top_level)
        result = fail("bins are not allowed in arrays");
      else
        result = expect_colon(s, pos, word) && parse_string(s, pos, bytes);
      if (result) {
        size_t n = bytes.size();
        if (word == "bin8" && n <= 0xFF) {
          out.push_back(ECIL_TAG_BIN8);
          out.push_back(static_cast<uint8_t>(n));
        } else if (word == "bin16" && n <= 0xFFFF) {
          out.push_back(ECIL_TAG_BIN16);
          put_u16(out, static_cast<uint16_t>(n));
        } else if (word == "bin32") {
          out.push_back(ECIL_TAG_BIN32);
          put_u32(out, static_cast<uint32_t>(n));
        } else {
          result = fail("string too long for " + word);
        }
        if (result)
          out.insert(out.end(), bytes.begin(), bytes.end());
      }
    } else if (word == "arr16" || word == "arr32") {
      std::vector<uint8_t> body;
      uint32_t count = 0;
      if (!top_level) {
        result = fail("arrays are not allowed in arrays");
      } else {
        skip_spaces(s, pos);
        if (pos >= s.size() || s[pos] != '[') {
          result = fail("expected [ after " + word);
        } else {
          ++pos;
          result = parse_array_elements(s, pos, body, count);
        }
      }
      if (result) {
        if (word == "arr16") {
          if (count > 0xFFFF) {
            result = fail("too many elements for arr16");
          } else {
            out.push_back(ECIL_TAG_ARR16);
            put_u16(out, static_cast<uint16_t>(count));
          }
        } else {
          out.push_back(ECIL_TAG_ARR32);
          put_u32(out, count);
        }
        if (result)
          out.insert(out.end(), body.begin(), body.end());
      }
    } else {
      result = fail("unknown operand '" + word + "'");
    }
  }
  return result;
}  /* an_assembler::parse_operand */

bool an_assembler::parse_supp(const std::string &s, size_t &pos,
                              std::vector<uint8_t> &out, uint8_t explicit_tag)
/*
Parse {key: value, ...} (pos is at the '{') and append the supp blob.
*/
{
  bool result = true;
  std::vector<uint8_t> body;
  bool closed = false;

  ++pos;
  skip_spaces(s, pos);
  if (pos < s.size() && s[pos] == '}') {
    ++pos;
    closed = true;
  }
  while (result && !closed) {
    result = parse_operand(s, pos, body, false);
    skip_spaces(s, pos);
    if (result && (pos >= s.size() || s[pos] != ':'))
      result = fail("expected : after a supp key");
    if (result) {
      ++pos;
      result = parse_operand(s, pos, body, true);
    }
    if (result) {
      skip_spaces(s, pos);
      if (pos < s.size() && s[pos] == ',') {
        ++pos;
      } else if (pos < s.size() && s[pos] == '}') {
        ++pos;
        closed = true;
      } else {
        result = fail("expected , or } in supp");
      }
    }
  }
  if (result) {
    size_t n = body.size();
    uint8_t tag = explicit_tag != 0 ? explicit_tag
                  : n <= 0xFF ? ECIL_TAG_SUPP8
                  : n <= 0xFFFF ? ECIL_TAG_SUPP16 : ECIL_TAG_SUPP32;
    if (tag == ECIL_TAG_SUPP8 && n > 0xFF) {
      result = fail("supp payload too long for supp8");
    } else if (tag == ECIL_TAG_SUPP16 && n > 0xFFFF) {
      result = fail("supp payload too long for supp16");
    } else {
      out.push_back(tag);
      if (tag == ECIL_TAG_SUPP8)
        out.push_back(static_cast<uint8_t>(n));
      else if (tag == ECIL_TAG_SUPP16)
        put_u16(out, static_cast<uint16_t>(n));
      else
        put_u32(out, static_cast<uint32_t>(n));
      out.insert(out.end(), body.begin(), body.end());
    }
  }
  return result;
}  /* an_assembler::parse_supp */

void an_assembler::end_frame()
/*
Write the pending frame (header and payload) to the stream.
*/
{
  if (in_frame_) {
    put_u32(stream_, frame_len_override_
                       ? frame_len_ : static_cast<uint32_t>(payload_.size()));
    stream_.push_back(frame_kind_);
    stream_.push_back(frame_flags_);
    put_u16(stream_, frame_reserved_);
    stream_.insert(stream_.end(), payload_.begin(), payload_.end());
    payload_.clear();
    in_frame_ = false;
    ++frames_written_;
  }
}  /* an_assembler::end_frame */

bool an_assembler::directive_hex(const std::string &rest,
                                 std::vector<uint8_t> &target)
/*
Append hex bytes (blanks allowed between digit pairs).
*/
{
  bool result = true;
  int pending = -1;

  for (size_t k = 0; k < rest.size() && result; ++k) {
    char c = rest[k];
    if (c == ' ' || c == '\t') {
      continue;
    } else if (std::isxdigit(static_cast<unsigned char>(c))) {
      int d = std::isdigit(static_cast<unsigned char>(c))
                ? c - '0' : std::tolower(c) - 'a' + 10;
      if (pending < 0) {
        pending = d;
      } else {
        target.push_back(static_cast<uint8_t>(pending * 16 + d));
        pending = -1;
      }
    } else {
      result = fail("bad hex digit");
    }
  }
  if (result && pending >= 0)
    result = fail("odd number of hex digits");
  return result;
}  /* an_assembler::directive_hex */

bool an_assembler::directive_ecil(const std::string &rest)
/*
Handle "ecil MAJOR.MINOR [features=0xH]": write the 16-byte preamble.
*/
{
  bool result = true;
  unsigned major = 0;
  unsigned minor = 0;
  unsigned long features = 0;
  const char *p = rest.c_str();
  char *end = nullptr;

  end_frame();
  major = static_cast<unsigned>(std::strtoul(p, &end, 10));
  if (end == p || *end != '.') {
    result = fail("expected MAJOR.MINOR");
  } else {
    p = end + 1;
    minor = static_cast<unsigned>(std::strtoul(p, &end, 10));
    if (end == p)
      result = fail("expected MAJOR.MINOR");
  }
  if (result) {
    p = end;
    while (*p == ' ')
      ++p;
    if (std::strncmp(p, "features=", 9) == 0)
      features = std::strtoul(p + 9, &end, 0);
    stream_.push_back(ECIL_MAGIC_0);
    stream_.push_back(ECIL_MAGIC_1);
    stream_.push_back(ECIL_MAGIC_2);
    stream_.push_back(ECIL_MAGIC_3);
    stream_.push_back(ECIL_MAGIC_4);
    stream_.push_back(ECIL_MAGIC_5);
    stream_.push_back(ECIL_MAGIC_6);
    stream_.push_back(ECIL_MAGIC_7);
    put_u16(stream_, static_cast<uint16_t>(major));
    put_u16(stream_, static_cast<uint16_t>(minor));
    put_u32(stream_, static_cast<uint32_t>(features));
  }
  return result;
}  /* an_assembler::directive_ecil */

bool an_assembler::directive_frame(const std::string &rest)
/*
Handle "frame KIND [supp] [flags=] [reserved=] [len=] [bytes=]".
*/
{
  bool result = true;
  size_t pos = 0;
  std::vector<uint8_t> bytes;
  bool first = true;

  end_frame();
  frame_kind_ = 0;
  frame_flags_ = 0;
  frame_reserved_ = 0;
  frame_len_override_ = false;
  while (result && pos < rest.size()) {
    skip_spaces(rest, pos);
    if (pos >= rest.size())
      break;
    size_t start = pos;
    while (pos < rest.size() && rest[pos] != ' ' && rest[pos] != '\t')
      ++pos;
    std::string word = rest.substr(start, pos - start);
    if (first) {
      first = false;
      static const struct { const char *name; uint8_t kind; } kinds[] = {
        {"HEADER", efk_header}, {"DECLS", efk_decls}, {"REGION", efk_region},
        {"GLOBALS", efk_globals}, {"NAMES", efk_names}, {"ABORT", efk_abort},
        {"END", efk_end}};
      bool found = false;
      for (const auto &k : kinds) {
        if (word == k.name) {
          frame_kind_ = k.kind;
          found = true;
        }
      }
      if (!found) {
        char *e = nullptr;
        unsigned long v = std::strtoul(word.c_str(), &e, 0);
        if (e == word.c_str() || *e != '\0' || v == 0 || v > 255)
          result = fail("bad frame kind '" + word + "'");
        else
          frame_kind_ = static_cast<uint8_t>(v);
      }
    } else if (word == "supp") {
      frame_flags_ = static_cast<uint8_t>(frame_flags_ |
                                          ECIL_FRAME_FLAG_HAS_SUPP);
    } else if (word.compare(0, 6, "flags=") == 0) {
      frame_flags_ = static_cast<uint8_t>(frame_flags_ |
                          std::strtoul(word.c_str() + 6, nullptr, 0));
    } else if (word.compare(0, 9, "reserved=") == 0) {
      frame_reserved_ = static_cast<uint16_t>(
                          std::strtoul(word.c_str() + 9, nullptr, 0));
    } else if (word.compare(0, 4, "len=") == 0) {
      frame_len_override_ = true;
      frame_len_ = static_cast<uint32_t>(
                          std::strtoul(word.c_str() + 4, nullptr, 0));
    } else if (word.compare(0, 6, "bytes=") == 0) {
      result = directive_hex(word.substr(6), bytes);
    } else {
      result = fail("unknown frame option '" + word + "'");
    }
  }
  if (result && frame_kind_ == 0)
    result = fail("frame needs a kind");
  in_frame_ = true;
  payload_ = bytes;
  return result;
}  /* an_assembler::directive_frame */

bool an_assembler::record(const std::string &text, bool check_arity)
/*
Assemble one record line (text starts at the opcode spelling).
*/
{
  bool result = true;
  size_t pos = 0;
  const an_opcode_info *info = nullptr;
  uint8_t opcode = 0;
  std::vector<uint8_t> bytes;
  unsigned count = 0;

  if (!in_frame_)
    result = fail("record outside a frame");
  while (result && pos < text.size() && text[pos] != ' ' && text[pos] != '\t')
    ++pos;
  std::string word = result ? text.substr(0, pos) : std::string();
  if (!result) {
    /* The failure is already recorded. */
  } else if (word.compare(0, 2, "?0") == 0 || word.compare(0, 2, "0x") == 0) {
    size_t skip = word[0] == '?' ? 1 : 0;
    opcode = static_cast<uint8_t>(std::strtoul(word.c_str() + skip, nullptr,
                                               0));
    check_arity = false;
  } else {
    info = find_opcode_by_name(word);
    if (info == nullptr)
      result = fail("unknown opcode '" + word + "'");
    else
      opcode = info->value;
  }
  bytes.push_back(opcode);
  while (result) {
    skip_spaces(text, pos);
    if (pos >= text.size())
      break;
    if (text.compare(pos, 4, "supp") == 0) {
      size_t q = pos + 4;
      uint8_t explicit_tag = 0;
      if (text.compare(q, 2, "16") == 0) {
        explicit_tag = ECIL_TAG_SUPP16;
        q += 2;
      } else if (text.compare(q, 2, "32") == 0) {
        explicit_tag = ECIL_TAG_SUPP32;
        q += 2;
      } else if (text.compare(q, 1, "8") == 0) {
        explicit_tag = ECIL_TAG_SUPP8;
        q += 1;
      }
      if (q < text.size() && text[q] == '{') {
        pos = q;
        result = parse_supp(text, pos, bytes, explicit_tag);
        skip_spaces(text, pos);
        if (result && pos < text.size())
          result = fail("trailing text after supp");
        break;
      }
    }
    result = parse_operand(text, pos, bytes, true);
    if (!result)
      break;
    ++count;
  }
  if (result && check_arity && info != nullptr && count != info->arity) {
    char buffer[96];
    std::snprintf(buffer, sizeof buffer, "%s takes %u operands, %u given",
                  info->name, info->arity, count);
    result = fail(buffer);
  }
  if (result)
    payload_.insert(payload_.end(), bytes.begin(), bytes.end());
  return result;
}  /* an_assembler::record */

bool an_assembler::line(const std::string &raw)
/*
Process one line of input.
*/
{
  bool result = true;
  std::string text;
  bool in_quote = false;
  size_t start = 0;

  /* Strip a trailing comment outside quotes. */
  text = raw;
  for (size_t k = 0; k < raw.size(); ++k) {
    char c = raw[k];
    if (in_quote) {
      if (c == '\\')
        ++k;
      else if (c == '"')
        in_quote = false;
    } else if (c == '"') {
      in_quote = true;
    } else if (c == ';') {
      text = raw.substr(0, k);
      break;
    }
  }
  while (start < text.size() && (text[start] == ' ' || text[start] == '\t'))
    ++start;
  text = text.substr(start);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' ||
                           text.back() == '\r'))
    text.pop_back();
  if (text.empty())
    result = true;
  else if (text.compare(0, 5, "ecil ") == 0)
    result = directive_ecil(text.substr(5));
  else if (text.compare(0, 6, "frame ") == 0)
    result = directive_frame(text.substr(6));
  else if (text.compare(0, 5, ".raw ") == 0) {
    if (!in_frame_)
      result = fail(".raw outside a frame");
    else
      result = directive_hex(text.substr(5), payload_);
  } else if (text.compare(0, 8, ".stream ") == 0) {
    end_frame();
    result = directive_hex(text.substr(8), stream_);
  } else if (text[0] == '!')
    result = record(text.substr(1), false);
  else
    result = record(text, true);
  return result;
}  /* an_assembler::line */

bool an_assembler::run(const std::string &text, std::vector<uint8_t> &out,
                       std::string &error)
/*
Assemble the whole text.
*/
{
  bool result = true;
  size_t pos = 0;

  while (result && pos <= text.size()) {
    size_t nl = text.find('\n', pos);
    std::string current = text.substr(pos, nl == std::string::npos
                                             ? std::string::npos : nl - pos);
    ++line_no_;
    result = line(current);
    if (nl == std::string::npos)
      break;
    pos = nl + 1;
  }
  if (result) {
    end_frame();
    out = stream_;
  } else {
    error = error_;
  }
  return result;
}  /* an_assembler::run */

}  /* anonymous namespace */

bool dump_stream(const uint8_t *data, size_t size,
                 const a_dump_options &options, std::string &out,
                 an_error &error)
/*
Write the text form of the stream to out.
*/
{
  a_dump_handler handler(out, options);
  a_reader_options reader_options;
  bool result = true;

  reader_options.check = options.check;
  a_stream_reader reader(handler, reader_options);
  if (options.split_at != 0 && options.split_at < size) {
    result = reader.consume(data, options.split_at) &&
             reader.consume(data + options.split_at, size - options.split_at);
  } else if (options.chunk_size == 0) {
    result = reader.consume(data, size);
  } else {
    for (size_t at = 0; result && at < size; at += options.chunk_size) {
      size_t n = size - at < options.chunk_size ? size - at
                                                : options.chunk_size;
      result = reader.consume(data + at, n);
    }
  }
  if (result)
    result = reader.finish();
  if (!result) {
    error = reader.error();
  } else if (options.vtable_layouts) {
    std::string problem;

    if (!print_abi_layouts(reader.ids(), out, problem)) {
      error.kind = an_error_kind::format;
      error.message = "vtable layouts: " + problem;
      error.stream_offset = reader.offset();
      result = false;
    }
  }
  return result;
}  /* dump_stream */

bool assemble_text(const std::string &text, std::vector<uint8_t> &out,
                   std::string &error)
/*
Assemble the text form to bytes.
*/
{
  an_assembler assembler;
  return assembler.run(text, out, error);
}  /* assemble_text */

}  /* namespace ecil */
