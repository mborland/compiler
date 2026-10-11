/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
ecil_decode.cpp -- zero-copy tag decoding, record decoding and operand
checks.  Nothing here recurses: arrays are flat and are scanned with a loop.
*/

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "ecil_reader.h"

namespace ecil {

namespace {

uint16_t read_u16(const uint8_t *p)
/*
Read a little-endian u16.
*/
{
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}  /* read_u16 */

uint32_t read_u32(const uint8_t *p)
/*
Read a little-endian u32.
*/
{
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}  /* read_u32 */

uint64_t read_u64(const uint8_t *p)
/*
Read a little-endian u64.
*/
{
  return static_cast<uint64_t>(read_u32(p)) |
         (static_cast<uint64_t>(read_u32(p + 4)) << 32);
}  /* read_u64 */

#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
void set_format_error(an_error &error, const char *format, ...)
/*
Record a format error with a printf-style message.
*/
{
  char buffer[256];
  va_list args;

  va_start(args, format);
  std::vsnprintf(buffer, sizeof buffer, format, args);
  va_end(args);
  error.kind = an_error_kind::format;
  error.message = buffer;
}  /* set_format_error */

void clear_operand(an_operand &operand)
/*
Reset operand to nil.
*/
{
  operand.kind = an_operand_kind::nil;
  operand.tag = ECIL_TAG_NIL;
  operand.u = 0;
  operand.i = 0;
  operand.data = nullptr;
  operand.length = 0;
  operand.count = 0;
  operand.all_uint = false;
}  /* clear_operand */

bool take(a_cursor &cursor, size_t n, an_error &error)
/*
Check that n bytes remain at cursor.
*/
{
  bool result = cursor.remaining() >= n;

  if (!result)
    set_format_error(error, "truncated operand: need %zu bytes, have %zu", n,
                     cursor.remaining());
  return result;
}  /* take */

bool decode_scalar(a_cursor &cursor, an_operand &operand, bool allow_bin,
                   an_error &error)
/*
Decode one non-array operand.  Bins are accepted only when allow_bin.
*/
{
  bool result = true;
  uint8_t tag;

  clear_operand(operand);
  if (!take(cursor, 1, error)) {
    result = false;
  } else {
    tag = *cursor.p++;
    operand.tag = tag;
    if (tag <= ECIL_TAG_FIXUINT_MAX) {
      operand.kind = an_operand_kind::uint;
      operand.u = tag;
    } else if (tag >= ECIL_TAG_NEGFIXINT) {
      operand.kind = an_operand_kind::sint;
      operand.i = static_cast<int8_t>(tag);
      operand.u = static_cast<uint64_t>(operand.i);
    } else if (tag >= ECIL_TAG_FIXBIN && tag < ECIL_TAG_FIXBIN + 0x20) {
      uint32_t n = static_cast<uint32_t>(tag - ECIL_TAG_FIXBIN);
      if (!allow_bin) {
        set_format_error(error, "bin operand not allowed here");
        result = false;
      } else if (take(cursor, n, error)) {
        operand.kind = an_operand_kind::bin;
        operand.data = cursor.p;
        operand.length = n;
        cursor.p += n;
      } else {
        result = false;
      }
    } else {
      switch (tag) {
      case ECIL_TAG_NIL:
        operand.kind = an_operand_kind::nil;
        break;
      case ECIL_TAG_FALSE:
      case ECIL_TAG_TRUE:
        operand.kind = an_operand_kind::boolean;
        operand.u = tag == ECIL_TAG_TRUE ? 1 : 0;
        break;
      case ECIL_TAG_BIN8:
      case ECIL_TAG_BIN16:
      case ECIL_TAG_BIN32: {
        size_t width = tag == ECIL_TAG_BIN8 ? 1
                       : tag == ECIL_TAG_BIN16 ? 2 : 4;
        if (!allow_bin) {
          set_format_error(error, "bin operand not allowed here");
          result = false;
        } else if (take(cursor, width, error)) {
          uint32_t n = width == 1 ? cursor.p[0]
                       : width == 2 ? read_u16(cursor.p) : read_u32(cursor.p);
          cursor.p += width;
          if (take(cursor, n, error)) {
            operand.kind = an_operand_kind::bin;
            operand.data = cursor.p;
            operand.length = n;
            cursor.p += n;
          } else {
            result = false;
          }
        } else {
          result = false;
        }
        break;
      }
      case ECIL_TAG_F32:
        if (take(cursor, 4, error)) {
          operand.kind = an_operand_kind::f32;
          operand.u = read_u32(cursor.p);
          cursor.p += 4;
        } else {
          result = false;
        }
        break;
      case ECIL_TAG_F64:
        if (take(cursor, 8, error)) {
          operand.kind = an_operand_kind::f64;
          operand.u = read_u64(cursor.p);
          cursor.p += 8;
        } else {
          result = false;
        }
        break;
      case ECIL_TAG_U8:
      case ECIL_TAG_U16:
      case ECIL_TAG_U32:
      case ECIL_TAG_U64: {
        size_t width = size_t(1) << (tag - ECIL_TAG_U8);
        if (take(cursor, width, error)) {
          operand.kind = an_operand_kind::uint;
          operand.u = width == 1 ? cursor.p[0]
                      : width == 2 ? read_u16(cursor.p)
                      : width == 4 ? read_u32(cursor.p) : read_u64(cursor.p);
          cursor.p += width;
        } else {
          result = false;
        }
        break;
      }
      case ECIL_TAG_I8:
      case ECIL_TAG_I16:
      case ECIL_TAG_I32:
      case ECIL_TAG_I64: {
        size_t width = size_t(1) << (tag - ECIL_TAG_I8);
        if (take(cursor, width, error)) {
          operand.kind = an_operand_kind::sint;
          switch (width) {
          case 1:
            operand.i = static_cast<int8_t>(cursor.p[0]);
            break;
          case 2:
            operand.i = static_cast<int16_t>(read_u16(cursor.p));
            break;
          case 4:
            operand.i = static_cast<int32_t>(read_u32(cursor.p));
            break;
          default:
            operand.i = static_cast<int64_t>(read_u64(cursor.p));
            break;
          }
          operand.u = static_cast<uint64_t>(operand.i);
          cursor.p += width;
        } else {
          result = false;
        }
        break;
      }
      case ECIL_TAG_U128:
      case ECIL_TAG_I128:
        if (take(cursor, 16, error)) {
          operand.kind = tag == ECIL_TAG_U128 ? an_operand_kind::u128
                                              : an_operand_kind::i128;
          operand.data = cursor.p;
          operand.length = 16;
          operand.u = read_u64(cursor.p);
          operand.i = static_cast<int64_t>(operand.u);
          cursor.p += 16;
        } else {
          result = false;
        }
        break;
      case ECIL_TAG_FBITS:
        if (take(cursor, 1, error)) {
          uint32_t n = *cursor.p++;
          if (n < 1 || n > 16) {
            set_format_error(error, "fbits byte count %u out of range 1..16",
                             n);
            result = false;
          } else if (take(cursor, n, error)) {
            operand.kind = an_operand_kind::fbits;
            operand.data = cursor.p;
            operand.length = n;
            cursor.p += n;
          } else {
            result = false;
          }
        } else {
          result = false;
        }
        break;
      case ECIL_TAG_POISON:
        set_format_error(error, "poison tag 0xC1 (stream desynchronized)");
        result = false;
        break;
      case ECIL_TAG_SUPP8:
      case ECIL_TAG_SUPP16:
      case ECIL_TAG_SUPP32:
        set_format_error(error, "supp tag 0x%02X where an operand is expected",
                         tag);
        result = false;
        break;
      default:
        set_format_error(error, "reserved tag 0x%02X", tag);
        result = false;
        break;
      }
    }
  }
  return result;
}  /* decode_scalar */

bool is_array_tag(uint8_t tag)
/*
Return true for fixarray, arr16 and arr32 tags.
*/
{
  return (tag >= ECIL_TAG_FIXARRAY && tag < ECIL_TAG_FIXARRAY + 0x20) ||
         tag == ECIL_TAG_ARR16 || tag == ECIL_TAG_ARR32;
}  /* is_array_tag */

bool is_supp_tag(uint8_t tag)
/*
Return true for supp8, supp16 and supp32.
*/
{
  return tag >= ECIL_TAG_SUPP8 && tag <= ECIL_TAG_SUPP32;
}  /* is_supp_tag */

bool validate_supp(const uint8_t *payload, uint32_t length, an_error &error)
/*
Check that a supp payload is a sequence of (uint key, scalar-or-bin value)
pairs that ends exactly at length.
*/
{
  bool result = true;
  a_cursor cursor(payload, payload + length);

  while (result && cursor.remaining() > 0) {
    an_operand key;
    an_operand value;
    uint64_t key_value;

    result = decode_scalar(cursor, key, false, error);
    if (result && !operand_u64(key, key_value)) {
      set_format_error(error, "supp key is not an unsigned integer");
      result = false;
    }
    if (result && cursor.remaining() == 0) {
      set_format_error(error, "supp key %llu has no value",
                       static_cast<unsigned long long>(key_value));
      result = false;
    }
    if (result)
      result = decode_scalar(cursor, value, true, error);
  }
  return result;
}  /* validate_supp */

bool version_newer(uint16_t stream_minor)
/*
Return true if the stream's minor version is newer than this reader's.
*/
{
  return stream_minor > ECIL_VERSION_MINOR;
}  /* version_newer */

}  /* anonymous namespace */

bool decode_operand(a_cursor &cursor, an_operand &operand, an_error &error)
/*
Decode one operand at cursor (scalars, bins and flat arrays).  On failure
set error.message and return false.
*/
{
  bool result = true;

  if (cursor.remaining() > 0 && is_array_tag(*cursor.p)) {
    uint8_t tag = *cursor.p++;
    uint32_t count = 0;

    clear_operand(operand);
    operand.tag = tag;
    if (tag == ECIL_TAG_ARR16) {
      result = take(cursor, 2, error);
      if (result) {
        count = read_u16(cursor.p);
        cursor.p += 2;
      }
    } else if (tag == ECIL_TAG_ARR32) {
      result = take(cursor, 4, error);
      if (result) {
        count = read_u32(cursor.p);
        cursor.p += 4;
      }
    } else {
      count = static_cast<uint32_t>(tag - ECIL_TAG_FIXARRAY);
    }
    if (result && count > cursor.remaining()) {
      set_format_error(error, "array of %u elements exceeds the record",
                       count);
      result = false;
    }
    if (result) {
      const uint8_t *body = cursor.p;
      an_operand element;

      bool all_uint = true;
      uint64_t ignored = 0;

      for (uint32_t k = 0; k < count && result; ++k) {
        result = decode_scalar(cursor, element, false, error);
        if (result && !operand_u64(element, ignored))
          all_uint = false;
      }
      if (result) {
        operand.all_uint = all_uint;
        operand.kind = an_operand_kind::array;
        operand.data = body;
        operand.length = static_cast<uint32_t>(cursor.p - body);
        operand.count = count;
      }
    }
  } else {
    result = decode_scalar(cursor, operand, true, error);
  }
  return result;
}  /* decode_operand */

an_array_reader array_elements(const an_operand &array)
/*
Return a reader positioned at the first element of array.
*/
{
  an_array_reader reader = {array.data, array.count};
  return reader;
}  /* array_elements */

bool next_array_element(an_array_reader &reader, an_operand &element)
/*
Decode the next element; return false at the end.
*/
{
  bool result = false;

  if (reader.remaining > 0) {
    an_error ignored;
    /* Elements were validated when the array operand was decoded. */
    a_cursor cursor(reader.p, reader.p + 32);
    /* Bound the cursor generously; scalar encodings are at most 18 bytes. */
    result = decode_scalar(cursor, element, false, ignored);
    reader.p = cursor.p;
    --reader.remaining;
  }
  return result;
}  /* next_array_element */

bool next_uint_element(an_array_reader &reader, uint64_t &value)
/*
The next element of an all-unsigned array.
*/
{
  bool result = false;

  if (reader.remaining > 0) {
    if (*reader.p <= ECIL_TAG_FIXUINT_MAX) {
      value = *reader.p++;
      --reader.remaining;
      result = true;
    } else {
      an_operand element;
      result = next_array_element(reader, element) &&
               operand_u64(element, value);
    }
  }
  return result;
}  /* next_uint_element */

bool operand_u64(const an_operand &operand, uint64_t &value)
/*
Return true and the value if operand is an integer in [0, 2^64).
*/
{
  bool result = false;

  switch (operand.kind) {
  case an_operand_kind::uint:
    value = operand.u;
    result = true;
    break;
  case an_operand_kind::sint:
    if (operand.i >= 0) {
      value = operand.u;
      result = true;
    }
    break;
  case an_operand_kind::u128:
  case an_operand_kind::i128:
    if (read_u64(operand.data + 8) == 0) {
      value = operand.u;
      result = true;
    }
    break;
  default:
    break;
  }
  return result;
}  /* operand_u64 */

namespace {

bool magnitude_below_pow2(uint64_t lo, uint64_t hi, uint64_t k)
/* True if the 128-bit magnitude hi:lo is below 2^k. */
{
  bool result;

  if (k >= 128)
    result = true;
  else if (k >= 64)
    result = hi < (uint64_t(1) << (k - 64)) || (k == 64 && hi == 0);
  else
    result = hi == 0 && (k == 0 ? lo == 0 : lo < (uint64_t(1) << k));
  return result;
}  /* magnitude_below_pow2 */

}  /* anonymous namespace */

bool operand_fits_int_width(const an_operand &operand, uint64_t bits)
/*
Return true if the integer operand is within [-2^(bits-1), 2^bits): the
signed or the unsigned range of an integer type of that many bits (the writer
sends the value of a signed type signed and of an unsigned type unsigned; both
are accepted for either).  Operands of other kinds fit.
*/
{
  uint64_t lo = 0;
  uint64_t hi = 0;
  bool negative = false;
  bool result = true;

  switch (operand.kind) {
  case an_operand_kind::uint:
    lo = operand.u;
    break;
  case an_operand_kind::sint:
    negative = operand.i < 0;
    lo = negative ? uint64_t(0) - static_cast<uint64_t>(operand.i)
                  : static_cast<uint64_t>(operand.i);
    break;
  case an_operand_kind::u128:
  case an_operand_kind::i128:
    lo = read_u64(operand.data);
    hi = read_u64(operand.data + 8);
    if (operand.kind == an_operand_kind::i128 && (hi >> 63) != 0) {
      negative = true;
      lo = ~lo + 1;
      hi = ~hi + (lo == 0 ? 1 : 0);
    }
    break;
  default:
    return true;
  }
  if (bits != 0 && bits < 128) {
    if (!negative) {
      result = magnitude_below_pow2(lo, hi, bits);
    } else {
      /* magnitude <= 2^(bits-1)  <=>  magnitude - 1 < 2^(bits-1) */
      uint64_t mlo = lo - 1;
      uint64_t mhi = hi - (lo == 0 ? 1 : 0);

      result = magnitude_below_pow2(mlo, mhi, bits - 1);
    }
  }
  return result;
}  /* operand_fits_int_width */

bool operand_i64(const an_operand &operand, int64_t &value)
/*
Return true and the value if operand is an integer in the int64 range.
*/
{
  bool result = false;

  switch (operand.kind) {
  case an_operand_kind::uint:
    if (operand.u <= static_cast<uint64_t>(INT64_MAX)) {
      value = static_cast<int64_t>(operand.u);
      result = true;
    }
    break;
  case an_operand_kind::sint:
    value = operand.i;
    result = true;
    break;
  case an_operand_kind::u128:
    if (read_u64(operand.data + 8) == 0 &&
        operand.u <= static_cast<uint64_t>(INT64_MAX)) {
      value = static_cast<int64_t>(operand.u);
      result = true;
    }
    break;
  case an_operand_kind::i128: {
    uint64_t high = read_u64(operand.data + 8);
    uint64_t expected = operand.i < 0 ? ~uint64_t(0) : 0;
    if (high == expected) {
      value = operand.i;
      result = true;
    }
    break;
  }
  default:
    break;
  }
  return result;
}  /* operand_i64 */

bool decode_record(a_cursor &cursor, const a_decode_context &context,
                   a_record &record, an_error &error)
/*
Decode one record (opcode, its fixed operands and, when the frame allows
it, a trailing supp blob).  The supp payload structure is checked.
*/
{
  bool result = true;
  uint8_t opcode;

  record.start = cursor.p;
  record.size = 0;
  record.opcode = 0;
  record.count = 0;
  record.supp = nullptr;
  record.supp_length = 0;
  record.supp_tag = 0;
  record.info = nullptr;
  if (cursor.remaining() == 0) {
    set_format_error(error, "missing record");
    result = false;
  }
  if (result) {
    opcode = *cursor.p++;
    record.opcode = opcode;
    if (opcode == 0x00 || opcode == ECIL_TAG_NIL ||
        opcode == ECIL_TAG_POISON || is_supp_tag(opcode)) {
      if (is_supp_tag(opcode) && !context.allow_supp)
        set_format_error(error, "supp blob in a frame without HAS_SUPP");
      else
        set_format_error(error, "reserved opcode byte 0x%02X (stream "
                         "desynchronized)", opcode);
      result = false;
    }
  }
  if (result) {
    record.info = find_opcode_info(opcode);
    if (record.info == nullptr) {
      if (version_newer(context.stream_minor)) {
        char buffer[160];
        std::snprintf(buffer, sizeof buffer, "unknown opcode 0x%02X: stream "
                      "needs ECIL minor >= %u (reader is %d.%d)", opcode,
                      context.stream_minor, ECIL_VERSION_MAJOR,
                      ECIL_VERSION_MINOR);
        error.kind = an_error_kind::version;
        error.message = buffer;
      } else {
        set_format_error(error, "unknown opcode 0x%02X", opcode);
      }
      result = false;
    }
  }
  if (result) {
    record.count = record.info->arity;
    for (uint8_t k = 0; k < record.count && result; ++k)
      result = decode_operand(cursor, record.ops[k], error);
    if (result && context.allow_supp && cursor.remaining() > 0 &&
        is_supp_tag(*cursor.p)) {
      uint8_t tag = *cursor.p++;
      size_t width = tag == ECIL_TAG_SUPP8 ? 1
                     : tag == ECIL_TAG_SUPP16 ? 2 : 4;

      result = take(cursor, width, error);
      if (result) {
        uint32_t n = width == 1 ? cursor.p[0]
                     : width == 2 ? read_u16(cursor.p) : read_u32(cursor.p);
        cursor.p += width;
        result = take(cursor, n, error);
        if (result) {
          record.supp = cursor.p;
          record.supp_length = n;
          record.supp_tag = tag;
          cursor.p += n;
          result = validate_supp(record.supp, n, error);
        }
      }
    }
  }
  if (result)
    record.size = static_cast<uint32_t>(cursor.p - record.start);
  else if (error.ok())
    set_format_error(error, "malformed record");
  if (!result && !error.message.empty() && record.info != nullptr)
    error.message = std::string(record.info->name) + ": " + error.message;
  return result;
}  /* decode_record */

bool check_record_operands(const a_record &record, uint16_t stream_minor,
                           an_error &error)
/*
Check operand kinds, flag masks and enumerator ranges against the schema.
*/
{
  bool result = true;
  const an_opcode_info &info = *record.info;

  for (uint8_t k = 0; k < record.count && result; ++k) {
    const an_operand &op = record.ops[k];
    const an_operand_spec &spec = info.operands[k];
    uint64_t v = 0;
    bool is_uint = operand_u64(op, v);
    bool is_nil = op.kind == an_operand_kind::nil;
    const char *problem = nullptr;

    switch (spec.cls) {
    case an_operand_class::id:
    case an_operand_class::uinteger:
      if (!is_uint)
        problem = "must be an unsigned integer";
      break;
    case an_operand_class::id_or_nil:
    case an_operand_class::uinteger_or_nil:
      if (!is_uint && !is_nil)
        problem = "must be an unsigned integer or nil";
      break;
    case an_operand_class::integer:
      if (op.kind != an_operand_kind::uint &&
          op.kind != an_operand_kind::sint &&
          op.kind != an_operand_kind::u128 &&
          op.kind != an_operand_kind::i128)
        problem = "must be an integer";
      break;
    case an_operand_class::boolean:
      if (!(op.kind == an_operand_kind::boolean ||
            (is_uint && v <= 1)))
        problem = "must be a boolean";
      break;
    case an_operand_class::bin:
      if (op.kind != an_operand_kind::bin)
        problem = "must be a byte string";
      break;
    case an_operand_class::array:
      if (op.kind != an_operand_kind::array) {
        problem = "must be an array";
      } else {
        if (!op.all_uint)
          problem = "array elements must be unsigned integers";
      }
      break;
    case an_operand_class::floating:
      if (op.kind != an_operand_kind::f32 && op.kind != an_operand_kind::f64 &&
          op.kind != an_operand_kind::fbits)
        problem = "must be a float (f32, f64 or fbits)";
      break;
    case an_operand_class::flags:
      if (!is_uint) {
        problem = "must be an unsigned integer (flags)";
      } else if ((v & ~static_cast<uint64_t>(spec.mask)) != 0) {
        char buffer[200];
        std::snprintf(buffer, sizeof buffer, "%s operand %d: unknown flag "
                      "bits 0x%llX%s", info.name, k + 1,
                      static_cast<unsigned long long>(
                                    v & ~static_cast<uint64_t>(spec.mask)),
                      version_newer(stream_minor)
                        ? " (stream needs a newer ECIL minor)" : "");
        error.kind = version_newer(stream_minor) ? an_error_kind::version
                                                 : an_error_kind::format;
        error.message = buffer;
        result = false;
      }
      break;
    case an_operand_class::enumerator:
      if (!is_uint) {
        problem = "must be an unsigned integer (enumerator)";
      } else if (v < spec.lo || v > spec.mask) {
        char buffer[200];
        std::snprintf(buffer, sizeof buffer, "%s operand %d: value %llu out "
                      "of range %u..%u%s", info.name, k + 1,
                      static_cast<unsigned long long>(v), spec.lo, spec.mask,
                      version_newer(stream_minor)
                        ? " (stream needs a newer ECIL minor)" : "");
        error.kind = version_newer(stream_minor) ? an_error_kind::version
                                                 : an_error_kind::format;
        error.message = buffer;
        result = false;
      }
      break;
    case an_operand_class::none:
      break;
    }
    if (problem != nullptr) {
      set_format_error(error, "%s operand %d %s", info.name, k + 1, problem);
      result = false;
    }
  }
  return result;
}  /* check_record_operands */

bool supp_pairs(const a_record &record,
                std::vector<std::pair<uint64_t, an_operand>> &pairs)
/*
Decode the supp payload into pairs.  False if malformed.
*/
{
  bool result = true;
  an_error error;
  a_cursor cursor(record.supp, record.supp + record.supp_length);

  pairs.clear();
  while (result && cursor.remaining() > 0) {
    an_operand key;
    an_operand value;
    uint64_t key_value = 0;

    result = decode_scalar(cursor, key, false, error) &&
             operand_u64(key, key_value) && cursor.remaining() > 0 &&
             decode_scalar(cursor, value, true, error);
    if (result)
      pairs.push_back(std::make_pair(key_value, value));
  }
  return result;
}  /* supp_pairs */

}  /* namespace ecil */
